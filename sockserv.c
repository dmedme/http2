/*
 * sockserv.c - routines that should function unchanged regardless of the
 * particular protocol, for a TCP-based protocol being driven at the network
 * level.
 ******************************************************************************
 * These are the routines that manage the communication end points and links.
 *
 * SSL is automagically available if it is compiled in, although .NET remoting
 * in particular can't make use of it.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
#include "http2.h"
#ifndef MINGW32
extern int h_errno;
#endif
/*****************************************************************************
 * Routines not currently used, intended for a hash implementation of the
 * End Point and Link look-ups.
 */
static unsigned int ep_hash(w, modulo)
unsigned char *w;
int modulo;
{
END_POINT * cur_ep = (END_POINT *) w;

    return string_hh(cur_ep->address,modulo) ^
           long_hh((long) cur_ep->cap_port_id,modulo);
}
static unsigned int link_hash(w, modulo)
unsigned char *w;
int modulo;
{
LINK * cur_link = (LINK *) w;

    return long_hh((long) cur_link->from_ep, modulo)
         ^ long_hh((long) cur_link->to_ep, modulo);
}
static int ep_match(x1, x2)
char * x1;
char * x2;
{
END_POINT * ep1 = (END_POINT *) x1;
END_POINT * ep2 = (END_POINT *) x2;
int i = strcmp(ep1->address, ep1->address);

    if (i)
        return i;
    else
    if ( ep1->cap_port_id == ep2->cap_port_id)
        return 0;
    else if ( ep1->cap_port_id <  ep2->cap_port_id)
        return -1;
    else return 1;
}
static int link_match(x1, x2)
char * x1;
char * x2;
{
LINK * ep1 = (LINK *) x1;
LINK * ep2 = (LINK *) x2;
int i;

    if ((ep1->from_ep ==  ep2->from_ep
          && ep1->to_ep == ep2->to_ep)
         || (ep1->from_ep ==  ep2->to_ep
          && ep1->to_ep == ep2->from_ep))
            return 0;
    else
    if (i = ep_match(ep1->from_ep, ep2->from_ep))
        return i;
    else
        return ep_match(ep1->to_ep, ep2->to_ep);
}
/***************************************************************************
 * Function to proxy a connection.
 * - This is a service routine invoked by be_a_thread().
 * - The incoming socket is on cur_in_file.
 * - cur_link is the connection that is going to be doing the sending and
 *   receiving.
 **************************************************************************
 * For the tunnel, we must set the output file descriptor, and strip the
 * headers.
 **************************************************************************
 */
void proxy_client(wdbp)
WEBDRIVE_BASE * wdbp;
{
char * x;
int which;

    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) proxy_client()",
                       wdbp->parser_con.pg.rope_seq);
/*
 * Loop - read data off the incoming socket, and adjust the HTTP headers.
 */
    strcpy(wdbp->narrative, "proxy_client");
    wdbp->ssl_proxy_flag = 0; /* Initially clear. CONNECT morphs to SSL */
    wdbp->cur_link = &wdbp->link_det[0];
    wdbp->cur_link->connect_fd = (int) wdbp->parser_con.pg.cur_in_file;
    while (wdbp->cur_link->connect_fd != -1
      &&  (wdbp->msg.send_receive.message_len =
            wdbp->webread(&(wdbp->in_buf), wdbp)) > 0)
    {
        if (wdbp->msg.send_receive.message_len > WORKSPACE)
            x = wdbp->overflow_send;
        else
            x = wdbp->in_buf.buf;
        wdbp->msg.send_receive.record_type = SEND_RECEIVE_TYPE;
        wdbp->msg.send_receive.msg = x;
        wdbp->rec_cnt++;
        if (wdbp->debug_level > 2)
            (void) fprintf(stderr,"(Client:%s) Proxy Service Loop\n\
=====================================\n\
Line: %d\n",
                               wdbp->parser_con.pg.rope_seq,
                               wdbp->rec_cnt);
/*
 * Send the message and receive the response.
 */          
        wdbp->cur_link =  &wdbp->link_det[wdbp->root_wdbp->not_ssl_flag];
/*
 * do_send_receive_async() won't work here because it doesn't call
 * proxy_forward(), the routine that returns the answers. So hard code.
 */
        do_send_receive(&wdbp->msg, wdbp);
        wdbp->cur_link =  &wdbp->link_det[wdbp->ssl_proxy_flag];
        if (wdbp->debug_level > 2)
        {
            fprintf(stderr, "(Client:%s) proxy_client() loop\n",
                wdbp->parser_con.pg.rope_seq);
        }
    }
    if (wdbp->debug_level > 2)
    {
        fprintf(stderr, "(Client:%s) proxy_client() finished\n",
                wdbp->parser_con.pg.rope_seq);
    }
#ifdef TUNDRIVE
    if (wdbp->cur_link->connect_fd != -1 && wdbp->cur_link->connect_fd != wdbp->mirror_fd)
#else
    if (wdbp->cur_link->connect_fd != -1)
#endif
    {
        socket_cleanup(wdbp);
        wdbp->cur_link =  &wdbp->link_det[wdbp->root_wdbp->not_ssl_flag];
        socket_cleanup(wdbp);
#ifndef TUNDRIVE
        pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
        add_close(&wdbp->root_wdbp->sc, wdbp->cur_link);
        pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
#endif
    }
    return;
}
/***************************************************************************
 * Function to action a link definition
 */
void do_link(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{

    strcpy(wdbp->narrative, "do_link");
    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_link(%s;%d => %s;%d)\n",
            wdbp->parser_con.pg.rope_seq,
            a->link.from_ep->host,
            a->link.from_ep->port_id,
            a->link.to_ep->host,
            a->link.to_ep->port_id);
        fflush(stderr);
    }
    wdbp->cur_link = link_find(a->link.from_ep, a->link.to_ep, wdbp);
