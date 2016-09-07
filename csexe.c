/*
 * csexe.c - module that implements the PATH C/S Expression Evaluator
 *
 * Back in 1989, SO Systems developed an expert system for sizing computer
 * systems based on the answers to questions for which answers are readily
 * available from a Business Unit; how many users there will be, how many
 * customers, how many stock lines etc. etc.
 *
 * The developer was more interested in Expert System Shells than in the
 * application of Expert Systems, and so he set to work to write his own
 * expert system shell.
 *
 * He focused on the inference engine.
 *
 * So rather than something we could sell, we ended up with a prototype
 * inference engine, with a rudimentary set of rules loaded directly into
 * the tree, which mostly served to test the inference engine.
 *
 * In an attempt to make the inference engine more generally useful, David
 * Edwards wrote a YACC grammar for an Expert System Shell that built a parse
 * tree. However, the writer of the inference engine found that the tree that
 * emerged from applying the YACC LR(1) grammar could not easily be adapted to
 * his engine, apparently because he had thought in terms of LL(1).
 *
 * When thinking about the features that Supernat V.3 should have over and
 * above Supernat V.2, an Expert System to manage Job Interdependencies sounded
 * suitably sexy, so David Edwards cobbled together a backward-chaining
 * inference engine to go with the YACC parser.
 *
 * This is not a smart piece of code, and may yet, sixteen or seventeen years
 * later, still be full of bugs. March 2012 - Twenty Three Years Later!?
 *
 * However, by virtue of E2 Systems' acquisition of the wound-up SO Systems'
 * Intellectual Property (IP), it is E2 Systems IP, and so, when a requirement
 * arose for an expression processor within PATH C/S (Client/Server), it was
 * adapted for that purpose.
 *
 * And now, Multi-threaded PATH needs an expression evaluation capability, and
 * so the code has been dusted down once more, and made thread-safe (using
 * Bison %pure-parser).
 *
 * We have never written rules with a view to exploiting the backward chaining.
 *
 * So no code exists to print out the chain of rules applied to arrive at an
 * answer.
 *
 * The main impact is that an author who imagines that a succession of
 * statements will simply be executed in sequence one after the other may well
 * get an unpleasant surprise.
 *
 * You have been warned.
 *
 * In this multi-threaded PATH variant, the non-volatile values are the
 * script substitution values. See the companion file, cspars.y, for details.
 *
 * The logical process is that the the rules are evaluated, and possibly new
 * values are determined for the substitution values.
 *
 * The new values are chained together (from upd_anchor).
 *
 * They are picked up after the rules have been evaluated.
 */
static char * sccs_id="@(#) $Name$ $Id$\nCopyright (c) E2 Systems 1992";
/* #define DEBUG 1 */
#ifdef XGUI
#include "recordplay.h"
#else
#include "webdrive.h"
#include <setjmp.h>
#endif
/*
 * Linked list of environment settings read from a file
 */
struct env_con {
    struct env_con * next;
    char *vl;
    char nm[1];
};
static struct env_con * env_anchor; 
static void write_config();
static void env_note();
static int envstuff();
static void env_replace();
static void env_update();
extern double atof();
extern double strtod();
extern long strtol();
extern double floor();
extern double fmod();
extern char * getenv();
extern char * to_char();    /* date manipulation */
#include "cscalc.h"
struct symbol * newsym();
struct s_expr  *newexpr(), * opsetup();
static struct res_word {char * res_word;
    int tok_val;
} res_words[] = { {"IS_DATE", IS_DATE},
    {"TO_DATE",TO_DATE},
    {"GETPID",GETPID},
    {"PUTENV",PUTENV},
    {"TRUNC",TRUNC},
    {"SYSTEM",SYSTEM},
    {"CHR",CHR},
    {"SYSDATE",SYSDATE},
    {"LENGTH",LENGTH},
    {"INSTR",INSTR},
    {"SUBSTR",SUBSTR},
    {"AND",AND},
    {"OR",OR},
    {"HEXTORAW",HEXTORAW},
    {"SET_MATCH",SET_MATCH},
    {"GET_MATCH",GET_MATCH},
    {"URL_ESCAPE",URL_ESCAPE},
    {"URL_UNESCAPE",URL_UNESCAPE},
    {"EVAL",EVAL},
    {"BYTEVAL",BYTEVAL},
    {"RECOGNISE_SCAN_SPEC",RECOGNISE_SCAN_SPEC},
#ifdef XGUI
    {"CALC_CHECKSUM",CALC_CHECKSUM},
#endif
    {"UPPER",UPPER},
    {"LOWER",LOWER},
    {"GETENV",GETENV},
    {"TO_CHAR",TO_CHAR},
    {"NUM_CHAR",NUM_CHAR},
    {"END",END},
    {NULL,0} } ;
/*
 * Functions implemented herein
 */
static int altern();
static int arithop();
static int assgn();
static int ident();
static int instr();
static int is_date();
static int length();
static int lgetenv();
static int lgetpid();
static int logop();
static int lputenv();
static int calc_checksum_fun();
static int lto_char();
static int lnum_char();
static int matchop();
static int substr();
static int sysdate();
static int to_date();
static int trunc();
static int system_fun();
static int eval_fun();
static int chr_fun();
static int byteval_fun();
static int rcs_fun();
static int updown();
static int hextoraw();
static int get_match();
static int set_match();
/*
 * The functions that implement particular expressions
 * Some functions do more than one type.
 */
static struct eval_funs {
   int op;
   int (*eval_fun)();
} eval_funs [] = {
{IS_DATE, is_date},
{TO_DATE, to_date},
{TO_CHAR, lto_char},
{NUM_CHAR, lnum_char},
{ IDENT, ident},
{ SYSDATE, sysdate},
{TRUNC, trunc},
{SYSTEM, system_fun},
{CHR, chr_fun},
{EVAL, eval_fun},
{BYTEVAL, byteval_fun},
{RECOGNISE_SCAN_SPEC, rcs_fun},
{URL_ESCAPE, updown},
{URL_UNESCAPE, updown},
{LENGTH, length},
{HEXTORAW, hextoraw},
{UPPER, updown},
{LOWER, updown},
{INSTR, instr},
{SET_MATCH, set_match},
{GET_MATCH, get_match},
#ifdef XGUI
{CALC_CHECKSUM, calc_checksum_fun},
#endif
{GETENV, lgetenv},
{GETPID, lgetpid},
{PUTENV, lputenv},
{SUBSTR, substr},
{'=', assgn},
{'?', altern},
{OR, logop},
{AND, logop},
{'|', arithop},
{'^', arithop},
{'&', arithop},
{EQ, matchop},
{NE, matchop},
{'<',matchop},
{'>',matchop},
{LE,matchop},
{GE, matchop},
{LS,arithop},
{RS, arithop},
{'+',arithop},
{'-', arithop},
{'*',arithop},
{'/',arithop},
{'%', arithop},
{UMINUS, arithop},
{'!',arithop},
{'~', arithop},
{0,(int (*)()) NULL}
};
static struct expr_link * in_link();
static struct sym_link * upd_link();
static int locsym_count=0;     /* local symbol count */

/*******************************************************************
 * Routines that are called from elsewhere (principally, cspars.y)
 */
char * new_loc_sym(csmp)         /* create a new local symbol name */
struct csmacro * csmp;
{
    (void) sprintf(csmp->locsym_name,"a%ld", locsym_count++);
    return csmp->locsym_name;
}
/*******************************************************************
 * Dump a symbol
 */
void dumpsym(xsym)
struct symbol * xsym;
{
    fflush(stderr);
    fputs("Dump of Symbol: ",stdout);
    fputs(xsym->name_ptr,stdout);
    if ( xsym->sym_value.tok_ptr != (char *) NULL
      && xsym->sym_value.tok_conf != UNUSED)
        printf(" String value: %-.*s\n",xsym->sym_value.tok_len,
                                xsym->sym_value.tok_ptr);
    fputc('\n',stdout);
    printf(" tok_ptr: %x tat_type: %s next_tok:%x tok_type: %d tok_len: %d\n\
 tok_conf: %d tok_val: %f\n",
        (unsigned long) (xsym->sym_value.tok_ptr),
        (char *) ((xsym->sym_value.tat_type == TATSTATIC) ?
                   "TATSTATIC" : (
                        (xsym->sym_value.tat_type == TATAUTO) ?
                             "TATAUTO" : (
                                  (xsym->sym_value.tat_type == TATMALLOC) ?
                                       "TATMALLOC" : "Invalid"))),
        (unsigned long) (xsym->sym_value.next_tok),
        xsym->sym_value.tok_type,
        xsym->sym_value.tok_len,
        xsym->sym_value.tok_conf,
        xsym->sym_value.tok_val);
    fflush(stdout);
    return;
}
/*******************************************************************
 * Allocate a new symbol
 *
 * Return a pointer to the symbol created or found.
 */
struct symbol * newsym(csmp, symname,tok_type)
struct csmacro * csmp;
char * symname;
enum tok_type tok_type;
{
HIPT * hash_entry;
struct symbol * xsym;

