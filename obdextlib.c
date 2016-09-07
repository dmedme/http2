/*
 * Scan a snoop file and pull out the Objective Document Management elements
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems 1996";

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "e2conv.h"
#include "e2net.h"
static struct frame_con * cur_frame;
static void do_obd();
static void do_log();
static FILE * ofp;
static int both_ways;
static int verbose;
static unsigned char * outstr();
unsigned char * obd_handle();
/*
 * Structure allocated when a session is started that holds Objective session
 * state.
 */
enum obd_state {OBD_HEAD, OBD_LEN, OBD_FLD};
struct obd_sess {
    enum obd_state obd_state[2];
    int len_to_do[2];
    int flds_to_do[2];
    unsigned char * kept_msg[2];
    unsigned char * cur_loc[2];
};
static struct bm_table *pav; /* Marker for records */
/***********************************************************************
 * The following logic allows us to feed in the interesting ports.
 */
static int extend_listen_flag; /* Feed in extra listener ports            */ 
static int obd_port[100];    /* List of ports to match against          */
static int obd_cnt;              /* Number of ports in the list    */
static void obd_match_add(arr, cnt, port)
int * arr;
int * cnt;
int port;
{
    if (*cnt < 100)
    {
       arr[*cnt] = port;
       (*cnt)++;
    }
    return;
}
/*
 * Allow listener ports to be specified in the environment
 */
static void extend_listen_list()
{
char * x;
int i;

    extend_listen_flag = 1;
    if ((x = getenv("E2_OBD_PORTS")) != (char *) NULL)
    {
        for (x = strtok(x," "); x != (char *) NULL; x = strtok(NULL, " "))
        {
            if ((i = atoi(x)) > 0 && i < 65536)   
                obd_match_add(obd_port, &obd_cnt, i);
        }
    }
    if ((x = getenv("E2_BOTH")) != (char *) NULL)
        both_ways = 1;
    if ((x = getenv("E2_VERBOSE")) != (char *) NULL)
        verbose = 1;
    return;
}
static int obd_match_true(arr, cnt, from, to)
int *arr;
int cnt;
int from;
int to;
{
int i;

#ifdef DEBUG
    printf("From port:%d To Port:%d\n",from,to);
#endif
    for (i = 0; i < cnt; i++)
    {
       if (arr[i] == from || arr[i] == to)
       {
           if (arr[i] == to)
               return  1;         /* Flag which end is the client */
           else
               return -1;
       }
    }
    return 0;
}
/*
 * Discard dynamically allocated session structures. Empty for now. But stops
 * the script file being closed on session close.
 */
static void do_cleanup(frp)
struct frame_con *frp;
{
    if (frp->app_ptr != (char *) NULL)
        free(frp->app_ptr);
    return;
}
/*
 * Function that decides which sessions are of interest, and sets up the
 * relevant areas of the frame control structure. We are aiming to get
 * genconv.c e2net.* etc. into a state where new applications can be added
 * with no changes to the framework.
 */
int obd_app_recognise(frp)
struct frame_con *frp;
{
int i;
unsigned short int from, to;

