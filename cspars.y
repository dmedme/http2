%{
/* #define DEBUG 1 */
/* #define YYDEBUG 1 */
/*************************************************************************
 *
 * cspars.y - the PATH/CS variable manipulation program.
 *
 *   The language is case-insensitive, with all values being
 *   cast to upper case. Internal variables are given names
 *   starting with a lower case character to distinguish them
 *   from the ones that are input. 
 *
 *   The main data structures are
 *     - a symbol table, essentially a hash of the symbol names
 *     - the output heap, which consists of all the things that have
 *       have been recognised. Each entry in the output heap has
 *        - a symbol structures, consisting of
 *           - a name
 *           - a type (string, date, number/boolean, constant)
 *           - a value (always a string)
 *           - a confidence factor (always a number)
 *           names are either external or generated (for intermediate results)
 *
 *   Generation only takes place if the parser returns validly,
 *   and consists of
 *        - writing out any new symbols
 *        - if the actions are not sg.read_write, updating any lvalues
 *      When maintaining this program, always start from the yacc specification
 *      (.y) rather than the .c form.
 *
 *      The following sequence of shell commands does the necessary conversion.
 *
        yacc cspars.y
#                       standard yacc parser generation
        sed < y.tab.c '/        printf/ s/      printf[         ]*(/ fprintf(stderr,/g' > y.tab.pc
#                       Change the yacc debugging code to write to the
#                       error log file rather than stdout.
        sed < y.tab.pc '/^# *line/d' > cspars.c
#                       Remove the line directives that supposedly allow
#                       the source line debugger to place you in the
#                       absolute source file (i.e. the .y) rather than the
#                       .c. This feature (of pdbx) does not work properly;
#                       the debugger does recognise the line numbers, but is
#                       not able to recognise the source files, so it places
#                       you at random points in the .c.
#
 *
 */
#include "webdrive.h"
extern double atof();
extern double strtod();
extern long strtol();
extern double floor();
extern double fmod();
extern char * getenv();
extern char * to_char();    /* date manipulation */
static char * sccs_id="@(#) $Name$ $Id$\nCopyright (c) E2 Systems 1993\n";

#ifdef DEBUG
/********************************************************************
 *
 * redirect yacc debugging output, in case linked with forms stuff
 *
 */
                 extern int yydebug;
#endif

/**********************************************************************
 * Lexical analyser status stuff
 */

static int yylex();        /* Lexical Analyser */

%}
%pure_parser
%parse-param {struct csmacro * csmp}
%lex-param   {struct csmacro * csmp}
%union {
int ival;
struct s_expr * tval;
}

%token <tval> E2STRING NUMBER IDENT IS_DATE TO_DATE TO_CHAR NUM_CHAR SYSDATE TRUNC SYSTEM LENGTH CHR
%token <tval> UPPER LOWER INSTR GETENV GETPID PUTENV SUBSTR HEXTORAW GET_MATCH SET_MATCH EVAL BYTEVAL RECOGNISE_SCAN_SPEC URL_ESCAPE URL_UNESCAPE CALC_CHECKSUM
%token <ival> '(' ')' ';' '{' '}' '[' ']' END
%type <tval> list expr statblock assnstat condstat
%right '='
%right '?' ':'
%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left LS RS
%left '+' '-'
%left '*' '/' '%'
%left UMINUS
%right '!' '~'
%start list


%{
#include "hashlib.h"
#include "csexe.h"
%}

%%