/*
 * See if we have already encountered this link. If we have not done
 * so, initialise it.
 */
    if (wdbp->cur_link->link_id != LINK_TYPE)
    {
/*
 * Needs initialising
 */
        *(wdbp->cur_link) = a->link; 
        if (a->link.from_ep->con_flag == 'C')
        {
#ifdef DOTNET
            t3drive_connect(wdbp);
#else
            wdbp->cur_link->connect_fd = -1;
#endif
            if (wdbp->cur_link->connect_fd == -1)
                return;
        }
/***************************************************************************
 * We are launching a Proxy.
 */
        else
            t3drive_listen(wdbp);
    }
    return;
}
#ifdef USE_SSL
static __inline void safe_ssl_deref(ssl)
SSL * ssl;
{
int ref;

    do
    {
        if ((ref = ssl->references) > 0)
            SSL_free(ssl);
    }
    while (ref > 1);
    return;
}
static __inline void safe_bio_deref(bio)
BIO * bio;
{
int ref;

    do
    {
        if ((ref = bio->references) > 0)
            BIO_free(bio);
    }
    while (ref > 1);
    return;
}
#endif
void link_clear(cur_link, wdbp)
LINK * cur_link;
WEBDRIVE_BASE * wdbp;
{
#ifdef USE_SSL
int ret;
int rete;

    if (cur_link == NULL
     || cur_link->connect_fd == -1)
        return;
    strcpy(wdbp->narrative, "link_clear");
    if (cur_link->ssl_spec_id != -1
     && cur_link->ssl != NULL)
    {
/*
 * SSL_shutdown() can return:
 * -   1 (shutdown complete)
 * -   0 (shutdown sent but no ack has been received)
 * -  -1 (error).
 *
 * If it returns 0, we can optionally call SSL_shutdown again.
 *
 * We would have to do this if we wanted to use the socket for another
 * connection.
 *
 * However, we only call SSL_shutdown() prior to closing the socket, so the
 * second call is unnecessary in our case.
 */
#ifdef DEBUG
        if (!cur_link->ssl->shutdown)
            fputs("Calling SSL_shutdown()\n", stderr);
#endif
        if ((!cur_link->ssl->shutdown)
         && (ret = SSL_shutdown(cur_link->ssl)) < 0)
        {
#ifdef DEBUG
            fputs("Calling SSL_get_error()\n", stderr);
#endif
            switch(rete = SSL_get_error(cur_link->ssl, ret))
            {
            case SSL_ERROR_NONE:
                break; /* Success */
            case SSL_ERROR_SYSCALL:
                if (ret != 0)
                    perror("SSL_shutdown()");
            default:
                fprintf(stderr,
                         "(%s:%d Client:%s) SSL_shutdown failed (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                ret, rete);
                ERR_print_errors(ssl_bio_error);
                break;
            }
        }
#ifdef DEBUG
        fputs("Calling SSL_Free()\n", stderr);
#endif
        safe_ssl_deref(cur_link->ssl);
        cur_link->ssl = NULL;
    }
    if (cur_link->bio != NULL)
    {
        safe_bio_deref(cur_link->bio);
        cur_link->bio = NULL;
    }
#endif
    shutdown(cur_link->connect_fd, 2);
    closesocket(cur_link->connect_fd);
    cur_link->connect_fd = -1;
    if (cur_link->remote_handle != (char *) NULL)
    {
        free(cur_link->remote_handle);
        cur_link->remote_handle = (char *) NULL;
    }
    return;
}
void socket_cleanup(wdbp)
WEBDRIVE_BASE * wdbp;
{
    link_clear(wdbp->cur_link, wdbp);
    return;
}
/***************************************************************************
 * Function to action a socket close definition
 */
void do_close(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
int ret;

    strcpy(wdbp->narrative, "do_close");
    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_close(%s;%d => %s;%d)\n",
            wdbp->parser_con.pg.rope_seq,
            a->link.from_ep->host,
            a->link.from_ep->port_id,
            a->link.to_ep->host,
            a->link.to_ep->port_id);
        fflush(stderr);
    }
    wdbp->cur_link = link_find(a->link.from_ep, a->link.to_ep, wdbp);
/*
 * See if we have already encountered this link. If we have not done
 * so, we have a logic error.
 */
    if (wdbp->cur_link->link_id != LINK_TYPE)
        fprintf(stderr, "(%s:%d Client:%s) Logic Error: closing a non-open connexion\n",
                  __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq);
    else
    {
        if ( wdbp->cur_link->connect_fd != -1
         && wdbp->cur_link->t3_flag != 2)
        {
            if (a->link.from_ep->con_flag == 'C')
            {
                socket_cleanup(wdbp);
                wdbp->cur_link->link_id = CLOSE_TYPE;
                wdbp->cur_link->pair_seq = 0;
            }
            else
            {
#ifdef MINGW32
                WSACleanup();
#endif
                exit(0);
            }
        }
/*
 *      Else do nothing, because streams are single-shot; they are closed
 *      by the send and receive
 */
    }
    return;
}
/***************************************************************************
 * Function to action a Communications End Point declaration
 */
void do_end_point(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
/*
 * Add the end point to the array
 * Go and set up the end-point, depending on what it is.
 */
int ep;

    strcpy(wdbp->narrative, "do_end_point");
    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_end_point(%d, %s;%d (%s;%d))\n",
            wdbp->parser_con.pg.rope_seq,
            a->end_point.end_point_id,
            a->end_point.address,
            a->end_point.cap_port_id,
            a->end_point.host,
            a->end_point.port_id);
        fflush(stderr);
    }
    if ((ep = a->end_point.end_point_id) < 0 || ep > MAXENDPOINTS)
                       /* Ignore if out of range */
        return;
    wdbp->end_point_det[ep] = a->end_point;
    pthread_mutex_init(&(wdbp->end_point_det[ep].rights), NULL);
    wdbp->end_point_det[ep].thread_cnt = 0;
    return;
}
/*
 * Returns when there is definitely incoming, with the mutex taken.
 * On Linux, this hangs if the file descriptor is closed in another thread ...
 */
int wait_for_incoming(f, mutex)
int f;
pthread_mutex_t * mutex;
{
fd_set readymask;
int ret;
struct timeval timeout = {600,0};
struct timeval nowait = {0,0};

    if (f < 0)
        return 0;
    FD_ZERO(&readymask);
    for (;;)
    {
        FD_SET(f, &readymask);
        while ((ret = select(f+1, &readymask, NULL, NULL, &timeout)) <= 0)
        {
            if (ret <= 0)
                return 0;
            FD_SET(f, &readymask);
        }
        pthread_mutex_lock(mutex);
        FD_SET(f, &readymask);
        if ((ret = select(f+1, &readymask, NULL, NULL, &nowait)) <= 0)
        {
            pthread_mutex_unlock(mutex);
            if (ret < 0)
                return 0;
            continue;
        }
        else
            return 1;
    }
}
#ifdef USE_SSL
static int pem_passwd_cb(buf, size, rwflag, userdata)
char *buf;
int size;
int rwflag;
unsigned char *userdata;
{
    strncpy(buf, (char *) userdata, size);
    buf[size - 1]='\0';
    return strlen(buf);
}
static char alpn_args_1[] = { 8, 'h','t','t','p','/','1','.','1'};
static char alpn_args_2[] = { 2, 'h','2', 5,'h','2','-','1','5',5,'h','2','-','1','4'};
static char alpn_args_1_2[] = { 2, 'h','2', 5,'h','2','-','1','5', 5,'h','2','-','1','4',8, 'h','t','t','p','/','1','.','1'};
/***************************************************************************
 * Function to action an SSL Specification
 */
void do_ssl_spec(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
/*
 * Add the end point to the array
 * Go and set up the end-point, depending on what it is.
 */
int i;

#ifdef DEBUG
    fputs("Entering do_ssl_spec()\n", stderr);
#endif
    strcpy(wdbp->narrative, "do_ssl_spec");
    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_ssl_spec(%d, %s,%s,%s))\n",
            (char *) ((wdbp->parser_con.pg.rope_seq != NULL) ?
            (unsigned long)wdbp->parser_con.pg.rope_seq :
            (unsigned long) "Unspecified"),
            a->ssl_spec.ssl_spec_id,
            a->ssl_spec.ssl_spec_ref,
            a->ssl_spec.key_file,
            a->ssl_spec.passwd);
        fflush(stderr);
    }
    for (i = 0; i < wdbp->ssl_spec_cnt; i++)
        if (!strcmp( a->ssl_spec.ssl_spec_ref,
                     wdbp->ssl_specs[i].ssl_spec_ref))
            break;
    if ( (i < wdbp->ssl_spec_cnt && wdbp->ssl_specs[i].ssl_ctx != NULL)
        || i >= MAX_SSL_SPEC)
        return;                /* Ignore if seen before or out of range */
    if ( i >= wdbp->ssl_spec_cnt)
        wdbp->ssl_spec_cnt++;
    a->ssl_spec.ssl_spec_id = i;
    wdbp->ssl_specs[i] = a->ssl_spec;
