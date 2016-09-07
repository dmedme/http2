/*
 * HTTP 2 implementation
 */
#include "webdrive.h"
#include "scripttree.h"
#include "hpack.h"
#include "http2.h"
#include <assert.h>
#ifndef MINGW32
#include <unistd.h>
#include <sys/syscall.h>
#endif
static char * http2_con_pref = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
/****************************************************************************
 * We need to interpose the HTTP/2 multiplexer above smart_read() and
 * smart_write(). Or below?
 * -  Benefits of 'below':
 *    -   Very simple interface
 *    -   Limited dependence on the rest of the program structure
 *    -   Hopefully we can deal with the switch between synchronous and
 *        asynchronous operation in the pipe_buf routines, with minimal
 *        impact on the overall structure.
 * -  Drawbacks
 *    -   I instinctively feel that lots of longjmp()ing to perhaps handle
 *        returns when data is available would be problematic, and not
 *        portable. What happens to the notion of the thread that owns
 *        the WEBDRIVE_BASE structure? If we longjmp() using a different
 *        thread, how will we be able to direct signals to it? 
 *    -   How many threads would we need? It makes sense to have a single
 *        thread reading the script, but in proxy (script capture) mode
 *        we can have a thread for each incoming. So we need to handle
 *        the possibility of multiple concurrent feeders anyway. If we block
 *        on I/O as we do at present, we would need two more threads than we
 *        do now; a reader and a writer. These have to be co-ordinated.
 *    -   At the moment, we have no need to worry about thread-safe-ness
 *        when accessing the WEBDRIVE_BASE structure. Now we will.
 *    -   We may process the message multiple times; it would be better to
 *        have the HTTP/2 headers added when the script is read/proxy fields
 *        the initial request.
 * -  Benefits of 'above'
 *    -   By making the I/O asynchronous, and using select(), we could stick
 *        with one thread per script. We would also finally refactor
 *        the hideous do_send_receive().
 * -  Drawbacks
 *    -   select() or poll() aren't portable. The Windows code would diverge
 *        further than at present.
 *    -   You cannot usefully connect OpenSSL to asynchronous sockets. You
 *        must instead use memory buffers to communicate with OpenSSL, and
 *        handle the sockets yourself. So big changes to this code as well.
 *    -   Complete change to the main control; we put a select() at the top,
 *        as was there originally, and then run separate routines to handle
 *        send() and recv() as data is available. And we still need multiple
 *        additional threads to cater for the proxy doing script capture.
 *
 * How about a radical departure. Use the 'script_tree' logic to handle script
 * execution.
 * -  Read in the script an event's worth at a time.
 * -  Pass the chain over to a processing engine.
 *    -   The chained elements provide hooks to hang the processing off.
 *    -   Conversion to HTTP/2 format would work on the tree.
 *    -   do_send_receive() processing at the moment has a number of
 *        'back to square 1' options, for example when dealing with NTLM.
 *        It could do this if its job was to follow the tree; the result of
 *        processing a 'send' node might be that a 'recv' node appears that
 *        in effect says 'go back and redo'. 
 *    -   We have to do this if we want to process multiple requests in
 *        parallel anyway, multiplexor or no multiplexor. Then we end up
 *        with a thread for every different socket ...
 * But there is a problem with this. Reading of include files, macro processing
 * loops and so forth are implemented in the routine that reads the scripts
 * at the moment, webread(), and this seeks backwards in the file for goto
 * processing. This could be a right mess.
 *
 * So, we are back with a re-write of do_send_receive(). It has to be the
 * routine that adds requests to the queues, and looks for requests that have
 * been answered. Instead of just sending a message, and waiting for a response,
 * it must:
 * -   Schedule the message for transmission.
 * -   Pick up any responses for any of the messages, and deal with them.
 * -   Recognise messages that block it.
 * -   Have a 'drain' mode for when we get to a timing point.
 *
 * So this looks to be the correct approach.
 *    -   The multiplexor has its own pair of threads
 *    -   We proceed tree-segment by tree segment.
 *    -   We write out the script tree at each timing point to give a
 *        re-usable trace.
 */