list : /* empty */
     {
         csmp->expr_anchor = newexpr(csmp, "xROOT");
         $$ = csmp->expr_anchor;
     }
     | list ';'
     | list IDENT END
     {
         csmp->expr_anchor = $2; /* Ugly; hard to justify or explain */
         YYACCEPT;
     }
     | END
     {
         YYACCEPT;
     }
     | list END
     {
         YYACCEPT;
     }
     | error
     {
         csmp->lex_first = LERROR;
         YYACCEPT;
     }
     | list condstat '{' statblock '}'
     {
     struct s_expr * next_expr;
/*
 * Loop - generate a conditional depending on condstat for
 * each of the assigns in statblock; they are chained together
 * with a null pointer indicating the last element
 */
        for (next_expr = $4;
                 next_expr != (struct s_expr *) NULL;
                     next_expr = next_expr->link_expr)
             next_expr->cond_expr = $2;
        $1->eval_expr = $4;
        $$ = $4;
     }
     | list condstat
     {
/*         $$ = newexpr(csmp, new_loc_sym(csmp));                */
/*         $$->cond_expr = $2;                         */
/*         $1->eval_expr = $$;                         */
         struct s_expr * x[2];
         TOK * x1;
         x[0] = $1;
         x[1] = $2;
         $$ = opsetup(csmp, (int) '=',2,x);
         for (x1 = $1->out_sym->sym_value.next_tok;
                 x1 != (TOK *) NULL;
                     x1 = x1->next_tok);    /* Find the end of the chain */
         x1 =  &($$->out_sym->sym_value);   /* Associate the IDENT with this
                                               value */
         $1->eval_expr = $$;
     }
     | list statblock 
     {
         $1->eval_expr = $2;
         $$ = $2;
     }
     | list error
     {
#ifdef DEBUG
(void) fprintf( stderr,"Parser Error:\n");
#endif
         csmp->lex_first = LERROR;
         YYACCEPT;
     }
     ;

statblock : assnstat
     | statblock assnstat 
     {
        /* link the new statement to its predecessor in the block */
         $2->link_expr = $1;
         $$ = $2;
     }
     ;
condstat : expr 
     ;
assnstat : IDENT '=' expr 
     {
     struct s_expr * x[2];
     TOK * x1;

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '=',2,x);
         for (x1 = $1->out_sym->sym_value.next_tok;
                 x1 != (TOK *) NULL;
                     x1 = x1->next_tok);    /* Find the end of the chain */
         x1 =  &($$->out_sym->sym_value);   /* Associate the IDENT with this
                                               value */
     }
     | assnstat ';'
     ;

