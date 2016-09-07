/*
 * csexe.h - Common definitions for sqldrive macro processing
 * @(#) $Name$ $Id$
 * Copyright (c) E2 Systems Limited 1992
 */
#ifndef CSEXE_H
#define CSEXE_H
#include <setjmp.h>
#include "submalloc.h"
#define EXPTABSIZE 500
/****************************
 * Variable Database information
 */
enum tok_type { NTOK, /* Number */
                STOK, /* String */
                CTOK, /* Constant */
                ITOK, /* Identifier */
                BTOK, /* Binary data */
                DTOK, /* Date */
                RTOK, /* Reserved Word */
                UTOK  /* Unevaluated expression */
              };
enum tok_all_type {
    TATSTATIC,        /* A static value */
    TATAUTO,          /* An automatically reclaimed value */
    TATMALLOC        /* Allocated with malloc() */
};

typedef struct tok {
    enum tok_type tok_type;
    enum tok_all_type tat_type;
    char * tok_ptr;
    double tok_val;
    int tok_len;
    int tok_conf;            /* token ID for reserved words,
                                 write_flag for variables,
                                 used flag for expressions
                               */
    struct tok * next_tok;   /* chain of values/back pointers */
} TOK;

#define UNUSED 0
#define UPDATE 1
#define INUSE 2
#define EVALTRUE 4
#define EVALFALSE 8
#define UNFINISHED 16
#define FINISHED 32
struct symbol
{
  int name_len;
  char * name_ptr;
  TOK sym_value;
};
struct expr_link {
   struct s_expr * s_expr;
   struct expr_link * next_link;
};
struct sym_link {
   struct symbol * sym;
   struct sym_link * next_link;
};
struct s_expr {
   struct symbol * out_sym;   /* The output from the evaluation */
   struct expr_link * in_link; /* Pointer to linked list of input
                                 symbols */
   int fun_arg;               /* Argument for evaluation function/
                                 expression operator */
   int (*eval_func)();        /* The evaluation function */
   struct s_expr * eval_expr; /* Pointer to the next expression in the
                               * block
                               */
   struct s_expr * link_expr; /* Pointer to the next expression in the
                               * block
                               */
   struct s_expr * cond_expr; /* Pointer to the conditional expression whose
                               * truth this depends on
                               */
};
char * new_loc_sym();         /* create a new local symbol name */
struct symbol * newsym();
struct s_expr *newexpr(), * opsetup();
int (*findop())();
char * Lmalloc();
void Lfree();
#define NUMSIZE 23
                                /* space malloc()'ed to take doubles */
int instantiate();
void cscalc_init();
char * getvarval();
void putvarval();
void cscalc_zap();
/**********************************************************************
 * Where the lexical analysis has got to
 */
enum lex_state {LBEGIN, /* First time */
                LGOING,  /* Currently processing */
                LERROR,  /*  Syntax error detected by yacc parser */
                LMEMOUT  /*  malloc() failure */
}; 
int cscalc();
void dumpsym();
/***************************************************************************
 *  The symbol table and the output heap
 *  -   Named variables are persistent
 *  -   Everything is is recovered at the end of the evaluation
 *  -   Care is needed with the final result
 */
struct csmacro {
    char *  alloced[EXPTABSIZE*4];                /* space to remember allocations        */
    char **  first_free;             /* space to remember allocations        */
    int locsym_count;                /* local symbol count = 0 to begin with */
    HASH_CON * vt;                   /* Variable table                       */
    char locsym_name[40];            /* Local symbol name                    */
    enum lex_state lex_first;        /* Lexical scan state                   */
    struct s_expr * expr_anchor;     /* The root of the parse tree           */
    struct sym_link * upd_anchor;    /* Chain of substitutions to update     */
    struct sym_link * last_link;     /* Current end of chain                 */
    jmp_buf env;
    FILE * in_file;                  /* If input is coming from a file       */
    char * in_base;                  /* If input is passed as text           */
    char * in_buf;
    struct symbol ret_symbol;        /* For compatibility with old version   */
    struct e2malloc_base *e2mbp;     /* Subsidiary malloc control            */
    struct _webdrive_base * wdbp;
};
void read_config();
#endif
