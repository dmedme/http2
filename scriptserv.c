/*
 *    scriptserv.c - Scripting services for E2 Systems multi-threaded script
 *    drivers.
 *
 *    Copyright (C) E2 Systems 1993, 2000
 *
 ******************************************************************************
 * This source file contains:
 * -  Universally applicable code to deal with taking timings etc.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
#include "submalloc.h"
static void repurpose();
/*********************************************************************
 * Recognise PATH directives that will occur in ALL files.
 *
 * Start Time (\S) records are:
 * id:number (ignored):description
 */
void recognise_start_timer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];

    strcpy(wdbp->narrative, "recognise_start_timer");
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
                    &(a->start_timer.timer_id[0]),
              &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to, &ret_buf[0],
                 &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few Start Timer fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                 &(a->start_timer.timer_description[0]),
                 &(a->start_timer.timer_description[sizeof(
                   a->start_timer.timer_description) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few Start Timer fields",
             &(wdbp->parser_con));
    return;
}
/*
 * Take Time (\T) records are:
 * id:
 */
void recognise_take_time(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;

    strcpy(wdbp->narrative, "recognise_take_time");
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
               &(a->start_timer.timer_id[0]),
          &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    return;
}
/*
 * Delay (\W) or Pause (\P) records are:
 * delay time (floating point number)
 */
void recognise_delay(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
    strcpy(wdbp->narrative, "recognise_delay");
    a->delay.delta = strtod(wdbp->parser_con.tlook,(char **) NULL);
    return;
}
/*
 * Label (\L) records are:
 *     goto target (string, case sensitive), optionally a block of code.
 */
void recognise_label(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char * x;

    strcpy(wdbp->narrative, "recognise_label");
    if ((x = strchr(wdbp->parser_con.tlook,'\\')) != NULL)
        *x = '\0';
    if (wdbp->verbosity > 2)
        fprintf(stderr, "(Client: %s) label %s\n",
                      wdbp->parser_con.pg.rope_seq, wdbp->parser_con.tlook);
    repurpose(wdbp);
    update_target(wdbp->parser_con.tlook,
                            &(wdbp->parser_con));
    return;
}
/*
 * Include (\I) records are:
 *     file name (string, local file system semantics)
 *     +name (string, name a this script segment)
 *     -anything (end of script segment)
 */
enum tok_id recognise_include(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char * x;
char num_buf[23];

    strcpy(wdbp->narrative, "recognise_include");
    if (wdbp->verbosity > 2)
        fprintf(stderr, "(Client: %s) include %s\n",
                      wdbp->parser_con.pg.rope_seq, wdbp->parser_con.tlook);
    if (wdbp->parser_con.tlook[0] == '-')
        return E2EOS;
    if ((x = strchr(wdbp->parser_con.tlook,'\\')) != NULL)
        *x = '\0';
    if (wdbp->parser_con.tlook[0] == '+')
    {
        sprintf(num_buf,"%u",ftell(wdbp->parser_con.pg.cur_in_file));
        while(fgets(wdbp->parser_con.tbuf,
                           WORKSPACE, wdbp->parser_con.pg.cur_in_file)
                  != NULL && strncmp(wdbp->parser_con.tbuf,"\\I:-", 4));
        putvarval(&wdbp->parser_con.csmacro, wdbp->parser_con.tlook,
                        num_buf);
    }
    else
        push_file(wdbp->parser_con.tlook, &wdbp->parser_con);
    return E2INCLUDE;
}
/*
 * Use the message memory for cscalc() purposes
 */