expr : '(' expr ')'
     {   $$ = $2; }
     | expr EQ expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, EQ,2,x);
     }
     | expr NE expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, NE,2,x);
     }
     | expr GE expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, GE,2,x);
     }
     | expr LE expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, LE,2,x);
     }
     | expr '<' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '<',2,x);
     }
     | expr '>' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '>',2,x);
     }
     | expr '+' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '+',2,x);
     }
     | expr '-' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '-',2,x);
     }
     | expr '*' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '*',2,x);
     }
     | expr '/' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '/',2,x);
     }
     | expr '&' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '&',2,x);
     }
     | expr '|' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '|',2,x);
     }
     | expr AND expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, AND,2,x);
     }
     | expr '?' expr ':' expr
     {
     struct s_expr * x[3];

         x[0] = $1;
         x[1] = $3;
         x[2] = $5;
         $$ = opsetup(csmp, (int) '?',3,x);
     }
     | expr OR expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, OR, 2, x);
     }
     | expr '^' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '^',2,x);
     }
     | expr RS expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, RS,2,x);
     }
     | expr LS expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, LS,2,x);
     }
     | expr '%' expr
     {
     struct s_expr * x[2];

         x[0] = $1;
         x[1] = $3;
         $$ = opsetup(csmp, (int) '%',2,x);
     }
     | '-' expr %prec UMINUS
     {
         $$ = opsetup(csmp, UMINUS, 1, &($2));
     }
     | '!' expr 
     {
         $$ = opsetup(csmp, (int) '!', 1, &($2));
     }
     | '~' expr 
     {
         $$ = opsetup(csmp, (int) '~', 1, &($2));
     }
     | IS_DATE '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, IS_DATE,2,x);
     }
     | TO_DATE '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, TO_DATE,2,x);
     }
       /* Get the date for this format */
     | TO_CHAR '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, TO_CHAR,2,x);
     }
     | TRUNC '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, TRUNC,2,x);
     }
     | TRUNC '(' expr  ')'
     {
         $$ = opsetup(csmp, TRUNC, 1, &($3));
     }
     | SYSTEM '(' expr  ')'
     {
         $$ = opsetup(csmp, SYSTEM, 1, &($3));
     }
     | CHR '(' expr  ')'
     {
         $$ = opsetup(csmp, CHR, 1, &($3));
     }
     | EVAL '(' expr  ')'
     {
         $$ = opsetup(csmp, EVAL, 1, &($3));
     }
     | BYTEVAL '(' expr  ')'
     {
         $$ = opsetup(csmp, BYTEVAL, 1, &($3));
     }
     | URL_UNESCAPE '(' expr  ')'
     {
         $$ = opsetup(csmp, URL_UNESCAPE, 1, &($3));
     }
     | URL_ESCAPE '(' expr  ')'
     {
         $$ = opsetup(csmp, URL_ESCAPE, 1, &($3));
     }
     | RECOGNISE_SCAN_SPEC '(' expr  ')'
     {
         $$ = opsetup(csmp, RECOGNISE_SCAN_SPEC, 1, &($3));
     }
     | INSTR '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, INSTR,2,x);
     }
     | SUBSTR '(' expr ';' expr ';' expr ')'
     {
     struct s_expr * x[3];

         x[0] = $3;
         x[1] = $5;
         x[2] = $7;
         $$ = opsetup(csmp, SUBSTR,3,x);
     }
     | CALC_CHECKSUM '(' expr ';' expr ';' expr ';' expr ')'
     {
#ifdef XGUI
     struct s_expr * x[4];

         x[0] = $3;
         x[1] = $5;
         x[2] = $7;
         x[3] = $9;
         $$ = opsetup(csmp, CALC_CHECKSUM,4,x);
#endif
     }
     | SET_MATCH '(' expr ';' expr ')'
     {
     struct s_expr * x[2];

         x[0] = $3;
         x[1] = $5;
         $$ = opsetup(csmp, SET_MATCH, 2, x);
     }
     | GET_MATCH '(' expr ')'
     {
         $$ = opsetup(csmp, GET_MATCH, 1, &($3));
     }
     | GETENV '(' expr ')'
     {
         $$ = opsetup(csmp, GETENV, 1, &($3));
     }
     | NUM_CHAR '(' expr ')'
     {
         $$ = opsetup(csmp, NUM_CHAR, 1, &($3));
     }
     | PUTENV '(' expr ')'
     {
         $$ = opsetup(csmp, PUTENV, 1, &($3));
     }
     | GETPID
     {
         $$ = opsetup(csmp, GETPID, 0, (struct s_expr **) NULL);
     }
     | HEXTORAW '(' expr ')'
     {
         $$ = opsetup(csmp, HEXTORAW, 1, &($3));
     }
     | UPPER '(' expr ')'
     {
         $$ = opsetup(csmp, UPPER, 1, &($3));
     }
     | LOWER '(' expr ')'
     {
         $$ = opsetup(csmp, LOWER, 1, &($3));
     }
     | LENGTH '(' expr ')'
     {
         $$ = opsetup(csmp, LENGTH, 1, &($3));
     }
     | SYSDATE
     | E2STRING
     | IDENT
     | NUMBER
     ;