static void close_old_idle(h2sp)
struct http2_stream * h2sp;
{
    if (h2sp->stream_state == HTTP2_STREAM_IDLE)
        h2sp->stream_state == HTTP2_STREAM_HAS_CLOSED;
    return;
}
void clear_in_flight(h2sp)
struct http2_stream * h2sp;
{
struct script_element * sep;

#ifdef DEBUG
    fprintf(stderr, "clear_in_flight(%d)\n", h2sp->stream_id);
#endif
    hremove(h2sp->own_h2cp->stream_hash, (char *) h2sp->stream_id);
    h2sp->stream_id = 0;
    if (h2sp->in_flight.subm_req != NULL
      && h2sp->in_flight.subm_req->child_track == NULL)
        h2sp->in_flight.subm_req->timestamp = 0.0;
    memset((unsigned char *) &h2sp->in_flight, 0,
                 sizeof(&h2sp->in_flight));
    return;
}
struct http2_stream * new_http2_stream(ep, stream_id)
END_POINT * ep;
int stream_id;
{
struct http2_stream * h2sp;

    if ((h2sp = (struct http2_stream *) malloc(sizeof(struct http2_stream)))
                          == NULL)
        return NULL;
    h2sp->http1_link = NULL;       /* Won't be known for incoming */
    h2sp->own_h2cp = ep->h2cp;
    h2sp->stream_typ = HTTP2_NORMAL_STREAM;
    if (stream_id == 0)
    {
        h2sp->stream_id = ep->h2cp->next_new_strm_out;
        ep->h2cp->next_new_strm_out += 2;
    }
    else
        h2sp->stream_id = stream_id;
    h2sp->win_size_in = ep->h2cp->set_in.initial_win_size;
    h2sp->win_size_out = ep->h2cp->set_out.initial_win_size;
    h2sp->stream_state = HTTP2_STREAM_IDLE;
    h2sp->weight = 16;
    h2sp->depend_stream = NULL;    /* Dependent stream, if there is one */
    h2sp->priority_chain = NULL;   /* Links all the streams together */
    h2sp->peer = NULL;   /* Links all the streams together */
    memset((unsigned char *) &h2sp->in_flight,0,sizeof(h2sp->in_flight));
    return h2sp;
}
struct http2_con * new_http2_con(ep, io_link)
END_POINT * ep;
LINK * io_link;
{
struct http2_con * h2cp;

    if ((h2cp = (struct http2_con *) malloc(sizeof(struct http2_con))) == NULL)
        return NULL;

    h2cp->ep = ep;
    h2cp->io_link = io_link;
    h2cp->http_level = 1;      /* 0 - HTTP/1 ; 1 - HTTP/2                 */
    h2cp->settings_timeout = 0; /* Timeout for settings acknowledgement - none */
    h2cp->min_new_strm_in = 1;
    h2cp->next_new_strm_out = 1;
    h2cp->win_size_in = 65535;
    h2cp->win_size_out = 65535;
    h2cp->hhccp = ini_http2_head_coding_context(4096);
    h2cp->hhdcp = ini_http2_head_decoding_context(4096);
/*
 * Settings
 */
    h2cp->set_out.header_table_size = 4096;
    h2cp->set_out.enable_push = 1;
    h2cp->set_out.initial_win_size = h2cp->win_size_out;
    h2cp->set_out.max_conc_strms = 16777215;
    h2cp->set_out.max_frame_size = 16384;
    h2cp->set_out.max_header_list_size = 16777215;
    h2cp->set_in = h2cp->set_out;  /* Will get over-ridden */
    h2cp->min_strm_retriable = 1;  /* Lowest retriable stream */
    h2cp->stream_hash = hash(128,long_hh, icomp);
                                   /* Hash of multiplexed streams */
    h2cp->priority_anchor = NULL;  /* root of priority-ordered chain */
    return h2cp;
}
void refresh_http2_con(h2cp)
struct http2_con * h2cp;
{
    h2cp->min_new_strm_in = 1;
    h2cp->next_new_strm_out = 1;
    h2cp->win_size_in = 65535;
    h2cp->win_size_out = 65535;
    zap_http2_head_coding_context(h2cp->hhccp);
    zap_http2_head_decoding_context(h2cp->hhdcp);
    h2cp->hhccp = ini_http2_head_coding_context(4096);
    h2cp->hhdcp = ini_http2_head_decoding_context(4096);
/*
 * Settings
 */
    h2cp->set_out.header_table_size = 4096;
    h2cp->set_out.enable_push = 1;
    h2cp->set_out.initial_win_size = h2cp->win_size_out;
    h2cp->set_out.max_conc_strms = 16777215;
    h2cp->set_out.max_frame_size = 16777215;
    h2cp->set_out.max_header_list_size = 16777215;
    h2cp->set_in = h2cp->set_out;  /* Will get over-ridden */
    h2cp->min_strm_retriable = 1;  /* Lowest retriable stream */
/*
 * Leave the stream_hash and the priority_anchor (and chain) alone.
 */
    return;
}
void zap_http2_con(h2cp)
struct http2_con * h2cp;
{
/*
 * There's a problem if the streams are indexed multiple times; there will
 * be multiple free()s of the same stream.
 */ 
    iterate(h2cp->stream_hash, NULL, free);
    cleanup(h2cp->stream_hash);
    free(h2cp);
    return;
}
static void q_error(ep, sid, err)
END_POINT * ep;
int sid;
int err;
{
unsigned char * bp = (unsigned char *) malloc(13);

    fill_frame((struct http2_frame_header *) bp, 4, HTTP2_RST_STREAM_TYPE, sid,
                          0);
    bp[9]  = ((err & 0xff000000) >> 24);
    bp[10]  = ((err & 0xff0000) >> 16);
    bp[11]  = ((err & 0xff00) >> 8);
    bp[12]  = ((err & 0xff));
#ifdef DEBUG
    fprintf(stderr, "Returning error: sid %d error %d\n", sid, err);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 13, bp, NULL, 0);
    return;
}
void q_end_stream(ep, sid)
END_POINT * ep;
int sid;
{
unsigned char * bp = (unsigned char *) malloc(9);

    fill_frame((struct http2_frame_header *) bp, 4, HTTP2_DATA_TYPE, sid,
                          HTTP2_END_STREAM);
#ifdef DEBUG
    fprintf(stderr, "Returning End Stream: sid %d\n", sid);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 9, bp, NULL, 0);
    return;
}
static void q_window_update(ep, sid, len)
END_POINT * ep;
int sid;
int len;
{
unsigned char * bp = (unsigned char *) malloc(13);

    fill_frame((struct http2_frame_header *) bp, 4, HTTP2_WINDOW_UPDATE_TYPE, sid, 0);
    bp[9]  = ((len & 0xff000000) >> 24);
    bp[10]  = ((len & 0xff0000) >> 16);
    bp[11]  = ((len & 0xff00) >> 8);
    bp[12]  = ((len & 0xff));
#ifdef DEBUG
    fprintf(stderr, "Returning window update: sid %d len %d\n", sid, len);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 13, bp, NULL, 0);
    return;
}
static void q_http2_goaway(ep, err)
END_POINT * ep;
int err;
{
unsigned char * bp = (unsigned char *) malloc(17);
int last_stream_id = ep->h2cp->next_new_strm_out - 2;

    fill_frame((struct http2_frame_header *) bp, 8, HTTP2_GOAWAY_TYPE, 0, 0);
    bp[9]  = ((last_stream_id & 0xff000000) >> 24);
    bp[10]  = ((last_stream_id & 0xff0000) >> 16);
    bp[11]  = ((last_stream_id & 0xff00) >> 8);
    bp[12]  = ((last_stream_id & 0xff));
    bp[13]  = ((err & 0xff000000) >> 24);
    bp[14]  = ((err & 0xff0000) >> 16);
    bp[15]  = ((err & 0xff00) >> 8);
    bp[16]  = ((err & 0xff));
#ifdef DEBUG
    fprintf(stderr, "Returning Go Away: sid %d error %d\n", last_stream_id, err);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 17, bp, NULL, 0);
    return;
}
void q_http2_settings(ep, flags)
END_POINT * ep;
int flags;
{
unsigned char * bp = (unsigned char *) malloc(9);
int last_stream_id = ep->h2cp->next_new_strm_out - 2;

    fill_frame((struct http2_frame_header *) bp, 0, HTTP2_SETTINGS_TYPE, 0, flags);
#ifdef DEBUG
    fprintf(stderr, "Sending Settings: flags %d\n", flags);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 9, bp, NULL, 0);
    return;
}
static void q_http2_ping(ep)
END_POINT * ep;
{
unsigned char * bp = (unsigned char *) malloc(17);

    fill_frame((struct http2_frame_header *) bp, 8, HTTP2_PING_TYPE, 0, HTTP2_ACK);
    memset(bp+9,0,8);
#ifdef DEBUG
    fputs("Returning PING type with ACK\n", stderr);
#endif
    pipe_buf_add(ep->owdbp->pbp, 1, 17, bp, NULL, 0);
    return;
}
/*
 * Thread to read things from an HTTP connection
 * -  Runs in its own thread, not the thread that writes, and not the thread
 *    that owns the wdbp, so care is needed there. In particular, we are going
 *    to own a lot of SSL context. Given that activity can trigger SSL hand
 *    shakes, can we have two threads running concurrently on the same
 *    connection? We use a mutex to co-ordinate.
 * -  We draw on the same pool of idle threads as does be_a_thread()
 * -  Reads frames, header plus payload.
 * -  Needs to deal with things at the low level protocol level, such as
 *    changes in parameters, PUSH frames, ping ack's, flow control and so forth.
 *    This involves queueing messages to be sent.
 * -  For things that are responses to what has gone before, we need to:
 *    -   Find the request corresponding to this response in the chain of
 *        requests
 *    -   Hook up the response
 *    -   Trigger
 * -  It shouldn't be invoked before the End Point points to a structure
 *    initialised by the write logic.
 */
static int deal_with_padding(bp, len, ep, wdbp)
unsigned char ** bp;
int len; 
END_POINT * ep;
WEBDRIVE_BASE * wdbp;
{
int pad_len = **bp;

    len -= pad_len;
    if (len <= 0)
    {
        q_error(ep, 0, HTTP2_PROTOCOL_ERROR);
        return -1;
    }
    (*bp)++;
    return pad_len;
}
/*
 * Unlink a stream from the priority tree.
 */
static void unlink_stream(h2sp)
struct http2_stream * h2sp;
{
struct http2_stream * xh2sp;

    if (h2sp->depend_stream != NULL)
    {
        if ((xh2sp = h2sp->depend_stream->priority_chain)
                            == h2sp)
            h2sp->depend_stream->priority_chain = h2sp->peer;
        else
            for (; xh2sp->peer != NULL; xh2sp = xh2sp->peer)
            { /* Can crash if the chain is corrupt ... */
                if (xh2sp->peer == h2sp)
                {
                    xh2sp->peer = h2sp->peer;
                    break;
                }
            }
    }
    return;
}
/*
 * Link a stream to another, honouring the exclusive flag
 */
static void link_stream(h2sp_targ, h2sp_float, xflag)
struct http2_stream * h2sp_targ;
struct http2_stream * h2sp_float;
int xflag;
{
struct http2_stream * xh2sp;
struct http2_stream * xh2sp1;

    h2sp_float->depend_stream = h2sp_targ;
    if (h2sp_targ == NULL)
        return;
    if (xflag && ((xh2sp = h2sp_targ->priority_chain) != NULL))
    {   /* Exclusive, the existing ones now depend on the new one */ 
        for (; xh2sp != NULL; xh2sp = xh2sp->peer)
        {
             xh2sp->depend_stream = h2sp_float;
             xh2sp1 = xh2sp;
        } 
/*
 * Hook up the peer chain
 */
        xh2sp1->peer = h2sp_float->priority_chain;
        h2sp_float->priority_chain = h2sp_targ->priority_chain;
        h2sp_targ->priority_chain = h2sp_float;
    }
    else
    {
        h2sp_float->peer = h2sp_targ->priority_chain;
        h2sp_targ->priority_chain = h2sp_float;
    }
    return;
}
static int is_child_stream(ph2sp, ch2sp)
struct http2_stream * ph2sp;
struct http2_stream * ch2sp;
{
struct http2_stream * xh2sp;