static void repurpose(wdbp)
WEBDRIVE_BASE * wdbp;
{
/*
 * if (WORKSPACE < sizeof(struct e2malloc_base))
 *     abort();
 */
    memset(&wdbp->in_buf.buf[0], 0, sizeof(struct e2malloc_base));
    wdbp->parser_con.csmacro.e2mbp = (struct e2malloc_base *) &wdbp->in_buf.buf[0];
    sub_bfree(wdbp->parser_con.csmacro.e2mbp,
            &wdbp->in_buf.buf[sizeof(struct e2malloc_base)],
            sizeof(wdbp->in_buf.buf) - 
            sizeof(struct e2malloc_base));
    sub_bfree(wdbp->parser_con.csmacro.e2mbp,
            &wdbp->ret_msg.buf[0],
            sizeof(wdbp->ret_msg.buf));
    sub_bfree(wdbp->parser_con.csmacro.e2mbp,
            &wdbp->msg.buf[0],
            sizeof(wdbp->msg.buf));
    wdbp->depth = 0;
    return;
}
/*
 * Goto (\G) records are:
 *     label name  (string, case sensitive) : Expression (evaluates to 0 or ! 0)
 *
 * That is, all gotos are conditional, and execute depending on whether the
 * expression evaluates false (0) or true (any other value).
 *
 * For an unconditional goto just put 1
 * To comment out a goto, put 0
 * To execute code, but not do anything otherwise, use a non-existent label
 * (you will get a warning message, though).
 */
void recognise_goto(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char label[ADDRESS_LEN + 1];
char * x;

    strcpy(wdbp->narrative, "recognise_goto");
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
                    &(label[0]),
              &(label[ADDRESS_LEN]));
    if ((x = strchr(got_to, '\\')) != NULL)
        *x = '\0';
    repurpose(wdbp);
    if ( cscalc(NULL, got_to + 1, &wdbp->parser_con.csmacro, wdbp))
        goto_target(label, wdbp);
    sub_e2osfree(wdbp->parser_con.csmacro.e2mbp);
    wdbp->parser_con.csmacro.e2mbp = NULL;
    return;
}
/*
 * Start the timer running
 */
void do_start_timer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char cur_pos[BUFLEN];
short int x;
HIPT * h;

    strcpy(wdbp->narrative, "do_start_timer");
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_start_timer(%s)\n",
            wdbp->parser_con.pg.rope_seq, a->start_timer.timer_id);
        fflush(stderr);
    }
    if (wdbp->verbosity > 1)
        fprintf(stderr,"(Client: %s) Start:%s:%s\n", wdbp->parser_con.pg.rope_seq,
                        a->start_timer.timer_id,
                        a->start_timer.timer_description);
    strcpy(cur_pos,a->start_timer.timer_id);
    strcat(cur_pos, ":120:");
    strcat(cur_pos, a->start_timer.timer_description);
    stamp_declare_r(cur_pos, &(wdbp->parser_con.pg));
    x = (short int) (((int) (cur_pos[0])) << 8) + ((int) (cur_pos[1]));
    if ((h = lookup(wdbp->parser_con.pg.poss_events, (char *) x)) == (HIPT *) NULL)
    {
        (void) fprintf(stderr,
                       "(%s:%d Client:%s) Error, event define failed for %s\n",
                       __FILE__,__LINE__, wdbp->parser_con.pg.rope_seq, cur_pos);
        return;       /* Crash out here */
    }
    wdbp->parser_con.pg.curr_event = (struct event_con *) (h->body);
    if (wdbp->parser_con.pg.curr_event != (struct event_con *) NULL)
    {
#ifdef DIFF_TIME
        wdbp->parser_con.pg.curr_event->time_int = timestamp();
#else
        wdbp->parser_con.pg.curr_event->time_int = 0.0;
#endif
    }
    return;
}
/*
 * Take a time stamp
 */