/*
 * Initialise the SSL Context etc.
    wdbp->ssl_specs[i].ssl_meth = TLSv1_2_method();
 */
    wdbp->ssl_specs[i].ssl_meth = SSLv23_method();
    wdbp->ssl_specs[i].ssl_ctx = SSL_CTX_new(wdbp->ssl_specs[i].ssl_meth);
    SSL_CTX_set_options(wdbp->ssl_specs[i].ssl_ctx,
                                     SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 );
    SSL_CTX_set_default_passwd_cb(wdbp->ssl_specs[i].ssl_ctx, pem_passwd_cb);
    SSL_CTX_set_default_passwd_cb_userdata(wdbp->ssl_specs[i].ssl_ctx,
                                           wdbp->ssl_specs[i].passwd);
/*
 * Needs OpenSSL 1.0.2
 */
    if (wdbp->do_send_receive == do_send_receive)
        SSL_CTX_set_alpn_protos(wdbp->ssl_specs[i].ssl_ctx,
                    alpn_args_1, sizeof(alpn_args_1)); /* Indicate HTTP/1.1 */
    else
/*
 * This tries to force HTTP/2.0. Below we allow the server to choose
 *
 *  if (wdbp->do_send_receive == do_send_receive_async)
 *      SSL_CTX_set_alpn_protos(wdbp->ssl_specs[i].ssl_ctx,
 *                  alpn_args_2, sizeof(alpn_args_2));
 *  else
 */
        SSL_CTX_set_alpn_protos(wdbp->ssl_specs[i].ssl_ctx,
                    alpn_args_1_2, sizeof(alpn_args_1_2)); /* Indicate either */
/*
 * If we invoke the session cache in client (driver) mode, it caches sessions
 * after cycles are completed, which we do not want; each cycle is supposed to
 * represent a fresh user instance, so we will never re-use the cache members.
 * 
 * The default is SERVER, which we do want when we are being a server. But
 * 20,000 of them???
 */
    SSL_CTX_sess_set_cache_size(wdbp->ssl_specs[i].ssl_ctx,
                                           wdbp->client_cnt);
    SSL_CTX_set_session_cache_mode(wdbp->ssl_specs[i].ssl_ctx,
               SSL_SESS_CACHE_NO_AUTO_CLEAR
              |SSL_SESS_CACHE_SERVER
              |SSL_SESS_CACHE_NO_INTERNAL_STORE);
    if (!SSL_CTX_use_certificate_chain_file(wdbp->ssl_specs[i].ssl_ctx,
                                            wdbp->ssl_specs[i].key_file))
    {
        (void) fprintf(stderr,"(Client:%s) cannot access certificates in %s\n",
            (char *) ((wdbp->parser_con.pg.rope_seq != NULL) ?
            (unsigned long)wdbp->parser_con.pg.rope_seq :
            (unsigned long) "Unspecified"),
            a->ssl_spec.key_file);
        ERR_print_errors(ssl_bio_error);
        fflush(stderr);
    }
    if (!SSL_CTX_use_PrivateKey_file(wdbp->ssl_specs[i].ssl_ctx,
                                     wdbp->ssl_specs[i].key_file,
                                     SSL_FILETYPE_PEM))
    {
        (void) fprintf(stderr,"(Client:%s) cannot access private key in %s\n",
            (char *) ((wdbp->parser_con.pg.rope_seq != NULL) ?
            (unsigned long)wdbp->parser_con.pg.rope_seq :
            (unsigned long) "Unspecified"),
            a->ssl_spec.key_file);
        ERR_print_errors(ssl_bio_error);
        fflush(stderr);
    }
    if (!SSL_CTX_load_verify_locations(wdbp->ssl_specs[i].ssl_ctx,
                   NULL, ROOT_CERTS))
    {
        (void) fprintf(stderr,"(Client:%s) cannot read CA List from %s\n",
            (char *) ((wdbp->parser_con.pg.rope_seq != NULL) ?
            (unsigned long)wdbp->parser_con.pg.rope_seq :
            (unsigned long) "Unspecified"),
            a->ssl_spec.key_file);
        ERR_print_errors(ssl_bio_error);
        fflush(stderr);
    }
    SSL_CTX_set_tmp_dh_callback(wdbp->ssl_specs[i].ssl_ctx, group2_dh_callback);
/*
 * We don't want to set a Cipher List, use the defaults
 * 
 *  if (!SSL_CTX_set_cipher_list(wdbp->ssl_specs[i].ssl_ctx, "DEFAULT"))
 *  {
 *      (void) fprintf(stderr,"(Client:%s) cannot set DEFAULT cipher list\n",
 *          wdbp->parser_con.pg.rope_seq);
 *      ERR_print_errors(ssl_bio_error);
 *      fflush(stderr);
 *  }
 *
 * We don't want a session cache either. We will have a single session, reset
 * with cookies etc..
 */
#ifdef DEBUG
    fputs("Leaving do_ssl_spec()\n", stderr);
#endif
    return;
}
#endif
/****************************************************************************
 * Routine to set up a socket address
 */
void sock_ready(host, port, out_sock,  mtx)
char * host;
int port;
struct sockaddr_in * out_sock;
pthread_mutex_t * mtx;
{
struct hostent  *hep;
in_addr_t addr;
int hlen;

    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"sock_ready(%s,%d)\n", host, port);
        (void) fflush(stderr);
    }
    if ((addr = inet_addr(host)) != -1)
        hlen = sizeof(addr);
    else
    {
        pthread_mutex_lock(mtx);
        do
        {
            hep = gethostbyname(host);
        }
        while (hep == (struct hostent *) NULL && h_errno == NULL);
        if (hep != NULL)
        {
            hlen = (hep->h_length < sizeof(addr)) ?
                    hep->h_length : sizeof(addr) ;
            memcpy(&addr, hep->h_addr_list[0], hlen);
            pthread_mutex_unlock(mtx);
        }
        else
        {
            fprintf(stderr,"sock_ready() cannot find host '%s' h_errno=%d?\n",
                       host, h_errno);
            pthread_mutex_unlock(mtx);
            return;
        }
    }
/*
 *    Set up the socket address
 */
    memset(out_sock,0,sizeof(*out_sock));
#ifdef OSF
    out_sock->sin_len = hlen + sizeof(out_sock->sin_port);
#endif
    out_sock->sin_family = AF_INET;
    out_sock->sin_port   = htons((unsigned short) port);
    memcpy((char *) &(out_sock->sin_addr.s_addr),
            (char *) &addr,(sizeof(out_sock->sin_addr.s_addr) < sizeof(addr)) ?
                                 sizeof(out_sock->sin_addr.s_addr) :
                                 sizeof(addr));
    return;
}
void log_sock_bind(fd)
int fd;
{
struct sockaddr_in check;
int len = sizeof(check);

    if (!getsockname(fd,(struct sockaddr *) (&check),&len))
    {
        (void) fprintf(stderr,"Socket %d bound as %x:%d\n",
                                fd, check.sin_addr.s_addr, check.sin_port);
        (void) fflush(stderr);
    }
    else
    { 
        fputs( "getsockname() failed\n", stderr); 
        perror("getsockname() failed"); 
    }
    return;
}
/************************************************************************
 * Find the end point, given the host and port
 */
