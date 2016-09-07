/***********************************************
 * timestamp.c; function to return the timestamp
 * conditional compilation; BSD or SYSV or AIX or whatever
 */
#include <sys/types.h>
#include <malloc.h>
#ifdef V32
#include <time.h>
#else
#ifndef LCC
#ifndef VCC2003
#include <sys/time.h>
#endif
#endif
#endif
#ifndef MINGW32
#include <sys/times.h>
#endif
#ifdef V4
#include <sys/acct.h>
#include <sys/resource.h>
#define FRAC_USECS 1000000.0
#define MEMTOK 1
#define AHZ 100
#endif
char * sccs_id = "@(#) $Name$ $Id$\nCopyright (c) E2 Systems, 1993";
#include <stdio.h>
#include "e2conv.h"
#include "hashlib.h"
#include "matchlib.h"
#include "circlib.h"
void event_record_r();
/*
 * Routine that is a cross between strtok() and strchr()
 * Splits string out into fields.
 * Differences from strtok()
 * - second parameter is a char, not a series of chars
 * - returns a zero length string for adjacent separator characters
 * - handles an escape character for the separators, '\'
 * Legacy facility. No longer used as we are going re-entrant.
 */
#ifdef CANT_USE_OLD
signed char * nextfield( signed char * start_point,
signed char sepchar)
#else
signed char * nextfield(start_point,sepchar)
signed char * start_point;
signed char sepchar;
#endif
{
    return (signed char *) nextasc(start_point,sepchar,'\\');
}
/*
 * Routine to de-allocate an event definition
 */
void event_con_destroy(slot)
struct event_con * slot;
{
struct word_con * curr_word;

    curr_word = (slot)->first_word;
    while ( curr_word != (struct word_con *) NULL)
    {
    struct word_con * free_word;

         if (curr_word->words != (short int *) NULL)
             (void) free ((char *) curr_word->words);
         if (curr_word->action != (short int *) NULL)
             (void) free ((char *) curr_word->action);
         if (curr_word->comped != (short int *) NULL)
             (void) free ((char *) curr_word->comped);
         free_word = curr_word;
         curr_word = curr_word->next_word;
         (void) free ((char *) free_word);
    }
    if (slot->comment != NULL)
        (void) free ((char *) slot->comment);
    (void) free ((char *) slot);
    return;
}
/*************************************************************************
 * Function that creates an event, given a pointer to a string of the
 * form ID:Timeout:Look_for:Response:Look_for:Response....:Comment
 ============================
 *
 * There are a series of 'slots' that can be allocated; A[0-9] - Z[0-9] with
 * some gaps.
 * - A is reserved for a time stamp declaration
 * - S is reserved for the start event
 * - F is reserved for the finish event
 * - Z is reserved for the abort event
 *
 * Dynamic allocation; need to know what each means. Therefore
 * use the A event to record the setting of an event to look for.
 *
 * Potential events have:
 * - a slot identifier
 * - a timeout (a time after which the process will abort)
 * - a series of:
 *   - a string to match
 *   - what to do when the string is seen.
 * - comment
 *
 * The timestamp event records the outcome of the event. This outcome can be:
 * - one of the expected events happens (in which case it is recorded)
 * - the timeout expires
 *
 * If an expected event occurs, there may be optional actions. However,
 * we do not allow action recursion; the actions must simply restore
 * the main thread.
 *
 * If the timeout expires, the process exits.
 *
 * Data Structures
 * ===============
 * - A hash table indicating the slots in use
 * - Structures, dynamically allocated, to hold the information that
 *   relates to a single slot
 * - Structures, dynamically allocated, tracking the individual words
 *   being matched. 
 *
 * Decode a time stamp definition
 */