    for (xh2sp = ph2sp->priority_chain;
             xh2sp != NULL;
                  xh2sp = xh2sp->peer)
    {
        if (xh2sp == ch2sp
        || ((xh2sp->priority_chain != NULL)
           && is_child_stream(xh2sp->priority_chain, ch2sp)))
            return 1;
    }
    return 0;
}
static void output_http2_error(err_code, wdbp)
int err_code;
WEBDRIVE_BASE * wdbp;
{
char * errdesc;

    switch(err_code)
    {
    case HTTP2_NO_ERROR:
        errdesc = "The associated condition is not as a result of an error.";
        break;
    case HTTP2_PROTOCOL_ERROR:
        errdesc= "The endpoint detected an unspecified protocol error.";
        break;
    case HTTP2_INTERNAL_ERROR:
        errdesc= "The endpoint encountered an unexpected internal error.";
        break;
    case HTTP2_FLOW_CONTROL_ERROR:
        errdesc= "The endpoint detected that its peer violated the flow control protocol.";
        break;
    case HTTP2_SETTINGS_TIMEOUT:
        errdesc = "The endpoint sent a SETTINGS frame, but did not receive a response in a timely manner.";
        break;
    case HTTP2_STREAM_CLOSED:
        errdesc = "The endpoint received a frame after a stream was half closed.";
        break;
    case HTTP2_FRAME_SIZE_ERROR:
        errdesc = "The endpoint received a frame with an invalid size.";
        break;
    case HTTP2_REFUSED_STREAM:
        errdesc = "The endpoint refuses the stream prior to performing any application processing";
        break;
    case HTTP2_CANCEL:
        errdesc = "Stream no longer needed";
        break;
    case HTTP2_COMPRESSION_ERROR:
        errdesc = "The endpoint is unable to maintain the header compression context for the connection.";
        break;
    case HTTP2_CONNECT_ERROR:
        errdesc = "The connection established in response to a CONNECT request was reset or abnormally closed.";
        break;
    case HTTP2_ENHANCE_YOUR_CALM:
        errdesc = "The endpoint detected that its peer is exhibiting a behavior that might be generating excessive load.";
        break;
    case HTTP2_INADEQUATE_SECURITY:
        errdesc = "The underlying transport has properties that do not meet minimum security requirements.";
        break;
    default:
        errdesc = "Unknown and unexpected error code.";
        break;
    }
    (void) fprintf(stderr,"(Client:%s) HTTP2 Error (%x,%s)\n",
                   wdbp->parser_con.pg.rope_seq, err_code, errdesc);
    return;
}
/*
 * Read routine running in its own thread
 */