%%
/* *************************************************************************

cscalc is called with the following syntax for the parameter string p:

cscalc <sequence of terms according to the yacc grammar above >

The terms are either assignments, or conditions. An assignment is an expression
of the form IDENT = expression. A condition is any other expression.

Everything has both a numeric (and boolean) value, and a string value.

Everything is categorised as a number, a string or a date.

The program attempts to preserve the date attributes, and numeric
attributes, as far as possible. However, its response to possible errors
is to change the type to something that makes sense. So, if two
alpha strings are multiplied, together, the boolean values will be
used, and zero will result. 

Strings have the value false, for boolean purposes.

The '+' sign serves as a concatenation operator when the arguments are
strings; note that two numeric strings cannot be concatenated, except
perhaps by '01234' + '' +'5678'.

Strings cannot have embedded delimiters, but concatenation and alternative
delimiters (\'"`) provide work-rounds.

All of the 'C' operators are supported, with the following exceptions;

  - (post|pre)-(in|dec)rement
  - the '+=' type assignments
  - the = assignment only works as the extreme left hand expression
  - all expressions are evaluated when considering ? :, &&, || etc.

Assignments and conditions are separated by semi-colons.

cscalc returns either when all the text has been parsed and
evaluated, or when a condition has evaluated false.

cscalc will return 0 if a condition evaluates false,
otherwise it will return 1.

Multiple disjointed conditions act as if linked by && operators; they
have an efficiency advantage over multiple && operators, since the
evaluation ceases if any evaluates false. Ordinarily, all tests are
evaluated.

It is not useful to input assignments and a condition, unless the
assignments are only to be actioned if the condition is true (since the
assignments are thrown away if the user exit returns IAPFAIL).

SYSDATE is recognised as the UNIX system date and time (format 'DD-MMM-YY').

Function IS_DATE (string,format_string) tests the string is a valid date.

Function TO_DATE (string,format_string) converts a valid date string to a
data item of type date (from the point of view of this routine).

Function TO_CHAR (date,format_string) converts a numeric value into a
date string.

Date arithmetic is supported; you can add or subtract days to or from
a date.

Subtraction will give you the days between two dates.

Function TRUNC (number) (truncate to integer) (or TRUNC (number,number)
(truncate to so many decimal places)) is analogous to the
SQL TRUNC operator, but does not support the Date/Time truncation
(simply because the date formatting does not support the time; easily
added). 

Function LENGTH(string) returns the length of a string, just like the
SQL LENGTH() function.

Function UPPER(string) converts any lower case characters to upper case
in the string, just like the SQL UPPER() function.

Function LOWER(string) converts any upper case characters to lower case
in the string, just like the SQL LOWER() function.

Function SUBSTR(string,number,number) reproduces the behaviour of the
SQL SUBSTR() function (including its behaviour with funny numbers).

Function INSTR(string,string) reproduces the behaviour of the
SQL INSTR() function.

Function GETENV(expr) returns the value of the UNIX environment variable.
It returns a zero length string if the UNIX getenv() call fails.

*************************************************************************** */
int cscalc(fp, ptxt, parent_csmp, wdbp)
FILE       *fp;             /* Input file */
char * ptxt;                /* Input buffer (if file is NULL) */
struct csmacro * parent_csmp;              /* Symbol table for persistent values */
WEBDRIVE_BASE * wdbp;
{
int last_exp_val;
struct s_expr * this_expr;
struct sym_link * link_next;
unsigned char * x;
struct csmacro *csmp = sub_malloc(parent_csmp->e2mbp, sizeof(struct csmacro));

    memset(csmp, 0, sizeof(struct csmacro));
    csmp->first_free = &csmp->alloced[0];
    csmp->vt = parent_csmp->vt; 
    csmp->e2mbp = parent_csmp->e2mbp; 
    csmp->in_file = fp;
    csmp->in_base = ptxt;
    csmp->in_buf = ptxt;
    csmp->wdbp = wdbp;
    if (wdbp != NULL && wdbp->verbosity > 2 && ptxt != NULL)
        fprintf(stderr, "(Client: %s) Evaluating ... %s\n",
                      wdbp->parser_con.pg.rope_seq, ptxt);
#ifdef DEBUG
#ifdef YYDEBUG
    yydebug=1;
#endif
    fputs("Dumping variable table ...\n", stderr);
    iterate(csmp->vt, NULL, dumpsym);
#endif
    csmp->lex_first = LGOING;
#ifdef DEBUG
    fflush(stdout);
    fflush(stderr);
#endif
    if (!setjmp(csmp->env))
        (void) yyparse(csmp);
#ifdef DEBUG
    fflush(stdout);
    fflush(stderr);
#endif
    if (csmp->lex_first == LMEMOUT)    /* malloc error detected */
    {
        fflush(stdout);
        perror("malloc() failed");
        (void) fprintf(stderr,"Error %d\n",errno);
        fflush(stderr);
        Lfree(csmp);            /* return Lmalloc()'ed space */
        return 0;
    }
    else if (csmp->lex_first == LERROR) /* syntax error detected */
    {
        fflush(stdout);
#ifdef NT4
        fputs("Parser Error\n", stderr);
#else
#ifdef LINUX
        fputs("Parser Error\n", stderr);
#else
#ifdef SOL10
        fputs("Parser Error\n", stderr);
#else
        (void) fprintf( stderr,"Parser Error: %.*s\n",
            (csmp->in_file == (FILE *) NULL) ?
                (csmp->in_buf - csmp->in_base) :
                (csmp->in_file->_ptr - csmp->in_file->_base),
            ((char *)((csmp->in_file == (FILE *) NULL) ?
                (int) csmp->in_base :
                (int) csmp->in_file->_base)));
#endif
#endif
#endif
        fflush(stderr);
        Lfree(csmp);            /* return Lmalloc()'ed space */
        return 0;
    }
/*****************************************************************************
 * Execute the parsed code
 */
    for (this_expr = csmp->expr_anchor,
         last_exp_val = 0;
             this_expr != (struct s_expr *) NULL
          && ((last_exp_val & FINISHED) != FINISHED);
                 this_expr = this_expr->eval_expr)
    {
        last_exp_val = instantiate(csmp, this_expr);
        if (csmp->expr_anchor->out_sym->sym_value.tok_type == UTOK
         && this_expr->out_sym->sym_value.tok_type != UTOK)
            csmp->expr_anchor->out_sym->sym_value =
                 this_expr->out_sym->sym_value;
    }
/*
 * Persist values.
 */
    for (link_next = csmp->upd_anchor;
             link_next != NULL;
                 link_next = link_next->next_link)
    {
        if (link_next->sym != NULL
         && link_next->sym->sym_value.tat_type != TATSTATIC)
        {
            x = (unsigned char *) malloc(link_next->sym->sym_value.tok_len + 1);
            memcpy(x, link_next->sym->sym_value.tok_ptr,
                      link_next->sym->sym_value.tok_len + 1);
            link_next->sym->sym_value.tok_ptr = x;
            link_next->sym->sym_value.tat_type = TATMALLOC;
            if (wdbp != NULL && wdbp->verbosity > 2)
                dumpsym(link_next->sym);
        }
    }
    last_exp_val = 
       (csmp->expr_anchor->out_sym->sym_value.tok_val == 0.0) ? 0 : 1;
    if (wdbp != NULL && wdbp->verbosity > 2)
        fprintf(stderr, "(Client: %s) cscalc() returns %d\n",
                      wdbp->parser_con.pg.rope_seq, last_exp_val);
    parent_csmp->ret_symbol = *(csmp->expr_anchor->out_sym);
    Lfree(csmp);            /* return Lmalloc()'ed space */
    return last_exp_val;
}
/************************************************************************
 *  Lexical Analyser
 */