struct event_con * stamp_read(parm)
signed char * parm;
{
struct event_con * slot;
#ifdef ST
int x;
struct word_con * curr_word;
struct word_con ** next_word;
char *got_to = (char *) NULL;
char ret_buf[1024];

    if (parm == (signed char *) NULL)
        return (struct event_con *) NULL;       /* cannot define nothing */
    if (!strcmp(parm,"F") || !strcmp(parm,"S")  ||
        strcmp(parm, "A") <= 0|| strcmp(parm, "Z") >=0
        || (int) strlen(parm) > 2)
    {
        (void) fprintf(stderr,"%s:%d Event %-2.2s is reserved or illegal\n",
                               __FILE__,__LINE__,parm);
        return (struct event_con *) NULL;       /* cannot define these */
    }
    x = (((int) (*parm)) << 8) + ((int) *(parm+1));
    slot = (struct event_con *)
             malloc (sizeof (struct event_con));
    strcpy((slot)->event_id,parm);
    parm = nextasc_r((signed char *) NULL, ':', '\\',
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]);
    if (parm == (signed char *) NULL)
    {
        (void) fprintf(stderr,"Illegal timeout period\n");
        (void) free ((char *) slot);
        return (struct event_con *) NULL;
    }
    slot->time_int = 0.0;
    slot->min_delay = pg.think_time;
    slot->timeout.tv_sec = atoi(parm);
    slot->timeout.tv_usec = 0;
    slot->first_word = (struct word_con *) NULL;
    slot->word_found = (struct word_con *) NULL;
    slot->comment = (short int *) NULL;
    slot->match_type = MAGIC;
    slot->flag_cnt[0] = 0;
    slot->flag_cnt[1] = 0;
    slot->flag_cnt[2] = 0;
    slot->flag_cnt[3] = 0;
    for (next_word = &((slot)->first_word),
         parm = nextasc_r((signed char *) NULL,':','\\', 
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]);
            parm != (signed char *) NULL;
                parm = nextasc_r((signed char *) NULL,':','\\', 
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]))
    {
    int i;
    short int * x;

        *(next_word) = (struct word_con *)
                        malloc(sizeof(struct word_con));
        curr_word = *(next_word);
        curr_word->next_word = (struct word_con *) NULL;
        i = strlen(parm) + 1;
        curr_word->words = (short int *)
                           malloc ( i * sizeof(short int));
        x = curr_word->words;
        while (i--)
           *x++ = (short int) *parm++;
        if (pg.match_comp != NULL &&
           (x = (*(pg.match_comp))(curr_word->words,&i)) !=
                (short int *) NULL)
        {  /* Valid regular expression */
            curr_word->comped = (short int *)
                        malloc ( (i+1)  * sizeof(short int));
            memcpy((char *) curr_word->comped,
               (char *) x,  (i+1)  * sizeof(short int));
        }
        else
            curr_word->comped = (short int *) NULL;
        parm = nextasc_r((signed char *) NULL, ':', '\\', 
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]);
        if (parm != (signed char *) NULL)
        {
            i = strlen(parm) + 1;
            curr_word->action = (short int *)
                               malloc ( i * sizeof(short int));
            x = curr_word->action;
            while (i--)
               *x++ = (short int) *parm++;
            next_word = &(curr_word->next_word);
        } 
        else
        {
            (slot)->comment = curr_word->words;
            (void) free ((char *) curr_word);
            *(next_word) = (struct word_con *) NULL;
        }
    }
#endif
    return slot;
}
/*
 * The re-entrant version doesn't have the extra ptydrive-specific gubbins.
 */
