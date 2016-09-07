/*
 * asyncsr.c - routines that support multiple messages in parallel through
 * HTTP2.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2015";
#include "webdrive.h"
#include "http2.h"
static pthread_t zeroth;
#ifndef MINGW32
#include <pwd.h>
#endif
#include <assert.h>
static void do_receive_queue();
#ifdef DOTNET
#define WANT_EXCEPTION
#endif
#ifdef NEEDS_ORA_KEEPS
int create_keepalive();
#endif
/*
 * Grab URL for logging purposes
 */
static void grab_url(sep, buf)
struct script_element * sep;
unsigned char * buf;       /* Assumed to be at least 256 bytes */
{
unsigned char * xp;
unsigned char * xp1;
int len = 0;

    if ((xp = memchr(sep->body, ' ', sep->body_len)) != NULL
     && (xp = memchr(xp + 1, ' ', sep->body_len - (xp - sep->body) - 1)) != NULL)
    {
        len = xp - sep->body;
        if (len > 255)
            len = 255;
        memcpy(buf, sep->body, len);
    }
    buf[len] = 0;
    return;
}
/*
 * All End Points should already exist, but links may not. Find and if necessary
 * copy across.
 */
LINK * find_script_link(wdbp, sep)
WEBDRIVE_BASE * wdbp;
struct script_element * sep;
{
char from_address[HOST_NAME_LEN];
char to_address[HOST_NAME_LEN];
int from_port_id;
int to_port_id;
WEBDRIVE_BASE * orig_wdbp;
WEBDRIVE_BASE * end_point_wdbp;
END_POINT * from_ep;
END_POINT * to_ep;
LINK * cur_link;
unsigned char ret_buf[24];
unsigned char * got_to = (unsigned char *) NULL;