void link_pipe_buf_forward(wdbp)
WEBDRIVE_BASE * wdbp;
{
struct http2_frame_header hfh;
int sid;
int xflag;
int dep_sid;
unsigned int err_code;
int len;
int pad_len;
int buf_len = WORKSPACE;
HIPT * hip;
struct http2_stream * h2sp;
struct http2_stream * xh2sp;
unsigned char * b = &wdbp->ret_msg.buf[0];
unsigned char * x;
unsigned char * top;
int poss_id;
int poss_val;
char * errdesc;
LINK * cur_link;
END_POINT * ep;
WEBDRIVE_BASE * parent_wdbp = wdbp->root_wdbp;
long int tid = GetCurrentThreadId();
/*
 * Loop - until told to go away, or something bad has happened to the
 * connection, read and despatch messages.
 *
 * A difficulty may be that smart_read() and smart_write() reference cur_link.
 * So we are relying on openssl to handle concurrent requests on the same
 * underlying socket, and concurrent access to wdbp not messing up cur_link.
 * Interesting to see if this works, but I'll sort that out when the routines
 * have been fleshed out if it doesn't.
 */
    sigrelse(SIGIO);
    ep = wdbp->cur_link->to_ep;    /* On entry, this points to the parent */ 
    cur_link = ep->h2cp->io_link;  /* I/O will be with respect to this */
    if (wdbp->debug_level > 3)
       fprintf(stderr, "tid:%lx:link_pipe_buf_forward(%lx) for (%lx,%lx,%s:%d) connect_fd=%d\n",
               tid, (long) wdbp, (long) wdbp->pbp,
               (long) ep, ep->host, ep->port_id,
                cur_link->connect_fd);
/*
 * Loop - read and despatch messages on the link
 */
    while (!wdbp->go_away && cur_link->connect_fd >= 0)
    {
        if (!wait_for_incoming(cur_link->connect_fd, &ep->rights))
            break; /* Interrupted */
        else
        if (wdbp->go_away)
        {
            pthread_mutex_unlock(&ep->rights);
            break;
        }
        wdbp->cur_link->ssl = cur_link->ssl; /* Needed by smart_read() */
        if (cur_link->ssl == NULL)
        {
            fputs("WTF!?\n", stderr);
            pthread_mutex_unlock(&ep->rights);
            break;
        }
        wdbp->cur_link = cur_link;
        if (smart_read(cur_link->connect_fd, (unsigned char *) &hfh,
               sizeof(hfh), 1, wdbp) == sizeof(hfh))
        {
            len = (hfh.len[0] << 16) | (hfh.len[1] << 8) | hfh.len[2];
            sid  = ((hfh.sid[0] & 0x7f) << 24)
                       + (hfh.sid[1] << 16)
                       + (hfh.sid[2] << 8)
                       + hfh.sid[3];
            if (wdbp->debug_level)
                fprintf(stderr, 
    "(Client:%s) (tid:%lx) Frame Header <==: type %d sid %d len %d flags %x\n",
                          wdbp->parser_con.pg.rope_seq, tid,
                           hfh.typ, sid, len, hfh.flags);
            pad_len = 0;
/*
 * Allocate more space if necessary
 */
            if (len > buf_len)
            {
                buf_len = len + len;
                wdbp->overflow_receive = (unsigned char *) realloc(
                                      wdbp->overflow_receive, buf_len);
                b = wdbp->overflow_receive;
            }
            wdbp->cur_link = cur_link;
            if (len > 0
             && len != smart_read(cur_link->connect_fd, b, len, 1, wdbp))
            {
                pthread_mutex_unlock(&ep->rights);
                break;   /* Something wrong, break out */
            }
            pthread_mutex_unlock(&ep->rights);
            switch(hfh.typ)
            {
            case HTTP2_DATA_TYPE:
/*
 * A data frame is:
 * - 1 byte pad length iff PADDED flag set
 * - payload
 * - optional pad length of zero bytes
 * - Look to see what stream it is on
 * It is invalid if:
 * - The session ID is zero or the session does not exist
 * - The session state isn't open or closed local
 */
                if (sid == 0
                 || ((hip = lookup(ep->h2cp->stream_hash, (char *) sid)) == NULL))
                {
                    q_error(ep, 0, HTTP2_PROTOCOL_ERROR);
                    goto leave;
                }
                h2sp = (struct http2_stream *)(hip->body);
/*
 * Validate the stream status
 */
                switch (h2sp->stream_state)
                {
                case HTTP2_STREAM_OPEN: 
                case HTTP2_STREAM_REMOTE_RESERVED:
                case HTTP2_STREAM_LOCAL_HALF_CLOSED: 
/*
 * Discard any padding
 */
                    if (hfh.flags & HTTP2_PADDED)
                    {
                        if (( pad_len = deal_with_padding(&b, len, ep, wdbp)) <= 0)
                            goto leave;
                        len -= (pad_len + 1);
                    }
                    break;
                case HTTP2_STREAM_LOCAL_RESERVED:
                case HTTP2_STREAM_IDLE:
                case HTTP2_STREAM_REMOTED_HALF_CLOSED:
                case HTTP2_STREAM_HAS_CLOSED:
                default:
                    q_error(ep, sid, HTTP2_STREAM_CLOSED);
                    continue;
                }
/*
 * b points to the start of the message, whose length is len.
 * We need to:
 * - Append the rest to the next received message we are assembling
 *   for this stream. This will be the in_flight. This is going to be the
 *   HTTP2 response element_tracker.
 */
                if (wdbp->debug_level > 3)
                    fprintf(stderr, "tid:%lx:Processing data len=%d\n",
                                     tid, len);
/*
 * We have jumped the gun if we know the length is zero. But we
 * need this if the data ends in a zero length record.
 */
                if (h2sp->in_flight.subm_req == NULL)
                {
                    fprintf(stderr, "tid:%lx:Stray HTTP2_END_STREAM? (sid=%d, flags=%d, length=%d\n", tid, sid, hfh.flags, len);
                }
                else
                    http2_data_append(h2sp, b, len,
                                         hfh.flags & HTTP2_END_STREAM);
#ifdef DEBUG
                fprintf(stderr, "tid:%lx:HTTP2 Data Payload <----\n", tid);
                gen_handle(stderr, b, b + len, 1);
                fputs("<=======================\n", stderr);
#endif
/*
 * - If it is complete
 *   -  If it is a response, hook it up to the original request, and signal
 *      that it is to be processed
 *   -  If it is a request (we are a server) then it needs to be processed by
 *      webserv stuff, or the tunnel.
 *
 * So, how can we tell if it is complete?
 *
 * In the final version of the specification, each stream is one shot. It
 * should have the END_OF_STREAM flag set.
 *
 * Additionally, we have to queue return flow control messages for:
 * - The stream
 * - The connection as a whole. 
 */ 
                if (pad_len > 0)
                    len += pad_len + 1;
                if (len > 0)
                    q_window_update(ep, 0, len);
                if (hfh.flags & HTTP2_END_STREAM)
                {
                    if (h2sp->stream_state == HTTP2_STREAM_OPEN)
                        q_end_stream(ep, sid);
                    h2sp->stream_state = HTTP2_STREAM_HAS_CLOSED;
                }
                else
                {
                    if (len > 0)
                        q_window_update(ep, sid, len);
                }
                break;
            case HTTP2_PUSH_PROMISE_TYPE:
/*
 * A PUSH_PROMISE frame contains:
 * - An optional 1 byte pad length (iff pad flag is set)
 * - A 1 bit reserved flag
 * - A promised stream ID, 31 bits
 * - An optional header block fragment
 * - optional padding
 * These need to be matched up with requests, that may or may not yet
 * exist. The question is, how?
 *
 * We need to match the header in the PUSH_PROMISE with an incoming
 * request.
 *
 * Treat it just like anything else to begin with; extra processing at the
 * end.
 */
            case HTTP2_CONTINUATION_TYPE:
/*
 * A CONTINUATION frame contains:
 * - Header information
 * You can have as many as you like; the sequence is terminated by a
 * CONTINUATION frame with the END_HEADERS flag set.
 */
            case HTTP2_HEADERS_TYPE:
/*
 * A headers frame is:
 * - 1 byte pad length iff PADDED flag set
 * - 1 bit stream dependency exclusive flag iff PRIORITY flag set
 * - 31 bit dependent Stream ID iff PRIORITY flag set
 * - 1 byte priority iff PRIORITY flag set
 * - compressed name/value pair data
 * - optional pad length of zero bytes
 * We need to:
 * - Look to see what stream it is on
 * - Discard any padding
 * - Accumulate it for that stream
 * - If it is finished:
 *   -  Pass it to the HPACK decoder
 *   -  If there isn't going to be any data for the request or response
 *      -  If it is a response, hook it up to the original request, and signal
 *         that it is to be processed
 *      -  If it is a request (we are a server) then it needs to be processed by
 *         webserv stuff, or the tunnel.
 */ 
                if (wdbp->debug_level > 4)
                    fprintf(stderr, "tid:%lx:Processing a header\n", tid);
                if (sid == 0)
                {
                    q_error(ep, 0, HTTP2_PROTOCOL_ERROR);
                    goto leave;
                }
/*
 * Look for the stream.
 * - If it isn't found:
 *   - Error if the stream ID is less than the legal minimum
 *   - We create a new one
 *   - We close any idle streams with lower stream ID's
 *   - We update the legal minimum
 */
                if ((hip = lookup(ep->h2cp->stream_hash, (char *) sid)) == NULL)
                {
                    fprintf(stderr, "tid:%lx:Failed to find hashed sid=%d!?\n",
                             tid, sid);
                    if (sid < ep->h2cp->min_new_strm_in
                      || hfh.typ == HTTP2_CONTINUATION_TYPE)
                    {
                        q_error(ep, 0, HTTP2_PROTOCOL_ERROR);
                        goto leave;
                    }
                    ep->h2cp->min_new_strm_in = sid + 1;
                    h2sp = new_http2_stream(ep, sid);
                    if (hfh.typ == HTTP2_PUSH_PROMISE_TYPE)
                    {
                        h2sp->stream_typ = HTTP2_PUSH_STREAM;
                        h2sp->stream_state = HTTP2_STREAM_LOCAL_HALF_CLOSED; 
                    }
                    else
                        h2sp->stream_state = HTTP2_STREAM_OPEN; 
                    iterate(ep->h2cp->stream_hash, NULL, close_old_idle); 
                    insert(ep->h2cp->stream_hash, sid, h2sp); 
                    h2sp->http1_link = h2sp->own_h2cp->io_link; /* Fallback */
                }
                else
                    h2sp = (struct http2_stream *)(hip->body);
                if (hfh.flags & HTTP2_PADDED)
                {
                    if (( pad_len = deal_with_padding(&b, len, ep, wdbp)) <= 0)
                        goto leave;
                    len -= (pad_len + 1);
                }
                if (len > 0)
                    http2_header_append(h2sp,b,len,
                         (hfh.flags & (HTTP2_END_HEADERS | HTTP2_END_STREAM)));
#ifdef DEBUG
                fprintf(stderr, "tid:%lx:HTTP2 Header Payload <----\n", tid);
                gen_handle(stderr, b, b + len, 1);
                fputs("<=======================\n", stderr);
#endif
                break;
            case HTTP2_PRIORITY_TYPE:
/*
 * A priority frame is:
 * - 1 byte pad length
 * - 1 bit stream dependency exclusive
 * - 31 bit dependent Stream ID
 ************************************************************************
 * Stream priorities are changed using the PRIORITY frame.  Setting a
 * dependency causes a stream to become dependent on the identified
 * parent stream.
 *
 * Dependent streams move with their parent stream if the parent is
 * reprioritized.  Setting a dependency with the exclusive flag for a
 * reprioritized stream moves all the dependencies of the new parent
 * stream to become dependent on the reprioritized stream.
 *
 * If a stream is made dependent on one of its own dependencies, the
 * formerly dependent stream is first moved to be dependent on the
 * reprioritized stream's previous parent.  The moved dependency retains
 * its weight.
 *
 * For example, consider an original dependency tree where B and C
 * depend on A, D and E depend on C, and F depends on D.  If A is made
 * dependent on D, then D takes the place of A.  All other dependency
 * relationships stay the same, except for F, which becomes dependent on
 * A if the reprioritization is exclusive.
 *
 *     ?                ?                ?                 ?
 *     |               / \               |                 |
 *     A              D   A              D                 D
 *    / \            /   / \            / \                |
 *   B   C     ==>  F   B   C   ==>    F   A       OR      A
 *      / \                 |             / \             /|\
 *     D   E                E            B   C           B C F
 *     |                                     |             |
 *     F                                     E             E
 *                (intermediate)   (non-exclusive)    (exclusive)
 *
 * When a stream is removed from the dependency tree, its dependencies
 * can be moved to become dependent on the parent of the closed stream.
 * The weights of new dependencies are recalculated by distributing the
 * weight of the dependency of the closed stream proportionally based on
 * the weights of its dependencies.
 *
 * Streams that are removed from the dependency tree cause some
 * prioritization information to be lost.  Resources are shared between
 * streams with the same parent stream, which means that if a stream in
 * that set closes or becomes blocked, any spare capacity allocated to a
 * stream is distributed to the immediate neighbors of the stream.
 *
 * However, if the common dependency is removed from the tree, those
 * streams share resources with streams at the next highest level.
 *
 * For example, assume streams A and B share a parent, and streams C and
 * D both depend on stream A.  Prior to the removal of stream A, if
 * streams A and D are unable to proceed, then stream C receives all the
 * resources dedicated to stream A.  If stream A is removed from the
 * tree, the weight of stream A is divided between streams C and D.  If
 * stream D is still unable to proceed, this results in stream C
 * receiving a reduced proportion of resources.  For equal starting
 * weights, C receives one third, rather than one half, of available
 * resources.
 *
 * It is possible for a stream to become closed while prioritization
 * information that creates a dependency on that stream is in transit.
 * If a stream identified in a dependency has no associated priority
 * information, then the dependent stream is instead assigned a default
 * priority (Section 5.3.5).  This potentially creates suboptimal
 * prioritization, since the stream could be given a priority that is
 * different to what is intended.
 *
 * To avoid these problems, an endpoint SHOULD retain stream
 * prioritization state for a period after streams become closed.  The
 * longer state is retained, the lower the chance that streams are
 * assigned incorrect or default priority values.
 *
 * This could create a large state burden for an endpoint, so this state
 * MAY be limited.  An endpoint MAY apply a fixed upper limit on the
 * number of closed streams for which prioritization state is tracked to
 * limit state exposure.  The amount of additional state an endpoint
 * maintains could be dependent on load; under high load, prioritization
 * state can be discarded to limit resource commitments.  In extreme
 * cases, an endpoint could even discard prioritization state for active
 * or reserved streams.  If a fixed limit is applied, endpoints SHOULD
 * maintain state for at least as many streams as allowed by their
 * setting for SETTINGS_MAX_CONCURRENT_STREAMS.
 *
 * An endpoint receiving a PRIORITY frame that changes the priority of a
 * closed stream SHOULD alter the dependencies of the streams that
 * depend on it, if it has retained enough state to do so.
 *
 * Providing priority information is optional.  Streams are assigned a
 * non-exclusive dependency on stream 0x0 by default.  Pushed streams
 * (Section 8.2) initially depend on their associated stream.  In both
 * cases, streams are assigned a default weight of 16.
 ************************************************************************
 * This describes in effect the way that the write thread behaves looking
 * for work, and commits us to maintaining links between all the streams
 * associated with a session, and also to maintaining stream stubs after
 * they are closed. The aggro, though, is on the write thread rather than
 * the read thread, which has to do things like match up the push promises
 * with the requests that would retrieve them if they hadn't been pushed.
 */
                if (hfh.flags & HTTP2_PADDED)
                {
                    if (( pad_len = deal_with_padding(&b, len, ep, wdbp)) <= 0)
                        goto leave;
                    len -= (pad_len + 1);
                }
                xflag = (*b & 0x80) ? 1 : 0;
                dep_sid  = ((*b & 0x7f) << 24)
                           + (*(b+1) << 16)
                           + (*(b+2) << 8)
                           + *(b+3);
                if (dep_sid == 0)
                {
                    unlink_stream(h2sp);
                }
                else
                if (dep_sid == sid)
                {
                    q_error(ep, 0, HTTP2_PROTOCOL_ERROR);
                    goto leave;
                }
                if ((hip = lookup(ep->h2cp->stream_hash, (char *) sid)) != NULL)
                {
                    xh2sp = (struct http2_stream *) (hip->body); 
                    if (is_child_stream(h2sp, xh2sp))
                    {
                        unlink_stream(xh2sp);
                        link_stream(h2sp->depend_stream, xh2sp, 0);
/*
 * If a stream is made dependent on one of its own dependencies, the
 * formerly dependent stream is first moved to be dependent on the
 * reprioritized stream's previous parent.  The moved dependency retains
 * its weight.
 * -> Need a hook-up routine along with the unlink()
 */
                    }
                    unlink_stream(h2sp);
                    link_stream(xh2sp, h2sp, xflag);
                }
                break;
            case HTTP2_RST_STREAM_TYPE:
/*
 * A rst frame is:
 * - 4 byte error code
 * Currently defined error codes are well known. Unknown errors should be
 * treated as Internal Error
 * - Print out the error (that may be 'No Error')
 * - Close the stream, and possibly the session.
 */
                err_code  = ((*b & 0x7f) << 24)
                           + (*(b+1) << 16)
                           + (*(b+2) << 8)
                           + *(b+3);
                output_http2_error(err_code, wdbp);
                switch(err_code)
                {
                case HTTP2_NO_ERROR:
                case HTTP2_SETTINGS_TIMEOUT:
                case HTTP2_STREAM_CLOSED:
                case HTTP2_FLOW_CONTROL_ERROR:
                case HTTP2_REFUSED_STREAM:
                case HTTP2_CANCEL:
                case HTTP2_ENHANCE_YOUR_CALM:
                default:
                    break;
                case HTTP2_PROTOCOL_ERROR:
                case HTTP2_INTERNAL_ERROR:
                case HTTP2_COMPRESSION_ERROR:
                case HTTP2_FRAME_SIZE_ERROR:
                case HTTP2_CONNECT_ERROR:
                case HTTP2_INADEQUATE_SECURITY:
                    goto leave;
                }
                break;
            case HTTP2_SETTINGS_TYPE:
/*
 * A Settings frame is:
 * - empty if the ACK flag is set; it simply acknowledges receipt of the peer's settings
 * - otherwise, zero or more pairs of:
 *   - 2 byte ID
 *   - 4 byte value
 */
                if (!(hfh.flags & HTTP2_ACK))
                {
                    for (x = b, top = x + len - 5; x < top;)
                    {
                        poss_id = ((*x ) << 8)
                           + *(x+1);
                        x += 2;
                        poss_val = ((*x & 0x7f) << 24)
                           + (*(x+1) << 16)
                           + (*(x+2) << 8)
                           + *(x+3);
                        x += 4;
                        if (wdbp->debug_level > 3)
                            fprintf(stderr, "tid:%lx:Incoming setting: id %d value %d\n",
                                  tid, poss_id, poss_val);
                        switch(poss_id)
                        {
                        case HTTP2_SETTINGS_HEADER_TABLE_SIZE:
                            ep->h2cp->set_in.header_table_size = poss_val;
                            break;
                        case HTTP2_SETTINGS_ENABLE_PUSH:
                            ep->h2cp->set_in.enable_push = poss_val;
                            break;
                        case HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
                            ep->h2cp->set_in.max_conc_strms = poss_val;
                            break;
                        case HTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
                            ep->h2cp->set_in.initial_win_size = poss_val;
                            break;
                        case HTTP2_SETTINGS_MAX_FRAME_SIZE:
                            if (poss_val > 3*WORKSPACE)
                                poss_val = 3*WORKSPACE; /* Our send buffer */
                            ep->h2cp->set_in.max_frame_size = poss_val;
                            break;
                        case HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
                            ep->h2cp->set_in.max_header_list_size = poss_val;
                            break;
                        default:
                            (void) fprintf(stderr,
                                 "(tid:%lx) Unknown setting value (%x,%x)\n",
                               tid, poss_id, poss_val);
                            break;
                        }
                    }
                    q_http2_settings(ep, HTTP2_ACK);
                }
                break;
            case HTTP2_PING_TYPE:
/*
 * A PING frame contains:
 * - A compulsory 8 byte payload, that can be anything
 *
 * The response ('pong?') is the same, but with the ACK flag set.
 * There must be no stream ID.
 */
                if (sid != 0 || len != 8)
                   q_http2_goaway(ep, HTTP2_PROTOCOL_ERROR);
                if (!(hfh.flags & HTTP2_ACK))
                   q_http2_ping(ep);
                break;
            case HTTP2_GOAWAY_TYPE:
/*
 * A GOAWAY frame contains:
 * - 1 bit reserved
 * - 31 bits Last Stream ID; all transactions on Stream ID's higher than this
 *   can be re-tried on a fresh connection.
 * - A 4 byte error code, as above
 * - Optionally, as much debug data as we care to include
 *
 * Print out the error code if there is one.
 *
 * Decide if we need to open a new connection. How? We just do ...
 * - Open the new connection
 * - Iterate over the streams, creating new streams on the new connection
 *   for any work that appears to be in flight, and forward it? Not sure if
 *   this is necessary.
 */
                sid  = ((*b & 0x7f) << 24)
                           + (*(b+1) << 16)
                           + (*(b+2) << 8)
                           + *(b+3);
                b += 4;
                err_code  = ((*b & 0x7f) << 24)
                           + (*(b+1) << 16)
                           + (*(b+2) << 8)
                           + *(b+3);
                output_http2_error(err_code, wdbp);
                goto leave;
            case HTTP2_WINDOW_UPDATE_TYPE:
/*
 * A WINDOW_UPDATE frame contains:
 * - 1 bit reserved
 * - 31 bits window increment. This can apply to the connection (Stream ID == 0)
 *   or an individual stream. Both need to be tracked. The SETTINGS frame is
 *   used to reduce the window size.
 * Only DATA frames are subject to flow control.
 */
                poss_val  = ((*b & 0x7f) << 24)
                           + (*(b+1) << 16)
                           + (*(b+2) << 8)
                           + *(b+3);
                if (wdbp->debug_level > 3)
                    fprintf(stderr, "tid:%lx:Window Update: sid %u increment %u\n",
                                  tid, sid, poss_val);
                if (sid == 0)
                    ep->h2cp->win_size_in += poss_val;
                else
                if ((hip = lookup(ep->h2cp->stream_hash, (char *) sid)) != NULL)
                {
                    xh2sp = (struct http2_stream *) (hip->body); 
                    xh2sp->win_size_in += poss_val;
                }
                else
                {
                    q_error(ep, sid, HTTP2_PROTOCOL_ERROR);
                }
                break;
            default:
/*
 * Presumably an extension, which is allowed ... ignore it.
 */
                break;
            }
        }
        else
        {
            pthread_mutex_unlock(&ep->rights);
            break;
        }
    }
leave:
    sighold(SIGIO);
    ep->iwdbp->go_away = 1;
    ep->owdbp->go_away = 1;
    notify_master(ep->iwdbp->root_wdbp->pbp, wdbp);
                    /* Notify the main thread */
    if (wdbp->debug_level > 3)
        fprintf(stderr, "link_pipe_buf_forward(%lx) exiting ... connect_fd=%d\n",
                tid, cur_link->connect_fd);
    return;
}
/*
 * If we were going to implement h2c this would have to be written ...
 */