struct event_con * stamp_read_r(parm, pgp, got_to, ret_buf, len)
signed char * parm;
struct ptydrive_glob * pgp;
char *got_to;
char *ret_buf;
int len;
{
int x;
struct event_con * slot;

    if (parm == (signed char *) NULL)
        return (struct event_con *) NULL;       /* cannot define nothing */
    if (!strcmp(parm,"F") || !strcmp(parm,"S")  ||
        strcmp(parm, "A") <= 0|| strcmp(parm, "Z") >=0
        || (int) strlen(parm) > 2)
    {
        (void) fprintf(stderr,"%s:%d Event %-2.2s is reserved or illegal\n",
                               __FILE__,__LINE__,parm);
        return (struct event_con *) NULL;       /* cannot define these */
    }
    x = (((int) (*parm)) << 8) + ((int) *(parm+1));
    slot = (struct event_con *)
             malloc (sizeof (struct event_con));
    strcpy((slot)->event_id, parm);
    parm = nextasc_r((signed char *) NULL, ':', '\\',
                     &got_to, ret_buf,ret_buf + len - 1);
    if (parm == (signed char *) NULL)
    {
        (void) fprintf(stderr,"Illegal timeout period\n");
        (void) free ((char *) slot);
        return (struct event_con *) NULL;
    }
    slot->time_int = 0.0;
    slot->min_delay = pgp->think_time;
    slot->timeout.tv_sec = atoi(parm);
    slot->timeout.tv_usec = 0;
    slot->first_word = (struct word_con *) NULL;
    slot->word_found = (struct word_con *) NULL;
    slot->comment = (short int *) NULL;
    slot->match_type = MAGIC;
    slot->flag_cnt[0] = 0;
    slot->flag_cnt[1] = 0;
    slot->flag_cnt[2] = 0;
    slot->flag_cnt[3] = 0;
    if (( parm = nextasc_r((signed char *) NULL,':','\\', 
                     &got_to, ret_buf,ret_buf + len - 1))
                 != (signed char *) NULL)
    {
    int i;
    short int * x;

        i = strlen(parm) + 1;
        slot->comment = (short int *) malloc ( i * sizeof(short int));
        x = slot->comment;
        while (i--)
           *x++ = (short int) *parm++;
    }
    return slot;
}
/*
 * Ready a timestamp for use.
 */
struct event_con * stamp_declare(parm)
signed char * parm;
{
struct event_con * slot;
#ifdef ST
HIPT * h;
int x;
char * got_to = (char *) NULL;
unsigned char ret_buf[1024];

    if (parm == (signed char *) NULL)
        return (struct event_con *) NULL;       /* cannot define nothing */
#ifdef DEBUG
    fputs( "Declaring Timestamp ", stderr);
    fputs( parm, stderr);
    fputc( '\n', stderr);
    fflush(stderr);
#endif
    parm = nextasc_r(parm,':','\\',
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]);
    if (parm == (signed char *) NULL)
    {
        fputs( "Timestamp identification Failed ", stderr);
        fputs( parm, stderr);
        fflush(stderr);
        return (struct event_con *) NULL;       /* cannot define nothing */
    }
    if (!strcmp(parm,"F") || !strcmp(parm,"S")  ||
        strcmp(parm, "A") <= 0|| strcmp(parm, "Z") >=0
        || (int) strlen(parm) > 2)
    {
        (void) fprintf(stderr,"%s:%d Event %-2.2s is reserved or illegal\n",
                               __FILE__,__LINE__,parm);
        return (struct event_con *) NULL;       /* cannot define these */
    }
    x = (((int) (*parm)) << 8) + ((int) *(parm+1));
    h = lookup(pg.poss_events,(char *) (x));
    if (h != (HIPT *) NULL)
    {
        if ( (struct event_con *) h->body == pg.curr_event)
        {
            (void) fprintf(stderr,"Event %-2.2s is current; cannot change\n",
                               parm);
            return (struct event_con *) NULL;       /* cannot define these */
        }
        else
            event_con_destroy((struct event_con *) h->body);
           /* clear up the existing item */
    }
    else
    {
        h = insert(pg.poss_events,(char *)(x),(char *) NULL);
        if (h == (HIPT *) NULL)
        {
            (void) fprintf(stderr,"Hash Insert Failure\n");
            return (struct event_con *) NULL;
        }
    }
    slot = stamp_read(parm);
    h->body = (char *) slot;
    event_record("A",slot);
#endif
    return slot;
}
/*
 * Re-entrant version.
 */