    (void) nextasc_r(sep->head + 5, ';', '\\', &got_to,
               &from_address[0],&from_address[sizeof(from_address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
               ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        return NULL;
    from_port_id = atoi(ret_buf);
    (void) nextasc_r((char *) NULL, ';', '\\', &got_to,
                          &to_address[0],&to_address[sizeof(to_address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                      ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        return NULL;
    to_port_id = atoi(ret_buf);
    orig_wdbp = wdbp;
    end_point_wdbp = (wdbp->root_wdbp != &webdrive_base) ? wdbp->root_wdbp :
                      wdbp;
    for (;;)
    {
        if (((from_ep = ep_find(from_address,from_port_id,
                         end_point_wdbp)) == (END_POINT *) NULL)
         || ((to_ep = ep_find(to_address,to_port_id,
                         end_point_wdbp)) == (END_POINT *) NULL)
         || ((cur_link = link_find(from_ep, to_ep, wdbp)) != NULL
           && cur_link->link_id == 0))
        {
            if (wdbp != orig_wdbp)
            {
                pthread_mutex_unlock(&wdbp->script_mutex);
                fprintf(stderr, "find_script_link(%lx, %lx) failed\n",
                          (long) orig_wdbp, (long) sep);
                return NULL;
            }
            else
            {
                wdbp = wdbp->root_wdbp;
                if (wdbp == &webdrive_base)
                    return NULL;
                pthread_mutex_lock(&wdbp->script_mutex);
                continue;
            }
        }
        else
        {
            if (wdbp != orig_wdbp)
            {   /* N.B. The End Points and SSL are always the parent ones */
                orig_wdbp->cur_link = &orig_wdbp->link_det[
                          (cur_link - &wdbp->link_det[0])];
                *orig_wdbp->cur_link = *cur_link;
                pthread_mutex_unlock(&wdbp->script_mutex);
                wdbp = orig_wdbp;
                cur_link = orig_wdbp->cur_link;
            }
            if (wdbp->debug_level > 3)
                fprintf(stderr,"find_script_link(%lx, %lx) found %lx (%s:%d,%s:%d)\n",
                          (long) orig_wdbp,
                          (long) sep,
                          cur_link,
                          from_address, from_port_id, to_address, to_port_id);
            return cur_link;
        }
    }
}
/*
 * See if we have work for this link.
 */
static int check_link_work(wdbp, cur_link)
WEBDRIVE_BASE * wdbp;
LINK * cur_link;
{
struct script_element * sep;
unsigned char ret_buf[24];
char * xp;

    sprintf(ret_buf, ":%s;%d\\\n", cur_link->to_ep->host,
                                   cur_link->to_ep->port_id);
    for (sep = wdbp->sc.anchor; sep != NULL; sep = sep->next_track)
    {
        xp = strchr(sep->head + 6, ':');
        assert (xp != NULL);
        if (!strcmp(xp, ret_buf))
            return 1;
    }
    return 0;
}
/*
 * Find out who has signalled exit
 */
LINK * find_wdbp_link(wdbp, search_wdbp)
WEBDRIVE_BASE * wdbp;
WEBDRIVE_BASE * search_wdbp;
{
LINK * cur_link;

    strcpy(wdbp->narrative, "find_wdbp_link");
    if (wdbp->debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) find_wdbp_link(%lx)\n",
                    wdbp->parser_con.pg.rope_seq,
                    (long int) search_wdbp);
    for (cur_link = wdbp->link_det;
            cur_link->link_id != 0
          && cur_link->from_ep != NULL
          && cur_link->to_ep != NULL;
                     cur_link++)
    {
        if (cur_link->to_ep->iwdbp == search_wdbp
         || cur_link->to_ep->owdbp == search_wdbp)
            return cur_link;
    }
    return NULL;
}
/*
 * Find the appropriate output thread and pass it on.
 */
void ts_handoff(wdbp, sep)
WEBDRIVE_BASE * wdbp;
struct script_element * sep;
{
LINK * output_link;

    if (wdbp->debug_level > 3)
        fprintf(stderr, "ts_handoff(%lx, %lx)\n", (long) wdbp, (long) sep);
    assert((output_link = find_script_link(wdbp, sep)) != NULL);
    pipe_buf_add(output_link->to_ep->owdbp->pbp, 1, 0,
                             sep, NULL, 0);
    return;
}
void relaunch_failed(wdbp)
WEBDRIVE_BASE * wdbp;
{
struct script_element * sep;
struct script_element * prev_sep;
LINK * output_link;

    if (wdbp->debug_level > 3)
        fputs("relaunch_failed\n", stderr);
restart:
/*
 * We do not want to re-submit a message whose response is already queued for
 * the main thread. The difficulty we have is knowing whether this is, in fact,
 * the case. It is the main thread that sorts out which out-going message, if
 * any, the response relates to. The retry_cnt == 0 test seems not to be
 * reliable, so we make a pass through any backlog.
 */
    do_receive_queue(wdbp, 0, 1);   /* Ensure the receive pipe_buf is clear */
    for (sep = wdbp->sc.anchor; sep != NULL; sep = sep->next_track)
    {
        assert((output_link = find_script_link(wdbp, sep)) != NULL);
        if (output_link->to_ep->iwdbp != NULL
             && output_link->to_ep->thread_cnt == 0)
            ini_multiplexor(output_link->to_ep, wdbp);
        if (sep->child_track != NULL
         || sep->retry_cnt == 0
         || sep->timestamp != 0.0)
        {
            if (wdbp->debug_level > 3)
                fprintf(stderr, "Script Element ignored:\n%s\nchild_track=%lx retry_cnt=%d timestamp=%.2f\n",
                    sep->body, (long int) sep->child_track, sep->retry_cnt,
                    sep->timestamp);
            continue;   /* Messages with responses awaiting processing */
        }
        if (sep->retry_cnt > 2)
        {
            prev_sep = sep->prev_track;
            if (prev_sep == NULL)
                assert(sep == wdbp->sc.anchor);
            unhook_script_element(&wdbp->sc, sep);
            fprintf(stderr, 
  "(Client:%s) Failed multiple times so retiring - %s\n\%.*s\n%s\n====:(====\n",
                        wdbp->parser_con.pg.rope_seq,
                        sep->head, sep->body_len, sep->body,sep->foot);
            zap_children(sep);
            zap_script_element(sep);
            sep = prev_sep;
            if (sep == NULL)
                goto restart;
            continue;
        }
        if ( sep->child_track == NULL
          && sep->retry_cnt != 0
          && sep->timestamp == 0.0)
        {
            pipe_buf_add(output_link->to_ep->owdbp->pbp, 1, 0,
                             sep, NULL, 0);
        }
        else
        if (wdbp->debug_level > 3)
        {
            fprintf(stderr, "Script Element cannot be resubmitted: %s\n%.*s\nchild_track=%lx retry_cnt=%d timestamp=%.2f\n",
                    sep->head,
                    sep->body_len,
                    sep->body,
                   (long int) sep->child_track, sep->retry_cnt, sep->timestamp);
        }
    }
    return;
}
/*
 * Process incoming messages
 */
static void do_receive_queue(wdbp, block_flag, recurse_flag)
WEBDRIVE_BASE * wdbp;
int block_flag;
int recurse_flag;
{
int ret;
int mess_len;
unsigned char * p1;
char * user = NULL;                    /* Values used for authentication */
char * password = NULL;
char * machine = NULL;
char * domain = NULL;
struct script_element * sep;
char name_buf[256];
LINK * exit_link;
LINK * resubmit_link;
WEBDRIVE_BASE * exit_wdbp;

    for (;;)
    {
        mess_len = 0;
        sep = (struct script_element *) NULL;
        if ((ret = pipe_buf_take(wdbp->pbp, &mess_len,
                      &sep, NULL,
                   block_flag)) < 0
          || mess_len == sizeof(wdbp))
        {
            if (ret >= 0 && mess_len == sizeof(wdbp))
            {     /* Child thread has signalled exit */
                exit_wdbp = *((struct wdbp **) sep); 
                free(sep);
                exit_link = find_wdbp_link(wdbp, exit_wdbp);
                assert(exit_link != NULL);
                if (exit_link->to_ep->thread_cnt <= 0)
                {
                    fprintf(stderr, "do_receive_queue(%lx,%d,%d) finds weird thread_cnt=%d for (%s:%d)\n", (long int) wdbp, block_flag, recurse_flag,
                    exit_link->to_ep->thread_cnt,
                    exit_link->to_ep->host,
                    exit_link->to_ep->port_id);
                }
                exit_link->to_ep->thread_cnt--;
                if (exit_link->to_ep->thread_cnt <= 0)
                {
                    drain_pipe_buf(exit_link->to_ep->owdbp->pbp);
                                             /* Make sure we start empty */
                    if (!recurse_flag && check_link_work(wdbp, exit_link))
                    {
                        pthread_mutex_lock(&wdbp->script_mutex);
                        wdbp->cur_link = exit_link;
                        relaunch_failed(wdbp);
                        pthread_mutex_unlock(&wdbp->script_mutex);
                    }
                }
                else
                if (exit_link->to_ep->iwdbp == exit_wdbp)
                    pipe_buf_add(exit_link->to_ep->owdbp->pbp, 1, 0,
                             NULL, NULL, 0);
                                   /* Notify write thread of exit */
                continue; /* The write thread notifies the reader */
            }
            break; /* Nothing to do now */
        }
        if (!recurse_flag)
            pthread_mutex_lock(&wdbp->script_mutex);
        if (sep->prev_track == NULL
         || (sep->prev_track->prev_track == NULL
         && sep->prev_track->next_track == NULL
         && sep->prev_track != wdbp->sc.anchor))
        {
            fprintf(stderr, "(Client: %s) Done again!? %s\n%.*s\n========\n",
                    wdbp->parser_con.pg.rope_seq, sep->head, sep->body_len,
                    sep->body);
            if (!recurse_flag)
                pthread_mutex_unlock(&wdbp->script_mutex);
        }
        else
            assert(check_integrity(&wdbp->sc, sep->prev_track));
#ifndef TUNDRIVE
        if (wdbp->parser_con.pg.curr_event != NULL
#ifdef DIFF_TIME
          && wdbp->verbosity > 2
#endif
           )
        {
            grab_url(sep->prev_track, name_buf);
            wdbp->last_activity = sep->timestamp;
            log_dotnet_rpc(&(wdbp->parser_con.pg), name_buf,
                       sep->prev_track->timestamp, (sep->timestamp - 
                              sep->prev_track->timestamp));
#ifndef DIFF_TIME
            wdbp->parser_con.pg.curr_event->time_int +=
                        (sep->timestamp - sep->prev_track->timestamp);
#endif
        }
#endif
/*
 * Now do whatever processing is necessary on the received message.
 */
#ifndef TUNDRIVE
        if (wdbp->verbosity || wdbp->except_flag)
        {
            if (wdbp->verbosity > 4 && wdbp->proxy_port == 0)
                event_record_r("R", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                                          /* Note the message */
            if (wdbp->except_flag & E2_GOOD_FOUND)
                event_record_r("G", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                    /* Announce success */
            else
            if (wdbp->except_flag &
                 (E2_HTTP_ERROR | E2_SERVER_ERROR | E2_ERROR_FOUND
                     | E2_DISASTER_FOUND))
                event_record_r("Z", (struct event_con *) NULL, &(wdbp->parser_con.pg));
            if (wdbp->parser_con.pg.curr_event != NULL)
            {
                if (wdbp->except_flag & E2_HTTP_ERROR)
                    wdbp->parser_con.pg.curr_event->flag_cnt[0]++;
                if (wdbp->except_flag & E2_SERVER_ERROR)
                    wdbp->parser_con.pg.curr_event->flag_cnt[1]++;
                if (wdbp->except_flag & E2_ERROR_FOUND)
                    wdbp->parser_con.pg.curr_event->flag_cnt[2]++;
                if (wdbp->except_flag & E2_GOOD_FOUND)
                    wdbp->parser_con.pg.curr_event->flag_cnt[3]++;
            }
            if (wdbp->verbosity > 1 || (wdbp->except_flag != 0
              && !(wdbp->except_flag & E2_GOOD_FOUND)))
            {
                if (wdbp->verbosity < 2)
                {    /* Otherwise we have output it already */
                    fprintf(stderr, "(Client: %s) Out===> except_flag: 0x%x\n",
                          wdbp->parser_con.pg.rope_seq, wdbp->except_flag);
                    (void) weboutrec(stderr, sep->prev_track->body, IN, sep->prev_track->body_len, wdbp);
                }
                fprintf(stderr, "(Client: %s) In<==== except_flag: 0x%x\n",
                      wdbp->parser_con.pg.rope_seq, wdbp->except_flag);
                if (((wdbp->except_flag & E2_HTTP_ERROR) == 0)
                 ||  (strncmp(&sep->body[9], "401", 3)
                  && strncmp(&sep->body[9], "407", 3)))
                {
                    (void) weboutrec(stderr, sep->body, IN, sep->body_len,
                            wdbp);
                }
            }
        }
/*
 * Take any special action based on the message just processed ...
 * If we have the keepalive response, resend
 */
#ifdef NEEDS_ORA_KEEPS
        if (bm_match(wdbp->kap,
            (unsigned char *) sep->body,
            ((unsigned char *) sep->body) + sep->body_len)
                         != (unsigned char *) NULL)
        {
            if ( sep->prev_track->body_len =  create_keepalive(wdbp,
                     sep->prev_track->body,
                     sep->prev_track->body_len))
            {
                sep = sep->prev_track;
                zap_children(sep);
                ts_handoff(wdbp, sep);
                if (!recurse_flag)
                    pthread_mutex_unlock(&wdbp->script_mutex);
                continue;
            }
        }
#endif
        if ((wdbp->except_flag & E2_ERROR_FOUND)
          && (check_recoverable(wdbp, sep->body, sep->body + sep->body_len)
                     != 0))
        {
/*
 * We have received instructions to Rerun the transaction ...
 */
            sep = sep->prev_track;
            zap_children(sep);
            ts_handoff(wdbp, sep);
            if (!recurse_flag)
                pthread_mutex_unlock(&wdbp->script_mutex);
            continue;
        }
#endif
#ifndef DOTNET
        if (wdbp->except_flag & E2_HTTP_ERROR
         && (!strncmp(&sep->body[9], "401", 3)  /* Web Server authenticate */
          ||!strncmp(&sep->body[9], "407", 3))) /* Proxy authenticate */
        {
        char * user_var;
        char * passwd_var;
        char * domain_var;

            if (sep->body[11] == '1')  /* Web Server authenticate */
            {
                user_var = "USER";
                passwd_var = "PASSWORD";
                domain_var = "DOMAIN";
            }
            else
            {
                user_var = "PROXY_USER";
                passwd_var = "PROXY_PASSWORD";
                domain_var = "PROXY_DOMAIN";
            }
            if ((p1 = bm_match(wdbp->authip, sep->body, sep->body + sep->body_len))
                         != (unsigned char *) NULL)
            {
            int len;

/*
 * NTLM Authentication
 *
 * Find out if it is a blank authorisation (in which case, inject Type 1)
 * or a Type 2 (in which case, inject a Type 3), then re-transmit
 */
                p1 += wdbp->authip->match_len;
/*
 * Use the returned message buffer to build up the details.
 */
                memcpy( ((unsigned char *) &(wdbp->ret_msg)), "NTLM ",5);
/*
 * We need to create a type 1 or type 3, and edit it in to the header
 */
                if (*p1 == '\r')
                {
                    if (machine == NULL
                      && ((machine = getvarval(&wdbp->parser_con.csmacro,"MACHINE"))
                             == NULL))
                        machine = "EDC-NS15";
                    if (domain == NULL
                      && ((domain = getvarval(&wdbp->parser_con.csmacro,domain_var))
                                   == NULL))
                        domain = "RMJM";
                    len = ntlm_construct_type1(machine, domain,
                             ((unsigned char *) &(wdbp->ret_msg)) + 5);
                }
                else
                {
                    if (user == NULL
                     && ((user = getvarval(&wdbp->parser_con.csmacro,user_var))
                             == NULL))
                        user = "perftest";
                    if (password == NULL
                     && ((password = getvarval(&wdbp->parser_con.csmacro,
                                               passwd_var))
                             == NULL))
                        password = "passw0rd1";
                    len = ntlm_construct_type3(machine, domain,
                               user, password, NULL, p1 + 1,
                       (((unsigned char *) strchr(p1 + 1, '\r')) - p1) + 1,
                             ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 5);
                }
                if (len < 1)
                    fprintf(stderr, "(Client: %s) NTLM authorisation failed\n",
                      wdbp->parser_con.pg.rope_seq);
                else
                {
                    if (wdbp->debug_level > 1)
                    {
                        fprintf(stderr, "(Client: %s) NTLM string %.*s\n",
                              wdbp->parser_con.pg.rope_seq, len, 
                             ((unsigned char *) &(wdbp->ret_msg)) + 5);
                            fflush(stderr);
                    }
                    sep->prev_track->body = realloc(sep->prev_track->body,
                         sep->prev_track->body_len + len + 5);
                    sep->prev_track->body_len = edit_header(wdbp,
                           (struct bm_table *) ((*user_var == 'P') ?
                                    (unsigned long) wdbp->authpp :
                                         (unsigned long) wdbp->authop),
                             ((unsigned char *) &(wdbp->ret_msg)),
                              len + 5, sep->prev_track->body,
                                         sep->prev_track->body_len);
                    if (bm_match(wdbp->authpc, sep->body ,sep->body +
                         sep->body_len )
                                != (unsigned char *) NULL)
                    {
                        fputs("Connection: close and HTTP2!?\n", stderr);
                        if (!recurse_flag)
                            pthread_mutex_unlock(&wdbp->script_mutex);
                        goto bottom;
                    }
                }
                sep = sep->prev_track;
                zap_children(sep);
                ts_handoff(wdbp, sep);
                if (!recurse_flag)
                    pthread_mutex_unlock(&wdbp->script_mutex);
                continue;
            }
            else
            if ((p1 = bm_match(wdbp->authbp, (unsigned char *) sep->body,
                       ((unsigned char *) sep->body + sep->body_len)))
                         != (unsigned char *) NULL)
            {
            int len;
/*
 * Basic Authentication
 */
                if (mess_len > WORKSPACE - 512)
                    fprintf(stderr,
"(Client: %s) Authorisation required but Incoming (%d) too long; increase WORKSPACE in webdrive.h\n",
                          wdbp->parser_con.pg.rope_seq, mess_len);
                else
                {
                    memcpy( ((unsigned char *) &(wdbp->ret_msg)),
                           "Basic ", 6);
                    if (user == NULL
                     && ((user = getvarval(&wdbp->parser_con.csmacro,user_var)) == NULL))
                        user = "perftest";
                    if (password == NULL
                     && ((password = getvarval(&wdbp->parser_con.csmacro,passwd_var))
                             == NULL))
                        password = "passw0rd1";
                    len = basic_construct(user, password,
                             ((unsigned char *) &(wdbp->ret_msg)) + 6);
                }
                if (len < 1)
                    fprintf(stderr, "(Client: %s) Basic authorisation failed\n",
                          wdbp->parser_con.pg.rope_seq);
                else
                {
                    if (wdbp->debug_level > 1)
                    {
                        fprintf(stderr, "(Client: %s) Basic string %.*s\n",
                              wdbp->parser_con.pg.rope_seq, len, 
                             ((unsigned char *) &(wdbp->ret_msg)) + 6);
                        fflush(stderr);
                    }
                    sep->prev_track->body = realloc(sep->prev_track->body,
                         sep->prev_track->body_len + len + 6);
                    sep->prev_track->body_len = edit_header(wdbp,
                                         wdbp->authop,
                             ((unsigned char *) &(wdbp->ret_msg)),
                              len + 6, sep->prev_track->body,
                              sep->prev_track->body_len);
                }
                sep = sep->prev_track;
                zap_children(sep);
                ts_handoff(wdbp, sep);
                if (!recurse_flag)
                    pthread_mutex_unlock(&wdbp->script_mutex);
                continue;
            }
        }
        if (wdbp->except_flag & E2_DISASTER_FOUND
#ifdef ORA9IAS_2
          || (bm_match(wdbp->erp, (unsigned char *) sep->body,
                       ((unsigned char *) sep->body + sep->body_len))
                         != (unsigned char *) NULL)
#endif
                   )
        {    /* Seen a Disastrous Error; have to give up and start over */
#ifndef DONT_TRUST_RESYNC
        enum tok_id tok_id;
#endif

#ifdef ORA9IAS_2
            fprintf(stderr, "(Client: %s) In<==== Pragma: %d\n",
                                     wdbp->parser_con.pg.rope_seq,
                                     wdbp->pragma_seq);
            (void) weboutrec(stderr, (char *) &(wdbp->ret_msg),IN,
                                            mess_len, wdbp);
#endif
            if (wdbp->proxy_port == 0)
                event_record_r("X", (struct event_con *) NULL,
                                    &(wdbp->parser_con.pg));
                                                   /* Note the message */
            alarm_restore(wdbp);
            socket_cleanup(wdbp);
#ifndef DONT_TRUST_RESYNC
            do_resync(wdbp);
#else
            wdbp->parser_con.pg.think_time = 1;
#endif
        }
#endif
        if (!recurse_flag)
            pthread_mutex_unlock(&wdbp->script_mutex);
bottom:
        if (!recurse_flag)
            pthread_mutex_lock(&wdbp->script_mutex);
        if (sep->prev_track->head == NULL)
        {
            fprintf(stderr, "Response without request %s\n\%.*s\n", sep->head,
                       sep->body_len, sep->body);
            p1 = NULL;
        }
        else
        {
            p1 = strdup(sep->prev_track->head);
            if (wdbp->debug_level > 3)
                fprintf(stderr, "Finished with %s\n\%.*s\n", sep->prev_track->head,
                       sep->prev_track->body_len, sep->prev_track->body);
        }
        remove_se_subtree(&wdbp->sc, sep->prev_track);
                                         /* Finished with this one ... */
        if (wdbp->sc.anchor == NULL || p1 == NULL)
        {
            if (!recurse_flag)
                pthread_mutex_unlock(&wdbp->script_mutex);
            if (p1 != NULL)
                free(p1);
            break;   /* Nothing more and we have reached a quiescent point */
        }
        else
        if (wdbp->sc.anchor != NULL)
        {
            for (sep = wdbp->sc.anchor; 
                    sep != NULL && strcmp(sep->head, p1);
                       sep = sep->next_track);
            if (sep != NULL)
            {
                assert((resubmit_link = find_script_link(wdbp, sep)) != NULL);
                if (wdbp->debug_level > 3)
                    fprintf(stderr, "Re-submit: %s\n\%.*s\n", sep->head,
                                   sep->body_len, sep->body);
                pipe_buf_add(resubmit_link->to_ep->owdbp->pbp, 1, 0,
                             sep, NULL, 0);
            }
        }
        if (!recurse_flag)
            pthread_mutex_unlock(&wdbp->script_mutex);
        free(p1);
    }
    return;
}
/*******************************************************************************
 * Process messages. This routine is only called if there is something
 * to do.
 *******************************************************************************
 * Web-specific code is included here. The general solution would
 * provide hooks into which any TCP/IP based message passing scheme could
 * be attached.
 *******************************************************************************
 * Evolutionary development over many years has given us:
 * -  A lump of context; the WEBDRIVE_BASE structure, that contains info for
 *    the communications end points, the session status, substitutions, reading
 *    of script input files, macro processing and what have you
 * -  A thread management framework, that creates, destroys and despatches
 *    threads to do work on the contextual things
 * -  A so far limited separation of the code to accept input, and progress the
 *    the context.
 * -  No separation at all of this, the routine that does most of the work
 *    on a request.
 *******************************************************************************
 * We really want this stuff to be in loadable modules, so that:
 * -  We don't need all the different #ifdef's spread through the code
 * -  Supporting new applications simply requires the development of a limited
 *    number of well defined routines.
 *******************************************************************************
 * This is the new asynchronous-capable version.
 * -  sync_flag indicates whether synchronous or asynchronous operation
 *    is required.
 * -  a NULL input message indicates we must wait for all outstanding
 *    calls to finish.
 * Processing:
 * -  If there is a message, add it to the script chain.
 * -  Pass the message on the chain off to the logic for message propagation.
 * -  Now process any return notifications.
 *    -   If synchronous, block
 *    -   Otherwise, non blocking.
 *    -   Process each pair in much the same way as at present, retransmitting
 *        if there were errors, or if there is a proxy/NTLM response needed.
 *    -   If successful, log the message pair if appropriate, log the timing,
 *        remove the message pair from the chain.
 *    -   If the input message was NULL, continue until the chain is empty
 *    -   If the sync_flag is set, continue until the original message is done,
 *        then clear the sync_flag
 */
void do_send_receive_async(msg, wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
struct script_element * sep;

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Send Receive Message Sequence %d\n",
            wdbp->parser_con.pg.rope_seq,
                   wdbp->parser_con.pg.seqX);
        fflush(stderr);
    }
    strcpy(wdbp->narrative, "do_send_receive_async");
#ifndef TUNDRIVE
    if (wdbp->parser_con.pg.curr_event != NULL
#ifdef DIFF_TIME
      && wdbp->verbosity > 2
#endif
       )
    {
        wdbp->last_activity = timestamp();
    }
#endif
    pthread_mutex_lock(&wdbp->script_mutex);
    if (msg != NULL)
    {
        if (wdbp->cur_link->to_ep->iwdbp == NULL
          || pthread_equal(wdbp->cur_link->to_ep->iwdbp->own_thread_id, zeroth))
        {
            ini_multiplexor(wdbp->cur_link->to_ep, wdbp);
            if (wdbp->cur_link->t3_flag != 2)
            {
                pthread_mutex_unlock(&wdbp->script_mutex);
                do_send_receive(msg, wdbp);
                return;
            }
        }
    }
    else
    if (wdbp->sc.anchor != NULL)
        relaunch_failed(wdbp);
/*
 * Add an incoming message to the script chain and queue it.
 */
    if (msg != NULL)
    {
        sep = add_message(&wdbp->sc, wdbp->cur_link);
        if ( msg->send_receive.message_len > WORKSPACE)
        {
            sep->body = wdbp->overflow_send; /* Re-point the overflow buffer */
            wdbp->overflow_send = NULL;
        }
        else
        {
            sep->body = (unsigned char *) malloc(msg->send_receive.message_len);
            memcpy(sep->body, msg->send_receive.msg->buf,
                      msg->send_receive.message_len);
        }
        sep->body_len = msg->send_receive.message_len;
        if (wdbp->verbosity > 1)
        {
            fprintf(stderr, "(Client: %s) Out===> (%s:%d => %s:%d)\n",
                      wdbp->parser_con.pg.rope_seq,
                      wdbp->cur_link->from_ep->host,
                      wdbp->cur_link->from_ep->port_id,
                      wdbp->cur_link->to_ep->host,
                      wdbp->cur_link->to_ep->port_id);
            (void) weboutrec(stderr, sep->body, IN, sep->body_len, wdbp);
        }
        ts_handoff(wdbp, sep);
    }
    pthread_mutex_unlock(&wdbp->script_mutex);
    do_receive_queue(wdbp, ((msg == NULL && wdbp->sc.anchor != NULL) ? 1 : 0),
                             0);
    return;
}