    if ((hash_entry= lookup(csmp->vt,symname))
             != (HIPT *) NULL)
    {
#ifdef DEBUG
        (void) fprintf(stderr,"Re-used symbol %s\n", symname);
#endif
        xsym = (struct symbol *) hash_entry->body;
        if ( xsym->sym_value.tok_conf == UNUSED)
        {
             xsym->sym_value.tok_ptr= (char *) NULL;
             xsym->sym_value.tat_type = TATSTATIC;
        }
    }
    else
/*
 * Named variables are persistent, generated variables are not
 */
    if ((*symname < 'a' && (xsym = (struct symbol *)
                malloc ( sizeof( struct symbol)))
                    != (struct symbol *) NULL)
    ||  (*symname >'Z' && (xsym = (struct symbol *)
                Lmalloc (csmp, sizeof( struct symbol)))
                    != (struct symbol *) NULL))
    {                                 /* there is room for another */
#ifdef DEBUG
        (void) fprintf(stderr,"Creating symbol %s\n", symname);
#endif    
        xsym->name_len = strlen(symname);       /* passed value */
/*
 * The name as well as the symbol are persistent
 */
        if (*symname >'Z')
            xsym->name_ptr = Lmalloc(csmp, xsym->name_len+1);
        else
            xsym->name_ptr = malloc(xsym->name_len+1);
        strcpy(xsym->name_ptr, symname);
        xsym->sym_value.tok_type = tok_type;
        xsym->sym_value.tat_type = TATSTATIC;
        xsym->sym_value.tok_ptr= (char *) NULL;
        xsym->sym_value.next_tok= (TOK *) NULL;
        xsym->sym_value.tok_len= 0;
        xsym->sym_value.tok_val = 0.0;
        xsym->sym_value.tok_conf = UNUSED;
        if ( *symname < 'a' &&
           insert(csmp->vt, xsym->name_ptr, (char *) xsym) == (HIPT *) NULL)
                               /* Allocate the hash entry */
        {
#ifdef DEBUG
            (void) fprintf(stderr,"Failed to add symbol %s to hash table\n",
                   symname);
#endif
            csmp->lex_first=LMEMOUT;
            longjmp(csmp->env,0);
        }                              /* Add it to the hash table */
    }
    else
    {  
#ifdef DEBUG
        (void) fprintf(stderr,"Too many symbols\n");
#endif    
        csmp->lex_first=LMEMOUT;
        longjmp(csmp->env,0);
    }
#ifdef DEBUG
    dumpsym(xsym);
#endif
    return xsym;
}
/***************************************************************************
 * Allocate a new expression, returning a pointer to the the expression
 * created.
 */
struct s_expr * newexpr(csmp, symname)
struct csmacro * csmp;
char * symname;
{
struct s_expr * new_expr;

    if (((new_expr = (struct s_expr *) Lmalloc (csmp, sizeof( struct s_expr)))
                    != (struct s_expr *) NULL)
    && ((new_expr->out_sym = newsym(csmp, symname,UTOK))
                    != (struct symbol *) NULL))
    {                        /* there is room for another */
        new_expr->fun_arg = 0;
        new_expr->eval_func = (int (*)()) NULL;
        new_expr->eval_expr = (struct s_expr *) NULL;
        new_expr->link_expr = (struct s_expr *) NULL;
        new_expr->cond_expr = (struct s_expr *) NULL;
        new_expr->in_link = (struct expr_link *) NULL;
        new_expr->out_sym->sym_value.tat_type = TATAUTO;
        new_expr->out_sym->sym_value.tok_ptr = (char *) new_expr;
                             /* Point the symbol back to the expression,
                                until it has a value */
    }
    else
    {  
#ifdef DEBUG
        (void) fprintf(stderr,"Too many expressions\n");
#endif    
        csmp->lex_first=LMEMOUT;
        longjmp(csmp->env,0);
    }
    return new_expr;
}
/*****************************************************************************
 * Chain together the input expressions
 */
static struct expr_link * in_link(csmp, in_expr)
struct csmacro * csmp;
struct s_expr * in_expr;
{
struct expr_link * new_link;

    if ((new_link = (struct expr_link *) Lmalloc (csmp,
                       sizeof( struct expr_link)))
                    != (struct expr_link *) NULL)
    {                        /* there is room for another */

        new_link->s_expr = in_expr;
        new_link->next_link = (struct expr_link *) NULL;
    }
    else
    {  
#ifdef DEBUG
        (void) fprintf(stderr,"Too little memory\n");
#endif    
        csmp->lex_first=LMEMOUT;
        longjmp(csmp->env,0);
    }
    return new_link;
}
/**************************************************************************
 * Function to identify the function that processes a given expression type
 */
int (*findop(op))()
int op;
{
struct eval_funs * x;

    for (x = eval_funs;;x++)
       if (x->op == op || x->op == 0)
           return x->eval_fun;
}
/**************************************************************************
 * Function to link together expressions for processing
 */
struct s_expr * opsetup(csmp, op,in_cnt,in_expr)
struct csmacro * csmp;
int op;
int in_cnt;
struct s_expr ** in_expr;
{
struct expr_link ** x_link;
struct s_expr ** x_expr;
struct s_expr * new_expr = newexpr(csmp, new_loc_sym(csmp));
                                          /* allocate a new expression */
    new_expr->fun_arg = op;
    new_expr->eval_func = findop(op);
    for (x_link = &(new_expr->in_link),
         x_expr = in_expr;
             in_cnt;
                 in_cnt--,
                 x_link = &((*x_link)->next_link),
                 x_expr = x_expr + 1)
        *x_link = in_link (csmp, *x_expr);
    return new_expr;            /* Stack a pointer to the new expression */
}
/*************************************************************************
 * All these functions process an expression node.
 * Points:
 * -  A lot of the processing is operator independent:
 *    -  Deciding whether the input expressions need to be evaluated
 *    -  Setting up the output values
 *    -  Returning yes or no.
 * -  However, with AND, OR and alternatives, do not necessarily need
 *    to process all the inputs.
 * -  So, the choice is between a small number of 'high level' functions,
 *    calling simple functions that just evaluate arguments, and a single
 *    'instantiate' function that is called from the simple functions to fill
 *    in values, and which itself calls simple functions.
 * -  The latter is closer in spirit to what we already have, so that is the
 *    way we shall go.
 ****************************************************************************
 * This function is the main driver, which is supposed to be like an expert
 * system shell. It has the following tasks:
 * - Loop through all linked expressions
 *   For each:
 *    - Check whether the condition applies.
 *      -  Inspect the value for the conditional expression.
 *      -  If it has not yet been processed, call instantiate() to process it
 *      -  If FINISH is indicated, finish
 *    - If the condition does not apply, continue
 *      (ie. the rule is inapplicable)
 *    - If the condition does apply, look at the output symbol. 
 *      Follow the chain of values, looking for an undefined reference 
 *      with a back pointer to this value that is unused. 
 *    - If it exists, and it is unused, stamp it as used
 *    - Otherwise, search for an unused back-pointer
 *    - If found, call instantiate() for its parent expression
 *  - If nothing is found return EVALTRUE or EVALFALSE, depending on the
 *    truth value of the last value element encountered (with the UPDATE
 *    flag set, if any have this flag).
 *  - Otherwise, return the value of the last indicated function
 */
int instantiate(csmp, s_expr )
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct s_expr * l_expr;
TOK * this_tok;
int ret_value = EVALTRUE;  /* make sure that it has a value */

    for (l_expr = s_expr;
             l_expr != (struct s_expr *) NULL;
                 l_expr = l_expr->link_expr)
    {
#ifdef DEBUG
(void)    fprintf(stderr,"l_expr:%x Instantiating %s, function %d\n",
                  (unsigned long) s_expr,
                  l_expr->out_sym->name_ptr,
                  l_expr->fun_arg);
(void)    fflush(stderr);
#endif
        if (l_expr->cond_expr != (struct s_expr *) NULL)
        {
#ifdef DEBUG
(void)      fprintf(stderr,"Following a condition\n");
(void)      fflush(stderr);
#endif
            ret_value = instantiate(csmp, l_expr->cond_expr);
            if (ret_value & FINISHED)
                return ret_value;
            else
            if (ret_value & EVALFALSE)
                continue;
        }
        this_tok =  &(l_expr->out_sym->sym_value);
/*
 * If the output expression is already instantiated, we have
 * already walked this part of the tree, update the truth value
 */
        if (this_tok != NULL
          && this_tok->tok_conf != UNUSED)
            ret_value = (this_tok->tok_val == 1.0) ? EVALTRUE : EVALFALSE;
        else
        {
/*
 *      Follow the chain of values, looking for an undefined reference 
 *      with a back pointer to this value that is unused. 
 */
            for (;
                     this_tok != (TOK *) NULL
                  && this_tok->tok_ptr != (char *) l_expr
                  && this_tok->tok_conf != UNUSED;
                         this_tok = this_tok->next_tok);
/*
 * We have found an unused one.
 */
            if (this_tok != (TOK *) NULL)
            {
#ifdef DEBUG
(void)      fprintf(stderr,"Processing the function\n");
(void)      fflush(stderr);
#endif
                this_tok->tok_conf = INUSE; /* If it exists stamp it as used */
                if (l_expr->eval_func != (int (*)()) NULL)
                    ret_value = (*l_expr->eval_func) (csmp, l_expr );
#ifdef DEBUG
(void)      fprintf(stderr,"Output Symbol is now\n");
(void)      fflush(stderr);
                dumpsym(l_expr->out_sym);
#endif
                if (ret_value & FINISHED)
                    return ret_value;
            }
            else
            {               /* Otherwise, search for any unused back-pointer */
#ifdef DEBUG
(void)    fprintf(stderr,"Already used; searching for unused back-pointer\n");
(void)    fflush(stderr);
#endif
                for (this_tok =  &(l_expr->out_sym->sym_value);
                         this_tok != (TOK *) NULL
                      && this_tok->tok_conf != UNUSED;
                             this_tok = this_tok->next_tok);
/*
 * We have found an unused one.
 */
                if (this_tok != (TOK *) NULL)
                {
#ifdef DEBUG
(void)          fprintf(stderr,"Following a related reference\n");
(void)          fprintf(stderr,"Related Symbol is\n");
                    dumpsym(((struct s_expr *) (this_tok->tok_ptr))->out_sym);
#endif
                    ret_value = instantiate (csmp, (struct s_expr *) this_tok->tok_ptr);
                    if (ret_value & FINISHED)
                        return ret_value;
                }
            }
        }
    }