END_POINT * ep_find(address, cap_port_id, wdbp)
char * address;
int cap_port_id;
WEBDRIVE_BASE * wdbp;
{
END_POINT * cur_ep;

    strcpy(wdbp->narrative, "ep_find");
    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"(Client:%s) ep_find(%s,%d)\n",
                        wdbp->parser_con.pg.rope_seq, address, cap_port_id);
    for (cur_ep = wdbp->end_point_det;
            cur_ep->record_type == END_POINT_TYPE &&
            (strcmp(cur_ep->address,address) ||
            cur_ep->cap_port_id != cap_port_id);
                     cur_ep++);
    if (cur_ep->record_type != END_POINT_TYPE)
        return (END_POINT *) NULL;
    else
        return cur_ep;
}
/************************************************************************
 * Find the link, given the from and to
 */
LINK * link_find(from_ep, to_ep, wdbp)
END_POINT * from_ep;
END_POINT * to_ep;
WEBDRIVE_BASE * wdbp;
{
LINK * cur_link;

    strcpy(wdbp->narrative, "link_find");
    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"(Client:%s) link_find(%s:%d => %s:%d)\n",
                    wdbp->parser_con.pg.rope_seq,
                    from_ep->host,
                    from_ep->port_id,
                    to_ep->host,
                    to_ep->port_id);