    if (pav == (struct bm_table *) NULL)
        pav = bm_compile("PAV~");
    cur_frame = frp;
/*
 * Decide if we want this session.
 * We want it if:
 * -  The protocol is TCP
 * -  The port is identified in the list of interesting ports, managed
 *    with obd_match_add() and obd_match_true()
 */
    if (extend_listen_flag == 0)
        extend_listen_list();
    if (frp->prot == E2_TCP)
    {
        if (!(frp->tcp_flags & TH_SYN))
            return 0;
        memcpy((char *) &to, &(frp->port_to[1]), 2);
        memcpy((char *) &from, &(frp->port_from[1]), 2);
        if (from == 8008)
            i = -1;
        else
        if ( to == 8008)
            i = 1;
        else
            i = obd_match_true(obd_port, obd_cnt, from, to);
        if (i)
        {
            if (ofp == (FILE *) NULL)
                ofp = fopen("obd_script.msg", "wb");
            frp->ofp = ofp;
            if (frp->ofp == (FILE *) NULL)
                frp->ofp = stdout;   /* Out of file descriptors */
            fputs( "\\M:", ofp);
            ip_dir_print(ofp, frp, 0);
            fputs( "\\\n", ofp);
            if (i == -1)
                frp->reverse_sense = 1;
            frp->do_mess = do_obd;
            frp->cleanup = do_cleanup;
            frp->app_ptr = (char *) malloc(sizeof(struct
                                   obd_sess));
            ((struct obd_sess *)(frp->app_ptr))->len_to_do[0] = 32;
            ((struct obd_sess *)(frp->app_ptr))->len_to_do[1] = 32;
            ((struct obd_sess *)(frp->app_ptr))->obd_state[0] = OBD_HEAD;
            ((struct obd_sess *)(frp->app_ptr))->obd_state[1] = OBD_HEAD;
            ((struct obd_sess *)(frp->app_ptr))->flds_to_do[0] = 0;
            ((struct obd_sess *)(frp->app_ptr))->flds_to_do[1] = 0;
            ((struct obd_sess *)(frp->app_ptr))->kept_msg[0] =
                            (unsigned char *) malloc(32);
            ((struct obd_sess *)(frp->app_ptr))->cur_loc[0] =
                        ((struct obd_sess *)(frp->app_ptr))->kept_msg[0];
            ((struct obd_sess *)(frp->app_ptr))->kept_msg[1] =
                            (unsigned char *) malloc(32);
            ((struct obd_sess *)(frp->app_ptr))->cur_loc[1] =
                        ((struct obd_sess *)(frp->app_ptr))->kept_msg[1];
            return 1;
        }
    }
    else
    if (frp->prot == E2_UDP )
    {
        memcpy((char *) &to, &(frp->port_to[1]), 2);
        memcpy((char *) &from, &(frp->port_from[1]), 2);
        if (from == 7 || to == 7)
        {
            if (ofp == (FILE *) NULL)
                ofp = fopen("obd_script.msg", "wb");
            frp->ofp = ofp;
            if (frp->ofp == (FILE *) NULL)
                frp->ofp = stdout;   /* Out of file descriptors */
            if (from == 7)
                frp->reverse_sense = 1;
            frp->do_mess = do_log;
            frp->cleanup = do_cleanup;
            frp->app_ptr = (char *) NULL;
            return 1;
        }
    }
    return 0;
}
static void inc_len(ap, dir_flag, inc_len)
struct obd_sess * ap;
int dir_flag;
int inc_len;
{
unsigned char * rp = (unsigned char *) realloc(ap->kept_msg[dir_flag],
                                          (ap->cur_loc[dir_flag] - 
                                           ap->kept_msg[dir_flag]) +
                          inc_len);
    ap->cur_loc[dir_flag] = rp + (ap->cur_loc[dir_flag] - 
                                           ap->kept_msg[dir_flag]);
    ap->kept_msg[dir_flag] = rp;
    return;
}

/*
 * Deal with a fragment of Objective forms traffic
 * -  We build the whole message up. We always know in advance how
 *    much we are expecting.
 * -  We begin by copying the lesser of what we are expecting, and what
 *    is available, to the location in te buffer.
 * -  If we have everything, we then decide what we are going to receive
 *    next
 * -  If we have just read the first 32, we need to set up the number of
 *    fields
 * -  If we have just read the header, or a field:
 *    -  If it is the last one for this record, output the whole record,
 *       and reset it to read another header
 *    -  Otherwise, set up to read another 6 byte length
 * -  Otherwise (we have just read a length)
 *    - set up to read the data
 */