#ifdef AIX
#undef EOF
#define EOF 255
#endif
#ifdef OSXDC
#undef EOF
#define EOF 255
#endif
static int yylex(yylval, csmp)
YYSTYPE * yylval;
struct csmacro * csmp;
{
/**************************************************************************
 * Lexical analysis of cscalc rules 
 */
enum comstate {CODE,COMMENT};
enum comstate comstate;
char parm_buf[4096];             /* maximum size of single lexical element */
char * parm = parm_buf;

   for ( *parm = (csmp->in_file == (FILE *) NULL)? *csmp->in_buf++: fgetc(csmp->in_file),
          comstate = (*parm == '#') ? COMMENT : CODE;
              *parm != EOF && ((comstate == CODE &&
                ((*parm <= ' '&& *parm > '\0') || *parm > (char) 126))
              ||   (comstate == COMMENT && *parm != '\0'));
                  *parm= (csmp->in_file == (FILE *) NULL)? *csmp->in_buf++:fgetc(csmp->in_file),
                 comstate = (comstate == CODE) ?
                      ((*parm == '#') ? COMMENT : CODE) :
                      ((*parm == '\n') ? CODE : COMMENT));
                /* skip over white space and comments */
    while(*parm != EOF && *parm != '\0')
    switch (*parm)
    {                               /* look at the character */
    case ';':
    case ',':
             yylval->ival = ';';    
             return ';';      /* return the operator */
    case '{':

             yylval->ival = '{';    
             return '{';      /* return the operator */
    case '[':
    case '(':
             yylval->ival = '(';    
             return '(';
    case '}':
             yylval->ival = '}';    
             return '}';      /* return the operator */
    case ']':
             yylval->ival = ']';    
             return END;      /* return the operator */
    case ')':
             yylval->ival = ')';    
             return ')';
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case ':':
    case '?':
             yylval->ival = (int) *parm;    
             return *parm;      /* return the operator */
    case '!':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)? *csmp->in_buf++: fgetc(csmp->in_file);
             if (*parm  != '=')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                yylval->ival = '!';    
                return '!';      /* return the operator */ 
             }
                                 /* else */
             return NE;          /* return '!='         */
    case '|':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)? *csmp->in_buf++: fgetc(csmp->in_file);
             if (*parm != '|')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                return '|';      /* return the operator */ 
             }
                                 /* else */
             yylval->ival = OR;    
             return OR;          /* return '||'         */
    case '&':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)?
                          *csmp->in_buf++ :
                           fgetc(csmp->in_file);
             if (*parm != '&')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                return '&';      /* return the operator */ 
             }
                                 /* else */
             yylval->ival = AND;    
             return AND;          /* return '&&'         */
    case '=':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)?
                         *csmp->in_buf++:
                          fgetc(csmp->in_file);
             if ( *parm != '=')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                return '=';      /* return the operator */ 
             }
                                 /* else */
             yylval->ival = EQ;    
             return EQ;          /* return '=='         */
    case '<':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)? *csmp->in_buf++: fgetc(csmp->in_file);
             if (*parm != '=')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                yylval->ival = '<';    
                return '<';      /* return the operator */ 
             }
             yylval->ival = LE;    
             return LE;          /* return '<='         */
    case '>':
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)?
                         *csmp->in_buf++ :
                          fgetc(csmp->in_file);
             if (*parm != '=')   
             {
                if (csmp->in_file == (FILE *) NULL)
                    csmp->in_buf--;
                else
                    (void) ungetc(*parm, csmp->in_file);
                yylval->ival = '>';    
                return '>';      /* return the operator */ 
             }
             yylval->ival = GE;    
             return GE;          /* return '>='         */
    case '"':
    case '\\':
    case '`':
    case '\'':                         /* string; does not support
                                       * embedded quotes, but concatenation
                                       * and alternative delimiters provide
                                       * work-rounds
                                       */
             yylval->tval = newexpr(csmp, new_loc_sym(csmp));
                                       /* allocate a symbol */
             parm++;
             *parm = (csmp->in_file == (FILE *) NULL)?
                         *csmp->in_buf++ :
                         fgetc(csmp->in_file);
             if (*parm == EOF) return 0;
                       /* ignore empty non-terminated string */
             for (yylval->tval->out_sym->sym_value.tok_len = 0;
                      *parm != EOF && *parm++ != parm_buf[0];
                            *parm = (csmp->in_file == (FILE *) NULL) ?
                                     *csmp->in_buf++ :
                                     fgetc(csmp->in_file),
                            yylval->tval->out_sym->sym_value.tok_len++);
                                      /* advance to end of string */
                 
                 yylval->tval->out_sym->sym_value.tok_ptr =
                      Lmalloc(csmp, yylval->tval->out_sym->sym_value.tok_len+1);
                 memcpy(yylval->tval->out_sym->sym_value.tok_ptr,parm_buf+1,
                            yylval->tval->out_sym->sym_value.tok_len);
                 *(yylval->tval->out_sym->sym_value.tok_ptr
                      + yylval->tval->out_sym->sym_value.tok_len) = '\0';
                 yylval->tval->out_sym->sym_value.tok_val =
                            strtod(yylval->tval->out_sym->sym_value.tok_ptr,
                                      (char **) NULL);
                 yylval->tval->out_sym->sym_value.tok_type = STOK;