struct event_con * stamp_declare_r(parm, pgp)
signed char * parm;
struct ptydrive_glob * pgp;
{
HIPT * h;
int x;
struct event_con * slot;
char * got_to = (char *) NULL;
unsigned char ret_buf[1024];

    if (parm == (signed char *) NULL)
        return (struct event_con *) NULL;       /* cannot define nothing */
#ifdef DEBUG
    fputs( "Declaring Timestamp ", stderr);
    fputs( parm, stderr);
    fputc( '\n', stderr);
    fflush(stderr);
#endif
    parm = nextasc_r(parm, ':', '\\',
                     &got_to, &ret_buf[0],&ret_buf[sizeof(ret_buf) - 1]);
    if (parm == (signed char *) NULL)
    {
        fputs( "Timestamp identification Failed ", stderr);
        fputs( parm, stderr);
        fflush(stderr);
        return (struct event_con *) NULL;       /* cannot define nothing */
    }
    if (!strcmp(parm,"F") || !strcmp(parm,"S")  ||
        strcmp(parm, "A") <= 0|| strcmp(parm, "Z") >=0
        || (int) strlen(parm) > 2)
    {
        (void) fprintf(stderr,"%s:%d Event %-2.2s is reserved or illegal\n",
                               __FILE__,__LINE__,parm);
        return (struct event_con *) NULL;       /* cannot define these */
    }
    x = (((int) (*parm)) << 8) + ((int) *(parm+1));
    h = lookup(pgp->poss_events,(char *) (x));
    if (h != (HIPT *) NULL)
    {
        if ( (struct event_con *) h->body == pgp->curr_event)
        {
            (void) fprintf(stderr,"Event %-2.2s is current; cannot change\n",
                               parm);
            return (struct event_con *) NULL;       /* cannot define these */
        }
        else
            event_con_destroy((struct event_con *) h->body);
           /* clear up the existing item */
    }
    else
    {
        h = insert(pgp->poss_events,(char *)(x),(char *) NULL);
        if (h == (HIPT *) NULL)
        {
            (void) fprintf(stderr,"Hash Insert Failure\n");
            return (struct event_con *) NULL;
        }
    }
    slot = stamp_read_r(parm,  pgp, got_to, ret_buf, sizeof(ret_buf));
    h->body = (char *) slot;
    event_record_r("A",slot, pgp);
    return slot;
}
double timestamp()
{
double strtod();
static double clock_slew;
static int clock_flag;
struct timeval tm;
char * x, *getenv();
/*
 * Use this if gettimeofday is not supported
 */
#ifndef ICL
struct timezone
#ifdef PTX
{ int tz_minuteswest; }
#endif
 tz;
#endif
#ifdef ICL
#ifndef SOLAR
(void) gettimeofday(&tm);
#else
struct timezone tz;
(void) gettimeofday(&tm,&tz);
#endif
#else
(void) gettimeofday(&tm,&tz);
#endif
if (!clock_flag)
{    /* A race condition is harmless here but probably won't happen */
    clock_flag = 1;
    if ((x = getenv("E2TRAF_SLEW")) != (char *) NULL)
        clock_slew = strtod(x, (char **) NULL) *100.0;
}
#ifdef DEBUG
    printf("timestamp: %u.%06u\n", tm.tv_sec, tm.tv_usec);
#endif
return (double) (((double) tm.tv_sec)*100.0 + ((double) tm.tv_usec)/10000.0)
 + clock_slew;
}
void int_out(ofp, int_arr)
FILE * ofp;
short int * int_arr;
{
short int * x;

    if (int_arr == (short int *) NULL)
        return;
    for (x = int_arr;
            *x != 0;
                 x++)
    {
        (void) fputc(  *x, ofp);
#ifdef ST
        if (pg.dumpin_flag == 2)
        {
            (void) fputc(*x, pg.log_output);
            if ((char) *x == '#')
                (void) fputc('#', pg.log_output);
        }
#endif
    }
    return;
}
/*
 * Output a word_con structure
 */
