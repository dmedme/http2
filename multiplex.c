#include <assert.h>
#ifndef MINGW32
#include <unistd.h>
#include <sys/syscall.h>
#endif
#include "webdrive.h"
#include "scripttree.h"
#include "hpack.h"
#include "http2.h"
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 2015";
static pthread_t zeroth;
#ifndef MINGW32
long int GetCurrentThreadId()
{
    return (long int) syscall(SYS_gettid);
}
#endif
void reinit_http2_socket(cur_link, wdbp)
LINK * cur_link;
WEBDRIVE_BASE * wdbp;
{
END_POINT * to_ep = cur_link->to_ep;

    pthread_mutex_lock(&to_ep->rights);
#ifdef DEBUG
    fputs("reinit_http2_socket()\n", stderr);
#endif
    cur_link = to_ep->h2cp->io_link;
    link_clear(cur_link, wdbp);
    iterate(to_ep->h2cp->stream_hash, NULL, clear_in_flight); 
    wdbp->cur_link = cur_link;
    t3drive_connect(wdbp);
    refresh_http2_con(to_ep->h2cp);
    to_ep->iwdbp->go_away = 0;
    to_ep->iwdbp->cur_link = to_ep->h2cp->io_link;
    to_ep->owdbp->go_away = 0;
    to_ep->owdbp->cur_link = to_ep->h2cp->io_link;
    smart_write(cur_link->connect_fd,
                 "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24, 1, wdbp);
    pthread_mutex_unlock(&to_ep->rights);
    despatch_a_thread(to_ep->iwdbp, wdbp);  /* Asynchronous */
    despatch_a_thread(to_ep->owdbp, wdbp);  /* Asynchronous */
    to_ep->thread_cnt = 2;
    return;
}
/*
 * Need to:
 * -  initialise the pipe_bufs (one for incoming, the other outgoing)
 * -  set up pointers for the links
 * -  allocate a pair of threads, one outgoing and one incoming.
 * Incoming will be:
 * -  Whole HTTP 1.1 messages, as script elements
 * Outgoing will be:
 * -  Whole HTTP 1.1 messages as script elements?
 * The multiplexor must encapsulate all HTTP2 stuff.
 * The caller is responsible for:
 * -  Working out when it is sensible to feed stuff (cannot feed things when
 *    we are waiting for dependencies on the outstanding)
 * -  Marrying up the responses to the requests.
 * SPNEGO (if it was supported) NTLM and Basic Authentication would have to be
 * handled outside the multiplexor, by the caller.
 */
void ini_multiplexor(ep, wdbp)
END_POINT * ep;
WEBDRIVE_BASE * wdbp;
{
/*
 * Get ready to do the forwarding.
 */
struct http2_con * h2cp; 
struct pipe_buf * ipb;
struct pipe_buf * opb;

    if (wdbp->debug_level > 3)
        fprintf(stderr, "ini_multiplexor(%lx, %lx)\n", (long) ep, (long) wdbp);
/*
 * Initialise the data structures
 */
    if (ep->iwdbp == NULL)
    {
        t3drive_connect(wdbp);
        if (wdbp->cur_link->t3_flag != 2)
            return;
        ep->iwdbp = (WEBDRIVE_BASE *) malloc(sizeof(*wdbp));
        *(ep->iwdbp) = *wdbp;
        ep->iwdbp->own_thread_id = zeroth;
        ep->owdbp = (WEBDRIVE_BASE *) malloc(sizeof(*wdbp));
        *(ep->owdbp) = *wdbp;
        ep->owdbp->own_thread_id = zeroth;
        ep->owdbp->pbp = pipe_buf_cre(128); /* How many do we need? */
        if (wdbp->pbp == NULL)
            wdbp->pbp = pipe_buf_cre(128); /* How many do we need? */
        ep->iwdbp->pbp = wdbp->pbp;
        ep->iwdbp->te2mbp = NULL;     /* be_a_thread() will fix these */
        ep->owdbp->te2mbp = NULL;
/*
 * We don't need these for the children.
 */
        ep->iwdbp->parser_con.tbuf = NULL;
        ep->iwdbp->parser_con.sav_tlook = NULL;
        ep->iwdbp->parser_con.tlook = NULL;
        ep->owdbp->parser_con.tbuf = NULL;
        ep->owdbp->parser_con.sav_tlook = NULL;
        ep->owdbp->parser_con.tlook = NULL;
/*
 * The End Points need to be the original ones, so save a pointer to the parent
 */
        ep->iwdbp->root_wdbp = wdbp;
        ep->iwdbp->cur_link = wdbp->cur_link;
        ep->owdbp->root_wdbp = wdbp;
        ep->owdbp->cur_link = wdbp->cur_link;
        ep->iwdbp->progress_client = link_pipe_buf_forward;
        ep->owdbp->progress_client = pipe_buf_link_forward;
    }
/*
 * Launch the threads that will service the connection, in and out.
 */
    if (ep->h2cp == NULL)
    {
        ep->h2cp = new_http2_con(ep, wdbp->cur_link);
        wdbp->cur_link = ep->h2cp->io_link;
        smart_write(ep->h2cp->io_link->connect_fd,
            "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24, 1, wdbp);
        despatch_a_thread(ep->iwdbp, wdbp);  /* Asynchronous */
        despatch_a_thread(ep->owdbp, wdbp);  /* Asynchronous */
        ep->thread_cnt = 2;
    }
    else
        reinit_http2_socket(ep->h2cp->io_link, wdbp);
    q_http2_settings(ep, 0);             /* Default HTTP2 settings */
    return;
}
void fill_frame(hfhp, len, typ, stream_id, flags)
struct http2_frame_header *hfhp;
int len;
int typ;
int stream_id;
{
#ifdef DEBUG
    fprintf(stderr, "fill_frame(%d, %d, %d, %d)\n", len, typ, stream_id, flags);
#endif
    hfhp->len[0] = (len & 0xff0000) >> 16;
    hfhp->len[1] = (len & 0xff00) >> 8;
    hfhp->len[2] = (len & 0xff);
    hfhp->flags = flags;
    hfhp->typ = typ;
    hfhp->sid[0]  = ((stream_id & 0x7f000000) >> 24);
    hfhp->sid[1]  = ((stream_id & 0xff0000) >> 16);
    hfhp->sid[2]  = ((stream_id & 0xff00) >> 8);
    hfhp->sid[3]  = ((stream_id & 0xff));
    return;
}
void notify_master(pbp, wdbp)
struct pipe_buf * pbp;
WEBDRIVE_BASE * wdbp;
{
WEBDRIVE_BASE ** send_wdbp = (WEBDRIVE_BASE **) malloc(sizeof(wdbp));

    *send_wdbp = wdbp;
#ifdef DEBUG
    fprintf(stderr, "notify_master(%lx, %lx)\n", (long) pbp, (long) wdbp);
#endif
    pipe_buf_add(pbp, 1, sizeof(wdbp), send_wdbp,  NULL, 0);
    return;
}
/*
 * End Point Write thread routine
 */
void pipe_buf_link_forward(wdbp)
WEBDRIVE_BASE * wdbp;
{
struct script_element * sep;
int len;
int bite;
int out_cnt;
int typ;
int i;
WEBDRIVE_BASE **notif;
END_POINT * to_ep;
unsigned char * xp, *xp1;
unsigned char * top;
LINK * cur_link;
LINK * http1_link;
char len_buf[13];
long int tid = GetCurrentThreadId();
pthread_t me = pthread_self();
struct http2_frame_header *hfhp;

    to_ep = wdbp->cur_link->to_ep;    /* On entry, this points to the parent */ 
    cur_link = to_ep->h2cp->io_link;  /* I/O will be with respect to this */
    if (wdbp->debug_level > 1)
        fprintf(stderr, "tid:%lx:pipe_buf_link_forward(%lx) for (%lx,%lx,%s:%d)\n",
               tid, (long) wdbp, (long) wdbp->pbp,
               (long) to_ep, to_ep->host, to_ep->port_id);
    sigrelse(SIGIO);
    while (!wdbp->go_away)
    {
        pipe_buf_take(wdbp->pbp, &len, &sep, NULL, 1); /* Wait for input */
        if ( !pthread_equal(to_ep->owdbp->own_thread_id, me))
        {
            sighold(SIGIO);
            fprintf(stderr, "tid:%lx:Screwed up; someone else has nicked it\n",
                     tid);
            pipe_buf_add(wdbp->pbp, 1, len, sep, NULL, 0); /* Put it back */
            return;
        }
        if (sep == NULL)
            break;    /* We've been told to exit */
/*
 * Everything coming in here is going out on the same socket, under the control
 * of the same SSL context. However, we use the original HTTP/1.1 sockets to
 * match up responses, so we need to track these as well.
 */
        wdbp->cur_link = cur_link; /* Everything fed here should be for this */
        if (len == 0) /* It is a script element, we need to build it up */
        {
            if (wdbp->debug_level > 3)
                fprintf(stderr, "tid:%lx:Incoming script element\n", tid);
            if (sep == NULL || sep->head == NULL || sep->foot == NULL)
            {
                scarper(__FILE__, __LINE__,
                        "pipe_buf message length mismatch",
                            &(wdbp->parser_con));
                break;
            }
            if (wdbp->debug_level > 3)
                fprintf(stderr, "tid:%lx:Script submission:\n%s\n%.*s\n%s\n=========\n",
                    tid, sep->head, sep->body_len, sep->body, sep->foot);
/*
 * Now we need to potentially set up the stream.
 */
            if ((http1_link = find_script_link(wdbp, sep)) == NULL)
            {
                fprintf(stderr, "tid:%lx:Logic Error no link for\n%s\n%.*s\n%s\n",
                     tid, sep->head, sep->body_len, sep->body, sep->foot);
                break;
            }
            wdbp->cur_link = cur_link; /* find_script_link() corrupts this */
            if (wdbp->debug_level > 3)
                fprintf(stderr, "tid:%lx:http1_link=%lx http1_link->h2sp=%lx\n",
                     tid,  (long) http1_link, (long) http1_link->h2sp);
/*
 * Each stream is only one-shot with respect to HTTP requests, so bump the
 * stream unconditionally.
 */
            if (http1_link->h2sp == NULL)
                http1_link->h2sp = new_http2_stream(to_ep, 0);
            else
            {
                if (http1_link->h2sp->in_flight.subm_req != NULL)
                {
                    if (http1_link->h2sp->in_flight.subm_req == sep)
                    {
                        fprintf(stderr, 
"(Client:%s) (tid=%lx) Submitting again when still in flight (sid=%d) - %s\n\%.*s\n====????====\n",
                        wdbp->parser_con.pg.rope_seq,
                        tid,
                        http1_link->h2sp->stream_id,
                        sep->head,
                        sep->body_len,
                        sep->body
                        );
                        continue;
                    }
                    fprintf(stderr, 
  "(Client:%s) (tid=%lx) (sid=%d) Still in flight - %s\n\%.*s\nwhen\n%.*s\nsent. Missing \\Q\\?\n", 
                        wdbp->parser_con.pg.rope_seq,
                        tid,
                        http1_link->h2sp->stream_id,
                        http1_link->h2sp->in_flight.subm_req->head,
                        http1_link->h2sp->in_flight.subm_req->body_len,
                        http1_link->h2sp->in_flight.subm_req->body,
                        sep->body_len, sep->body);
                    if (!sep->retry_cnt)
                        sep->retry_cnt++; /* Eligible for resubmission */
                    continue;
                }
                if (wdbp->debug_level > 3)
                    fprintf(stderr, "tid:%lx:Re-used stream id: %d state: %d\n",
                        tid, http1_link->h2sp->stream_id,
                        http1_link->h2sp->stream_state);
                if (http1_link->h2sp->stream_id != 0)
                    hremove(to_ep->h2cp->stream_hash, 
                              http1_link->h2sp->stream_id);
                http1_link->h2sp->stream_id =
                               to_ep->h2cp->next_new_strm_out;
                to_ep->h2cp->next_new_strm_out += 2;
            }
            http1_link->h2sp->stream_state = HTTP2_STREAM_OPEN; 
            http1_link->h2sp->http1_link = cur_link;
            insert(to_ep->h2cp->stream_hash,
                         http1_link->h2sp->stream_id,
                         http1_link->h2sp); 
            if (wdbp->debug_level)
                fprintf(stderr, 
                 "(Client:%s) (tid=%lx) (sid=%d) for - %s\n", 
                        wdbp->parser_con.pg.rope_seq,
                        tid,
                        http1_link->h2sp->stream_id,
                        sep->head);
            http1_link->h2sp->in_flight.subm_req = sep;
            sep->retry_cnt++;
/*
 * To send the request we need to:
 * -  Find the end of the HTTP 1.1 headers
 * -  Code up the head stream
 * -  Send a header frame (don't bother with continuations ...)
 * -  Send the rest as a data frame.
 * If the maximum frame size turns out to be a problem, then cross that bridge when
 * we come to it.
 */
            for (typ = HTTP2_HEADERS_TYPE,
                 top = bm_match(wdbp->ehp, sep->body, sep->body + sep->body_len),
                 xp = sep->body;
                       xp < top; )
            {
/*
 * This assumes the maximum frame size is less than 3*WORKSPACE (which we make
 * sure is the case).
 */
                len = code_head_stream(to_ep->h2cp->hhccp, &xp , top,
                        &wdbp->in_buf.buf[sizeof(struct http2_frame_header)],
                        &wdbp->in_buf.buf[to_ep->h2cp->set_in.max_frame_size]);
#ifdef GZIP_POST
                if (xp == top
                  && (top != sep->body + sep->body_len -4))
                {
                    if (*(top + 4) != 0x1f)
                    {          /* Not compressed yet */
                        xp1 = &wdbp->in_buf.buf[
                                sizeof(struct http2_frame_header) + len];
/*
 * Compression takes place in place, using a scratch buffer. xp1 is suitable.
 */
                        sep->body_len += apply_compression(wdbp, top + 4,
                             sep->body + sep->body_len - (top + 4), xp1);
                        xp1 = sep->body + sep->body_len;
/*
 * In case of resubmission, get it right ...
 */
                        adjust_content_length(wdbp, sep->body, &xp1);
                    }
                    xp1 = &wdbp->in_buf.buf[sizeof(struct http2_frame_header) +
                                     len];
                    xp1 = code_header( to_ep->h2cp->hhccp,
                          "content-encoding", 16, "gzip",4,
                          xp1,
                        &wdbp->in_buf.buf[to_ep->h2cp->set_in.max_frame_size]);
                    i = sprintf(len_buf, "%u",
                         (sep->body + sep->body_len - (top + 4)));
                    xp1 = code_header( to_ep->h2cp->hhccp,
                          "content-length", 14, len_buf, i,
                          xp1,
                        &wdbp->in_buf.buf[to_ep->h2cp->set_in.max_frame_size]);
                    len += (xp1 - (unsigned char *)
                      &wdbp->in_buf.buf[len+sizeof(struct http2_frame_header)]);
                }
#endif
                xp1 = &wdbp->in_buf.buf[sizeof(struct http2_frame_header)];
                if (wdbp->debug_level > 3)
                {
                    out_cnt = decode_head_stream(to_ep->h2cp->hhccp->debug,
                       &xp1,
                      &wdbp->in_buf.buf[sizeof(struct http2_frame_header)+len],
                       &wdbp->ret_msg.buf[0],
                       &wdbp->ret_msg.buf[WORKSPACE]);
                    fprintf(stderr, "tid:%lx:Decode of transmission:\n%.*s\n========\n",
                       tid, out_cnt, &wdbp->ret_msg.buf[0]);
                }
                fill_frame((struct http2_frame_header *) &wdbp->in_buf.buf[0],
                            len, typ, http1_link->h2sp->stream_id, 
                            (xp < top) ? 0
                             : (HTTP2_END_HEADERS |
                             ((top != sep->body + sep->body_len -4)? 0
                             :  HTTP2_END_STREAM)));
                if (wdbp->debug_level)
                    fprintf(stderr, 
    "(Client:%s) (tid:%lx) Frame Header ==>: type %d sid %d len %d flags %x\n",
                          wdbp->parser_con.pg.rope_seq, tid,
                           typ, http1_link->h2sp->stream_id, len,
                            (xp < top) ? 0
                             : (HTTP2_END_HEADERS |
                             ((top != sep->body + sep->body_len -4)? 0
                             :  HTTP2_END_STREAM)));
                if (wdbp->debug_level > 3)
                {
                    fprintf(stderr, "tid:%lx:HTTP2 Header Frame ---->\n", tid);
                    gen_handle(stderr, 
                          &wdbp->in_buf.buf[0],
                          &wdbp->in_buf.buf[0]+
                           len + sizeof(struct http2_frame_header), 1);
                    fputs("=======================>\n", stderr);
                }
/*
 * N.B. The writes are with respect to the passed-in link
 */
                pthread_mutex_lock(&to_ep->rights);
                if (wdbp->go_away || wdbp->cur_link->ssl == NULL)
                {
                    pthread_mutex_unlock(&to_ep->rights);
                    break;
                }
                sep->timestamp = timestamp();
                if ((len = smart_write(cur_link->connect_fd, 
                          &wdbp->in_buf.buf[0],
                           len + sizeof(struct http2_frame_header),
                                1, wdbp)) < 1)
                {
                    fprintf(stderr, "%lx:%s:%d:%s\n",
                              tid, __FILE__, __LINE__, "Failed header send");
                    pthread_mutex_unlock(&to_ep->rights);
                    break;
                }
                pthread_mutex_unlock(&to_ep->rights);
                if (xp < top)
                    typ = HTTP2_CONTINUATION_TYPE;
            }
            xp += 4;
#ifndef TUNDRIVE
            if (wdbp->verbosity > 4 && wdbp->proxy_port == 0)
                event_record_r("T", (struct event_con *) NULL,
                   &(wdbp->parser_con.pg));
                                         /* Note the message */
#endif
            if (xp < sep->body + sep->body_len)
            {
            char head[sizeof(struct http2_frame_header)];

                typ = HTTP2_DATA_TYPE;
                len = (sep->body + sep->body_len) - xp;
                bite = (len > to_ep->h2cp->set_in.max_frame_size)
                       ? to_ep->h2cp->set_in.max_frame_size
                       : len;
                while (len > 0)
                {

                    fill_frame((struct http2_frame_header *)
                               &head[0], bite,
                                typ, http1_link->h2sp->stream_id, 
                               ((bite != len) ? 0 : HTTP2_END_STREAM)); 
/*
 * Have taken no account whatsoever of Flow Control.
 */
                    if (wdbp->debug_level)
                        fprintf(stderr, 
    "(Client:%s) (tid:%lx) Frame Header ==>: type %d sid %d len %d flags %x\n",
                          wdbp->parser_con.pg.rope_seq, tid,
                           typ, http1_link->h2sp->stream_id, bite,
                               ((bite != len) ? 0 : HTTP2_END_STREAM)); 
                    if (wdbp->debug_level > 3)
                    {
                        fprintf(stderr,"tid:%lx:HTTP2 Data Frame ---->\n", tid);
                        gen_handle(stderr, &head[0],
                              &head[sizeof(struct http2_frame_header)], 1);
                        gen_handle(stderr, xp, xp + bite, 1);
                        fputs("=======================>\n", stderr);
                    }
                    pthread_mutex_lock(&to_ep->rights);
                    if (wdbp->go_away || wdbp->cur_link->ssl == NULL)
                    {
                        pthread_mutex_unlock(&to_ep->rights);
                        break;
                    }
                    if (smart_write(cur_link->connect_fd, 
                                   &head[0],
                                   sizeof(struct http2_frame_header), 1,
                                       wdbp) < 0
                     || smart_write(cur_link->connect_fd, 
                                   xp,
                                   bite, 1,
                                       wdbp) < 0)
                    {
                        fprintf(stderr, "tid:%lx:%s:%d:%s\n",
                           tid,__FILE__, __LINE__, "Failed data send");
                        pthread_mutex_unlock(&to_ep->rights);
                        break;
                    }
                    pthread_mutex_unlock(&to_ep->rights);
                    len -= bite;
                    bite = (len > to_ep->h2cp->set_in.max_frame_size)
                       ? to_ep->h2cp->set_in.max_frame_size
                       : len;
                }
/*
 *              fill_frame((struct http2_frame_header *)
 *                             &head[0], 0,
 *                              typ, http1_link->h2sp->stream_id,
 *                               HTTP2_END_STREAM);
 *              pthread_mutex_lock(&to_ep->rights);
 *              if (smart_write(cur_link->connect_fd, 
 *                                 &head[0],
 *                                 sizeof(struct http2_frame_header), 1,
 *                                     wdbp) < 0)
 *                  fprintf(stderr, "tid:%lx:%s:%d:%s\n",
 *                         tid,__FILE__, __LINE__, "Failed data send");
 *              pthread_mutex_unlock(&to_ep->rights);
 */
            }
            http1_link->h2sp->stream_state = HTTP2_STREAM_LOCAL_HALF_CLOSED;
        }
        else /* It is raw data, an error or flow control etc. */
        {
            if (wdbp->debug_level > 3)
                fprintf(stderr, "tid:%lx:Incoming HTTP2 Internal element\n",
                          tid);
            pthread_mutex_lock(&to_ep->rights);
            if (wdbp->go_away || wdbp->cur_link->ssl == NULL)
            {
                pthread_mutex_unlock(&to_ep->rights);
                break;
            }
            hfhp = (struct http2_frame_header *) sep;
            if (wdbp->debug_level)
                fprintf(stderr, 
   "(Client:%s) (tid:%lx) Frame Header ==>: type %d sid %d len %d flags %x\n",
                 wdbp->parser_con.pg.rope_seq, tid,
                          hfhp->typ,
                   ((hfhp->sid[0] & 0x7f) << 24)
                       + (hfhp->sid[1] << 16)
                       + (hfhp->sid[2] << 8)
                       + hfhp->sid[3],
                  (hfhp->len[0] << 16) | (hfhp->len[1] << 8) | hfhp->len[2],
                               hfhp->flags);
            if ((len = smart_write(cur_link->connect_fd,
                            (unsigned char *) sep,
                            len, 1, wdbp)) < 1)
            {
                fprintf(stderr, "tid:%lx:%s:%d:%s\n",
                       tid, __FILE__, __LINE__, "Failed raw send");
                pthread_mutex_unlock(&to_ep->rights);
                free(sep);         /* Always allocated space */
                break;
            }
            pthread_mutex_unlock(&to_ep->rights);
            free(sep);         /* Always allocated space */
        }
        if (len < 0)
            break;
    }
    if (wdbp->debug_level > 1)
        fprintf(stderr,
          "tid:%lx:pipe_buf_link_forward(%lx) exiting go_away=%d sep=%lx ...\n",
                         tid, (long) wdbp,
                         to_ep->owdbp->go_away, (unsigned long) sep);
    to_ep->iwdbp->go_away = 1;
    to_ep->owdbp->go_away = 1;
    sighold(SIGIO);
    if ( !pthread_equal(to_ep->iwdbp->own_thread_id, zeroth))
        pthread_kill(to_ep->iwdbp->own_thread_id, SIGIO);
    notify_master(to_ep->iwdbp->root_wdbp->pbp, wdbp);
    return;
}
