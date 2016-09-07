/*
 * http2script.c - test bed for the HTTP/2 multiplexor
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
#include "hpack.h"
/***********************************************************************
 * Main Program Starts Here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 * This is going to become the replacement for do_send_receive() and all
 * its works.
 */
int main(argc,argv,envp)
int argc;
char * argv[];
char * envp[];
{
struct script_element * sep;
struct script_element * asep;
struct script_element * dsep;;
int i;
int bothways;
int debug_level = 0;
int c;
union all_records a;
WEBDRIVE_BASE w;
unsigned char * got_to;
#ifdef SOLAR
    setvbuf(stderr, NULL, _IOFBF, BUFSIZ);
#endif
/****************************************************
 * Initialise.
 *
 * Set up the hash table for scan specifications
 */
    memset(&w, 0, sizeof(w));
    w.ht = hash(MAX_SCAN_SPECS, hash_func, comp_func);
    while ( ( c = getopt ( argc, argv, "hd:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'd' :
            debug_level = atoi(optarg);
            break;
        default:
        case '?' : /* Default - invalid opt.*/
            (void) fprintf(stderr,"Invalid argument; try -h\n");
        case 'h' :
            (void) fputs("http2script: E2 HTTP/2.0 Script Processor Test Bed\n\
Options:\n\
 -h prints this message on stderr\n\
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
/*
 * This will need to be replaced by a progressive load
 */
    if ((sep = load_script(argv[optind], debug_level)) == NULL)
    {
        fprintf(stderr, "Failed to load a valid script from %s\n",
                 argv[optind]);
        exit(1);
    }
/*
 * Now process the script (fragment).
 * -   Loop through the answers, looking for matches
 * -   If a match is found, re-write the header for the send with what is
 *     found
 */
    for (dsep = NULL, asep = sep; asep != NULL; asep = asep->next_track)
    {
        if (asep->head != NULL)
        {
            if (asep->head[1] == 'D')
                dsep = asep;
            else
            if (dsep != NULL && asep->head[1] == 'A')
            {
                reset_progressive(&w);
                scan_incoming_body(&w, asep->body,
                      asep->body + asep->body_len);
/*
 * Now see if any of them have matched.
 */
                for (c = 0; c <  w.scan_spec_used; c++)
                {
                    if ( w.scan_spec[c]->encrypted_token != NULL)
                    {
/*
 * Append the scan spec to the D header.
 */
                        append_scan_spec(dsep, w.scan_spec[c]);
/*
 * Clear the token ready for the next one
 */
                        free(w.scan_spec[c]->encrypted_token);
                        w.scan_spec[c]->encrypted_token = NULL;
                    }
                }
            }
            if (asep->head[1] == 'A' && !bothways)
            {
                free(asep->head);
                asep->head = NULL;
            }
        }
    }
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
int cscalc() { return 0;}
#ifdef USE_GLIBC_MALLOC
long bsr() { return 0;}
int cmp_size() { return 0;}
int domain_check() { return 0;}
void mqarrsort() { return;}
int bfree() { return 0;}
#endif