void word_con_out (curr_word)
struct word_con * curr_word;
{
#ifdef ST
     (void) fputc(':', pg.fo);
     if (pg.dumpin_flag == 2)
         (void) fputc(':', pg.log_output);
     if (pg.match_out != NULL &&
         pg.force_flag && curr_word->head != curr_word->tail)
         (*(pg.match_out))(curr_word);
     else
         int_out(pg.fo, curr_word->words);
     (void) fputc(':', pg.fo);
     if (pg.dumpin_flag == 2)
         (void) fputc(':', pg.log_output);
     int_out(pg.fo, curr_word->action);
     return;
#endif
}
#ifdef ST
static char fbuf[16384];
#endif
/***********************************************
 * Routine to record that something has happened
 */
void event_record(event, event_con)
char *event;
struct event_con * event_con;
{
#ifdef ST
double time_stamp;

     if (pg.fo == (FILE *) NULL)
     {
         if (!strcmp(event,"F"))
             return;         /* Do not open a file just to log exit */
         if ((pg.fo = fopen(pg.logfile, "wb")) == (FILE *) NULL)
         {                              /*  Open the logfile  */
             fprintf(stderr,"event_record:Attempting to open file %s\n",
                       pg.logfile);
             perror("Cannot open log file");
#ifdef PATH_AT_TIDY
             if (isatty(0))
             {
                 endwin();
#ifdef OSF
                 unset_curses_modes();
#endif
             }
#endif
             exit(1);
         }
         setvbuf(pg.fo, &fbuf[0], _IOFBF, sizeof(fbuf));
     }
     time_stamp = timestamp();
     (void) fprintf(pg.fo, "%s:%s:%s:%05d:%f:%s",
                     pg.fdriver_seq,pg.bundle_seq,pg.rope_seq,
                   ++(pg.seqX),time_stamp,event);
#ifdef DEBUG
        (void) fflush(pg.fo);
#endif
     if (event_con != (struct event_con *) NULL)
     {
         if (strcmp(event, event_con->event_id))
         {                    /* record the set-up, or an error */
             struct word_con * curr_word;
             (void) fprintf(pg.fo, ":%s:%d", event_con->event_id,
                  event_con->timeout.tv_sec);
             if (pg.dumpin_flag == 2)
             {
                 if (!strcmp(event,"A"))
                     (void) fputs("\\S", pg.log_output);
                 else
                     (void) fputs("# Z ", pg.log_output);
                 (void) fprintf(pg.log_output, "%s:%d", event_con->event_id,
                                    event_con->timeout.tv_sec);
             }
#ifdef DEBUG
             (void) fflush(pg.fo);
#endif
             for (curr_word = event_con->first_word;
                  curr_word != (struct word_con *) NULL;
                  curr_word = curr_word->next_word)
                  word_con_out(curr_word);
             if (event_con->comment != (short int *) NULL)
             { 
                  (void) fputc(':', pg.fo);
                  if (pg.dumpin_flag == 2)
                      (void) fputc(':', pg.log_output);
#ifdef DEBUG
                  (void) fflush(pg.fo);
#endif
                  int_out(pg.fo, event_con->comment);
             } 
             if (pg.dumpin_flag == 2)
             {
                 if (!strcmp(event,"A"))
                     (void) fputs("\\\n", pg.log_output);
                 else
                     (void) fputs("#\n", pg.log_output);
                 fflush(pg.log_output);
             }
         }
         else
         {
             if (event_con->time_int != 0.0)
             {
                 event_con->time_int = time_stamp - event_con->time_int;
                 (void) fprintf(pg.fo, ":%f", event_con->time_int);
#ifdef DEBUG
                 (void) fflush(pg.fo);
#endif
             }
             if (event_con->word_found != (struct word_con *) NULL)
             {
                 if (pg.dumpin_flag == 2)
                     fprintf(pg.log_output, "# %6.2f Matched:",
                             event_con->time_int/100.0);
                 (void) fputc(':', pg.fo);
#ifdef DEBUG
                 (void) fflush(pg.fo);
#endif
/*
 * Ordinarily, output the regular expression, but in the 
 * force match case, output what was seen
 */
                 if (pg.force_flag && pg.match_out != NULL)
                     (*(pg.match_out))(event_con->word_found);
                 else
                     int_out(pg.fo, event_con->word_found->words);
                 if (pg.dumpin_flag == 2)
                 {
                     fputs("#\n", pg.log_output);
                     fflush(pg.log_output);
                 }
             }
         }
     }
#ifdef  JUNK
     if (!strcmp(event, "F"))
     {    /* Write out the Resource Usage for the Children */
     struct rusage res_stuff;
     double user_time;
     double sys_time;
     double floor();
     int comb_ticks;

         (void) getrusage(RUSAGE_CHILDREN,&res_stuff);
         if ( (double)((double) res_stuff.ru_utime.tv_sec
                + ((double) res_stuff.ru_utime.tv_usec)/FRAC_USECS) != 0.0 ||
                 (double)((double) res_stuff.ru_stime.tv_sec 
                + ((double) res_stuff.ru_stime.tv_usec)/FRAC_USECS) != 0.0)
         {
             user_time = (double)((double) res_stuff.ru_utime.tv_sec
                           + ((double) res_stuff.ru_utime.tv_usec)/FRAC_USECS);
                                       /* user time used */
             sys_time = (double)((double) res_stuff.ru_stime.tv_sec 
                            + ((double) res_stuff.ru_stime.tv_usec)/FRAC_USECS);
                                       /* system time used */
             comb_ticks = (int) floor(((double) AHZ) * (user_time + sys_time));
             if (comb_ticks == 0)
                 comb_ticks = 1;
             fprintf(pg.fo,
":%8.2f:%8.2f:%10.1u:%10.1u:%7.1u:%7.1u:%7.1u:%8.1u:%8.1u:%8.1u:%8.1u:%8.1u:%8.1u:%7.1u:%7.1u:%7.1u",
                     user_time,
                     sys_time,
                     res_stuff.ru_ixrss * MEMTOK/comb_ticks,
                                         /* integral shared memory size */
                     res_stuff.ru_idrss * MEMTOK/comb_ticks,
                                        /* integral unshared data " */
                     res_stuff.ru_inblock, /* block input operations */
                     res_stuff.ru_oublock,
                 res_stuff.ru_maxrss,
                 res_stuff.ru_isrss*MEMTOK/comb_ticks,
                 res_stuff.ru_minflt, /* page reclaims */
                 res_stuff.ru_majflt, /* page faults */
                 res_stuff.ru_nswap, /* swaps */
                 res_stuff.ru_msgsnd, /* messages sent */
                 res_stuff.ru_msgrcv, /* messages received */
                 res_stuff.ru_nsignals, /* signals received */
                 res_stuff.ru_nvcsw, /* voluntary context switches */
                 res_stuff.ru_nivcsw); /* involuntary " */
         }
     }
#endif
     (void) fputc('\n', pg.fo);
     if (strcmp(event,"A"))
         (void) fflush(pg.fo);
     if (!strcmp(event, "F"))
        (void) fclose(pg.fo);
     else
     if ((pg.force_flag & FORCE_REQUEST) && strcmp(event,"A"))
         event_record("A", event_con);   /* Retroactively define */
#ifdef DEBUG
     fflush(pg.fo);
#endif
#endif
     return;
}
/*******************************************************************
 * Re-entrant routine to record that something has happened
 * This does much less than the other, much of whose code only applies to
 * ptydrive, which will never be multi-threaded.
 */
