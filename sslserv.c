/*
 * sslserv.c - SSL Server routines.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
/*
 * Find the session cookie
 */
void fish_things(lake, bank, prey, net, len)
char * lake;
char * bank;
struct bm_table * prey;
char * net;
int len;
{
char * p1;
char * p2;

    if ((p1 = bm_match(prey, lake, bank)) == NULL)
        return;
    p1 += prey->match_len;
    for (p2 = p1 + 1; *p2 < bank && *p2 != ';' && *p2 != '\r' && *p2 != ' '; p2++);
    if ((p2 - p1) >= len)
        return;
    len = (p2 - p1);
    memcpy(net, p1, len);
    net[len] = '\0';
    return;
}
/*
 * Create a new session token
 * -   Address of wdbp + pid + timestamp
 * -   By using the address we can quickly check the session
 * -   sess_buf must be at least 48 bytes long
 */
static void make_session(sess_buf, wdbp)
char * sess_buf;
WEBDRIVE_BASE * wdbp;
{
static int pid = -1;
double tt;
char buf[49];

    memset(buf,'0',48);
    buf[48] = '\0';
    if (pid == -1)
        pid = getpid();
    tt = (double) time(0);
    (void) hexin_r((unsigned char *) &wdbp, sizeof(wdbp),&buf[0],
            &buf[sizeof(wdbp) + sizeof(wdbp)]);
    (void) hexin_r((unsigned char *) &pid, sizeof(pid),
            &buf[sizeof(wdbp) + sizeof(wdbp)],
            &buf[sizeof(wdbp) + sizeof(wdbp) + sizeof(pid) + sizeof(pid)]);
    (void) hexin_r((unsigned char *) &tt, sizeof(tt),
       &buf[sizeof(wdbp) + sizeof(wdbp) + sizeof(pid) + sizeof(pid)], &buf[48]);
    strcpy(sess_buf, buf);
    return;
}
/*
 * Find a session, or reject it
 */
static WEBDRIVE_BASE * check_session(sess_buf, wdbp)
char * sess_buf;
WEBDRIVE_BASE * wdbp;
{
WEBDRIVE_BASE * poss_wdbp;

    if (strlen(sess_buf) != sizeof(char*) + sizeof(char*) +
                            sizeof(double)+sizeof(double) +
                              sizeof(int) + sizeof(int))
    {
        if (wdbp->debug_level > 2)
            fprintf(stderr,"(Client:%s) Session Length %d of (%s) not %d\n",
                                    wdbp->parser_con.pg.rope_seq,
                  strlen(sess_buf), sess_buf, ( sizeof(char*) + sizeof(char*) +
                            sizeof(double)+sizeof(double) +
                              sizeof(int) + sizeof(int)));
        return NULL;
    }
    hexout((unsigned char *) (&poss_wdbp), sess_buf, sizeof(poss_wdbp));
    if (poss_wdbp < &wdbp->root_wdbp->client_array[0]
     || poss_wdbp >= &wdbp->root_wdbp->client_array[wdbp->root_wdbp->client_cnt]
     || ((((unsigned char *) poss_wdbp) - ((unsigned char *) &wdbp->root_wdbp->client_array[0]))
          % sizeof(*wdbp) ) != 0
     || strcmp(sess_buf, poss_wdbp->session))
    {
        if (wdbp->debug_level > 2)
            fprintf(stderr,"(Client:%s) Stale Session Received: (%s)?\n",
                                    wdbp->parser_con.pg.rope_seq,
                  sess_buf);
        return NULL;
    }
    return poss_wdbp;
}
/*
 * Needs to exec() something to be safe because PAM calls fork() then malloc()
 */
static int validate(username, passwd, wdbp)
char * username;
char * passwd;
WEBDRIVE_BASE * wdbp;
{
int out_pipe[2];
int in_pipe[2];
char buf[512];
int len;
int child_pid;
 
    strcpy(buf, username);
    len = strlen(buf) + 1;
    strcpy(buf + len, passwd);
    len += strlen(buf + len);
/*
 * Cannot launch pipeline; give up
 */ 
    if (!(child_pid = launch_pipeline(in_pipe, out_pipe, "pwdserv", wdbp)))
    {
        close(in_pipe[1]);
        close(out_pipe[0]);
        return 0;
    }
    write( in_pipe[1], buf, len);
    close( in_pipe[1]);
    if ((len = read( out_pipe[0], buf, sizeof(buf))) > 0
          && buf[0] == 'O'
          && buf[1] == 'K')
        len = 1;
    else
        len = 0;
    close(out_pipe[0]);
#ifdef UNIX
/*
 * Tidy up any zombies
 */
    while (waitpid(0,0,WNOHANG) > 0);
#endif
    return len;
}
/*******************************************************************************
 * Validate the session and credentials, challenging if the user ID and password are
 * missing or invalid.
 *
 * This returns the wdbp entry whose session should own this request
 * (poss_wdbp).
 *
 * If hrp->scan_flag is not true, then the session cookie needs to be set.
 *
 * If poss_wdbp->username[0] == '\0', the request wasn't authenticated, and it
 * needs to be discarded ASAP.
 */