struct script_element *  ready_for_http2(union all_records * msg, WEBDRIVE_BASE * wdbp)
{
unsigned char * b = msg->send_receive.msg->buf;
unsigned char * top = b + msg->send_receive.message_len;
/*
 * Find the end of the headers
 * Look for:
 * - Connection: Upgrade, HTTP2-Settings
 * - Upgrade: h2c
 * - HTTP2-Settings: <base64url encoding of HTTP/2 SETTINGS payload>
 * If they are not present, provide them.
 * Return the updated message
 */ 
    return NULL;
}
void http2_data_append(struct http2_stream *h2sp, 
                       unsigned char *b,unsigned int len, int end_stream)
{
struct http2_req_response *req_resp = &h2sp->in_flight;
WEBDRIVE_BASE * wdbp = h2sp->own_h2cp->ep->iwdbp;
struct element_tracker saved;
struct script_element * sep = NULL;
unsigned char * old_base;
int jump;

    if (req_resp->hr.read_cnt > 0)
        req_resp->hr.read_cnt -= len;
    if (len > 0
     && (req_resp->hr.scan_flag || wdbp->verbosity > 2 || wdbp->mirror_bin))
    {
/*
 * We may now have to decompress the data stream
 */
        if (req_resp->hr.gzip_flag)
        {
            assert( req_resp->hr.from_wire.element
                    == req_resp->hr.head_start.element +
                         req_resp->hr.head_start.len);
            saved = req_resp->hr.from_wire;
            req_resp->hr.from_wire.element = b;
            req_resp->hr.from_wire.len = len;
            req_resp->hr.decoded.len = 0;
            if ((req_resp->hr.gzip_flag == 1 && inf_block(&req_resp->hr,len) < 0)
                  || (req_resp->hr.gzip_flag == 2
                       && brot_block(&req_resp->hr,len)
                        == BROTLI_RESULT_ERROR))
            {
                fprintf(stderr,
        "(Client:%s) decompression of following %d bytes failed for some reason ...\n",
                          wdbp->parser_con.pg.rope_seq, len);
                gen_handle(stderr, b, b + len, 1);
                fputs("=========\n", stderr);
                req_resp->hr.from_wire = saved;
            }
            else
            if (req_resp->hr.decoded.len < (saved.alloc - saved.len))
            {
#ifdef DEBUG
                fprintf(stderr, "Decompressed Fragment:\n%.*s\n____\n",
                         req_resp->hr.decoded.len,
                         req_resp->hr.decoded.element);
#endif
                memcpy(saved.element + saved.len, req_resp->hr.decoded.element,
                         req_resp->hr.decoded.len);
                saved.len += req_resp->hr.decoded.len;
                req_resp->hr.from_wire = saved;
            }
            else
            {
                sep = req_resp->subm_req->child_track;
#ifdef DEBUG
                fprintf(stderr, "Decompressed Fragment:\n%.*s\n\
____Resized from: %d to %d\n",
                         req_resp->hr.decoded.len,
                         req_resp->hr.decoded.element,
                         sep->body_len, sep->body_len + sep->body_len);
#endif
                old_base = sep->body;
                jump = ((sep->body_len +  (saved.alloc - saved.len)) >
                            req_resp->hr.decoded.len)
                       ? sep->body_len
                       : req_resp->hr.decoded.len;
                sep->body = (unsigned char *) realloc(old_base,
                               sep->body_len + jump);
                req_resp->hr.head_start.element = sep->body;
                req_resp->hr.from_wire.element = sep->body +
                            (saved.element - old_base);
                assert( req_resp->hr.from_wire.element
                    == req_resp->hr.head_start.element +
                         req_resp->hr.head_start.len);
                memcpy(req_resp->hr.from_wire.element + saved.len,
                         req_resp->hr.decoded.element,
                         req_resp->hr.decoded.len);
                req_resp->hr.from_wire.len = saved.len +
                                              req_resp->hr.decoded.len;
                req_resp->hr.from_wire.alloc = saved.alloc + jump;
                sep->body_len += jump;
            }
        }
        else
        {    /* Potentially broken if no compression and no size provided ... */
            memcpy(req_resp->hr.from_wire.element + req_resp->hr.from_wire.len,
                        b, len);
            req_resp->hr.from_wire.len += len;
            assert(req_resp->hr.from_wire.len <=
                   req_resp->hr.from_wire.alloc);
        }
    }
    if (end_stream != 0)
    {
        if (req_resp->hr.scan_flag)
        {
/*
 * The assumption here is that only one of any parallel incoming streams will
 * contain state of interest. That is true. However, things like Google
 * Analytics may well be indistinguishable from 'real' stuff to the program
 * so this uncoordinated multi-thread access to shared state is problematic.
 */
            reset_progressive(wdbp->root_wdbp);
            scan_incoming_body(wdbp->root_wdbp, req_resp->hr.from_wire.element,
                 req_resp->hr.from_wire.element + req_resp->hr.from_wire.len);
            if (!wdbp->except_flag)
                scan_incoming_error(wdbp->root_wdbp,
                   req_resp->hr.from_wire.element,
                   req_resp->hr.from_wire.element + req_resp->hr.from_wire.len);
        }
        if (req_resp->hr.gzip_flag
          &&(req_resp->hr.scan_flag || wdbp->verbosity > 2 || wdbp->mirror_bin))
        {
            if (req_resp->hr.gzip_flag == 1)
                inf_close(&req_resp->hr);
            else
            if (req_resp->hr.gzip_flag == 2)
                brot_close(&req_resp->hr);
            old_base = req_resp->hr.from_wire.element +
                       req_resp->hr.from_wire.len;
            assert( req_resp->subm_req->child_track->body
                        == req_resp->hr.head_start.element
                 && req_resp->hr.from_wire.element
                    == req_resp->hr.head_start.element +
                         req_resp->hr.head_start.len);
            adjust_content_length(wdbp, 
                  req_resp->subm_req->child_track->body,&old_base);
            req_resp->subm_req->child_track->body_len =
                  old_base - req_resp->subm_req->child_track->body;
            assert( req_resp->subm_req->child_track->body_len > 0);
            req_resp->subm_req->child_track->body = (unsigned char *)
                   realloc( req_resp->subm_req->child_track->body,
                       req_resp->subm_req->child_track->body_len);
        }
        sep = req_resp->subm_req->child_track;
        sep->timestamp = timestamp();
        memset((unsigned char *) req_resp, 0,
                 sizeof(*req_resp));
        pipe_buf_add(h2sp->http1_link->to_ep->iwdbp->pbp, 1, 0,
                       sep, NULL, 0); /* Actually points to the master */
    }
    return;
}
/*
 * Function that counts and de-lineates the headers, and optionally processes
 * any cookies. It works on both genuine HTTP/1.1 headers and decoded HTTP/2.0
 * headers.
 */
