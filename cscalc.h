/* A Bison parser, made by GNU Bison 3.0.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2013 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_YY_CSPARS_TAB_H_INCLUDED
# define YY_YY_CSPARS_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    E2STRING = 258,
    NUMBER = 259,
    IDENT = 260,
    IS_DATE = 261,
    TO_DATE = 262,
    TO_CHAR = 263,
    NUM_CHAR = 264,
    SYSDATE = 265,
    TRUNC = 266,
    SYSTEM = 267,
    LENGTH = 268,
    CHR = 269,
    UPPER = 270,
    LOWER = 271,
    INSTR = 272,
    GETENV = 273,
    GETPID = 274,
    PUTENV = 275,
    SUBSTR = 276,
    HEXTORAW = 277,
    GET_MATCH = 278,
    SET_MATCH = 279,
    EVAL = 280,
    BYTEVAL = 281,
    RECOGNISE_SCAN_SPEC = 282,
    URL_ESCAPE = 283,
    URL_UNESCAPE = 284,
    CALC_CHECKSUM = 285,
    END = 286,
    OR = 287,
    AND = 288,
    EQ = 289,
    NE = 290,
    LE = 291,
    GE = 292,
    LS = 293,
    RS = 294,
    UMINUS = 295
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE YYSTYPE;
union YYSTYPE
{
#line 78 "cspars.y" /* yacc.c:1909  */

int ival;
struct s_expr * tval;

#line 100 "cspars.tab.h" /* yacc.c:1909  */
};
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif



int yyparse (struct csmacro * csmp);

#endif /* !YY_YY_CSPARS_TAB_H_INCLUDED  */