WEBDRIVE_BASE * poss_challenge(fd, hrp, wdbp)
int fd;
struct http_req_response * hrp;
WEBDRIVE_BASE * wdbp;
{
int len;
int i;
char * p1;
WEBDRIVE_BASE * poss_wdbp = NULL;
char username[64];
char passwd[64];
char session[64];
char buf[128];
char buf1[170];

    username[0] = '\0';
    passwd[0] = '\0';
    session[0] = '\0';

    strcpy(wdbp->narrative, "poss_challenge");
/*
 * Now loop through the headers, looking for the session cookie and the
 * authorisation.
 *
 * We are going to use the scan_flag to signal that a cookie is required or not.
 */
    for (hrp->read_cnt = 0, hrp->scan_flag = 0, hrp->gzip_flag = 0, i = 1;
            i < hrp->element_cnt - 1;
                i++)
    {
        switch (hrp->headings[i].label.len)
        {
        case 6:
            if (!strncasecmp(hrp->headings[i].label.element, "Cookie", 6))
            {
                fish_things(hrp->headings[i].value.element - 1,
                    hrp->headings[i+1].label.element, wdbp->ebp, session, 49);
                if ((poss_wdbp = check_session(session, wdbp)) != NULL)
                    hrp->scan_flag = 1; /* Flag that we don't need to set the cookie */
            }
            continue;
        case 10:
            if (!strncasecmp(hrp->headings[i].label.element, "Connection", 10)
              && !strncasecmp(hrp->headings[i].value.element, "close", 5))
            {
                if (wdbp->debug_level > 2)
                    fprintf(stderr,"(Client:%s) Seen a close (read_cnt:%d)\n",
                                    wdbp->parser_con.pg.rope_seq, hrp->read_cnt);
                if (hrp->read_cnt == 0)
                    hrp->read_cnt = -3; /* Chunked has to take precedence
                                         because we may need to decompress it */
            }
            continue;
        case 13:
            if (!strncasecmp(hrp->headings[i].label.element,"Authorization", 13)
              && hrp->headings[i].value.len < 170)
            {
                fish_things(hrp->headings[i].value.element - 1,
                 hrp->headings[i+1].label.element, wdbp->rbp, buf1, 170);
                len = b64dec(strlen(buf1), buf1, buf);
                buf[len] = '\0';
                if ((p1 = strchr(buf, ':')) != NULL
                    && (p1 - &buf[0]) < 64 
                    && (len - (p1 - &buf[0])) < 65)
                {
                    memcpy(username, buf, (p1 - &buf[0]));
                    username[ (p1 - &buf[0]) ] = '\0';
                    memcpy(passwd, p1 + 1, len - (p1 - &buf[0]) - 1);
                    username[ len - (p1 - &buf[0]) - 1 ] = '\0';
                }
            }
            continue;
        case 14:
            if (!strncasecmp(hrp->headings[i].label.element,"Content-Length", 14)
             && hrp->read_cnt != -1)
                hrp->read_cnt = atoi(hrp->headings[i].value.element);
            continue;
        case 16:
            if (!strncasecmp(hrp->headings[i].label.element,
                              "Content-Encoding", 16)
              && (!strncasecmp(hrp->headings[i].value.element, "gzip", 4)
               ||!strncasecmp(hrp->headings[i].value.element, "deflate", 7)))
                hrp->gzip_flag = 1;
            continue;
        case 17:
            if (!strncasecmp(hrp->headings[i].label.element,
                              "Transfer-Encoding", 17)
              && !strncasecmp(hrp->headings[i].value.element, "chunked", 7))
                hrp->read_cnt = -1;
            continue;
        default:
/*
 * All the other headers, which at the moment we ignore
 */
            break;
        }
    }
    if (username[0] == '\0'
     || passwd[0] == '\0'
     || !validate(username, passwd, wdbp))
    {
        len = 90;
        memcpy(wdbp->parser_con.tlook,
    "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"E2 Systems\"\r\nContent-Length: 0\r\n", 90);
        if (!hrp->scan_flag)
        {
            make_session(wdbp->session, wdbp);
            len += sprintf(wdbp->parser_con.tlook + len,
                         "Set-Cookie: E2SESSION=%s;Path=/;HttpOnly;Secure\r\n\r\n", wdbp->session);
            wdbp->username[0] = '\0';
            poss_wdbp = wdbp;
            hrp->scan_flag = 1;
        }
        else
        {
            wdbp->parser_con.tlook[len++] = '\r';
            wdbp->parser_con.tlook[len++] = '\n';
            poss_wdbp->username[0] = '\0';
        }
        smart_write(fd, wdbp->parser_con.tlook, len, 1, wdbp);
    }
    else
    {
        if (!hrp->scan_flag)
        {
            make_session(wdbp->session, wdbp);
            poss_wdbp = wdbp;
        }
        strcpy(poss_wdbp->username, username);
        strcpy(poss_wdbp->passwd, passwd);
    }
    return poss_wdbp;
}
/***************************************************************************
 * Functions to provide a password-protected SSL service
 * - There is a service routine invoked by be_a_thread().
 * - The incoming socket is on cur_in_file.
 **************************************************************************
 * 1.  The HTTP read routines need to be able to read SSL.
 * 2.  find_a_slot() needs to find a free slot:
 *     -   We have 4 categories of slot
 *     -   No owning thread, no session:
 *         -    Available to find_a_slot()
 *     -   Owning thread, no session
 *         -    Will hand back static content on any valid session
 *         -    No session incoming, will provide a session, and Basic
 *              authentication:
 *              -    Will become Owning Thread, Session
 *     -   Owning thread, session
 *         -    Launches the application (webpath.sh, natmenu.sh, tundrive etc.)
 *         -    Must process all dynamic requests for this session
 *     -   No owning thread, session
 *         -    Owns an application instance
 *         -    Must process all dynamic requests for this session, so dynamic
 *              requests will 'capture' it.
 * 3.  We need to allocate a session cookie. Doesn't need to be that smart;
 *     combine the time with the process ID and the free slot address. Need an
 *     idle timeout as well. 
 * 4.  For a new connection it is all fine; we create our cookie, and the code
 *     continues to process requests on that connection as at present (though we
 *     still ought to check). Since we are only going to use HTTPS, perhaps we
 *     can use Basic Authentication, and validate against the password file? No
 *     good for Cloud access, but only use this capability on machines with
 *     conventional resources (ie. ours).
 * 5.  The tricky case is a request that already has a session cookie.
 *     -   If we don't recognise it, we continue as 4. above.
 *     -   If we do recognise it, we need to take into account the state of the
 *         existing session.
 *         -   If the existing session is quiescent, just hand it off:
 *             -   Our thread switches to the other WEBDRIVE_BASE
 *             -   The current slot is marked idle once more
 *         -   If the existing session is not quiescent:
 *             -   Static content, we can just return the contents from our
 *                 current thread.
 *             -   Dynamic content, tricky:
 *                 -   Needs to be serialised; how do we queue requests? Answer,
 *                     make the processing loop look for things being
 *                     handed off to it.
 *                 -   'Back Button' issues? Might do strange things.
 *                 -   Rationalise variable handling, restrict the opportunities
 *                     for shell injection attacks.
 * ********************************************************************
 * Deal with an incoming request when we are an HTTPS Web Server.
 * -  f will be the accepted connection port.
 * -  The buffer usually used for processing the script will hold the
 *    request.
 * -  We know that b == &wdbp->in_buf.buf[0]; we may need to extend it.
 * ********************************************************************
 */