static void do_obd(frp, dir_flag)
struct frame_con * frp;
int dir_flag;
{
unsigned char * x;
unsigned char * top;
unsigned char * rp;
int len;
struct obd_sess * ap = (struct obd_sess *) (frp->app_ptr);

    cur_frame = frp;
    x = frp->hold_buf[dir_flag];
    top = frp->top[dir_flag];
    while (x < top)
    {
        len = top - x;
        if (len < ap->len_to_do[dir_flag])
        {
            memcpy(ap->cur_loc[dir_flag], x, len);
            ap->len_to_do[dir_flag] -= len;
            ap->cur_loc[dir_flag] += len; 
            return;
        }
        memcpy(ap->cur_loc[dir_flag], x, ap->len_to_do[dir_flag]);
        x += ap->len_to_do[dir_flag];
        ap->cur_loc[dir_flag] += ap->len_to_do[dir_flag];
        if (ap->obd_state[dir_flag] == OBD_HEAD)
        {
            if ((rp = bm_match(pav,(ap->cur_loc[dir_flag] - 32),
                                    ap->cur_loc[dir_flag])) !=
                                   (ap->cur_loc[dir_flag] - 32))
            {
                if (rp == (unsigned char *) NULL)
                    ap->cur_loc[dir_flag] = ap->kept_msg[dir_flag];
                else
                {
                    ap->len_to_do[dir_flag] =
                       32 - (ap->cur_loc[dir_flag] - rp);                
                    inc_len(ap, dir_flag, ap->len_to_do[dir_flag]);
                }
                continue;
            }
            ap->flds_to_do[dir_flag] =
                     (ap->kept_msg[dir_flag][23] - 48) +
                     (ap->kept_msg[dir_flag][22] - 48)*10 +
                     (ap->kept_msg[dir_flag][21] - 48)*100 +
                     (ap->kept_msg[dir_flag][20] - 48)*1000;
            if ( ap->flds_to_do[dir_flag] > 0)
            {
                ap->len_to_do[dir_flag] = 6;
                ap->obd_state[dir_flag] = OBD_LEN;
                inc_len(ap, dir_flag, 6);
            }
            else
                ap->flds_to_do[dir_flag] = 0;
        }
        else
        if (ap->obd_state[dir_flag] == OBD_FLD)
        {
            ap->flds_to_do[dir_flag]--;
            if ( ap->flds_to_do[dir_flag] == 0)
                ap->obd_state[dir_flag] = OBD_HEAD;
            else
            {
                inc_len(ap, dir_flag, 6);
                ap->len_to_do[dir_flag] = 6;
                ap->obd_state[dir_flag] = OBD_LEN;
            }
        }
        else
        if (ap->obd_state[dir_flag] == OBD_LEN)
        {
            ap->len_to_do[dir_flag] =
                     (ap->cur_loc[dir_flag][-1] - 48) +
                     (ap->cur_loc[dir_flag][-2] - 48)*10 +
                     (ap->cur_loc[dir_flag][-3] - 48)*100 +
                     (ap->cur_loc[dir_flag][-4] - 48)*1000 +
                     (ap->cur_loc[dir_flag][-5] - 48)*10000 +
                     (ap->cur_loc[dir_flag][-6] - 48)*100000;
            if (ap->len_to_do[dir_flag] < 0)
            {
                ap->cur_loc[dir_flag] = ap->kept_msg[dir_flag];
                ap->obd_state[dir_flag] = OBD_HEAD;
                ap->flds_to_do[dir_flag] = 0;
                ap->len_to_do[dir_flag] = 32;
                continue;
            }
            ap->obd_state[dir_flag] = OBD_FLD;
            inc_len(ap, dir_flag, ap->len_to_do[dir_flag]);
        }
        if ( ap->obd_state[dir_flag] == OBD_HEAD
           && ap->flds_to_do[dir_flag] == 0)
        {
            if ((!dir_flag) ^ frp->reverse_sense)
            {
                fputs("\\D:B:", frp->ofp);
                ip_dir_print(frp->ofp, frp, dir_flag);
                fputs("\\\n", frp->ofp);
                obd_handle(frp->ofp, ap->kept_msg[dir_flag],
                                     ap->cur_loc[dir_flag], 1);
                fputc('\n', frp->ofp);
                fputs("\\D:E\\\n", frp->ofp);
            }
            else
            if (both_ways)
            {
                fputs("\\A:B:", frp->ofp);
                ip_dir_print(frp->ofp, frp, dir_flag);
                fputs("\\\n", frp->ofp);
                obd_handle(frp->ofp, ap->kept_msg[dir_flag],
                                     ap->cur_loc[dir_flag], 1);
                fputc('\n', frp->ofp);
                fputs("\\A:E\\\n", frp->ofp);
            }
            ap->len_to_do[dir_flag] = 32;
            ap->cur_loc[dir_flag] = ap->kept_msg[dir_flag];
            inc_len(ap, dir_flag, 32);
        }
    }
    return;
}
static int event_id;
static char * event_desc;
static void open_event()
{
    if (ofp !=  NULL)
        fprintf(ofp, "\\S%X:120:%s \\\n", event_id, event_desc);
    return;
}
static void close_event()
{
    if (ofp !=  NULL && event_id != 0)
        fprintf(ofp, "\\T%X:\\\n",event_id);
    return;
}
/*
 * Function that is called to process log messages
 */
static void do_log(frp, dir_flag)
struct frame_con * frp;
int dir_flag;
{
unsigned short int to;

    cur_frame = frp;
    if ((!dir_flag) ^ frp->reverse_sense)
    {
        if (event_id != 0)
        {
            close_event();
            event_id++;
            if (event_id > 239)
                event_id = 160;
        }
        else
            event_id = 160;
/*
 * This is one of our event definitions. Only pick up the ECHO packet
 * going in one direction, by specifying the Destination port. Note that
 * we expect PATHSYNC to put a trailing NULL on the message.
 */
        if (event_desc != (char *) NULL)
            free(event_desc);
        event_desc = strdup(frp->hold_buf[dir_flag]);
        open_event();
    }
    return;
}
/*
 * Dump out a human-readable rendition of the Objective messages
 * - Messages consist of:
 *   - A 32 byte header
 *   - Including a count of the sub-elements to follow
 *   - Followed by zero or more further elements, each of which is
 *     - A six byte ASCII length
 *     - The data (that can be anything)
 */
unsigned char * obd_handle(ofp, base, top, out_flag)
FILE *ofp;
unsigned char * base;
unsigned char * top;
int out_flag;
{
    return gen_handle_no_uni(ofp, base, top, out_flag);
}