/*
 * Make sure that links don't move
 */
    for (cur_link = wdbp->link_det;
            cur_link->link_id != 0
          && cur_link->from_ep != NULL
          && cur_link->to_ep != NULL;
                     cur_link++)
        if ((cur_link->from_ep == from_ep
          && cur_link->to_ep == to_ep)
         || (cur_link->from_ep ==  to_ep
          && cur_link->to_ep == from_ep))
            break;
    return cur_link;
}
#ifdef USE_SSL
void convert_to_ssl(wdbp)
WEBDRIVE_BASE * wdbp;
{
LINK * link = wdbp->cur_link;
int ret;
int rete;
X509 * peer;
unsigned char *proto;
unsigned int proto_len;

    link->ssl_spec_id = link->to_ep->ssl_spec_id;
    if (link->ssl_spec_id != -1)
    {
        if (wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx == NULL)
        {
            fputs("convert_to_ssl():No SSL_CTX!?\n", stderr);
            socket_cleanup(wdbp);
            return;
        }
        if (link->ssl == NULL)
        {
#ifdef DEBUG
            fputs("Calling SSL_new()\n", stderr);
#endif
            if ((link->ssl =
                            SSL_new(wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx))
                               == NULL)
            {
                fputs("No SSL!?\n", stderr);
                socket_cleanup(wdbp);
                return;
            }
        }
/*
 *      SSL_set_tlsext_host_name(link->ssl, link->to_ep->address);
 */
        if (link->bio != NULL)
        {
#ifdef DEBUG
            fputs("Calling BIO_free_all()\n", stderr);
#endif
            BIO_free(link->bio);
        }
#ifdef DEBUG
        fputs("Calling BIO_new_socket()\n", stderr);
#endif
        link->bio = BIO_new_socket( link->connect_fd, BIO_NOCLOSE);
#ifdef DEBUG
        fputs("Calling BIO_set_bio()\n", stderr);
#endif
        SSL_set_bio( link->ssl, link->bio, link->bio);
#ifdef TRY_SESS_REUSE
        if (link->to_ep->ssl_sess != NULL)
        {
#ifdef DEBUG
            fputs("Calling SSL_set_session()\n", stderr);
#endif
  
            if ((ret = SSL_set_session( link->ssl,
                            link->to_ep->ssl_sess)) <= 0)
            {
#ifdef DEBUG
                fputs("Calling SSL_get_error()\n", stderr);
#endif
                switch(rete = SSL_get_error(link->ssl, ret))
                {
                case SSL_ERROR_NONE:
                    break;
                case SSL_ERROR_SYSCALL:
                    if (ret != 0)
                        perror("SSL_set_session()");
                default:
                    fprintf(stderr,
                         "(%s:%d Client:%s) SSL_set_session failed (%d:%d)\n",
                               __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                            ret, rete);
                    ERR_print_errors(ssl_bio_error);
                    break;
                }
/*
 * Double free problem?
#ifdef DEBUG
                fputs("Calling SSL_SESSION_free()\n", stderr);
#endif
                if (link->to_ep->ssl_sess->references > 0)
                    SSL_SESSION_free(link->to_ep->ssl_sess);
                link->to_ep->ssl_sess = NULL;
 */
            }
        }
#endif
#ifdef DEBUG
        fputs("Calling SSL_connect()\n", stderr);
#endif
retry:
        if ((ret = SSL_connect( link->ssl)) <= 0)
        {
#ifdef DEBUG
            fputs("Calling SSL_get_error()\n", stderr);
#endif
            switch(rete = SSL_get_error(link->ssl, ret))
            {
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                goto retry;
            case SSL_ERROR_SYSCALL:
                if (ret != 0)
                    perror("SSL_connect()");
                else
                {
                    (void) fprintf(stderr,
                           "(%s:%d Client:%s) SSL_connect() Bogus EOF?\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq);
                }
            default:
                fprintf(stderr,
                          "(%s:%d Client:%s) SSL_connect failed (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                ret, rete);
                ERR_print_errors(ssl_bio_error);
                safe_ssl_deref(link->ssl);
                link->ssl = NULL;
                socket_cleanup(wdbp);
                return; /* Give up at this point */
            }
        } 
        SSL_get0_alpn_selected(link->ssl, &proto, &proto_len);
        if (proto_len >= 2 && !strncmp(proto, "h2", proto_len))
            link->t3_flag = 2;    /* HTTP/2.0                */
        else
            link->t3_flag = 0;    /* Otherwise we are HTTP/1.1 */
        if (wdbp->verbosity > 1)
        {
            if ((peer = SSL_get_peer_certificate(link->ssl)) != NULL)
            {
                fprintf(stderr,
                    "(%s:%d Client:%s) Peer Certificate Name (%s) ALPN (%.*s)\n",
                      __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                    peer->name, proto_len, proto);
                X509_free(peer);
            }
        }
        if ( link->to_ep->ssl_sess == NULL)
        {
/*
 * This looks to have been misconceived
 *
#ifdef DEBUG
            fputs("Calling SSL_SESSION_new()\n", stderr);
#endif
            link->to_ep->ssl_sess = SSL_SESSION_new();
 */
#ifdef DEBUG
            fputs("Calling SSL_get?_session()\n", stderr);
#endif
/*
 * If the session cache mode isn't 'client' or 'both', we crash (memory shot
 * from under us) unless the call is
 */
            link->to_ep->ssl_sess = SSL_get1_session( link->ssl);
        }
    }
    return;
}
/*
 * Negotiate the SSL handshake
 */
int ssl_serv_accept(wdbp)
WEBDRIVE_BASE * wdbp;
{
LINK * link;
int ret;
int rete;

    strcpy(wdbp->narrative, "ssl_serv_accept");
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) ssl_serv_accept()",
                       wdbp->parser_con.pg.rope_seq);
    link = &wdbp->link_det[wdbp->ssl_proxy_flag];
    wdbp->cur_link = link;
    link->connect_fd = (int) wdbp->parser_con.pg.cur_in_file;
    link->ssl_spec_id = link->to_ep->ssl_spec_id;
    if (wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx == NULL)
    {
        fputs("ssl_serv_accept():No SSL_CTX!?\n", stderr);
        link_clear(link, wdbp);
        return 0;
    }
    if ((link->ssl = SSL_new(wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx))
           == NULL)
    {
        fputs("ssl_serv_accept():No SSL!?\n", stderr);
        link_clear(link, wdbp);
        return 0;
    }
    SSL_set_verify(link->ssl, SSL_VERIFY_NONE, NULL);
    link->bio = BIO_new_socket( link->connect_fd, BIO_NOCLOSE);
    SSL_set_bio( link->ssl, link->bio, link->bio);
/*
 * We cannot set the session for servers, because it is the client that controls
 */
retry:
    if ((ret = SSL_accept( link->ssl)) <= 0)
    {
        switch(rete = SSL_get_error(link->ssl, ret))
        {
        case SSL_ERROR_NONE:
            break;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            goto retry;
        case SSL_ERROR_SYSCALL:
            if (ret != 0)
                perror("SSL_accept()");
        default:
            fprintf(stderr,
                        "(%s:%d Client:%s) SSL_accept failed (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                ret, rete);
            ERR_print_errors(ssl_bio_error);
            link_clear(link, wdbp);
            return 0;
        }
    }
    link->to_ep->ssl_sess = SSL_get0_session(link->ssl);
    if (link->to_ep->ssl_sess == NULL)
    {
        fputs("ssl_serv_accept():No SSL sessions!?\n", stderr);
        link_clear(link, wdbp);
        return 0;
    }
    if (!SSL_session_reused(link->ssl))
    {
    long tm0;
    long tm1;

        tm0 = 0;
        while(!SSL_CTX_add_session( wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx,
                  link->to_ep->ssl_sess))
        {
            if (tm0 == 0)
            {
                tm1 = (long) time(0);
                tm0 = SSL_SESSION_get_timeout(link->to_ep->ssl_sess);
            }
            else
            if (tm0 < tm1)
            {
                tm1 += tm0;
                tm0 = tm1;
            }
            else
                break;
            SSL_CTX_flush_sessions( wdbp->ssl_specs[link->ssl_spec_id].ssl_ctx,
                        tm1);
        }
    }
    if (wdbp->debug_level > 1)
        fprintf(stderr, "(%s:%d Client:%s) SSL_accept session reused? %d\n",
                             __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
              SSL_session_reused(link->ssl));

    return 1;
}
/*
 * Add an SSL Proxy Header. Prevent recursive entry.
 */
void proxy_connect(wdbp)
WEBDRIVE_BASE * wdbp;
{
union all_records * save_bufs;

    strcpy(wdbp->narrative, "proxy_connect");
    wdbp->head_flag = 1;
    if (wdbp->proxy_connect_flag)
        return;
    wdbp->proxy_connect_flag = 1;
    save_bufs = (union all_records *) malloc(sizeof(union all_records)
                                           + sizeof(union all_records) + 16);
    save_bufs[0] = wdbp->in_buf;
    save_bufs[1] = wdbp->msg;
    wdbp->msg.send_receive.msg = wdbp->in_buf.buf;
    wdbp->msg.send_receive.message_len = sprintf(wdbp->in_buf.buf,
       "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\nProxy-Connection: Keep-Alive\r\n\r\n",
       wdbp->cur_link->to_ep->host,
       wdbp->cur_link->to_ep->port_id,
       wdbp->cur_link->to_ep->host,
       wdbp->cur_link->to_ep->port_id);
    socket_cleanup(wdbp);               /* Must start with clean socket */
    wdbp->do_send_receive(&wdbp->msg, wdbp);
    if (wdbp->ret_msg.buf[0] == 'H'
      && (wdbp->ret_msg.buf[9] == '4'
       || wdbp->ret_msg.buf[9] == '5'))
    {
        fprintf(stderr,
            "(%s:%d Client:%s) Lost proxy contact (%.30s)\n",
             __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
            wdbp->ret_msg.buf);
        exit(1);
    }
    wdbp->in_buf = save_bufs[0];
    wdbp->msg = save_bufs[1];
    free(save_bufs);
    wdbp->proxy_connect_flag = 0;
    convert_to_ssl(wdbp);
    return;
}
#endif
/************************************************************************
 * Establish a connexion
 * - Fills in the socket stuff.
 * - Sets up a calling socket if it is allowed to.
 */
void t3drive_connect(wdbp)
WEBDRIVE_BASE * wdbp;
{
LINK * link = wdbp->cur_link;
int ret;
int rete;
/*
 *    Initialise - use input parameters to set up port, and
 *        address of port to connect to
 *       -    Data Definitions
 */
struct protoent *t3drive_prot;
static int t3prot = 6; /* This isn't going to change ... ever ... */
int backoff;
int intlen;
int timeout;
#ifndef WIN32
struct timespec ts, tsrem;
struct timeval tv;

    ts.tv_nsec = 0;
#endif

    strcpy(wdbp->narrative, "t3drive_connect");
    if (link == (LINK *) NULL)
    {
        fputs( "Logic Error: t3drive_connect() called with NULL link\n",
               stderr);
        return;
    }
    if (webdrive_base.debug_level > 1)
        (void) fprintf(stderr,"t3drive_connect(%s;%d => %s;%d)\n",
            link->from_ep->host,
            link->from_ep->port_id,
            link->to_ep->host,
            link->to_ep->port_id);
#ifdef USE_SSL
    if (wdbp->proxy_url != NULL && !wdbp->proxy_connect_flag)
    {
        proxy_connect(wdbp);
        return;
    }
#endif
    if (t3prot == -1)
    {
        t3drive_prot = getprotobyname("tcp");
        if ( t3drive_prot == (struct protoent *) NULL)
        { 
            fputs("Logic Error; no tcp protocol!\n", stderr);
            return;
        }
        t3prot = t3drive_prot->p_proto;
    }
    link->connect_fd = -1;
    wdbp->alert_flag = 0;
    if (wdbp->proxy_url != NULL)
        sock_ready( wdbp->proxy_ep.host, wdbp->proxy_ep.port_id,
                  &(link->connect_sock),
                               &(wdbp->root_wdbp->encrypt_mutex));
    else
        sock_ready(link->to_ep->host, link->to_ep->port_id,
                  &(link->connect_sock),
                               &(wdbp->root_wdbp->encrypt_mutex));
#ifndef TUNDRIVE
    if (wdbp->proxy_port != 0)
    {
        pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
        add_open(&wdbp->root_wdbp->sc, wdbp->cur_link);
        pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
    }
#endif
/*
 *    Now create the socket to output on
 */
    for (backoff = 5; ;backoff++)
    {
#ifdef MINGW32
        if ((link->connect_fd =
              socket(AF_INET,SOCK_STREAM,t3prot)) == -1)
#else
        if ((link->connect_fd =
              socket(AF_INET,SOCK_STREAM,t3prot)) < 0)
#endif
        {
#ifdef MINGW32
        int err1 = WSAGetLastError();
        int err2 = errno;
#endif
            fputs( "Output create failed\n", stderr);
            perror("Output create failed");
            fflush(stderr);
#ifdef WIN32
            sleep(backoff * SLEEP_FACTOR);
#else
            ts.tv_nsec = backoff;
/*            nanosleep (&ts, &tsrem); */
#endif
            if (errno != EINTR || wdbp->alert_flag)
            {
                link->connect_fd = -1;
                return;
            }
            continue;
        }
        else
        {
#ifdef MINW32
            timeout = 10000;
            if (setsockopt(link->connect_fd,SOL_SOCKET,SO_RCVTIMEO,
                       (char *) &timeout, sizeof(int)) < 0)
                fprintf(stderr, "Set Receive Timeout Error: %x\n",
                                 WSAGetLastError());
            intlen = sizeof(timeout);
            timeout = 10000;
            if (setsockopt(link->connect_fd,SOL_SOCKET,SO_SNDTIMEO,
                       (char *) &timeout, sizeof(int)) < 0)
                fprintf(stderr, "Set Send Timeout Error: %x\n",
                                 WSAGetLastError());
            timeout = 0;    /* Make Windows use the application buffer */
            if (setsockopt(link->connect_fd,SOL_SOCKET,SO_SNDBUF,
                       (char *) &timeout, sizeof(int))) < 0
                fprintf(stderr, "Set Use Application Buffer Error: %x\n",
                                 WSAGetLastError());
            timeout = 1;    /* Enable keepalive (so we will die eventually) */
            if (setsockopt(link->connect_fd,SOL_SOCKET,SO_KEEPALIVE,
                       (char *) &timeout, sizeof(int)) < 0)
                fprintf(stderr, "Set Use Application Buffer Error: %x\n",
                                 WSAGetLastError());
#else
/*
 *          tv.tv_sec = 2;
 *          tv.tv_usec = 2;
 *          if (setsockopt(link->connect_fd,SOL_SOCKET,SO_RCVTIMEO,
 *                     (char *) &tv, sizeof(tv)) < 0)
 *              perror("Problem setting receive timeout");
 *          tv.tv_sec = 2;
 *          tv.tv_usec = 2;
 *          if (setsockopt(link->connect_fd,SOL_SOCKET,SO_SNDTIMEO,
 *                     (char *) &tv, sizeof(tv)) < 0)
 *              perror("Problem setting send timeout");
 */
#endif
            timeout = 1;          /* Disable Nagle */
            if (setsockopt(link->connect_fd,t3prot,TCP_NODELAY,
                       (char *) &timeout, sizeof(int)) < 0)
            {
                perror("Disable Nagle failed");
                fflush(stderr);
            }
/*
 * If we need to bind names, use a variation on the following code
 */
            if (bind_host != (char *) NULL)
            {
            int cntr = 30031;
            struct sockaddr_in bind_sock;

                while(cntr > 0)
                {
                    wdbp->bind_port++;
                    if (wdbp->bind_port > 32767)
                        wdbp->bind_port = 1050;
                    sock_ready(bind_host, wdbp->bind_port, &bind_sock,
                               &(wdbp->root_wdbp->encrypt_mutex));
                    if (!bind(link->connect_fd, 
                          (struct sockaddr *) &bind_sock, sizeof(bind_sock)))
                        break;
                    cntr--;
                }
            }
/*
 * Connect to the destination. Leave as blocking
 */
            if (connect(link->connect_fd,
                           (struct sockaddr *) &link->connect_sock,
                               sizeof(link->connect_sock)))
            {
                fputs( "Initial connect() failure\n", stderr);
                perror("connect() failed");
                fflush(stderr);
#ifdef MINGW32
                fprintf(stderr,"connect(): error: %x\n", WSAGetLastError());
                if (WSAGetLastError() == 0)
                     return;          /* See if we get more clues later ... */
#endif
                closesocket(link->connect_fd);
                link->connect_fd = -1;
#ifdef WIN32
                sleep(backoff * SLEEP_FACTOR);
#else
                ts.tv_nsec = backoff;
/*                nanosleep (&ts, &tsrem); */
#endif
                if (errno != EINTR || wdbp->alert_flag)
                {
                    socket_cleanup(wdbp);
                    return;
                }
                continue;
            }
            else
            {
#ifdef USE_SSL
                if (wdbp->proxy_connect_flag)
                    link->ssl_spec_id = wdbp->proxy_ep.ssl_spec_id;
                else
                    link->ssl_spec_id = link->to_ep->ssl_spec_id;
                if (link->ssl_spec_id != -1)
                    convert_to_ssl(wdbp);
#endif
                return;
            }
        }
    }
}
/************************************************************************
 * Listen set up - needed to drive loop-back tests and for single stepping
 *
 * Note that the source is still the from_ep.
 */
int listen_setup(host, port, sockp)
char * host;
int port;
struct sockaddr_in *sockp;
{
struct protoent *t3drive_prot;
/*
 * This will never ever change ...
 */
static int t3prot = 6;
unsigned int flag = 1;
int listen_fd;

    if (webdrive_base.debug_level > 1)
        (void) fprintf(stderr,"listen_setup(%s,%d)\n", host,port);
    if (t3prot == -1)
    {
        t3drive_prot=getprotobyname("tcp");
        if ( t3drive_prot == (struct protoent *) NULL)
        { 
            fputs( "Logic Error; no tcp protocol!\n", stderr);
            return;
        }
        t3prot = t3drive_prot->p_proto;
    }
/*
 *    Construct the Socket Address
 */
    sock_ready(host, port, sockp, &(webdrive_base.encrypt_mutex));
    if (bind_host == (char *) NULL)
        sockp->sin_addr.s_addr = INADDR_ANY;
    else
        sockp->sin_addr.s_addr = inet_addr(bind_host);
/*
 *    Now create the socket to listen on
 */
    if ((listen_fd=
         socket(AF_INET,SOCK_STREAM,t3prot))<0)
    { 
        fputs( "Listen socket create failed\n", stderr);
        perror("Listen socket create failed"); 
    }
/*
 * Bind its name to it
 */
    if (bind(listen_fd,(struct sockaddr *) (sockp),sizeof(*sockp)))
    { 
        fputs( "Listen bind failed\n", stderr);
        perror("Listen bind failed"); 
    }
    else
    if (webdrive_base.debug_level > 1)
        log_sock_bind(listen_fd);
/*
 * Make sure we can reuse the address
 */
    if (setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,
                       (char *) &flag, sizeof(int)) < 0)
    {
        fprintf(stderr, "Set Address Re-use Error:%d\n", errno);
        perror("setsockopt()");
    }
#ifndef MINGW32
    fcntl(listen_fd, F_SETFD, FD_CLOEXEC);
#endif
/*
 *    Declare it ready to accept calls
 */
    if (listen(listen_fd, MAXLINKS))
    { 
        fputs( "Listen() failed\n", stderr);
        perror("listen() failed"); 
        fflush(stderr);
    }
    return listen_fd;
}
/************************************************************************
 * Listen set up - used to implement a Proxy for script capture.
 * -  We do not fork for the accept. Instead, we need to schedule a thread
 *    which:
 *    -   Runs along with any other threads
 *    -   Reads its 'script' off the socket instead of the input file
 *    -   Handles SSL if appropriate
 *    -   Exits when the socket is closed
 */