#ifdef DEBUG
(void)    fprintf(stderr,"No more at this level, ret_value %d\n", ret_value);
(void)    fflush(stderr);
#endif
    return ret_value;
}
/*****************************************************************************
 * Chain together the input expressions that update persistent values.
 *
 * Make sure each symbol only occurs once.
 */
static struct sym_link * upd_link(csmp, in_sym)
struct csmacro * csmp;
struct symbol * in_sym;
{
struct sym_link * new_link;

    for (new_link = csmp->upd_anchor;
             new_link != NULL && new_link->sym != in_sym;
                 new_link = new_link->next_link);
    if (new_link == NULL
     && (new_link = (struct sym_link *) Lmalloc (csmp,
                       sizeof( struct sym_link)))
                    != (struct sym_link *) NULL)
    {                        /* there is room for another */
        new_link->sym = in_sym;
        new_link->next_link = (struct sym_link *) NULL;
        if (csmp->upd_anchor == (struct sym_link *) NULL)
            csmp->upd_anchor = new_link;
        else
            csmp->last_link->next_link = new_link;
        csmp->last_link = new_link;
    }
    return new_link;
}
/****************************************************************************
 * Individual functions:
 * - These generally work as follows:
 *   -  Instantiate as many input variables as are necessary
 *   -  Process the input arguments
 *   -  Set the value of the output corresponding to this expression
 *   -  Return the truth value
 ***************************************************************************
 * Process the '?' ':' operation.
 */
static int altern(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int ret_value;
struct symbol * x;
struct symbol * x1;
struct s_expr * s;

    x = s_expr->out_sym;
    ret_value = instantiate(csmp, s_expr->in_link->s_expr);
    if (ret_value & EVALTRUE)
        s = s_expr->in_link->next_link->s_expr;
    else
        s = s_expr->in_link->next_link->next_link->s_expr;
    ret_value = instantiate(csmp, s);
    x1 = s->out_sym;
    x->sym_value.tok_ptr = x1->sym_value.tok_ptr ;
    if (x1->sym_value.tat_type == TATSTATIC)
        x->sym_value.tat_type = TATSTATIC;
    else
        x->sym_value.tat_type = TATAUTO; /* The other one will sort it out */
    x->sym_value.tok_type = x1->sym_value.tok_type ;
    x->sym_value.tok_val = x1->sym_value.tok_val ;
    x->sym_value.tok_len = x1->sym_value.tok_len ;
    return ret_value;
}
/****************************************************************************
 * Implement all the arithmetic operators, plus string concatenation
 */
