/*
 * autoscript.c - re-write a bothways script with various events already
 * set up.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
/*
 * Things that nothing should call; satisfy link references
 */
int cscalc() { return 0;}
int http_read() { return 0;}
int smart_read() { return 0;}
int smart_write() { return 0;}
void block_enc_dec() { return;}
void socket_cleanup() { return;}
void recognise_start_timer() {}
void recognise_take_time() {}
char * forms60_handle() { return NULL; }
void recognise_delay() {}
void sort_out_send() {}
void cscalc_init() {}
void cscalc_zap() {}
void putvarval() {}
char * getvarval() { return NULL; }
/*
 * Routine to stuff colons and backslashes
 */
static unsigned char * colon_stuff(outp, inp, len)
unsigned char * outp;
unsigned char * inp;
int len;
{
register unsigned char *x1=inp, *x2=outp;

    while (len-- > 0)
    {
        if (*x1 == (unsigned char) ':'
          ||*x1 == (unsigned char) '\\')
            *x2++ = (unsigned char) '\\';
        *x2++ = *x1++;
    }
    return x2;
}
/*
 * Append the SCAN_SPEC pointed to by ssp to the head of the script element pointed to
 * by sep
 */
static void append_scan_spec(sep, ssp)
struct script_element * sep;
SCAN_SPEC * ssp;
{
int len;
int needed;
int i;
unsigned char * hp;
/*
 * Work out how much space we need
 */
    len = strlen(sep->head);
    needed = len + strlen(ssp->scan_key) + 80 +
                   ssp->ebp.cnt + 2 * ssp->o_len;
    for (i = 0; i < ssp->ebp.cnt; i++)
        needed += 2 * (ssp->ebp.bmp[i]->match_len); 
/*
 * Allocate a new head, and free the old one
 */
    hp = (unsigned char *) malloc(needed);
    memcpy(hp, sep->head, len - 1);
    free(sep->head);
    sep->head = hp;
    hp += len - 2;
/*
 * Build up an H directive that will substitute what was
 * found
 */
    *hp++ = ':';
    *hp++ = 'H';
    *hp++ = ':';
    hp += sprintf(hp, "%s:", ssp->scan_key);
    hp += sprintf(hp, "%d:", ssp->ebp.cnt);
    for (i = 0; i < ssp->ebp.cnt; i++)
    {
        hp = colon_stuff(hp, ssp->ebp.bmp[i]->match_word,
                         ssp->ebp.bmp[i]->match_len);  
        *hp++ = ':';
    }
    hp += sprintf(hp, "%d:%d:1:", ssp->i_offset, ssp->i_len);
    (void) hexin_r( ssp->encrypted_token, ssp->o_len,
                     hp, sep->head + needed - 15);
    hp += 2 * ssp->o_len;
    hp += sprintf(hp, ":0:%d\\\n", ssp->o_len);
    return;
}
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
    if ((got_to = getenv("E2_BOTH")) != NULL)
        bothways = 1;
    else
        bothways = 0;
    memset(&w, 0, sizeof(w));
    w.ht = hash(MAX_SCAN_SPECS, hash_func, comp_func);
    while ( ( c = getopt ( argc, argv, "hd:p:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'd' :
            debug_level = atoi(optarg);
            break;
        case 'p' :
            got_to = optarg;
            while (recognise_scan_spec(&a, &w, &got_to) == SCAN_SPEC_TYPE);
            break;
        default:
        case '?' : /* Default - invalid opt.*/
            (void) fprintf(stderr,"Invalid argument; try -h\n");
        case 'h' :
            (void) fputs("autoscript: E2 Bothways Script Processor\n\
Options:\n\
 -h prints this message on stderr\n\
 -d set the debug level (between 0 and 4)\n\
 -p pattern to create events from (as it would appear in a script)\n\
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
 * Now process the script.
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
 * Dummy to resolve e2net.c linkage issues
 */
void app_recognise()
{
}
