/*
 * ungz.c - re-write a bothways script with compressed content expanded,
 * ORACLE forms traffic decrypted, etc..
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include <stdio.h>
#include <errno.h>
#include <zlib.h>
#include "scripttree.h"
#ifdef MINGW32
#ifndef InterlockedAnd
#define InterlockedAnd InterlockedAnd_Inline
static __inline LONG InterlockedAnd_Inline(LONG volatile *Target,LONG Set)
{
LONG i;
LONG j;

    j = *Target;
    do
    {
        i = j;
        j = InterlockedCompareExchange(Target,i & Set,i);
    }
    while(i != j);
    return j;
}
#endif
#endif
extern char * optarg;
extern int optind;
/***********************************************************************
 * Main Program Starts Here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
int main(argc,argv,envp)
int argc;
char * argv[];
char * envp[];
{
struct script_element * sep;
struct script_element * xsep;
struct script_element * xsep1;
struct script_element * xsep2;
struct script_element * xsep3;
int i;
int debug_level = 0;
char *x;
char *x1;
char *x2;
int c;
int json_flag = 0;
int forms_flag = 0;
FILE * ofp;
#ifdef SOLAR
    setvbuf(stderr, NULL, _IOFBF, BUFSIZ);
#endif
/****************************************************
 * Initialise.
 */
    while ( ( c = getopt ( argc, argv, "fjhd:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'd' :
            debug_level = atoi(optarg);
            break;
        case 'f' :
            forms_flag = 1;
            break;
        case 'j' :
            json_flag = 1;
            break;
        default:
        case '?' : /* Default - invalid opt.*/
            (void) fprintf(stderr,"Invalid argument; try -h\n");
        case 'h' :
            (void) fputs("ungz: E2 Bothways Script Decompressor\n\
Options:\n\
 -h prints this message on stderr\n\
 -j output JSON\n\
 -f decrypt ORACLE Forms traffic\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Input File, Output File; use '-' for stdin or stdout\n", stderr);
            fflush(stderr);
            exit(1);
        } 
    }
    if ((argc - optind) < 2 )
    {
        fputs( "Insufficient Arguments Supplied; try -h\n", stderr);
        exit(1);
    } 
    if ((sep = load_script(argv[optind], debug_level)) == NULL)
    {
        fprintf(stderr, "Failed to load a valid script from %s\n",
                 argv[optind]);
        exit(1);
    }
/*
 * Deal with compressed data and Expect 100
 */
    if (getenv("E2_WEB_DECOMP") != NULL)
    for (xsep = sep; xsep != NULL; xsep = xsep->next_track)
    {
/*
 * If the answer is an HTTP 100, attempt to concatenate the message either side.
 */
        if (xsep->head != NULL && xsep->head[1] == 'D'
        &&  xsep->child_track != NULL && xsep->child_track->head[1] == 'A'
        &&  xsep->child_track->body != NULL
        && !strncmp("HTTP/1.1 100", xsep->child_track->body, 12)
        && (xsep1 = xsep->next_track) != NULL)
        {
            if ((xsep2 = search_forw(xsep1, xsep->head)) != NULL
               && xsep2 != xsep1)
            {    /* The next  message pair is out of order */
/*
 * Put the current next record and its mate after the matching next record
 */
                xsep3 = xsep1->next_track;
                xsep1->next_track = xsep2->next_track;
                xsep1->next_track->prev_track = xsep1;
                if (xsep3 != xsep2)
                {
                    xsep1->prev_track = xsep2->prev_track;
                    xsep1->prev_track->next_track = xsep1;
                    xsep2->next_track = xsep3;
                    xsep3->prev_track = xsep2;
                }
                else
                {
                    xsep1->prev_track = xsep2;
                    xsep2->next_track = xsep1;
                }
/*
 * Make the matching record the next record
 */
                xsep->next_track = xsep2;
                xsep2->prev_track = xsep;
            }
/*
 * Now take out the HTTP/1.1 100 Continue response
 */
            xsep1 = xsep->child_track;
            xsep->child_track = NULL;
            free(xsep1->head);
            free(xsep1->body);
            free(xsep1->foot);
            free(xsep1);
            xsep1 = xsep->next_track;
            if (xsep1->body == NULL)
                continue;
/*
 * Decompress the POST block
 */
            if (!strncmp(xsep1->body + xsep1->body_len - 2, "\r\n",2))
                xsep1->body_len -= 2;
            i = xsep1->body_len * 4;
            do
            {
                x = (unsigned char *) malloc(i);
                x1 = x + i;
                c = decomp(xsep1->body, xsep1->body_len, &x, &x1);
            }
            while (c < 0 && ((i += i) < WORKSPACE));
            if (c < 0)
                fprintf(stderr, "Decompression failed for block with %s\n",
                         xsep->body);
            else
            {
                free(xsep1->body);
                xsep1->body = x;
                xsep1->body_len = c;
            }
            xsep->body = (unsigned char *) realloc(xsep->body,
                           xsep->body_len + xsep1->body_len);
            memcpy(xsep->body + xsep->body_len, xsep1->body,
                           xsep1->body_len);
            xsep->body_len += xsep1->body_len;
            xsep->next_track = xsep1->next_track;
            xsep->next_track->prev_track = xsep;
            xsep->child_track = xsep1->child_track;
            xsep->child_track->prev_track = xsep;
            free(xsep1->head);
            free(xsep1->foot);
            free(xsep1->body);
            free(xsep1);
        }
    }
    if (forms_flag)
    {
        for (xsep = sep; xsep != NULL; xsep = xsep->next_track)
        {
             if (xsep->head[1] == 'D' && xsep->head[3] == 'B')
             {
                 do_ora_forms(xsep);
                 if (xsep->child_track != NULL && xsep->child_track->head[1] == 'A')
                     do_ora_forms(xsep->child_track);
             }
        }
    }
/*
 * Onewayise if E2_BOTH isn't set
 */
    if ((x = getenv("E2_BOTH")) == (char *) NULL)
    {
        for (xsep = sep; xsep != NULL; xsep = xsep->next_track)
        {
             if (xsep->child_track != NULL && xsep->child_track->head[1] == 'A')
             {
                 free(xsep->child_track->head);
                 xsep->child_track->head = NULL;
             }
             else
             if (xsep->head != NULL && xsep->head[1] == 'A')
             {
                 free(xsep->head);
                 xsep->head = NULL;
             }
        }
    }
    if (json_flag)
    {
        if ((ofp = fopen(argv[optind+1], "wb")) != NULL)
        {
            json_chain(ofp, sep, 0);
            fclose(ofp);
        }
        else
        {
            perror("fopen()");
            fprintf(stderr, "Failed to open output file %s, error %d\n",
                 argv[optind+1], errno);
            exit(1);
        }
    }
    else
        dump_script(sep, argv[optind+1], debug_level);
    exit(0);
}
/*
 * Dummies to resolve linkage issues
 */
void app_recognise() {}
char * getvarval() { return NULL;}
void cscalc_init() {}
void putvarval() {}
void cscalc_zap() {}
#ifdef USE_GLIBC_MALLOC
long bsr() { return 0;}
int cmp_size() { return 0;}
int domain_check() { return 0;}
void mqarrsort() { return;}
int bfree() { return 0;}
#endif