int http_head_delineate_1(hp, wdbp, cookie_flag)
struct http_req_response * hp;
WEBDRIVE_BASE * wdbp;
int cookie_flag;
{
unsigned char * ncrp;
unsigned char * crp;
unsigned char * colp;
unsigned char * xp;
int set_cnt = 0;
int adj;
struct script_element * sep;
/*
 * Loop - delineate all the headers.
 */
    hp->status = (*(hp->head_start.element) == 'H'
                && *(hp->head_start.element + 1) == 'T'
                && *(hp->head_start.element + 2) == 'T'
                && *(hp->head_start.element + 3) == 'P')
                 ? 0 : -1;   /* Works for an incoming HTTP/1.1 */
    hp->read_cnt = -1;
    for (ncrp = hp->head_start.element, hp->element_cnt = 0;
            ncrp < (hp->head_start.element + hp->head_start.len)
               && ((crp = memchr(ncrp,'\r',
          hp->head_start.len + 1 - (ncrp - hp->head_start.element)))  != NULL);
                ncrp = crp + 2)
    {
        if (hp->element_cnt > 63)
            return -1;
        hp->headings[hp->element_cnt].label.element = ncrp;
        if ((colp = memchr(ncrp, ':', (crp - ncrp))) != NULL)
        {
            hp->headings[hp->element_cnt].label.len = (colp - ncrp);
            for (hp->headings[hp->element_cnt].value.element = colp + 2,
                 hp->headings[hp->element_cnt].value.len = (crp - colp - 2);
                       hp->headings[hp->element_cnt].value.len >0
                   &&  hp->headings[hp->element_cnt].value.element[0] == ' ';
                             hp->headings[hp->element_cnt].value.len--,
                             hp->headings[hp->element_cnt].value.element++);
            switch( hp->headings[hp->element_cnt].label.len)
            {
            case 0:   /* HTTP/2.0-only headers */
/*
 * Response =>
 * :status: whatever
 * Request =>
 * :method: VERB (e.g. GET, POST, OPTIONS etc. goes to the first line)
 * :scheme: http (goes to the Host: header)
 * :path: /      (goes to the first line)
 * :authority: www.example.com (goes to the Host header)
 */
                adj = (unsigned char *)
                        memchr(hp->headings[hp->element_cnt].value.element,':',
                             hp->headings[hp->element_cnt].value.len)
                       - hp->headings[hp->element_cnt].value.element;
                if (adj < 1)
                   continue;       /* Something has gone wrong ... */
                hp->headings[hp->element_cnt].label.len += adj + 2;
                hp->headings[hp->element_cnt].value.element += adj + 2 ;
                hp->headings[hp->element_cnt].value.len -= adj - 2;
                if (adj == 5 )
                {
                    if (!strncasecmp(
                         hp->headings[hp->element_cnt].label.element,
                                  ":status: ", 9))
                    {
                        hp->status = atoi(
                              hp->headings[hp->element_cnt].value.element);
                        hp->status_ind = hp->element_cnt;
                    }
                    else
                    if (!strncasecmp(
                         hp->headings[hp->element_cnt].label.element,
                                  ":method: ", 9))
                        hp->verb_ind = hp->element_cnt;
                    else
                    if (!strncasecmp(
                         hp->headings[hp->element_cnt].label.element,
                                  ":scheme: ", 9))
                        hp->scheme_ind = hp->element_cnt;
                }
                else
                if (adj == 3
                  && (!strncasecmp(
                         hp->headings[hp->element_cnt].label.element,
                                  ":path: ", 7)))
                    hp->path_ind = hp->element_cnt;
                else
                if (adj == 8
                  && (!strncasecmp(
                         hp->headings[hp->element_cnt].label.element,
                                  ":authority: ", 12)))
                    hp->authority_ind = hp->element_cnt;
                break;
            case 8:
                if (hp->status != -1
                  && !strncasecmp(hp->headings[hp->element_cnt].label.element,
                       "Location", 8))
                {
                    reset_progressive(wdbp);
                    scan_incoming_body( wdbp,
                        hp->headings[hp->element_cnt].value.element,
                        hp->headings[hp->element_cnt].value.element
                          + hp->headings[hp->element_cnt].value.len);
                }
                break;
            case 14:
                if (!strncasecmp(hp->headings[hp->element_cnt].label.element,
                       "Content-Length", 14))
                {
                    hp->read_cnt = atoi(hp->headings[hp->element_cnt].value.element);
                }
                break;
            case 10:
                if (cookie_flag
                && !strncasecmp(hp->headings[hp->element_cnt].label.element,
                             "Set-Cookie", 10))
                {
                    cache_cookie( wdbp,
                        hp->headings[hp->element_cnt].value.element,
                        hp->headings[hp->element_cnt].value.element
                        + hp->headings[hp->element_cnt].value.len);
                    set_cnt++;
                }
                else
                if (!strncasecmp(hp->headings[hp->element_cnt].label.element,
                                  "X-Compress", 10)
                  && !strncasecmp(hp->headings[hp->element_cnt].value.element,
                              "yes", 3))
                    hp->gzip_flag = 1;
                break;
            case 12:
                if (!strncasecmp(hp->headings[hp->element_cnt].label.element,
                       "Content-Type", 12))
                {
                    if (!strncasecmp(hp->headings[hp->element_cnt].value.element,
                                  "text/", 5))
                    {
                        xp = hp->headings[hp->element_cnt].value.element + 5;
                        if (!strncasecmp(xp, "plain", 5))
                            hp->scan_flag = 1;
                        else
                        if (!strncasecmp(xp, "javascript", 10))
                            hp->scan_flag = 0;
                        else
                        if (!strncasecmp(xp, "css", 3))
                            hp->scan_flag = 0;
                        else
                            hp->scan_flag = 1;
                    }
                    else
                    if (!strncasecmp(hp->headings[hp->element_cnt].value.element,
                              "image/", 6))
                        hp->scan_flag = 0;
#if ! ORA9IAS_1|ORA9IAS_2
                    else
                    if (!strncasecmp(hp->headings[hp->element_cnt].value.element,
                               "application/", 12))
                    {
                        if (!strncasecmp(hp->headings[hp->element_cnt].value.element + 12,"json", 4))
                            hp->scan_flag = 1;
                        else
                            hp->scan_flag = 0;
                    }
#endif
                    else
                        hp->scan_flag = 1;
                }
                break;
            case 16:
                if (!strncasecmp(hp->headings[hp->element_cnt].label.element,
                                   "Content-Encoding", 16))
                {
                    if (!strncasecmp(hp->headings[hp->element_cnt].value.element,
                                   "gzip", 4)
                     ||!strncasecmp(hp->headings[hp->element_cnt].value.element,
                                   "deflate", 7))
                        hp->gzip_flag = 1;
                    else
                    if (!strncasecmp(hp->headings[hp->element_cnt].value.element,
                                   "br", 2))
                        hp->gzip_flag = 2;
                }
                break;
            default:
                break;
            }
        }
        else
        {
            hp->headings[hp->element_cnt].label.len = 0;
            hp->headings[hp->element_cnt].value.element = ncrp;
            hp->headings[hp->element_cnt].value.len = (crp - ncrp);
        }
        hp->element_cnt++;
    }
    hp->headings[hp->element_cnt].label.element = crp + 2;
    hp->headings[hp->element_cnt].label.len = 0;
    hp->headings[hp->element_cnt].value.element = crp + 2;
    hp->headings[hp->element_cnt].value.len = 0;
    if (cookie_flag && set_cnt > 0)
    {
        pthread_mutex_lock(&wdbp->script_mutex);
        for (sep = wdbp->sc.anchor;
                sep != NULL;
                    sep = sep->next_track)
            edit_cookies(wdbp, sep);
        pthread_mutex_unlock(&wdbp->script_mutex);
    }
    return hp->element_cnt;
}
static struct http1_status {
    int code;
    char * narr;
} http1_statuses[41] = {
{100, "Continue"},
{101, "Switching Protocols"},
{200, "OK"},
{201, "Created"},
{202, "Accepted"},
{203, "Non-Authoritative Information"},
{204, "No Content"},
{205, "Reset Content"},
{206, "Partial Content"},
{300, "Multiple Choices"},
{301, "Moved Permanently"},
{302, "Found"},
{303, "See Other"},
{304, "Not Modified"},
{305, "Use Proxy"},
{306, "(Unused, obsolete)"},
{307, "Temporary Redirect"},
{400, "Bad Request"},
{401, "Unauthorized"},
{402, "Payment Required"},
{403, "Forbidden"},
{404, "Not Found"},
{405, "Method Not Allowed"},
{406, "Not Acceptable"},
{407, "Proxy Authentication Required"},
{408, "Request Timeout"},
{409, "Conflict"},
{410, "Gone"},
{411, "Length Required"},
{412, "Precondition Failed"},
{413, "Request Entity Too Large"},
{414, "Request-URI Too Long"},
{415, "Unsupported Media Type"},
{416, "Requested Range Not Satisfiable"},
{417, "Expectation Failed"},
{500, "Internal Server Error"},
{501, "Not Implemented"},
{502, "Bad Gateway"},
{503, "Service Unavailable"},
{504, "Gateway Timeout"},
{505, "HTTP Version Not Supported"}};
/*
 * Look up the Huffman code based on the string of ones
 */