int ssl_serv_read(b, wdbp, poss_wdbp)
unsigned char * b;
WEBDRIVE_BASE * wdbp;
WEBDRIVE_BASE ** poss_wdbp;
{
long int f;
struct http_req_response hr;
struct element_tracker poss_host;
int host_flag;
int host_seen;
int i;
unsigned char * x, *y;
int mess_len;
int cont_len;
LINK * prev_link;
/*
 * Our accept port is in script file position.
 */
    strcpy(wdbp->narrative, "ssl_serv_read");
    f = wdbp->cur_link->connect_fd;
/*
 * Not sure what this will mean, but leave it for now in case I can think
 * of a use for it.
 */
    if (wdbp->proxy_url != NULL)
    {
        wdbp->link_det[0].to_ep = &(wdbp->proxy_ep);
        wdbp->link_det[0].connect_sock = wdbp->proxy_link.connect_sock;
    }
    hr.status = -1;
    idle_alarm(wdbp);
    if (wdbp->debug_level > 1)
    {
        fprintf(stderr, "(Client:%s) ssl_serv_read() incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, f);
    }
    if (get_http_head(f, b, wdbp, &hr) <= 0)
    {
        alarm_restore(wdbp);
        if (wdbp->debug_level > 1)
        {
            fprintf(stderr,
               "(Client:%s) ssl_serv_read() saw nothing on incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, f);
        }
        return -1;
    }
    alarm_restore(wdbp);
/*
 * Now need to see if we have a session and authorisation
 */
    *poss_wdbp = poss_challenge(f, &hr, wdbp);
/*
 * Now loop through the headers. We need to:
 * -  Assemble the rest of the message
 * -  We will if necessary extend the buffer if it isn't big enough
 */
    memcpy(wdbp->parser_con.tbuf, hr.headings[0].label.element,
             hr.headings[1].label.element - hr.headings[0].label.element);
    x = wdbp->parser_con.tbuf +
             (hr.headings[1].label.element - hr.headings[0].label.element);
/*
 * Now loop through the headers, copying across as appropriate.
 * - Because we de-chunk and decompress, we remove these headers
 */
    for (hr.read_cnt = 0,  hr.gzip_flag = 0, i = 1;
            i < hr.element_cnt - 1;
                i++)
    {
        switch (hr.headings[i].label.len)
        {
        case 10:
            if (!strncasecmp(hr.headings[i].label.element, "Connection", 10)
              && !strncasecmp(hr.headings[i].value.element, "close", 5))
            {
                if (wdbp->debug_level > 2)
                    fprintf(stderr,"(Client:%s) Seen a close (read_cnt:%d)\n",
                                    wdbp->parser_con.pg.rope_seq, hr.read_cnt);
                if (hr.read_cnt == 0)
                    hr.read_cnt = -3; /* Chunked has to take precedence
                                         because we may need to decompress it */
            }
            break;
        case 14:
            if (!strncasecmp(hr.headings[i].label.element,"Content-Length", 14)
             && hr.read_cnt != -1)
                hr.read_cnt = atoi(hr.headings[i].value.element);
            continue;
        case 16:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Content-Encoding", 16)
              && (!strncasecmp(hr.headings[i].value.element, "gzip", 4)
               ||!strncasecmp(hr.headings[i].value.element, "deflate", 7)))
                hr.gzip_flag = 1;
            break;
        case 17:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Transfer-Encoding", 17)
              && !strncasecmp(hr.headings[i].value.element, "chunked", 7))
                hr.read_cnt = -1;
            continue;
        default:
/*
 * All the other headers, which at the moment we ignore
 */
            break;
        }
/*
 * Save the heading in the new buffer
 */
        if (hr.headings[i].label.len > 0)
        {
            memcpy(x,hr.headings[i].label.element,
                 hr.headings[i].label.len +
                 hr.headings[i].value.len + 2);
            x += hr.headings[i].label.len + hr.headings[i].value.len + 2;
            *x++ = '\r';   /* We may have stripped off a port */
            *x++ = '\n';   /* We may have stripped off a port */
        }
        else
        {
            memcpy(x,hr.headings[i].value.element,
                 hr.headings[i].value.len + 2);
            x += hr.headings[i].value.len + 2;
        }
    }
    hr.declen = -1;          /* Flag that compression isn't initialised */
    if (hr.gzip_flag && *poss_wdbp != NULL && (*poss_wdbp)->username[0] == '\0')
        hr.gzip_flag = 0;    /* Don't bother to decompress if we are ignoring it */
/*
 * Three approaches:
 * - Content length supplied; use it
 * - Chunking selected; read until all done (closed or zero length chunk).
 * - Close indicated; read until EOF
 */
    if (wdbp->debug_level > 2)
    {
        fprintf(stderr,
            "(Client:%s) so_far read_cnt=%d b=%x content=%x top=%x\n",
                      wdbp->parser_con.pg.rope_seq, hr.read_cnt, (long) b,
                      (long) hr.from_wire.element,
                      (long) hr.from_wire.element +  hr.from_wire.len);
    }
/*
 * If this is a HEAD, there isn't anything more
 */
    if (wdbp->head_flag)
    {
        wdbp->head_flag = 0;
        cont_len = 0;
    }
    else
    {
        if (hr.read_cnt == -1)
/*
 * Chunked data
 */
            cont_len = get_chunked_data(f, b, wdbp, &hr);
        else
        if (hr.read_cnt > 0)
            cont_len = get_known_data(f, b, wdbp, &hr);
        else
        if (hr.read_cnt == -3 )
            cont_len = get_close_data(f, b, wdbp, &hr);
        else
        if (hr.read_cnt == 0 )
            cont_len = 0;
        if (hr.gzip_flag)
            inf_close(&hr);
    }
/*
 * Add the VIA header and the content length
 */
    if (!strncmp(wdbp->parser_con.tbuf, "PUT ", 4)
     || !strncmp(wdbp->parser_con.tbuf, "POST ", 5))
        x += sprintf(x, "Content-Length: %d\r\n",
          (cont_len > hr.head_start.len) ? (cont_len - hr.head_start.len) : 0);
    x += sprintf(x, "\r\n");
    if ((*poss_wdbp)->username[0] == '\0')
        return 0;
    mess_len = (x - (unsigned char *) wdbp->parser_con.tbuf);
    if (mess_len + cont_len > WORKSPACE)
    {
        fprintf(stderr,
                 "(Client:%s) Not enough space; need %d!?\n",
            wdbp->parser_con.pg.rope_seq, mess_len + cont_len);
        wdbp->parser_con.tbuf = realloc(wdbp->parser_con.tbuf, 
                    mess_len + cont_len + 1);
        x = ((unsigned char *) wdbp->parser_con.tbuf) + mess_len;
    }
    if (cont_len > 0)
    {
        memcpy(x, hr.from_wire.element, hr.from_wire.len);
        x += hr.from_wire.len;
        mess_len = (x - (unsigned char *) wdbp->parser_con.tbuf);
    }
/*
 * Because the buffers are sitting in the WEBDRIVE_BASE structure, they
 * cannot be re-sized. We fix this by looking somewhere else for the
 * message in this case.
 */
    if (mess_len > WORKSPACE)
    {
        wdbp->overflow_send = (unsigned char *) malloc(mess_len);
        b = wdbp->overflow_send;
    }
    memcpy(b, wdbp->parser_con.tbuf,  mess_len);
    if (wdbp->debug_level > 1)
    {
        fprintf(stderr,
                "(Client:%s) ssl_serv_read() saw %d on incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, mess_len, f);
    }
    return mess_len;
}
/*
 * Loop processing requests on a SSL socket.
 */
void ssl_service(wdbp)
WEBDRIVE_BASE * wdbp;
{
char * x;
int which;
int ret;
LINK * link;
WEBDRIVE_BASE * sess_wdbp;

    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) ssl_service()",
                       wdbp->parser_con.pg.rope_seq);
    wdbp->ssl_proxy_flag = 2;
    if (!wdbp->parser_con.break_flag && !ssl_serv_accept(wdbp))
        return;
    wdbp->cur_link = &wdbp->link_det[wdbp->ssl_proxy_flag];