int do_take_time(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
HIPT * h;
long int event_id;

    strcpy(wdbp->narrative, "do_take_time");
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_take_time(%s)\n",
                       wdbp->parser_con.pg.rope_seq, a->take_time.timer_id);
        fflush(stderr);
    }
    if (wdbp->verbosity > 1)
        fprintf(stderr,"(Client:%s) End:%s\n",
                       wdbp->parser_con.pg.rope_seq, a->take_time.timer_id);
    event_id = (((int) (a->take_time.timer_id[0])) << 8)
             + ((int) (a->take_time.timer_id[1]));
    if ((h = lookup(wdbp->parser_con.pg.poss_events,(char *) event_id)) ==
           (HIPT *) NULL)
    {
        (void) fprintf(stderr,"(%s:%d Client:%s) Error, undefined event %x\n",
                       __FILE__,__LINE__, wdbp->parser_con.pg.rope_seq, event_id);
        return 0;       /* Crash out here */
    }
    wdbp->parser_con.pg.curr_event = (struct event_con *) (h->body);
    if (wdbp->parser_con.pg.curr_event  != (struct event_con *) NULL)
    {
    int sleep_left;

        event_record_r(wdbp->parser_con.pg.curr_event->event_id,
                            wdbp->parser_con.pg.curr_event, &(wdbp->parser_con.pg));
        sleep_left = (int) (((double) wdbp->parser_con.pg.think_time)
                          - wdbp->parser_con.pg.curr_event->time_int/100.0);
        wdbp->parser_con.pg.curr_event = (struct event_con *) NULL;
        if (sleep_left > 0)
        {
            add_time(wdbp->root_wdbp, wdbp, sleep_left);
            return 1;
        }
    }
    return 0;
}
extern double floor();
extern double strtod();
/*
 * Suspend execution
 */
int do_pause(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
    strcpy(wdbp->narrative, "do_pause");
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_pause(%f)\n",
                                wdbp->parser_con.pg.rope_seq, a->delay.delta);
        fflush(stderr);
    }
    if (a->delay.delta >= 1.0)
    {
        add_time(wdbp->root_wdbp, wdbp, (int) a->delay.delta);
        return 1;
    }
    return 0;
}
/*
 * Set the think time.
 */
int do_delay(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
    strcpy(wdbp->narrative, "do_delay");
    wdbp->parser_con.pg.think_time = (short int) a->delay.delta;
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_delay(%f)\n",
                                wdbp->parser_con.pg.rope_seq, a->delay.delta);
        fflush(stderr);
    }
    return 0;
}
/*
 * Branching support
 */
void goto_target(label, wdbp)
char * label;
WEBDRIVE_BASE * wdbp;
{
    strcpy(wdbp->narrative, "goto_target");
    if (wdbp->verbosity > 2)
        fprintf(stderr, "(Client: %s) going to %s\n",
                      wdbp->parser_con.pg.rope_seq, label);
    if (!strcmp(label, "RESYNC"))
        do_resync(wdbp);
    else
        do_goto(label, &(wdbp->parser_con));
    return;
}
/*
 * Skip forward to a resynchronisation point
 */
void do_resync(wdbp)
WEBDRIVE_BASE * wdbp;
{
enum tok_id tok_id;

    strcpy(wdbp->narrative, "do_resync");
    if (wdbp->verbosity > 2)
        fprintf(stderr, "(Client: %s) doing RESYNC\n",
                      wdbp->parser_con.pg.rope_seq);
    while ((tok_id = get_tok(wdbp->debug_level, &wdbp->parser_con)) != E2RESET
          &&  tok_id != E2EOF);
    clear_cookie_cache(wdbp);
    wdbp->parser_con.look_status = CLEAR;
    return;
}
/*
 * Deal with send buffer overflow
 */
void sort_out_send(bp, xp, overflowp, needed, wdbp)
unsigned char ** bp;
unsigned char ** xp;
int needed;
int * overflowp;
WEBDRIVE_BASE * wdbp;
{
    *overflowp += needed;
    if (wdbp->overflow_send == NULL)
    {
        wdbp->overflow_send = (unsigned char *) malloc(*overflowp);
        memcpy(wdbp->overflow_send, *bp, (*xp - *bp));
    }
    else
        wdbp->overflow_send = (unsigned char *) realloc(wdbp->overflow_send,
                               *overflowp);
    *xp = wdbp->overflow_send + (*xp - *bp);
    *bp = wdbp->overflow_send;
    return;
}