#ifdef DEBUG
    (void) fprintf(stderr,"Found STRING %s\n",
                 yylval->tval->out_sym->sym_value.tok_ptr);
    (void) fflush(stderr);
#endif   
                 return E2STRING ;
    case '.':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':                         /* apparently a number */
    {
        char * x;                      /* for where strtod() gets to */
        yylval->tval = newexpr(csmp, new_loc_sym(csmp)); /* allocate a symbol */
/*
 * First pass; collect likely numeric characters
 */
        for ( parm++,
             *parm = (csmp->in_file == (FILE *) NULL) ?
                         *csmp->in_buf++: fgetc(csmp->in_file);
                  *parm == '.' || ( *parm >='0' && *parm <= '9');
                       parm++,
                       *parm = (csmp->in_file == (FILE *) NULL) ?
                                    *csmp->in_buf++ :
                                    fgetc(csmp->in_file));
           if (csmp->in_file == (FILE *) NULL)
               csmp->in_buf--;
           else
               (void) ungetc(*parm, csmp->in_file);
        *parm = '\0';
/*
 * Now apply a numeric validation to it
 */
       yylval->tval->out_sym->sym_value.tok_val = strtod( parm_buf, &x);
                                      /* convert */
       yylval->tval->out_sym->sym_value.tok_len = x - parm_buf;
/*
 * Push back any over-shoot
 */
       while (parm > x)
       {
           parm--;
           if (csmp->in_file == (FILE *) NULL)
               csmp->in_buf--;
           else
               (void) ungetc(*parm,csmp->in_file);
       }
       *parm = '\0';
       yylval->tval->out_sym->sym_value.tok_ptr =
                      Lmalloc(csmp, yylval->tval->out_sym->sym_value.tok_len+1);
       memcpy(yylval->tval->out_sym->sym_value.tok_ptr,parm_buf,
                            yylval->tval->out_sym->sym_value.tok_len);
       *(yylval->tval->out_sym->sym_value.tok_ptr
              + yylval->tval->out_sym->sym_value.tok_len) = '\0';
       yylval->tval->out_sym->sym_value.tok_type = NTOK;
#ifdef DEBUG
   (void) fprintf(stderr,"Found NUMBER %s\n",
                         yylval->tval->out_sym->sym_value.tok_ptr);
   (void) fflush(stderr);
#endif   
       return NUMBER;
     }
     default:                        /* assume everything else is an identifier
                                      * or a reserved word
                                      */
         for (;
             (isalnum (*parm) || *parm == '_' || *parm == '$' || *parm == '.' );
                  *parm = islower(*parm) ? toupper(*parm) : *parm,
                  parm++,
                  *parm = (csmp->in_file == (FILE *) NULL)?
                              *csmp->in_buf++ :
                              fgetc(csmp->in_file));
         if (csmp->in_file == (FILE *) NULL)
             csmp->in_buf--;
         else
             (void) ungetc(*parm, csmp->in_file); /* skip this character */
         if (parm_buf != parm)
         {
         HIPT* xent;

             *parm = '\0';
             if ((xent = lookup(csmp->vt,parm_buf)) == (HIPT *) NULL)
             {                                   /* New symbol */
                 yylval->tval = newexpr(csmp, parm_buf);
                 yylval->tval->fun_arg = IDENT;
                 yylval->tval->eval_func = findop(IDENT);
             }
             else                                 /* Existing Symbol */
             {
                 if (((struct symbol *) xent->body)->sym_value.tok_type == RTOK)
                 {
                     if (((struct symbol *) xent->body)->sym_value.tok_conf == 
                           SYSDATE)
                     {
                         if ((xent = lookup(csmp->vt,"xSYSDATE"))
                               == (HIPT *) NULL)
                         {
                             yylval->tval = newexpr(csmp, "xSYSDATE");
                                                       /* allocate a symbol */
                             yylval->tval->out_sym->sym_value.tok_ptr
                                     = (char *) yylval->tval;
                             yylval->tval->fun_arg = SYSDATE;
                             yylval->tval->eval_func = findop(SYSDATE);
                         }
                         else
                             yylval->tval = (struct s_expr *)
                            ((struct symbol *) xent->body)->sym_value.tok_ptr;
#ifdef DEBUG
(void) fprintf(stderr,"SYSDATE: Output %.16g\n",
                yylval->tval->out_sym->sym_value.tok_val);
    (void) fflush(stderr);
#endif
                         return SYSDATE;
                     }
                     else yylval->ival =
                           ((struct symbol *) xent->body)->sym_value.tok_conf;
                     if (yylval->ival == END)
                         return 0;
                     else 
                         return yylval->ival;
                }
                else
                {
                     yylval->tval = newexpr(csmp, new_loc_sym(csmp));
                     yylval->tval->fun_arg = IDENT;
                     yylval->tval->eval_func = findop(IDENT);
                     yylval->tval->out_sym = (struct symbol *) xent->body;
                }
             }
#ifdef DEBUG
   (void) fprintf(stderr,"Found IDENT %s\n",parm_buf);
   (void) fflush(stderr);
#endif   
             return IDENT;
        }
    } /* switch is repeated on next character if have not returned */
    if (*parm == EOF || *parm == '\0')
        return (0);                   /* exit if all done */    

    return ';'; /* ie statement terminator, nice and harmless */
}
#ifdef AIX
#undef EOF
#define EOF -1
#endif
#ifdef OSXDC
#undef EOF
#define EOF -1
#endif
int yyerror()
{
/* We do not have errors */
    return 0;
}
/* End of cspars.y */