void t3drive_listen(wdbp)
WEBDRIVE_BASE * wdbp;
{
LINK * link = wdbp->cur_link; /* So we are not affected by the SSL toggle */
static char nseq[10];
struct protoent *t3drive_prot;
struct sockaddr_in listen_sock;
unsigned int adlen;
int listen_fd;

    strcpy(wdbp->narrative, "t3drive_listen");
    if (webdrive_base.debug_level > 1)
        (void) fprintf(stderr,"t3drive_listen(%s,%d)\n",
          link->from_ep->host,link->from_ep->port_id);
#ifdef SCRIPT_LISTEN
    listen_fd = listen_setup( link->from_ep->host,link->from_ep->port_id, &listen_sock);
#else
    listen_fd = listen_setup( "127.0.0.1", wdbp->proxy_port, &listen_sock);
#ifndef MINGW32
    if (wdbp->root_wdbp->non_priv_uid != 0
      && setuid(wdbp->root_wdbp->non_priv_uid) < 0)
    {
        (void) fputs("Couldn't drop privileges; giving up\n",stderr);
        perror("setuid()");
        exit(1);
    }
    else
#endif
        wdbp->root_wdbp->non_priv_uid = 0;
#endif
    sigrelse(SIGTERM);
    sigrelse(SIGUSR1);
    for (adlen = sizeof(link->connect_sock);
            wdbp->go_away == 0;
                adlen = sizeof(link->connect_sock))
    {
        link->connect_fd = accept(listen_fd, (struct sockaddr *)
                          &(link->connect_sock), &adlen);
        if (link->connect_fd < 0)
        {
            if ((errno == EINTR || errno == EAGAIN)
              && !wdbp->alert_flag)
                continue;
            break;
        }
#ifdef SCRIPT_LISTEN
#ifndef MINGW32
        if (fork() == 0)
#endif
        {
/*
 * Child. One shot. There has to be one file per transaction,
 * because the listener never advances past the listening point.
 */
        char buf[128];

            fclose(wdbp->parser_con.pg.cur_in_file);
            (void) sprintf(buf,"%s_%s", wdbp->control_file,
                               wdbp->parser_con.pg.rope_seq);
            close(listen_fd);
            if ((wdbp->parser_con.pg.cur_in_file = fopen(buf,"rb"))
                   == (FILE *) NULL)
            {
                fprintf(stderr, "No script %s available!\n", buf); 
#ifdef MINGW32
                WSACleanup();
#endif
                exit(1);
            }
            (void) sprintf(buf, "%s_%s", wdbp->parser_con.pg.logfile,
                               wdbp->parser_con.pg.rope_seq);
            wdbp->parser_con.pg.fo = fopen(buf,"wb");
/*
 * Record the other end for possible future reference
 */ 
            sock_ready(link->to_ep->host,link->to_ep->port_id,
                  &(link->connect_sock),
                               &(wdbp->root_wdbp->encrypt_mutex));
            return;
        }
/*
 * Give the next child a new rope number
 */
        sprintf(nseq, "%d", atoi(wdbp->parser_con.pg.rope_seq)+1);
        wdbp->parser_con.pg.rope_seq = nseq;
#else
        if (find_a_slot(wdbp, link->connect_fd))      /* Handle proxy */
            (void) fprintf(stderr,"Successfully processing fd %d\n",
                              link->connect_fd);
        else
        {
            (void) fprintf(stderr,"Failed to find a slot to process fd %d\n",
                              link->connect_fd);
            socket_cleanup(wdbp);
        }
#endif
    }
    perror("accept() failed or exit requested"); 
#ifdef MINGW32
    WSACleanup();
#endif
    exit(1);
}
/*
 * Function to get known incoming. The link_flag says whether or not to
 * consult the current_link for SSL possibilities.
 */