static char * http1_status_narr(match)
int match;
{
struct http1_status * low = & http1_statuses[0];
struct http1_status * high = & http1_statuses[40];
struct http1_status *guess;

    while (low <= high)
    {
        guess = low + ((high - low) >> 1);
        if (match == guess->code)
            return guess->narr; 
        else
        if (match > guess->code)
            low = guess + 1;
        else
            high = guess - 1;
    }
    return "Unknown";
}
/*
 * Work out how much space is needed for a complete HTTP/1.1 version of
 * of an HTTP/2 request or response
 */
int calc_http1_space(hrp)
struct http_req_response * hrp;
{
int ret;

    ret  = ((hrp->headings[hrp->element_cnt].label.element
                           - hrp->headings[0].label.element) + 4
    + ((hrp->status == -1) ? /* Request */
      ((hrp->headings[hrp->verb_ind].value.len + 12 /* White Space + HTTP/1.1 */
       - (hrp->headings[hrp->verb_ind + 1].label.element
              - hrp->headings[hrp->verb_ind].label.element))
      +(hrp->headings[hrp->scheme_ind].value.len + 11 /* White Space + Host: + :// */
       - (hrp->headings[hrp->scheme_ind + 1].label.element
              - hrp->headings[hrp->scheme_ind].label.element))
      +(hrp->headings[hrp->authority_ind].value.len  /* Going with Host: */
       - (hrp->headings[hrp->authority_ind + 1].label.element
              - hrp->headings[hrp->authority_ind].label.element))
      +(hrp->headings[hrp->path_ind].value.len  /* Going with verb */
       - (hrp->headings[hrp->path_ind + 1].label.element
              - hrp->headings[hrp->path_ind].label.element)))
    :  /* Response */
      ((hrp->headings[hrp->status_ind].value.len + 12 /* White Space + HTTP/1.1 */
       - (hrp->headings[hrp->status_ind + 1].label.element
              - hrp->headings[hrp->status_ind].label.element))
        + strlen(http1_status_narr(hrp->status)))));
    if (hrp->read_cnt == -1)
    {     /* Need to allow for a Content-length header and some data */
        ret += WORKSPACE;
    }
    else
        ret += hrp->read_cnt;
    return ret;
}
void http2_header_append(h2sp, b, len, end_headers)
struct http2_stream * h2sp ;
unsigned char * b;
unsigned int len;
int end_headers;
{
int out_cnt;
unsigned char * ip = b;
unsigned char * top = b + len;
unsigned char * xp;
int i;
WEBDRIVE_BASE * wdbp = h2sp->own_h2cp->ep->iwdbp->root_wdbp;
struct script_element * sep;
struct http2_req_response * h2rrp = &h2sp->in_flight;
struct http2_head_decoding_context * hhdcp = h2sp->own_h2cp->hhdcp;

    if (h2rrp->http2_resp_headers.element == NULL)
    {
        h2rrp->http2_resp_headers.element =
               (unsigned char *) malloc(WORKSPACE);
        h2rrp->http2_resp_headers.alloc = WORKSPACE;
        h2rrp->http2_resp_headers.len = 0;
    }
/*
 * Unpack as much as possible of the input.
 *
 * Return the number of bytes output.
 *
 * In case we run out of output space, or we haven't got the whole thing, we
 * return how far we got in ibasep. 
 *
 * At the moment, the output is an HTTP2 set of headers. We need to recast the
 * stuff that corresponds to the first line, and merge multiple cookie headers,
 * before the data can be passed to the HTTP 1-aware t3drive code.
 */
    for (ip = b; ip < top; )
    {
        out_cnt = decode_head_stream(hhdcp, &ip,top,
           h2rrp->http2_resp_headers.element +
           h2rrp->http2_resp_headers.len ,
           h2rrp->http2_resp_headers.element +
           h2sp->in_flight.http2_resp_headers.alloc);
        h2rrp->http2_resp_headers.len += out_cnt;
        if (ip < top)
        {
           h2rrp->http2_resp_headers.alloc +=
                    h2rrp->http2_resp_headers.alloc;
           h2rrp->http2_resp_headers.element = (unsigned char *)
                 realloc(h2rrp->http2_resp_headers.element,
                    h2rrp->http2_resp_headers.alloc);
        }
    }
/*
 * If the END_HEADERS flag is set, we need to deal with this:
 * -   Split into headers
 * -   Find the length of the body, if there is one
 * -   Allocate space for an HTTP/1.1 version of the headers (which means
 *     reworking the first four headers) plus the body.
 * -   Initialise things so that the body, if there is going to be one, is
 *     going to be written beyond the headers.
 * -   If there isn't going to be a body, we should hand this off now?
 */
    if (end_headers)
    {
        h2rrp->hr.head_start = h2rrp->http2_resp_headers;
        h2rrp->hr.declen = -1;
        if (http_head_delineate_1( &h2rrp->hr, wdbp, 1) < 1)
        {
            fputs("Too many headers\nYou must recompile\n", stderr);
            siggoaway();   /* Does not return */
        }
/*
 * If this is an HTTP/2.0 request, then we need to construct:
 * - The request from the VERB and the path for line one.
 * - Host header from the scheme plus the authority
 * If it is a response, we need to put the status on line 1.
 *
 * The space to be allocated has to be:
 * => The space needed for the reconstructed HTTP/1.1 elements
 *  + The space needed for the body
 *  + The space used by the HTTP/2.0 headers
 *  - The space taken by the HTTP/2.0-specific bits.
 */
        pthread_mutex_lock(&(wdbp->script_mutex));
        sep = add_answer(&wdbp->sc, h2sp->http1_link);
        pthread_mutex_unlock(&(wdbp->script_mutex));
        sep->body_len = calc_http1_space(&h2rrp->hr);
        sep->body = (unsigned char *) malloc(sep->body_len);
        xp = sep->body + sprintf(sep->body,"HTTP/1.1 %u %s\r\n",
                               h2rrp->hr.status,
                               http1_status_narr( h2rrp->hr.status));
        if (h2rrp->hr.status_ind != 0)
        {
            memcpy(xp,h2rrp->hr.headings[0].label.element,
                     (h2rrp->hr.headings[h2rrp->hr.status_ind - 1].label.element
                           - h2rrp->hr.headings[0].label.element));
            xp += (h2rrp->hr.headings[h2rrp->hr.status_ind - 1].label.element
                           - h2rrp->hr.headings[0].label.element);
        }
        if (h2rrp->hr.status_ind != h2rrp->hr.element_cnt)
        {
            memcpy(xp,h2rrp->hr.headings[h2rrp->hr.status_ind + 1].label.element,
                     (h2rrp->hr.headings[h2rrp->hr.element_cnt].label.element
                 - h2rrp->hr.headings[h2rrp->hr.status_ind + 1].label.element));
            xp += (h2rrp->hr.headings[h2rrp->hr.element_cnt].label.element
                  - h2rrp->hr.headings[h2rrp->hr.status_ind + 1].label.element);
        }
        *xp++ = '\r';
        *xp++ = '\n';
        h2rrp->hr.head_start.element = sep->body;
        h2rrp->hr.head_start.len = xp - sep->body;
        http_head_delineate_1( &h2rrp->hr, wdbp, 0);
        if (h2rrp->hr.read_cnt == -1)
        {     /* Need to reserve space for the content length */
            memcpy(xp - 2, "Content-Length:        1\r\n\r\n", 28);
            xp += 26;
            h2rrp->hr.head_start.len += 26;
            h2rrp->hr.element_cnt++;
            h2rrp->hr.headings[h2rrp->hr.element_cnt].label.element = xp;
            h2rrp->hr.headings[h2rrp->hr.element_cnt].label.len = 0;
            h2rrp->hr.headings[h2rrp->hr.element_cnt].value = 
                        h2rrp->hr.headings[h2rrp->hr.element_cnt].label;
        }
        h2rrp->hr.from_wire.element = xp;
        h2rrp->hr.from_wire.len = 0;
        h2rrp->hr.from_wire.alloc = (sep->body + sep->body_len) - xp;
        h2rrp->http2_resp_headers.alloc = 0;
        h2rrp->http2_resp_headers.len = 0;
        free(h2rrp->http2_resp_headers.element);
        h2rrp->http2_resp_headers.element = (unsigned char *) NULL;
        if (h2rrp->subm_req == NULL)
        {
            fprintf(stderr, "Unexpected response for stream %d - creating dummy\n%.*s\n",
                      h2sp->stream_id, sep->body_len, sep->body);
            h2rrp->subm_req = add_message(
                             &h2sp->http1_link->to_ep->owdbp->root_wdbp->sc,
                             h2sp->http1_link);
        }
        pthread_mutex_lock( &(wdbp->script_mutex));
        assert(check_integrity(&wdbp->sc, sep));
        make_child( &wdbp->sc, h2rrp->subm_req, sep);
        assert(check_integrity(&wdbp->sc, sep->prev_track));
        pthread_mutex_unlock(&(wdbp->script_mutex));
        if (end_headers & HTTP2_END_STREAM)
        {
            sep->timestamp = timestamp();
            sep->body_len = h2rrp->hr.head_start.len;
            h2rrp->subm_req = NULL;
            pipe_buf_add(h2sp->http1_link->to_ep->iwdbp->pbp, 1, 0,
                           sep, NULL, 0); /* Actually points to the master */
        }
    }
    return;
}