static int arithop(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1, *x3;

    x0 = s_expr->out_sym;
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    if (s_expr->in_link->next_link != (struct expr_link *) NULL)
    {
        (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
        x3 = s_expr->in_link->next_link->s_expr->out_sym;
    }
    if (s_expr->fun_arg == (int) '+'
       && ( (x1->sym_value.tok_type == STOK
           || x3->sym_value.tok_type == STOK)
           || (x1->sym_value.tok_type == DTOK
               && x3->sym_value.tok_type == DTOK)))
    {             /* String concatenation */
        x0->sym_value.tok_type = STOK;
        x0->sym_value.tok_len = x1->sym_value.tok_len+x3->sym_value.tok_len;
        x0->sym_value.tok_ptr =
               Lmalloc(csmp, x1->sym_value.tok_len+x3->sym_value.tok_len+1);
        x0->sym_value.tat_type = TATAUTO;
        memcpy(x0->sym_value.tok_ptr,
               x1->sym_value.tok_ptr,x1->sym_value.tok_len);
        memcpy(x0->sym_value.tok_ptr + x1->sym_value.tok_len,
                x3->sym_value.tok_ptr,x3->sym_value.tok_len);
        x0->sym_value.tok_val = (double) 0.0;
        *(x0->sym_value.tok_ptr + x0->sym_value.tok_len ) = '\0';
    }
    else /* Treat as arithmetic */
    {
        switch (s_expr->fun_arg)
        {
        case '+':
            if (x1->sym_value.tok_type == DTOK)
            {
                 x0->sym_value.tok_type = DTOK;
                 x0->sym_value.tok_val = x1->sym_value.tok_val +
                      x3->sym_value.tok_val*((double) 86400.0);
            }
            else if (x3->sym_value.tok_type == DTOK)
            {
                 x0->sym_value.tok_type = DTOK;
                 x0->sym_value.tok_val = x3->sym_value.tok_val +
                                    x1->sym_value.tok_val*((double) 86400.0);
            }
            else
            {
                 x0->sym_value.tok_type = NTOK;
                 x0->sym_value.tok_val = x3->sym_value.tok_val +
                                                x1->sym_value.tok_val;
            }
            break;
        case '-':
            if (x1->sym_value.tok_type == DTOK)
            {
                if (x3->sym_value.tok_type != DTOK)
                { 
                    x0->sym_value.tok_type = DTOK;
                    x0->sym_value.tok_val = x1->sym_value.tok_val -
                                    x3->sym_value.tok_val*((double) 86400.0);
                } 
                else
                {   
                    x0->sym_value.tok_type = NTOK;
                    x0->sym_value.tok_val = (x1->sym_value.tok_val -
                                    x3->sym_value.tok_val)/((double) 86400.0);
                } 
            } 
            else
            {
                x0->sym_value.tok_type = NTOK;
                x0->sym_value.tok_val = x1->sym_value.tok_val -
                                                x3->sym_value.tok_val;
            }
            break;
        case '*':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = x1->sym_value.tok_val *
                                                x3->sym_value.tok_val;
            break;
        case '/':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = x1->sym_value.tok_val /
                                                x3->sym_value.tok_val;
            break;
        case UMINUS:
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = -x1->sym_value.tok_val;
            break;
        case '!':
            x0->sym_value.tok_type = NTOK;
            if (x1->sym_value.tok_val == (double) 0.0)
                x0->sym_value.tok_val = (double) 1.0;
            else
                x0->sym_value.tok_val = (double) 0.0;
            break;
        case '~':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = (double)( ~ ((int) x1->sym_value.tok_val));
            break;
        case '%':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = fmod(x1->sym_value.tok_val ,
                                                x3->sym_value.tok_val);
            break;
        case '&':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val =  (double)((int) x1->sym_value.tok_val &
                              (int) x3->sym_value.tok_val);
 
            break;
        case '|':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val =  (double)((int) x1->sym_value.tok_val |
                              (int) x3->sym_value.tok_val);
 
            break;
        case '^':
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val =  (double)((int) x1->sym_value.tok_val ^
                              (int) x3->sym_value.tok_val);
 
            break;
        case RS:
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val =  (double)((int) x1->sym_value.tok_val >>
                              (int) x3->sym_value.tok_val);
 
            break;
        case LS:
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val =  (double)((int) x1->sym_value.tok_val <<
                              (int) x3->sym_value.tok_val);
 
            break;
        }
        x0->sym_value.tok_ptr = Lmalloc(csmp, NUMSIZE);  /* default value */
        x0->sym_value.tat_type = TATAUTO;
        if (x0->sym_value.tok_type == NTOK)
        {
            (void) sprintf(x0->sym_value.tok_ptr,"%.16g",
                                            x0->sym_value.tok_val);
            x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
        }
        else
        {    /* Date */
            strcpy (x0->sym_value.tok_ptr,
                    to_char("DD-MON-YYYY",x0->sym_value.tok_val));
            x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
        }
    }
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/****************************************************************************
 * An assignment of a value to an identifier.
 */
static int assgn(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int ret_value;
struct symbol * x;
struct symbol * x1;

    (void) instantiate(csmp, s_expr->in_link->s_expr);
    ret_value = instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x = s_expr->in_link->s_expr->out_sym;
    x1 = s_expr->in_link->next_link->s_expr->out_sym;
    x->sym_value.tok_conf |= UPDATE;
/*
 * This should be used to preserve the values afterwards.
 */
    if (x->name_ptr != NULL && *(x->name_ptr) <= 'Z')
        (void) upd_link(csmp, x);
/*
 * The problem that we have is that the existing sym_value.tok_ptr value may
 * have been malloc()ed, or Lmalloc()ed, or may be a fixed value (e.g. "1").
 ****************************************************************************
 * We could find if it was Lmalloc()ed by searching csmp->alloced, but only if
 * we don't allow recursion; we might have to search the entire stack for
 * alloced arrays. Yuck. We need a better way. We need to add an allocation
 * type to the sym_value.
 ***************************************************************************
 */
    if ( x->sym_value.tat_type == TATMALLOC)
    {
        free(x->sym_value.tok_ptr);
        x->sym_value.tok_ptr = NULL;
        x->sym_value.tok_len = 0;
        x->sym_value.tat_type = TATSTATIC;
    }
    if (x1->sym_value.tok_len == 0)
    {
        x->sym_value.tok_ptr = NULL;
        x->sym_value.tok_len = 0;
        x->sym_value.tat_type = TATSTATIC;
    }
    else
    {
        if ( x->sym_value.tat_type == TATSTATIC
          || x->sym_value.tok_len < x1->sym_value.tok_len) 
        {
            x->sym_value.tok_ptr = (char *) Lmalloc( csmp,
                               x1->sym_value.tok_len + 1);
        }
        memcpy(x->sym_value.tok_ptr, x1->sym_value.tok_ptr,
                                     x1->sym_value.tok_len);
        *(x->sym_value.tok_ptr + x1->sym_value.tok_len) = '\0';
        x->sym_value.tat_type = TATAUTO;
    }
    x->sym_value.tok_type = x1->sym_value.tok_type ;
    x->sym_value.tok_val = x1->sym_value.tok_val ;
    x->sym_value.tok_len = x1->sym_value.tok_len ;
#ifdef DEBUG
    puts("assgn()");
    dumpsym(x);
    dumpsym(x1);
#endif
    x = s_expr->out_sym;
    x->sym_value.tok_ptr = x1->sym_value.tok_ptr ;
    x->sym_value.tok_type = x1->sym_value.tok_type ;
    if (x1->sym_value.tat_type == TATSTATIC)
        x->sym_value.tat_type = TATSTATIC;
    else
        x->sym_value.tat_type = TATAUTO;
    x->sym_value.tok_val = x1->sym_value.tok_val ;
    x->sym_value.tok_len = x1->sym_value.tok_len ;
#ifdef DEBUG
    puts("result");
    dumpsym(x);
#endif
    return ret_value;
}
/*****************************************************************************
 * Get the value of a variable from the hash table.
 */
static int ident(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
HIPT * hash_entry;    
/*
 * Look to see if the symbol already exists in the hash table
 */
    if ((hash_entry= lookup(csmp->vt,s_expr->out_sym->name_ptr))
             != (HIPT *) NULL)
    {    /* The symbol is already known; read its value */
        memcpy((char *) s_expr->out_sym,hash_entry->body,
                          sizeof(struct symbol));
#ifdef DEBUG
        (void) fflush(stdout);
        fprintf(stderr, "Read Variable - Name: %s Value %-.*s\n",
                                 s_expr->out_sym->name_ptr,
                         s_expr->out_sym->sym_value.tok_len,
                         s_expr->out_sym->sym_value.tok_ptr);
        (void) fflush(stderr);
#endif
    }
    else
    {
        s_expr->out_sym->sym_value.tok_type = ITOK;
        s_expr->out_sym->sym_value.tok_val = 0.0;
        s_expr->out_sym->sym_value.tok_ptr = NULL;
        s_expr->out_sym->sym_value.tat_type = TATSTATIC;
        s_expr->out_sym->sym_value.tok_len = 0;
        s_expr->out_sym->sym_value.tok_conf = 0;
#ifdef DEBUG
        (void) fflush(stdout);
        fprintf(stderr, "Variable %s is undefined\n",
                                 s_expr->out_sym->name_ptr);
        (void) fflush(stderr);
#endif
    }
#ifdef DEBUG
    puts("ident()");
    dumpsym(s_expr->out_sym);
#endif
    if (s_expr->out_sym->sym_value.tok_val == 0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Get the value of a variable from the hash table.
 */
char * getvarval(csmp, varname)
struct csmacro * csmp;
char * varname;
{
HIPT * hash_entry;    

    if (csmp != NULL
      && csmp->vt != NULL
      && (hash_entry = lookup(csmp->vt,varname)) != (HIPT *) NULL)
    {
        if ( ((struct symbol *) hash_entry->body)->sym_value.tok_ptr == NULL)
            return "";
        else
            return ((struct symbol *) hash_entry->body)->sym_value.tok_ptr;
    }
    else
        return getenv(varname);
}
/*****************************************************************************
 * Put the value of a variable in the hash table. Beware; bad things will
 * happen if you use an existing temporary name.
 */
void putvarval(csmp, varname, varval)
struct csmacro * csmp;
char * varname;
char * varval;
{
HIPT * hash_entry;    
struct symbol * xsym;

    if ( csmp != NULL
      && csmp->vt != NULL)
    {
        if ((hash_entry= lookup(csmp->vt, varname)) != (HIPT *) NULL)
        {    /* The symbol is already known; discard its value */
            xsym = (struct symbol *) (hash_entry->body);
            if ( xsym->sym_value.tok_ptr != (char *) NULL
              && xsym->sym_value.tok_conf != UNUSED)
            {
                if (xsym->sym_value.tat_type == TATMALLOC)
                    free(xsym->sym_value.tok_ptr);
            }
        }
        else
            xsym = newsym(csmp, varname);
        xsym->sym_value.tok_type = STOK;
        xsym->sym_value.tok_val = strtod(varval, NULL);
        xsym->sym_value.tok_ptr = strdup(varval);
        xsym->sym_value.tok_len = strlen(varval);
        xsym->sym_value.tat_type = TATMALLOC;
        xsym->sym_value.tok_conf = INUSE; /* Stamp it as used */
    }
    else
    {
    }
    return;
}
/*****************************************************************************
 * ORACLE-style INSTR()
 * Note that with NULL arguments it returns NULL, not a number.
 */
static int instr(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1, *x3;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->s_expr->out_sym;
    if (x3->sym_value.tok_len == 0 || x1->sym_value.tok_len == 0)
    {
        x0->sym_value.tok_type = STOK;
        x0->sym_value.tok_val = (double) 0.0;
        x0->sym_value.tok_len = 0;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr = NULL;
        x0->sym_value.tok_ptr[0] = '\0';
    }
    else if (x3->sym_value.tok_len > x1->sym_value.tok_len)
    {
        x0->sym_value.tok_type = NTOK;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_val = (double) 0.0;
        x0->sym_value.tok_len = 1;
        x0->sym_value.tok_ptr = "0";
    }
    else
    {
    char * x3_ptr = x1->sym_value.tok_ptr;
    register char * x5_ptr = x3->sym_value.tok_ptr;
    int i = x1->sym_value.tok_len - x3->sym_value.tok_len +1;
    register char * x1_ptr;
    register int j;

        for (;i; i--, x3_ptr++)
        {
            for (j = x3->sym_value.tok_len,
                 x1_ptr = x3_ptr,
                 x5_ptr = x3->sym_value.tok_ptr;
                    j > 0 && *x1_ptr++ == *x5_ptr++;
                      j--);
            if (j == 0)
                break;
        }
        x0->sym_value.tok_type = NTOK;
        if (i == 0)
        {
            x0->sym_value.tat_type = TATSTATIC;
            x0->sym_value.tok_val = (double) 0.0;
            x0->sym_value.tok_len = 1;
            x0->sym_value.tok_ptr = "0";
        }
        else
        {
            x0->sym_value.tok_ptr = Lmalloc(csmp, NUMSIZE);
            x0->sym_value.tat_type = TATAUTO;
            x0->sym_value.tok_val = (double) (x1->sym_value.tok_len -
                                             x3->sym_value.tok_len +2 -i);
            (void) sprintf(x0->sym_value.tok_ptr,"%.16g",
                                            x0->sym_value.tok_val);
            x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
        }
    }
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Check the date is of the required format
 */
static int is_date(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1, *x3;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->s_expr->out_sym;
    x= Lmalloc(csmp, x3->sym_value.tok_len+1);  /* format must be null
                                                   terminated */
    (void) memcpy (x,x3->sym_value.tok_ptr,x3->sym_value.tok_len);
    *(x + x3->sym_value.tok_len) = '\0';
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_len = 1;
    if ( date_val (x1->sym_value.tok_ptr,x,&x,&(x0->sym_value.tok_val)))
    {                                    /* returned valid date */
#ifdef DEBUG
(void) fprintf(stderr,"IS_DATE Valid: Input %*.*s,%*.*s\n",
        x1->sym_value.tok_len,x1->sym_value.tok_len,
                              x1->sym_value.tok_ptr, 
        x3->sym_value.tok_len,x3->sym_value.tok_len,
                              x3->sym_value.tok_ptr);
(void) fprintf(stderr,"             : Output %.16g\n",x0->sym_value.tok_val);
#endif
        x0->sym_value.tok_val = (double) 1.0;
        x0->sym_value.tok_ptr="1";
        x0->sym_value.tat_type = TATSTATIC;
        return EVALTRUE;
    }
    else
    {
#ifdef DEBUG
(void) fprintf(stderr,"IS_DATE Failed: Input %*.*s,%*.*s\n",
        x1->sym_value.tok_len,x1->sym_value.tok_len,
                              x1->sym_value.tok_ptr, 
        x3->sym_value.tok_len,x3->sym_value.tok_len,
                              x3->sym_value.tok_ptr);
(void) fprintf(stderr,"              : Output %.16g\n",x0->sym_value.tok_val);
#endif
        x0->sym_value.tok_val = (double) 0.0;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr="0";
        return EVALFALSE;
    }
}
/*****************************************************************************
 * Return the length of a string
 */
static int length(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_ptr= Lmalloc(csmp, NUMSIZE);
    (void) sprintf(x0->sym_value.tok_ptr,"%d", x1->sym_value.tok_len);
    x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
    x0->sym_value.tok_val = (double) x1->sym_value.tok_len;
    x0->sym_value.tat_type = TATAUTO;
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Extract a value from the environment
 */
static int lgetenv(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    x= Lmalloc(csmp, x1->sym_value.tok_len+1);  /* environment string must
                                            be null terminated */
    (void) memcpy (x,x1->sym_value.tok_ptr,x1->sym_value.tok_len);
    *(x + x1->sym_value.tok_len) = '\0';
    if ((x0->sym_value.tok_ptr = getenv(x)) == NULL)
    {
        x0->sym_value.tok_type = STOK;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr = NULL;
        x0->sym_value.tok_len = 0; 
        x0->sym_value.tok_val = (double) 0.0; 
    }
    else
    {
    char * strtodgot;

        x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
        x0->sym_value.tat_type = TATSTATIC;   /* Do not attempt to free()! */
        x0->sym_value.tok_val = strtod( x0->sym_value.tok_ptr,&strtodgot);
                                /* attempt to convert to number */
#ifdef OLD_DYNIX
        if (strtodgot != x0->sym_value.tok_ptr) strtodgot--;
                                /* correct strtod() overshoot
                                   doesn't happen on Pyramid */
#endif
        if (strtodgot == x0->sym_value.tok_ptr + x0->sym_value.tok_len)
                                /* valid numeric */ 
            x0->sym_value.tok_type = NTOK;
        else
            x0->sym_value.tok_type = STOK;
    }
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Set a value in the list of substitutions
 */
static int set_match(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1, *x2;
SCAN_SPEC validate_spec;
SCAN_SPEC *sp;
int len;
/*
 * If the expression processor isn't linked with a Web driver, this
 * feature is meaningless. Take simple-minded evasive action.
 */
    if (csmp->wdbp == NULL)
        return EVALTRUE;
    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x2 = s_expr->in_link->next_link->s_expr->out_sym;
/*
 * Save the Scan Spec Name
 */
    len = ( x1->sym_value.tok_len < (sizeof(validate_spec.scan_key) - 1)) ?
           (x1->sym_value.tok_len) : (sizeof(validate_spec.scan_key) -1);
    (void) memcpy (&validate_spec.scan_key[0],x1->sym_value.tok_ptr, len);
    validate_spec.scan_key[len] = '\0';
/*
 * Search for the Scan Spec.
 */
    if ((sp = find_scan_spec(csmp->wdbp,&validate_spec)) ==  (SCAN_SPEC *) NULL
     || x2->sym_value.tok_ptr == NULL
     || x2->sym_value.tok_ptr[0] == '\0')
    {
        fprintf(stderr,"(Client:%s) Warning: SCAN_SPEC %s %s\n",
                       csmp->wdbp->parser_con.pg.rope_seq,
                       validate_spec.scan_key,
                       (sp == NULL) ? "does not exist" : "cannot be null");
        x0->sym_value.tok_type = STOK;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr = NULL;
        x0->sym_value.tok_len = 0; 
        x0->sym_value.tok_val = (double) 0.0; 
    }
    else
    {
    int olen;

        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr= "1";
        x0->sym_value.tok_len = 1;
        x0->sym_value.tok_val = (double) 1.0; 
        olen = x2->sym_value.tok_len;
        if (sp->c_e_r_o_flag[0] == 'U')
            olen += olen + olen;
        if (sp->encrypted_token == (char *) NULL)
            sp->encrypted_token = (char *) malloc(olen + 1);
        else
            sp->encrypted_token = (char *) realloc( sp->encrypted_token,
                                                 olen + 1);
        if (sp->c_e_r_o_flag[0] == 'U')
        {
            olen = url_escape(sp->encrypted_token, x2->sym_value.tok_ptr,
                                     x2->sym_value.tok_len, 0);
            sp->encrypted_token = (char *) realloc( sp->encrypted_token,
                                                 olen + 1);
            x2->sym_value.tok_len = olen;
        }
        else
            memcpy( sp->encrypted_token, x2->sym_value.tok_ptr,
                                     x2->sym_value.tok_len + 1);
        *(sp->encrypted_token + x2->sym_value.tok_len) = '\0';
    }
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Extract a value from the list of substitutions
 */
static int get_match(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1;
SCAN_SPEC validate_spec;
SCAN_SPEC *sp;
int len;
/*
 * If the expression processor isn't linked with a Web driver, this
 * feature is meaningless. Take simple-minded evasive action.
 */
    if (csmp->wdbp == NULL)
        return EVALTRUE;
    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
/*
 * Save the Scan Spec Name
 */
    len = ( x1->sym_value.tok_len < (sizeof(validate_spec.scan_key) - 1)) ?
           (x1->sym_value.tok_len) : (sizeof(validate_spec.scan_key) -1);
    (void) memcpy (&validate_spec.scan_key[0],x1->sym_value.tok_ptr, len);
    validate_spec.scan_key[len] = '\0';
/*
 * Search for the Scan Spec.
 */
    if ((sp = find_scan_spec(csmp->wdbp,&validate_spec))
             ==  (SCAN_SPEC *) NULL
        || sp->encrypted_token == NULL)
    {
        fprintf(stderr,"(Client:%s) Warning: SCAN_SPEC %s %s\n",
                       csmp->wdbp->parser_con.pg.rope_seq,
                       validate_spec.scan_key,
                       (sp == NULL) ? "does not exists" : "has not been seen");
        x0->sym_value.tok_type = STOK;
        x0->sym_value.tok_ptr = NULL;
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_len = 0; 
        x0->sym_value.tok_val = (double) 0.0; 
    }
    else
    {
        len =  (sp->c_e_r_o_flag[0] == 'H') ?
                     sp->o_len :
                     strlen(sp->encrypted_token);
        x0->sym_value.tok_ptr= Lmalloc(csmp, len );
        x0->sym_value.tat_type = TATAUTO;
        x0->sym_value.tok_len = len;
        memcpy( x0->sym_value.tok_ptr, sp->encrypted_token, len);
        if (sp->c_e_r_o_flag[0] != 'H')
        {
        char * strtodgot;

            x0->sym_value.tok_val = strtod( x0->sym_value.tok_ptr,&strtodgot);
                                /* attempt to convert to number */
#ifdef OLD_DYNIX
            if (strtodgot != x0->sym_value.tok_ptr)
                strtodgot--;
                                /* correct strtod() overshoot
                                   doesn't happen on Pyramid */
#endif
            if (strtodgot == x0->sym_value.tok_ptr + len)
                                /* valid numeric */ 
                x0->sym_value.tok_type = NTOK;
            else
                x0->sym_value.tok_type = STOK;
        }
        else
        {
            x0->sym_value.tok_type = BTOK;
            x0->sym_value.tok_val = (double) (
                (sp->o_len >= 8) ? (*((long int *) (sp->encrypted_token))) :
                ((sp->o_len >= 4) ? (*((int *) (sp->encrypted_token))) :
                ((sp->o_len >= 2) ? (*((short int *) (sp->encrypted_token))) :
                 (*((char *) (sp->encrypted_token))))));
        }
    }
    if ( x0->sym_value.tok_val == (double)0.0)
            return EVALFALSE;
    else
        return EVALTRUE;
}
/****************************************************************************
 * Get the pid
 */
static int lgetpid(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0;

    x0 = s_expr->out_sym;
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_val = (double) getpid();
    x0->sym_value.tok_ptr= Lmalloc(csmp, NUMSIZE);
    x0->sym_value.tat_type = TATAUTO;
    (void) sprintf(x0->sym_value.tok_ptr,"%.16g", x0->sym_value.tok_val);
    x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
    return EVALTRUE;
}
/****************************************************************************
 * Implement AND and OR
 */
static int logop(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
    int ret_value;
    struct symbol * x0;
    x0 = s_expr->out_sym;
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_len = 1;
    ret_value = instantiate(csmp, s_expr->in_link->s_expr);
    if (((ret_value & EVALFALSE) && s_expr->fun_arg == OR)
      || ((ret_value & EVALTRUE) && s_expr->fun_arg == AND))
        ret_value = instantiate(csmp, s_expr->in_link->next_link->s_expr);
    if (ret_value & EVALTRUE)
    {
        x0->sym_value.tok_ptr = "1";
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_val = (double)1.0;
    }    
    else
    {
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr = "0";
        x0->sym_value.tok_val = (double)0.0;
    }
    return ret_value;
}
/*****************************************************************************
 * Put something in the environment
 */
static int lputenv(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    x= (char *) malloc(x1->sym_value.tok_len+1);
/*
 * The environment string must be null terminated and should be static.
 * Call malloc() directly to avoid re-use.
 */
    (void) memcpy (x,x1->sym_value.tok_ptr,x1->sym_value.tok_len);
    *(x + x1->sym_value.tok_len) = '\0';
    x0->sym_value.tok_val = (double) putenv(x);
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_ptr= Lmalloc(csmp, NUMSIZE);
    x0->sym_value.tat_type = TATAUTO;
    (void) sprintf(x0->sym_value.tok_ptr,"%.16g", x0->sym_value.tok_val);
    x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Change a number into a string
 */
static int lnum_char(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    x0->sym_value.tok_val = x1->sym_value.tok_val;
    x0->sym_value.tok_type = STOK;
    x0->sym_value.tok_ptr= Lmalloc(csmp, NUMSIZE);
    x0->sym_value.tat_type = TATAUTO;
    (void) sprintf(x0->sym_value.tok_ptr,"%.16g", x0->sym_value.tok_val);
    x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Convert an internal date entity into a printable string
 */
static int lto_char(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1, *x3;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->s_expr->out_sym;
    x= Lmalloc(csmp, x3->sym_value.tok_len+1);  /* format must be null
                                            terminated */
    (void) memcpy (x,x3->sym_value.tok_ptr,x3->sym_value.tok_len);
    *(x + x3->sym_value.tok_len) = '\0';
    x0->sym_value.tok_type = DTOK;
    x = to_char (x,x1->sym_value.tok_val);
    x0->sym_value.tok_len= strlen(x);
    x0->sym_value.tok_ptr= Lmalloc(csmp, x0->sym_value.tok_len+1);
    x0->sym_value.tat_type = TATAUTO;
    (void) memcpy(x0->sym_value.tok_ptr,x, x0->sym_value.tok_len);
    return EVALTRUE;
}
/****************************************************************************
 * Establish the relationship between two TOK's
 * Returns -1 if the first is less, 0 if equal, +1 if the first one is greater
 */
static int tok_comp(t1, t3)
struct symbol * t1;
struct symbol * t3;
{
/*
 * Can we do a numeric comparison?
 */
    if ((t1->sym_value.tok_type == NTOK && t3->sym_value.tok_type == NTOK)
        || (t1->sym_value.tok_type == DTOK &&
                   t3->sym_value.tok_type == DTOK))
    {
        if (t1->sym_value.tok_val == t3->sym_value.tok_val)
            return 0;
        else
        if (t1->sym_value.tok_val < t3->sym_value.tok_val)
            return -1;
        else
            return 1;
    }
    else
/*
 * In the event of a type mismatch, compare as strings
 */
    {
    register int i = t1->sym_value.tok_len >= t3->sym_value.tok_len
                               ?  t3->sym_value.tok_len
                               :  t1->sym_value.tok_len;
    register char * x1_ptr = t1->sym_value.tok_ptr;
    register char * x2_ptr = t3->sym_value.tok_ptr;

        for (;i && (*x2_ptr == *x1_ptr);
                         i-- , x2_ptr++, x1_ptr++);
        if (i == 0)
        {
            if (t1->sym_value.tok_len == t3->sym_value.tok_len)
                return 0;
            else
            if (t1->sym_value.tok_len < t3->sym_value.tok_len)
                return -1;
            else
                return 1;
        }
        else
        if (*x1_ptr < *x2_ptr)
            return -1;
        else
            return 1;
    }
}
/****************************************************************************
 * Implement the comparison operators
 * EQ, <, > , NE, GE, LE
 */
static int matchop(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int comp_value;
struct symbol * x0;

    (void) instantiate(csmp, s_expr->in_link->s_expr);
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x0 = s_expr->out_sym;
    comp_value = tok_comp(s_expr->in_link->s_expr->out_sym,
                          s_expr->in_link->next_link->s_expr->out_sym);
    x0->sym_value.tok_type = NTOK;
    x0->sym_value.tok_len = 1;
    if ((comp_value == 0
         && ( s_expr->fun_arg == EQ
           || s_expr->fun_arg == GE
           || s_expr->fun_arg == LE))
    || (comp_value == -1
         && ( s_expr->fun_arg == NE
           || s_expr->fun_arg == (int) '<'
           || s_expr->fun_arg == LE))
    || (comp_value == 1
         && ( s_expr->fun_arg == NE
           || s_expr->fun_arg == (int) '>'
           || s_expr->fun_arg == GE)))
    {
        x0->sym_value.tok_ptr = "1";
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_val = (double)1.0;
    }    
    else
    {
        x0->sym_value.tok_ptr = "0";
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_val = (double)0.0;
    }    
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * ORACLE-style SUBSTR()
 */
static int substr(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1, *x2, *x3;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x2 = s_expr->in_link->next_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->next_link->s_expr->out_sym;
    x0->sym_value.tok_type = STOK;
    if ((x3->sym_value.tok_val <= (double) 0.0) ||
         (x1->sym_value.tok_len == 0) ||
         (((int) x2->sym_value.tok_val) > x1->sym_value.tok_len) ||
         ((x2->sym_value.tok_val < (double) 0.0) &&
         (-((int) x2->sym_value.tok_val) > x1->sym_value.tok_len)))
    {
        x0->sym_value.tat_type = TATSTATIC;
        x0->sym_value.tok_ptr = NULL;
        x0->sym_value.tok_val=(double) 0.0;
        x0->sym_value.tok_len=0;
    }
    else
    {
    register int i;
    register char * x2_ptr;
    register char * x1_ptr;
    char * strtodgot;

        if (x2->sym_value.tok_val == (double) 0.0)
        {
            i= x1->sym_value.tok_len;
            x1_ptr = x1->sym_value.tok_ptr;
        }
        else if (x2->sym_value.tok_val > (double) 0.0)
        {
            if (((int) (x2->sym_value.tok_val + 
                     x3->sym_value.tok_val -1.0) <= x1->sym_value.tok_len))
                i = x3->sym_value.tok_val;
            else
                i = 1 + x1->sym_value.tok_len -
                          ((int) x2->sym_value.tok_val);
            x1_ptr = x1->sym_value.tok_ptr + ((int) x2->sym_value.tok_val) -1;
        }
        else
        {
            if ((x0->sym_value.tok_len -
                          ((int) (x2->sym_value.tok_val) + (int)
                 (x3->sym_value.tok_val -1.0) <= x1->sym_value.tok_len)))
                i = x3->sym_value.tok_val;
            else
                i = -((int) x2->sym_value.tok_val);
            x1_ptr = x1->sym_value.tok_ptr + x1->sym_value.tok_len
                          + ((int) x2->sym_value.tok_val);
        }
          
        x2_ptr= Lmalloc(csmp, i+1);
        x0->sym_value.tok_ptr = x2_ptr;
        x0->sym_value.tat_type = TATAUTO;
        x0->sym_value.tok_len = i;
        while (i--)
           *x2_ptr++ = *x1_ptr++;
        *x2_ptr='\0';
        x0->sym_value.tok_val = strtod( x0->sym_value.tok_ptr,&strtodgot);
                                /* attempt to convert to number */
   
#ifdef OLD_DYNIX
        if (strtodgot != x0->sym_value.tok_ptr) strtodgot--;
                                /* correct strtod() overshoot
                                 * doesn't happen on Pyramid */
#endif
        if (strtodgot == x0->sym_value.tok_ptr + x0->sym_value.tok_len)
                                /* valid numeric */ 
            x0->sym_value.tok_type = NTOK;
        else
            x0->sym_value.tok_type = STOK;
    }
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
#ifdef XGUI
/*****************************************************************************
 * Macro access to screen checksum capability
 */
static int calc_checksum_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
struct symbol * x0, *x1, *x2, *x3, *x4;
char * x2_ptr;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x2 = s_expr->in_link->next_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->next_link->s_expr->out_sym;
    (void) instantiate(csmp,
            s_expr->in_link->next_link->next_link->next_link->s_expr);
    x4 = s_expr->in_link->next_link->next_link->next_link->s_expr->out_sym;
    x0->sym_value.tok_type = STOK;
    x2_ptr= Lmalloc(csmp, 17);
    x0->sym_value.tok_ptr = x2_ptr;
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_len = 17;
    sprintf(x0->sym_value.tok_ptr, "%lx", calc_checksum(get_display(),
       (int) x1->sym_value.tok_val,
       (int) x2->sym_value.tok_val,
       (int) x3->sym_value.tok_val,
       (int) x4->sym_value.tok_val));
    x0->sym_value.tok_val = strtod( x0->sym_value.tok_ptr, NULL);
                                /* attempt to convert to number */
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
#endif
/*****************************************************************************
 * Get the current date - only executed once per execution
 */
static int sysdate(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
    if (s_expr->out_sym->sym_value.tok_type != DTOK)
    {
        s_expr->out_sym->sym_value.tok_type = DTOK;
        s_expr->out_sym->sym_value.tat_type = TATAUTO;
        s_expr->out_sym->sym_value.tok_ptr = Lmalloc(csmp, NUMSIZE);
        s_expr->out_sym->sym_value.tok_len = 9;
        (void) date_val((char *) 0,"SYSDATE", (char *) 0,
                    &(s_expr->out_sym->sym_value.tok_val));
        (void) strcpy (s_expr->out_sym->sym_value.tok_ptr,
                     to_char("DD-MON-YY",s_expr->out_sym->sym_value.tok_val));
#ifdef DEBUG
(void) fprintf(stderr,"SYSDATE: Output %.16g\n",
                    s_expr->out_sym->sym_value.tok_val);
#endif
    }
    return EVALTRUE;
}
/****************************************************************************
 * Convert a date string to an internal date representation.
 */
static int to_date(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x;
struct symbol * x0, *x1, *x3;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
    x3 = s_expr->in_link->next_link->s_expr->out_sym;
    x= Lmalloc(csmp, x3->sym_value.tok_len+1);  /* format must be null
                                            terminated */
    (void) memcpy (x,x3->sym_value.tok_ptr,x3->sym_value.tok_len);
    *(x + x3->sym_value.tok_len) = '\0';
    x0->sym_value.tok_type = DTOK;
    (void) date_val (x1->sym_value.tok_ptr,x,&x,
                              &(x0->sym_value.tok_val));
                  /* throw away return code */
    x0->sym_value.tok_ptr = x1->sym_value.tok_ptr;
                               /* i.e. whatever it was */
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_len = x1->sym_value.tok_len;
#ifdef DEBUG
(void) fprintf(stderr,"TO_DATE:  Input %*.*s,%*.*s\n",
        x1->sym_value.tok_len,x1->sym_value.tok_len,
                              x1->sym_value.tok_ptr, 
        x3->sym_value.tok_len,x3->sym_value.tok_len,
                              x3->sym_value.tok_ptr);
(void) fprintf(stderr,"             : Output %.16g\n",
                              x0->sym_value.tok_val);
#endif
    return EVALTRUE;
}
/****************************************************************************
 * Truncate a number to some number of decimal places
 */
static int trunc(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int j;
struct symbol * x0, *x1, *x3;
struct symbol x4;

    x0 = s_expr->out_sym;
    x0->sym_value.tok_ptr= Lmalloc(csmp, NUMSIZE);
    x0->sym_value.tat_type = TATAUTO;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    if (s_expr->in_link->next_link != (struct expr_link *) NULL)
    {
        (void) instantiate(csmp, s_expr->in_link->next_link->s_expr);
        x3 = s_expr->in_link->next_link->s_expr->out_sym;
    }
    else
    {
       x3 = &x4;
       x4.sym_value.tok_type = STOK;
    }
    if (x1->sym_value.tok_type != NTOK || x3->sym_value.tok_type != NTOK
        || floor(x3->sym_value.tok_val) == ((double) 0.0))
    {
        j = 0;
        if (x1->sym_value.tok_type == DTOK)
        {
            x0->sym_value.tok_type = DTOK;
            x0->sym_value.tok_val = floor(x1->sym_value.tok_val/86400.0) *
                                          86400.0;
            x0->sym_value.tok_len = x1->sym_value.tok_len;
            x0->sym_value.tok_ptr = x1->sym_value.tok_ptr;
            return EVALTRUE;
        }
        else
        {
            x0->sym_value.tok_type = NTOK;
            x0->sym_value.tok_val = floor(x1->sym_value.tok_val);
        }
    }
    else
    {
        double x_tmp;
        short int i = (short int) floor(x3->sym_value.tok_val);
        j = i;
        x0->sym_value.tok_type = NTOK;
        for (x_tmp = (double) 10.0;
               i-- > 1 ;
                   x_tmp *= 10.0);
        x0->sym_value.tok_val = floor (x_tmp * x1->sym_value.tok_val) /x_tmp;
    }
    (void) sprintf(x0->sym_value.tok_ptr,"%.*f",
                                (int) j, x0->sym_value.tok_val);
    x0->sym_value.tok_len = strlen(x0->sym_value.tok_ptr);
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Set everything to the same case (lower or upper) or do url escape or unescape
 */
static int updown(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
register int i;
register char * x2_ptr;
register char * x1_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    i = x1->sym_value.tok_len;
    if (s_expr->fun_arg == URL_ESCAPE)
        x2_ptr= Lmalloc(csmp, 3 * i + 1);
    else
        x2_ptr= Lmalloc(csmp, i + 1);
    x1_ptr = x1->sym_value.tok_ptr;
    x0->sym_value.tok_ptr = x2_ptr;
    x0->sym_value.tok_type = STOK;
    x0->sym_value.tat_type = TATAUTO;
    if (s_expr->fun_arg == UPPER)
    {
        x0->sym_value.tok_len = i;
        while (i--)
            if (islower(*x1_ptr)) *x2_ptr++ = *x1_ptr++ & 0xdf;
            else *x2_ptr++ = *x1_ptr++;
        x0->sym_value.tok_val = x1->sym_value.tok_val;
    }
    else
    if (s_expr->fun_arg == LOWER)
    {
        x0->sym_value.tok_len = i;
        while (i--)
            if (isupper(*x1_ptr)) *x2_ptr++ = *x1_ptr++ | 0x20;
            else *x2_ptr++ = *x1_ptr++;
        x0->sym_value.tok_val = x1->sym_value.tok_val;
    }
    else
    if (s_expr->fun_arg == URL_UNESCAPE)
    {
        memcpy(x2_ptr, x1_ptr, i);
        i = url_unescape(x2_ptr, i);
        x0->sym_value.tok_val = strtod(x2_ptr, NULL);
        x0->sym_value.tok_len = i;
        x2_ptr += i;
    }
    else
/*    if (s_expr->fun_arg == URL_ESCAPE) */
    { 
        i = url_escape(x2_ptr, x1_ptr, i, 0);
        x0->sym_value.tok_val = strtod(x2_ptr, NULL);
        x0->sym_value.tok_len = i;
        x2_ptr += i;
    }
    *x2_ptr='\0';
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Translate hexadecimal strings to binary data (for LONG RAW input)
 */
static int hextoraw(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int i;
char * x2_ptr;
char * x1_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    i = x1->sym_value.tok_len;
    x2_ptr= Lmalloc(csmp, i/2+1);
    x0->sym_value.tat_type = TATAUTO;
    x1_ptr = x1->sym_value.tok_ptr;
    x0->sym_value.tok_ptr = x2_ptr;
    x0->sym_value.tok_type = BTOK;
    x0->sym_value.tok_len = i/2;
    hexout(x2_ptr, x1_ptr, i);
    x0->sym_value.tok_val = 1.0;
    return EVALTRUE;
}
/*****************************************************************************
 * Execute a command using the system() library function
 */
static int system_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int i;
char * x2_ptr;
char * x1_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    i = x1->sym_value.tok_len;
    x2_ptr= Lmalloc(csmp, i+1);
    x1_ptr = x1->sym_value.tok_ptr;
    x0->sym_value.tok_ptr = x2_ptr;
    x0->sym_value.tok_type = STOK;
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_len = i;
    memcpy(x2_ptr, x1_ptr, i);
    *(x2_ptr + i) = '\0';
#ifndef MINGW32
    system(x2_ptr);
#else
/*
 * Need to use E2SYSTEM to avoid thread problems. Sort out later
 */
#endif
    x0->sym_value.tok_val = 1.0;
    return EVALTRUE;
}
/*****************************************************************************
 * Execute the argument as an independent evaluation
 */
static int eval_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int i;
char * x2_ptr;
char * x1_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    i = x1->sym_value.tok_len;
    x2_ptr= Lmalloc(csmp, i+1);
    x1_ptr = x1->sym_value.tok_ptr;
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_ptr = x2_ptr;
    x0->sym_value.tok_type = STOK;
    x0->sym_value.tok_len = i;
    memcpy(x2_ptr, x1_ptr, i);
    *(x2_ptr + i) = '\0';
/*
 * There is a problem with respect to which variable values will be seen in the called
 * expression evaluation instance. This should work; the sub-expression should see the
 * current values of the global values, and update them. But what order do things happen
 * in? It needs great care to ensure that what is updated for recursion reasons gets updated
 * before the recurse happens, or we get an infinite recursions.
 * visible.
 */
    if (csmp->wdbp != NULL)
    {
        csmp->wdbp->depth++;
        if (csmp->wdbp->depth > 256)
        {
            csmp->lex_first=LMEMOUT;
            longjmp(csmp->env, 0);
        }
        cscalc(NULL, x2_ptr, csmp, csmp->wdbp);
    }
    x0->sym_value.tok_val = 1.0;
    return EVALTRUE;
}
/*****************************************************************************
 * Return the byte value of the first byte of the argument.
 */
static int byteval_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x1_ptr;
char * x2_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    if ( x1->sym_value.tok_len == 0)
    {
        x0->sym_value.tok_type = STOK;
        x2_ptr = NULL;
        x0->sym_value.tok_val = 0.0;
        x0->sym_value.tok_len = 0;
    }
    else
    {
        x0->sym_value.tok_type = NTOK;
        x2_ptr= Lmalloc(csmp, 4);
        x1_ptr = x1->sym_value.tok_ptr;
        x0->sym_value.tat_type = TATAUTO;
        x0->sym_value.tok_len =
            sprintf(x2_ptr,"%u",((unsigned int) (*x1_ptr & 0xff)));
        x0->sym_value.tok_val = (double) (*x1_ptr & 0xff);
    }
    x0->sym_value.tok_ptr = x2_ptr;
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Create a byte from the value of the argument.
 */
static int chr_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
char * x2_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    x2_ptr= Lmalloc(csmp, 2);
    *x2_ptr = ((unsigned char) (((int) x1->sym_value.tok_val & 0xff))); 
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_len = 1;
    x0->sym_value.tok_type = BTOK;
    x0->sym_value.tok_val = ((double) *x2_ptr);
    x0->sym_value.tok_ptr = x2_ptr;
    if ( x0->sym_value.tok_val == (double)0.0)
        return EVALFALSE;
    else
        return EVALTRUE;
}
/*****************************************************************************
 * Execute the argument as an independent evaluation
 */
static int rcs_fun(csmp, s_expr)
struct csmacro * csmp;
struct s_expr * s_expr;
{
int i;
char * x1_ptr;
char * x2_ptr;
struct symbol * x0, *x1;

    x0 = s_expr->out_sym;
    (void) instantiate(csmp, s_expr->in_link->s_expr);
    x1 = s_expr->in_link->s_expr->out_sym;
    i = x1->sym_value.tok_len;
    x2_ptr= Lmalloc(csmp, i+2);
    x1_ptr = x1->sym_value.tok_ptr;
    *x2_ptr = ':';
      /* Needed because of internal details of recognise_scan_spec() */
    x0->sym_value.tok_ptr = x2_ptr + 1;
    x0->sym_value.tat_type = TATAUTO;
    x0->sym_value.tok_type = STOK;
    x0->sym_value.tok_len = i;
    memcpy(x2_ptr + 1, x1_ptr, i);
    *(x2_ptr + i + 1) = '\0';
    if (csmp->wdbp != NULL)
        recognise_scan_spec(NULL, csmp->wdbp, &x2_ptr);
    x0->sym_value.tok_val = 0.0;
    return EVALTRUE;
}
/******************************************************************
 * Initialise the variable stuff. Call this every now and then to
 * reset all the variables. Note that this does not initialise the
 * symbol table.
 */
void cscalc_init(csmp)
struct csmacro * csmp;
{
struct res_word * xres;    /* for populating reserved words */

    Lfree(csmp);           /* return Lmalloc()'ed space */
    hempty(csmp->vt);      /* Clear the symbol table */
/*
 * Populate it with the reserved words, so that these
 * are available to the lexical analyser
 */
    for (xres = res_words;
            xres->res_word != NULL;
                xres++)
    {
    struct symbol * xsym;

        xsym = newsym(csmp, xres->res_word,RTOK);  /* create a symbol    */
        xsym->sym_value.tok_conf = xres->tok_val;
    }
    return;
}
/******************************************************************
 * Free all memory.
 */
void cscalc_zap(csmp)
struct csmacro * csmp;
{
    Lfree(csmp);            /* return Lmalloc()'ed space */
    return;
}
/***********************************************************************
 * Dynamic space management avoiding need to track all calls to malloc() in
 * the program.
 */
char * Lmalloc(csmp, space_size)
struct csmacro * csmp;
int space_size;
{
    *csmp->first_free = (char *) sub_malloc((struct e2malloc_base *)
                                  &csmp->wdbp->in_buf.buf[0], space_size);
#ifdef DEBUG_E2_MALLOC
    fprintf(stderr, "LM:%x\n", (long) *csmp->first_free);
#endif
    if (*csmp->first_free == NULL)
    {
        csmp->lex_first=LMEMOUT;
        longjmp(csmp->env, 0);
    }
    if (csmp->first_free < &csmp->alloced[EXPTABSIZE *4 - 1])
        return *csmp->first_free++;
    else
        return *csmp->first_free;
}
void Lfree(csmp)
struct csmacro * csmp;
{                                  /* return space to malloc() */
#ifdef DEBUG_E2_MALLOC
/*
 * This routine triggers a malloc() arena report from our malloc()
 */
    fputs("Before Lfree()\n", stderr);
    print_managed( (struct e2malloc_base *) &csmp->wdbp->in_buf.buf[0]);
#endif
    while (csmp->first_free > csmp->alloced)
    {
        csmp->first_free--;
        if (*csmp->first_free != NULL)
        {
#ifdef DEBUG_E2_MALLOC
            fprintf(stderr, "LF:%x\n", (long) *csmp->first_free);
#endif
            sub_free(csmp->e2mbp, *csmp->first_free);
        }
    }
#ifdef DEBUG_E2_MALLOC
    fputs("After Lfree()\n", stderr);
    print_managed( (struct e2malloc_base *) &csmp->wdbp->in_buf.buf[0]);
    fputs("=============\n", stderr);
#endif
    csmp->first_free = csmp->alloced;
    return;
}
/**********************************************************************
 * Routines to manage a configuration file with environment variables
 * in it.
 **********************************************************************
 * This code isn't thread safe, due to the presence of strok() calls
 **********************************************************************
 */
static void env_note(n, v)
char *n;
char *v;
{
int vl;
int nl;
struct env_con * tp;

    if (n != (char *) NULL && v != (char *) NULL)
    {
        nl = strlen(n);
        vl = strlen(v);
        if ((tp = (struct env_con *)
               malloc(sizeof(struct env_con) + nl + vl + 1))
                           != (struct env_con *) NULL)
        {
            tp->next = env_anchor;
            strcpy(&(tp->nm[0]), n);
            tp->vl = &(tp->nm[nl + 1]);
            strcpy(tp->vl, v);
            env_anchor = tp;
        }
    }
    return;
}
/*
 * Put the environment values into the environment; unhook them from the list
 * as they go, to avoid accidently freeing them.
 */
static void env_update()
{
struct env_con * tp;

    while (env_anchor != (struct env_con *) NULL)
    {
        tp = env_anchor;
        tp->nm[strlen(tp->nm)] = '=';
        putenv(&(tp->nm[0]));
        env_anchor = tp->next;
    }
    return;
}
/*
 * Put the environment values into the environment; unhook them from the list
 * as we go, to avoid accidently freeing them.
 */
static void env_replace(n,v)
char * n;
char * v;
{
struct env_con * tp, *tp1;

    for (tp = (struct env_con *) NULL,
         tp1 = env_anchor;
               tp1 != (struct env_con *) NULL;
                   tp = tp1,
                   tp1 = tp1->next)
    {
        if (!strcmp(n, tp1->nm))
        {
            if (tp == (struct env_con *) NULL)
                env_anchor = tp1->next;
            else
                tp->next = tp1->next;
            free((char *) tp1);
            break;
        }
    }
    env_note(n,v);
    return;
}
/*
 * Find an environment value in the environment.
 */
char * env_value(n)
char * n;
{
struct env_con * tp;

    for ( tp = env_anchor;
               tp != (struct env_con *) NULL;
                   tp = tp->next)
    {
        if (!strcmp(n, tp->nm))
            return tp->vl;
    }
    return (char *) NULL;
}
static void env_zap()
{
struct env_con * tp;

    while (env_anchor != (struct env_con *) NULL)
    {
        tp = env_anchor;
        env_anchor = tp->next;
        free((char *) tp);
    }
    return;
}
/*
 * Just to tidy things up; long shell commands can be part of a single
 * menu option, if a trailing \ is provided
 */
static char  * egets(ptr, len, f)
char  * ptr;
int len;
FILE  * f;
{
char  * w;
char  * r;
int to_do;
int linelen;

    for (w = ptr, r = w, to_do = len;
            to_do > 0 && r != (char *) NULL;)
    {
        r = fgets (w, to_do, f);
        if (r == (char *) NULL)
        {
            if (w == ptr)
                return r;
            else
                return ptr;
        }
        linelen = strlen(r);
        w = w + linelen - 2;
        if (w >= ptr && *w == '\r')
        {
            w--;
            to_do--;
        }
        if (w >= ptr && *w == '\\')
        {
            *w++ = '\n';
            to_do = to_do - linelen - 1;
        }
        else
        {
            w++;
            *w = '\0';
            break;
        }
    }
    return ptr;
}
/*
 * Routine to stuff environment variables, so that the shell syntax is valid
 */
int envstuff(in, out, bound)
char * in;
char * out;
char * bound;
{
register char *x1=in, *x2=out;

    while (x2 < bound && *x1 != '\0')
    {
        if (*x1 == '\'')
        {
            *x2++ = '\'';
            *x2++ = '\\';
            *x2++ = '\'';
        }
        *x2++ = *x1++;
    }
    *x2 = '\0';
    return (x2 - out);
}
/*
 * Remove wrapping quotation marks and escapes, UNIX Shell style
 */
static void strip_quotes(raw_string)
char * raw_string;
{
char * bound = raw_string + strlen(raw_string);
char * x;
char nc;
int l;
char * op;
char quote = '\0';

    for ( x = raw_string,
          op = raw_string;
              x < bound;)
    {
        switch(*x)
        {
        case '"':
            if (quote == 0)
            {
                quote = '"';
                x++;
            }
            else
            if (quote == '"')
            {
                quote = 0;
                x++;
            }
            else
                *op++ = *x++;
            break;
        case '\'':
            if (quote == 0)
            {
                quote = '\'';
                x++;
            }
            else
            if (quote == '\'')
            {
                quote = 0;
                x++;
            }
            else
                *op++ = *x++;
            break;
        case '\\':
            if (quote == 0)
                x++;
            else
            if (quote == '"')
            {
                nc = *(x + 1);
                if (nc == '"' || nc == '$' || nc == '\\')
                    x++;
            }
        default:
            if (x == op)
            {
                x++;
                op++;
            }
            else
                *op++ = *x++;
              
            break;
        }
    }
    if (op < bound)
       *op = '\0';
    return;
}
/*
 * Read in a set of environment variable definitions
 */
void read_config(fname)
char * fname;
{
FILE * fp;
char line_buf[4096];
/*
 * Loop through the strings that are fed on the stream until
 * no more, stuffing them in a control structure
 */
    if ((fp = fopen(fname,"rb")) == (FILE *) NULL)
    {
        fprintf(stderr,"%s:%d Failed to open configuration file %s error %d",
                 __FILE__, __LINE__, fname, errno);
        perror("read_config()");
        return;
    }
    while ( egets(line_buf,sizeof(line_buf),fp) != (char *) NULL ) 
    {
    char * n;
    char * v;

        n = strtok(line_buf,"=");
        if ( n == (char *) NULL  || *n == '#')
            continue;
        v = strtok(NULL,"\n\r");
        if (v != (char *) NULL)
        {
            strip_quotes(v);
            env_replace(n, v);
        }
    }
    (void) fclose(fp);
    env_update();
    return;
}
/*
 * Write out a set of environment variable definitions
 */
static void write_config(fname)
char * fname;
{
FILE * fp;
char line_buf[4096];
struct env_con * tp;
/*
 * Loop writing out environment variables to a configuration file
 */
    if ((fp = fopen(fname,"wb")) == (FILE *) NULL)
    {
        fprintf(stderr,"%s:%d Failed to open configuration file %s error %d",
                 __FILE__, __LINE__, fname, errno);
        perror("write_config()");
        return;
    }
    for (tp = env_anchor; tp != (struct env_con *) NULL;tp = tp->next)
         fprintf(fp, "%s=\"%s\"\n", tp->nm, tp->vl);
    (void) fclose(fp);
    return;
}