int smart_read(f,buf,len, link_flag, wdbp)
int f;
char * buf;
int len;
int link_flag;
WEBDRIVE_BASE * wdbp;
{
int so_far = 0;
int r;
int rete;
int loop_detect = 0;

#ifdef DEBUG
    fprintf(stderr, "(Client:%s) smart_read(%d, %x, %d, %x) ssl_spec_id=%d\n",
                  wdbp->parser_con.pg.rope_seq, f, (long) buf, len,
                     (long) wdbp, wdbp->cur_link->ssl_spec_id);
    fflush(stderr);
#endif
    strcpy(wdbp->narrative, "smart_read");
    do
    {
        if ((wdbp->alert_flag && wdbp != &webdrive_base)
      || (link_flag && ((
#ifdef USE_SSL
         (wdbp->cur_link->ssl_spec_id == -1) && 
#endif
              f < 0)))
      || (!link_flag && f < 0))
            r = -1;
        else
#ifdef USE_SSL
        if (link_flag && (wdbp->cur_link->ssl_spec_id != -1))
        {
#ifdef DEBUG
            fputs("Calling SSL_read()\n", stderr);
#endif
            if (wdbp->cur_link->ssl == NULL)
            {
                (void) fprintf(stderr,
                     "(%s:%d Client:%s) SSL_read() but no ssl structure!?\n",
                          __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq);
                return -1;
            }
            r = SSL_read(wdbp->cur_link->ssl,
                     buf, len);
#ifdef DEBUG
            fputs("Calling SSL_get_error()\n", stderr);
#endif
            switch(rete = SSL_get_error(wdbp->cur_link->ssl, r))
            {
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                continue;
            case SSL_ERROR_ZERO_RETURN:
                r = 0;
                break;
            case SSL_ERROR_SYSCALL:
                if (r != 0)
                    perror("SSL_read()");
                else
                {
                    (void) fprintf(stderr,
                           "(%s:%d Client:%s) SSL_read() failure (%d:%d) Bogus EOF?\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, rete);
                    safe_ssl_deref(wdbp->cur_link->ssl);
                    wdbp->cur_link->ssl = NULL;
                    socket_cleanup(wdbp);
                    return -1;
                }
            default:
                if (wdbp->debug_level > 1)
               (void) fprintf(stderr,
                           "(%s:%d Client:%s) SSL_read() failure (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, rete);
                ERR_print_errors(ssl_bio_error);
                fflush(stderr);
                safe_ssl_deref(wdbp->cur_link);
                wdbp->cur_link->ssl = NULL;
                r = -1;
            }
        }
        else
#endif
        {
#ifdef TUNDRIVE
            r = read(f, buf, len);
#else
            r = recvfrom(f, buf, len, 0,0,0);
#endif
        }
        if (r == 0)
            return so_far;
        else
        if (r < 0)
        {
            loop_detect++;
#ifndef MINGW32
            if (errno == EINTR && loop_detect < 100 && !wdbp->alert_flag)
            {
#ifdef DEBUG
                fprintf(stderr, "(Client:%s) smart_read() loop=%d\n",
                  wdbp->parser_con.pg.rope_seq, loop_detect);
                fflush(stderr);
#endif
                continue;
            }
#endif
#ifdef DEBUG
            fprintf(stderr, "(Client:%s) smart_read() =%d failed errno=%d\n",
                  wdbp->parser_con.pg.rope_seq, r, errno);
            fflush(stderr);
#endif
            if (so_far > 0)
                return so_far;
            return r;
        }
        else
            loop_detect = 0;
        so_far += r;
        len -= r;
        buf += r;
#ifdef DEBUG
        fprintf(stderr, "(Client:%s) smart_read() so_far=%d len=%d\n",
                  wdbp->parser_con.pg.rope_seq, so_far, len);
        fflush(stderr);
#endif
    }
    while (len > 0);
    if (wdbp->debug_level > 3 && so_far > 0)
    {
        fprintf(stderr, "smart_read(%d, %x, %d, %d, %x) = %d\n",
                 f, (long) buf,  len,
                     link_flag, (long) wdbp, so_far);
        (void) gen_handle(stderr,buf - so_far, buf, 1);
        fputc('\n', stderr);
        fflush(stderr);
    }
    return so_far;
}
/*
 * Function to send if at all possible. The link_flag says whether or not to
 * consult the current_link for SSL possibilities.
 */
int smart_write(f,buf,len, link_flag, wdbp)
int f;
char * buf;
int len;
int link_flag;
WEBDRIVE_BASE * wdbp;
{
int so_far = 0;
int r;
int rete;
int loop_detect = 0;

    strcpy(wdbp->narrative, "smart_write");
#ifdef DEBUG
    fprintf(stderr, "smart_write(%d, %x, %d, %d, %x) called\n",
                      f, (long) buf, len, link_flag, (long) wdbp);
    fflush(stderr);
#endif
    do
    {
        if ((wdbp->alert_flag && wdbp != &webdrive_base)
         || (link_flag && ((
#ifdef USE_SSL
             (wdbp->cur_link->ssl_spec_id == -1) && 
#endif
              f < 0)))
         || (!link_flag && f < 0))
            r = -1;
        else
#ifdef USE_SSL
        if (link_flag && (wdbp->cur_link->ssl_spec_id != -1))
        {
            if (wdbp->cur_link->ssl == NULL)
            {
                (void) fprintf(stderr,
                     "(%s:%d Client:%s) SSL_write() but no ssl structure!?\n",
                          __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq);
                return -1;
            }
#ifdef DEBUG
            fputs("Calling SSL_write()\n", stderr);
#endif
            if (wdbp->debug_level > 2)
                fprintf(stderr, "(Client:%s) SSL smart_write(%d, %x, %d, %x)\n",
                  wdbp->parser_con.pg.rope_seq, f, (long) buf, len,
                     (long) wdbp);
            r = SSL_write(wdbp->cur_link->ssl,
                     buf, len);
#ifdef DEBUG
            fputs("Calling SSL_get_error()\n", stderr);
#endif
            switch(rete = SSL_get_error(wdbp->cur_link->ssl, r))
            {
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_READ:
                continue;
            case SSL_ERROR_ZERO_RETURN:
                r = 0;
                break;
            case SSL_ERROR_SYSCALL:
                if (r != 0)
                    perror("SSL_write()");
                else
                {
                    (void) fprintf(stderr,
                           "(%s:%d Client:%s) SSL_write() failure (%d:%d) Bogus EOF?\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, rete);
                    safe_ssl_deref(wdbp->cur_link->ssl);
                    wdbp->cur_link->ssl = NULL;
                    socket_cleanup(wdbp);
                    return -1;
                }
            default:
                (void) fprintf(stderr,
                          "(%s:%d Client:%s) SSL_write() failure (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, rete);
                ERR_print_errors(ssl_bio_error);
                fflush(stderr);
                safe_ssl_deref(wdbp->cur_link->ssl);
                wdbp->cur_link->ssl = NULL;
                socket_cleanup(wdbp);
                return -1;
            }
        }
        else
#endif
        {
            if (wdbp->debug_level > 2)
                fprintf(stderr, "(Client:%s) Non-SSL smart_write(%d, %x, %d, %x)\n",
                  wdbp->parser_con.pg.rope_seq, f, (long) buf, len,
                     (long) wdbp);
#ifdef TUNDRIVE
            r = write(f, buf, len);
#else
            r = sendto(f, buf, len, 0,0,0);
#endif
        }
        if (r <= 0)
        {
            loop_detect++;
#ifndef MINGW32
            if (errno == EINTR && loop_detect < 100 && !wdbp->alert_flag)
                continue;
#endif
            fprintf(stderr, "smart_write(%d, %x, %d, %d, %x) = %d, error:%d\n",
                 f, (long) buf,  len,
                     link_flag, (long) wdbp, r, errno);
            fflush(stderr);
            return r;
        }
        else
            loop_detect = 0;
#ifdef DEBUG
        if (wdbp->debug_level > 3 && r > 0)
        {
            fprintf(stderr, "smart_write(%d, %x, %d, %d, %x) = %d\n",
                 f, (long) buf,  len,
                     link_flag, (long) wdbp, r);
            (void) gen_handle(stderr,buf,buf + r,1);
            fputc('\n', stderr);
            fflush(stderr);
        }
#endif
        so_far += r;
        len -= r;
        buf+=r;
    }
    while (len > 0);
    return so_far;
}
/*
 * Populate an end point from a URL
 */
int url_to_ep(url, ep, wdbp)
char * url;
END_POINT * ep;
WEBDRIVE_BASE * wdbp;
{
char *x;
char *x1;
int len;

    ep->cap_port_id = -1;
    if (!memcmp(url, "http", 4))
    {
        x1 = url + 4;
        if (*x1 == ':')
        {
            ep->cap_port_id = 80;
            x1 += 1 + strspn(x1 + 1, "/");
        }
        else
        if (*x1++ == 's' && *x1++ == ':')
        {
            ep->cap_port_id = 443;
            x1 += strspn(x1, "/");
        }
        else
            x1 = url;
    }
    else
        x1 = url;
    if ((x = strrchr(url, ':')) != NULL)
        ep->cap_port_id = atoi(x + 1);
    else
        ep->cap_port_id = default_port(url, wdbp);
    if (ep->cap_port_id <= 0)
    {
        (void) fprintf(stderr,
           "(Client:%s) url_to_ep: url %s does not specify a valid port\n",
                 wdbp->parser_con.pg.rope_seq, url);
        return 0;
    }
    if (x == NULL)
        len = strlen(x1);
    else
        len = x - x1;
    memcpy(ep->host, x1, len);
    ep->host[len] = '\0';
    ep->port_id = ep->cap_port_id;
    memcpy(ep->address, x1, len);
    sprintf(&ep->address[len], ":%d", ep->port_id);
    ep->record_type = END_POINT_TYPE;
    ep->proto_flag = 0;
    ep->con_flag = 'L';
#ifdef USE_SSL
    ep->ssl_spec_id = -1;
    ep->ssl_spec_ref[0] = '\0';
#endif
    return 1;
}
/*
 * Set up stuff for a proxy that only needs to be done once.
 */
int ini_prox(wdbp)
WEBDRIVE_BASE * wdbp;
{
char *x;
char *x1;
int len;
/*
 * Do nothing and return success if no proxy has been provided.
 */
    if (wdbp->proxy_url == NULL || wdbp->proxy_url[0] == '\0')
        return 1;
/*
 * The proxy must specify a port
 */
    if (!url_to_ep(wdbp->proxy_url, &(wdbp->proxy_ep), wdbp))
        return 0;
    wdbp->proxy_link.to_ep = &wdbp->proxy_ep;
    sock_ready( wdbp->proxy_ep.host, wdbp->proxy_ep.port_id,
        & wdbp->proxy_link.connect_sock,
                               &(wdbp->root_wdbp->encrypt_mutex));
    return 1;
}