/*
 * Read the first request
 *
 * Look for the cookie.
 *
 * If there is no cookie, Respond with Basic Authentication Required, and go
 * through a round of BASIC Authentication.
 *
 * Loop - read data off the incoming socket, and adjust the HTTP headers.
 */
    strcpy(wdbp->narrative, "ssl_service");
    while ((wdbp->msg.send_receive.message_len =
            wdbp->webread(&(wdbp->in_buf), wdbp, &sess_wdbp)) >= 0)
    {
        if (wdbp->msg.send_receive.message_len == 0)
            continue;    /* We have dealt with this already by challenging it */
        if (wdbp->msg.send_receive.message_len > WORKSPACE)
            x = wdbp->overflow_send;
        else
            x = wdbp->in_buf.buf;
        wdbp->msg.send_receive.record_type = SEND_RECEIVE_TYPE;
        wdbp->msg.send_receive.msg = x;
        wdbp->rec_cnt++;
        wdbp->alert_flag = 0;
        if (wdbp->debug_level > 2)
            (void) fprintf(stderr,"(Client:%s) SSL Service Loop\n\
=====================================\n\
Line: %d\n",
                               wdbp->parser_con.pg.rope_seq,
                               wdbp->rec_cnt);
/*
 * Process the message and output a response.
 */          
        if (do_web_request(&wdbp->msg, wdbp, sess_wdbp) == 0)
            break;
        if (wdbp->debug_level > 2)
        {
            fprintf(stderr, "(Client:%s) ssl_service() loop\n",
                wdbp->parser_con.pg.rope_seq);
        }
    }
    if (wdbp->debug_level > 2)
    {
        fprintf(stderr, "(Client:%s) ssl_service() finished\n",
                wdbp->parser_con.pg.rope_seq);
    }
    if (wdbp->cur_link->connect_fd != -1)
        socket_cleanup(wdbp);
    return;
}