void event_record_r(event, event_con, pgp)
char *event;
struct event_con * event_con;
struct ptydrive_glob * pgp;
{
double time_stamp;

    if (pgp->fo == (FILE *) NULL)
    {
        if (!strcmp(event,"F"))
            return;         /* Do not open a file just to log exit */
        if ((pgp->fo = fopen(pgp->logfile, "wb")) == (FILE *) NULL)
        {                              /*  Open the logfile  */
            fprintf(stderr,"event_record_r:Attempting to open file %s\n",
                     pgp->logfile);
            perror("Cannot open log file");
            return;
        }
    }
    time_stamp = timestamp();
    (void) fprintf(pgp->fo, "%s:%s:%s:%05d:%f:%s",
                     pgp->fdriver_seq,pgp->bundle_seq,pgp->rope_seq,
                   ++(pgp->seqX),time_stamp,event);
#ifdef DEBUG
    (void) fflush(pgp->fo);
#endif
    if (event_con != (struct event_con *) NULL)
    {
        if (strcmp(event, event_con->event_id))
        {                    /* record the set-up, or an error */
        struct word_con * curr_word;

            (void) fprintf(pgp->fo, ":%s:%d", event_con->event_id,
                 event_con->timeout.tv_sec);
            event_con->flag_cnt[0] = 0;
            event_con->flag_cnt[1] = 0;
            event_con->flag_cnt[2] = 0;
            event_con->flag_cnt[3] = 0;
            for (curr_word = event_con->first_word;
                     curr_word != (struct word_con *) NULL;
                         curr_word = curr_word->next_word)
                 word_con_out(curr_word);
            if (event_con->comment != (short int *) NULL)
            { 
                (void) fputc(':', pgp->fo);
#ifdef DEBUG
                (void) fflush(pgp->fo);
#endif
                int_out(pgp->fo, event_con->comment);
            } 
        }
        else
        {
            if (event_con->time_int > 0.0)
            {
#ifdef DIFF_TIME
                event_con->time_int = time_stamp - event_con->time_int;
                if (event_con->time_int > 0.0)
#endif
                {
                (void) fprintf(pgp->fo, ":%f", event_con->time_int);
                if (event_con->flag_cnt[0] != 0
                  || event_con->flag_cnt[1] != 0
                  || event_con->flag_cnt[2] != 0
                  || event_con->flag_cnt[3] != 0)
                    (void) fprintf(pgp->fo, ":%d:%d:%d:%d",
                          event_con->flag_cnt[0],
                          event_con->flag_cnt[1],
                          event_con->flag_cnt[2],
                          event_con->flag_cnt[3]);
#ifdef DEBUG
                (void) fflush(pgp->fo);
#endif
                }
            }
            if (event_con->word_found != (struct word_con *) NULL)
            {
                (void) fputc(':', pgp->fo);
#ifdef DEBUG
                (void) fflush(pgp->fo);
#endif
                int_out(pgp->fo, event_con->word_found->words);
            }
        }
    }
    (void) fputc('\n', pgp->fo);
    if (strcmp(event,"A"))
        (void) fflush(pgp->fo);
#ifdef DEBUG
    fflush(pgp->fo);
#endif
    if (!strcmp(event, "F"))
        (void) fclose(pgp->fo);
    return;
}
/****************************************************************************
 * Log a function call timing
 */
void log_dotnet_rpc(pgp, funp, ts, duration)
struct ptydrive_glob * pgp;
char * funp;
double ts;
double duration;
{
    if (pgp->fo == (FILE *) NULL)
    {
        if ((pgp->fo = fopen(pgp->logfile, "wb")) == (FILE *) NULL)
        {                              /*  Open the logfile  */
            fprintf(stderr,"log_dotnet_rpc:Attempting to open file %s\n",
                     pgp->logfile);
            perror("Cannot open log file");
            return;
        }
    }
    (void) fprintf(pgp->fo, "%s:%s:%s:%05d:%f:ZA:%f:%s\n",
                     pgp->fdriver_seq,pgp->bundle_seq,pgp->rope_seq,
                   ++(pgp->seqX),ts,duration, funp);
    return;
}
