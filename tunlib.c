/*
 *    tunlib.c - Routines to read and write the streams handled by tundrive.
 *
 *    Copyright (C) E2 Systems 1993, 2000, 2011
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995";
#include "webdrive.h"
#ifndef SD_SEND
#define SD_SEND 1
#endif
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
static int tun_read();
/*
 * Routine to set up a tunnel device on Linux and assign it an IP address.
 *
 * Return the tunnel file descriptor. In addition, set the tunnel file
 * descriptor to the script input file descriptor (if we are a 'client') or
 * the mirror file descriptor if we are the server.
 */
int tun_create(nm, addr, wdbp)
char * nm;
char * addr;
WEBDRIVE_BASE * wdbp;
{
struct ifreq ifr;
int fd;
int sockfd;
 
    if (nm == NULL || nm[0] == '\0')
    {
        (void) fprintf(stderr,
          "(Client:%s) tun_create: name is compulsory\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        return -1;
    }
    strcpy(wdbp->narrative, "tun_create");
/*
 * Get an FD from the clone device
 */
    if ((fd = open("/dev/net/tun" , O_RDWR)) < 0 )
    {
        (void) fprintf(stderr,
          "(Client:%s) tun_create: open() failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        perror("Opening /dev/net/tun");
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN;
    strncpy(ifr.ifr_name, nm, IFNAMSIZ);
    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0 )
    {
        (void) fprintf(stderr,
          "(Client:%s) tun_create: ioctl(TUNSETIFF) failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        perror("ioctl(TUNSETIFF)");
        close(fd);
        return -1;
    }
/*
 * Now give it an IP address
 */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        (void) fprintf(stderr,
          "(Client:%s) tun_create: socket() failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        perror("socket()");
        close(fd);
        return -1;
    }
/*
 * Read interface flags
 */
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0)
    {
        (void) fprintf(stderr,
          "(Client:%s) tun_create: ioctl(SIOCGIFFLAGS) failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        perror("ioctl(SIOCGIFFLAGS)");
        close(sockfd);
        close(fd);
        return -1;
    }
/*
 * If interface is down, bring it up
 */
    if (!(ifr.ifr_flags & IFF_UP))
    {
        if (wdbp->verbosity)
          (void) fprintf(stderr,
          "(Client:%s) tun_create: device(%s) down (flags:%x) ... bringing it up\n",
                 wdbp->parser_con.pg.rope_seq, ifr.ifr_name, ifr.ifr_flags);
        ifr.ifr_flags |= IFF_UP;
        if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0)
        {
            (void) fprintf(stderr,
              "(Client:%s) tun_create: ioctl(SIOCSIFFLAGS) failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
            perror("ioctl(SIOCSIFFLAGS)");
            close(sockfd);
            close(fd);
            return -1;
        }
    }
/*
 * Ready the interface address, converting the input address from whatever form
 * it has been presented in as we do so.
 */
    sock_ready(addr, 0, &ifr.ifr_addr,
                               &(wdbp->root_wdbp->encrypt_mutex));
/*
 * Set the interface address
 */
    if (ioctl(sockfd, SIOCSIFADDR, &ifr) < 0)
    {
        (void) fprintf(stderr,
              "(Client:%s) tun_create: ioctl(SIOCSIFADDR) failed error %d\n",
                 wdbp->parser_con.pg.rope_seq, errno);
        perror("ioctl(SIOCSIFADDR)");
        close(sockfd);
        close(fd);
        return -1;
    }
    close(sockfd);
    if (wdbp->proxy_port == 0)
    {     /* Don't need privileges any longer */
        if (wdbp->non_priv_uid != 0
          &&  setuid(wdbp->non_priv_uid) < 0)
        {
            (void) fputs("Couldn't drop privileges; giving up\n",stderr);
            perror("setuid()");
            exit(1);
        }
        wdbp->parser_con.pg.cur_in_file = (FILE *) fd;
    }
    else
        wdbp->mirror_fd = fd;
    return fd;
}
/*
 * Validate the server URL
 */
int ini_server(wdbp)
WEBDRIVE_BASE * wdbp;
{
    if (wdbp->server_url == NULL || wdbp->server_url[0] == '\0')
        return 0;
    if (url_to_ep(wdbp->server_url, &(wdbp->server_ep), wdbp))
    {
        wdbp->server_ep.ssl_spec_id = 0;
        strcpy(wdbp->server_ep.ssl_spec_ref, "1");
        return 1;
    }
    return 0;
}
/*****************************************************************************
 * - Routines that read or write one of the valid record types
 *   off a FILE.
 *
 * webinrec()
 *   - Sets up the record in a buffer that is passed to it
 *   - Returns 1 if successful
 *
 * weboutrec()
 *   - Fills a buffer with the data that is passed to it
 *   - Returns 1 if successful, 0 if not.
 *********************************************************************
 * webinrec - read a record off the input stream
 *
 * HTTP commands are line-delimited. There may be a content length
 *
 * Return 0
 *
 * IN means that the data is coming from the Tunnel device. We need to add
 * HTTP headers.
 *
 * The physical reading is a simple read on Linux.
 *
 * OUT means that the data is in binary, and we are actually reading it.
 * When we are the client end, there are HTTP headers that we need to strip.
 * When we are the server end, there are no HTTP headers, and we need to add
 * them?
 *
 * The buffer contents are always binary.
 */
int webinrec(f, b, in_out, wdbp)
long int f;
unsigned char * b;
enum direction_id in_out;
WEBDRIVE_BASE * wdbp;
{
unsigned char * x;
int i;
int mess_len;

    if ( b == (unsigned char *) NULL)
    {
        (void) fprintf(stderr,
          "(Client:%s) Logic Error: webinrec() called with NULL parameter(s)\n",
                 wdbp->parser_con.pg.rope_seq);
        return 0;
    }
    strcpy(wdbp->narrative, "webinrec");
    if (wdbp->debug_level > 3 && (in_out == IN || in_out == ORAIN))
        (void) fprintf(stderr,"(Client:%s) webinrec(%x,%s,%d)\n", 
                   wdbp->parser_con.pg.rope_seq, f, b, (int) in_out);
    if (in_out == IN || in_out == ORAIN)
        mess_len = tun_read(f, b, in_out, wdbp);
    else
    {
/*
 * Strip the header
 */
        mess_len = http_read(f, b,  wdbp);
        if (b[9] == '2' && b[10] == '0' && b[11] == '0')
        {
            if ((x = bm_match(wdbp->ehp, b, b+mess_len))
                        != (unsigned char *) NULL)
            {
                mess_len -= ((x + 4) - b);
                memmove(b,x + 4, mess_len);
            }
        }
    }
    return mess_len;
}
/****************************************************************************
 * weboutrec() - write out a record
 ****************************************************************************
 * The input data is always in binary format. If IN, it is written out
 * out in ASCII; if OUT, it is written out in binary.
 */
int weboutrec(fp, b, in_out, mess_len, wdbp)
long int fp;
unsigned char * b;
enum direction_id in_out;
int mess_len;
WEBDRIVE_BASE * wdbp;
{
int buf_len;

    if ( b == (unsigned char *) NULL)
    {
         (void) fprintf(stderr,
"(Client:%s) Logic Error: weboutrec(%x, %x, %d) called with NULL parameter(s)\n",
                   wdbp->parser_con.pg.rope_seq,
                  (unsigned long int) fp,
                  (unsigned long int) b,(unsigned long int) in_out);
        return 0;
    }
    strcpy(wdbp->narrative, "weboutrec");
    if (wdbp->debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) weboutrec(%x,%.*s,%d,%d)\n",
              wdbp->parser_con.pg.rope_seq, fp, mess_len, b, (int) in_out, mess_len);
    if (mess_len > 0)
    {
        if (in_out == OUT)
        {
            if (*b == 'P' || *b == 'G' || *b == 'C')
                buf_len =  smart_write((int) fp, b,mess_len, 1, wdbp);
            else
            {
                tun_write((int) fp, b, b + mess_len, wdbp);
                buf_len = mess_len;
            }
            if (webdrive_base.debug_level > 1)
                 (void) fprintf(stderr,
                       "(Client:%s) Message Length %d Sent with return code: %d\n",
                          wdbp->parser_con.pg.rope_seq,
                          mess_len, buf_len);
            if (wdbp->debug_level > 2)
                (void) weboutrec(stderr, b, ((in_out == ORAOUT) ? ORAIN : IN),
                         mess_len, wdbp);
        }
        else
        {
/*
 * Convert the record from binary, and log it.
 */
            gen_handle((FILE *) fp, b, b + mess_len, 1);
            buf_len = 1;
        }
    }
    if (webdrive_base.debug_level > 2)
        (void) fprintf(stderr, "(Client:%s) weboutrec() File: %x\n",
                        wdbp->parser_con.pg.rope_seq, fp);
    return buf_len;
}
static char * counted_read(f, x, bound)
int f;
unsigned char * x;
unsigned char * bound;
{
int read_cnt;

    if ((bound - x < 1500)         /* MTF check */
     || (read_cnt = read(f, x + 4, bound - x - 4)) <= 0)
        return x;
    *x = (read_cnt & 0xff000000) >> 24;
    *(x + 1) = (read_cnt & 0xff0000) >> 16;
    *(x + 2) = (read_cnt & 0xff00) >> 8;
    *(x + 3) = (read_cnt & 0xff);
    return x + 4 + read_cnt;
}
static char * counted_write(f, x, bound, wdbp)
int f;
unsigned char * x;
unsigned char * bound;
WEBDRIVE_BASE * wdbp;
{
int write_cnt;

    write_cnt = ((*x) << 24)
              +((*(x + 1)) << 16)
              +((*(x + 2)) << 8)
              +(*(x + 3));
    x += 4;
    if (x + write_cnt > bound)
        return bound;
    smart_write(f, x, write_cnt, 0, wdbp);
    return x + write_cnt;
}
/**********************************************************************
 * Read the tunnel device, constructing appropriate HTTP headers
 */
static int tun_read(f, b, in_out, wdbp)
long int f;
unsigned char * b;
enum direction_id in_out;
WEBDRIVE_BASE * wdbp;
{
unsigned char * x;
unsigned char *top = b;
unsigned char *bound = b + WORKSPACE;
char num_buf[13];
int read_cnt;
int mess_len;
int i;
/*
 * The record is to be read off the tunnel device. It has to be given
 * appropriate HTTP headers.
 *
 * There are three cases.
 * -   We are the server; we read whatever, and provide an HTTP response.
 * -   We are the client and there is no proxy; we read whatever, and provide
 *     an HTTP POST.
 * -   We are the client, and there is a proxy; we read whatever, and provide
 *     an HTTP POST with the server URL in the request line.
 */
    strcpy(wdbp->narrative, "tun_read");
    if (wdbp->proxy_port != 0)
    {
        memcpy(b, "HTTP/1.1 200 OK\r\n\
Cache-Control: private, no-cache\r\n\
Content-Type: application/octet-stream\r\n\
Expires: Mon, 05 Sep 2011 12:02:42 GMT\r\n\
Content-Length:      \r\n\r\n", 156);
        top = b + 156;
    }
    else
    if (wdbp->proxy_url != NULL)
        top = b + sprintf(b, "POST %s/ HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Cookie: E2SESSION=%s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
Content-Length:      \r\n\r\n", wdbp->server_url, wdbp->server_ep.address,
                 wdbp->basic_auth, wdbp->root_wdbp->session);
    else
        top = b + sprintf(b, "POST / HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Cookie: E2SESSION=%s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
Content-Length:      \r\n\r\n", wdbp->server_ep.address,
                 wdbp->basic_auth, wdbp->root_wdbp->session);
/*
 * Lock the tunnel device. But don't queue here. We want one thread
 * blocked on each tunnel device, and the others blocked on the network.
 */ 
    if (pthread_mutex_trylock(&(wdbp->root_wdbp->script_mutex)))
    {
        *(top - 5) = '0';
        return (top - b);
    }
/*
 * Issue a blocking read. Return if it doesn't read anything.
 */
    if ((x = counted_read(f, top, bound)) == top)
    {
        pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
        *(x - 5) = '0';
        return (x - b);
    }
    read_cnt = x - top;
    top = x;
/*
 * Make the file-descriptor non-blocking, then read until we have a
 * full buffer load, or there is no more
 */
    i = fcntl(f,F_GETFL,0);
    if (fcntl(f, F_SETFL, i | O_NDELAY) != -1)
    {            /* Set non-blocking */
        while((x = counted_read(f, top, bound)) != top)
        {
            read_cnt += (x - top);
            top = x;
        }
        fcntl(f, F_SETFL, i & ~O_NDELAY);
    }
    pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
    mess_len = top - b;
    i = sprintf(num_buf, "%u", read_cnt);
    memcpy(top - read_cnt - 4 - i, num_buf, i);
    return mess_len;
}
void tun_write(f, b, bound, wdbp)
int f;
unsigned char * b;
unsigned char * bound;
WEBDRIVE_BASE * wdbp;
{
char * x;

    while ((x = counted_write(f,
                 b, bound, wdbp)) < bound)
        b = x;
    return;
}
/*
 * Routine responsible for output to the incoming tun device.
 *
 * The HTTP headers have already been stripped, so we just need to write it.
 */
int tun_forward(b, len, wdbp)
unsigned char * b;
int len;
WEBDRIVE_BASE * wdbp;
{
char * bound = b + len;
char * x;

    strcpy(wdbp->narrative, "tun_forward");
    if (len <= 0 || !memcmp(b,"HTTP",4))
        return 0;
    tun_write((int) wdbp->parser_con.pg.cur_in_file,
                 b, bound, wdbp);
    return len;
}
/*
 * Maintains a back channel
 */
void watchdog_tunnel(wdbp)
WEBDRIVE_BASE * wdbp;
{
char * x;

    strcpy(wdbp->narrative, "watchdog_tunnel");
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) watchdog_tunnel()",
                wdbp->parser_con.pg.rope_seq);
    wdbp->cur_link = &wdbp->link_det[1];
    wdbp->cur_link->to_ep = &(wdbp->server_ep);
    sigrelse(SIGTERM);
    sigrelse(SIGUSR1);
    while (wdbp->go_away == 0)
    {
        wdbp->in_buf.send_receive.record_type = SEND_RECEIVE_TYPE;
        x = (unsigned char *) &(wdbp->msg);
        wdbp->in_buf.send_receive.msg = x;
        if (wdbp->proxy_url != NULL)
            wdbp->in_buf.send_receive.message_len =
                sprintf(x, "POST %s/ HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Cookie: E2SESSION=%s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
Content-Length: 0\r\n\r\n", wdbp->server_url, wdbp->server_ep.address,
                 wdbp->basic_auth, wdbp->root_wdbp->session);
        else
            wdbp->in_buf.send_receive.message_len =
                sprintf(x, "POST / HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Cookie: E2SESSION=%s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
Content-Length: 0\r\n\r\n", wdbp->server_ep.address,
                 wdbp->basic_auth, wdbp->root_wdbp->session);
/*
 * Send the message and receive the response.
 */          
        do_send_receive(&(wdbp->in_buf), wdbp);
    }
    return;
}
void tun_client(wdbp)
WEBDRIVE_BASE * wdbp;
{
char * x;

    strcpy(wdbp->narrative, "tun_client");
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) tun_client()",
                wdbp->parser_con.pg.rope_seq);
    wdbp->cur_link = &wdbp->link_det[1];
    wdbp->cur_link->to_ep = &(wdbp->server_ep);
    sigrelse(SIGTERM);
    sigrelse(SIGUSR1);
    while (wdbp->go_away == 0)
    {
        wdbp->msg.send_receive.record_type = SEND_RECEIVE_TYPE;
        x = (unsigned char *) &(wdbp->in_buf);
        wdbp->msg.send_receive.message_len =
            wdbp->webread(x, wdbp);
        wdbp->msg.send_receive.msg = x;
/*
 * Send the message and receive the response.
 */          
        wdbp->cur_link = &wdbp->link_det[1];
        do_send_receive(&(wdbp->msg), wdbp);
    }
    return;
}
/* ********************************************************************
 * Deal with an incoming request.
 * -  f will be the tunnel.
 * -  the buffer usually used for processing the script will hold it.
 * -  we know that b == &wdbp->in_buf.buf[0]; we may need to extend it.
 * ********************************************************************
 */
int tunnel_read(b, wdbp)
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
/*
 * Our tunnel FD is in script file position.
 */
    return tun_read((int) wdbp->parser_con.pg.cur_in_file, b, IN, wdbp);
}
/*
 * Shouldn't be called!
 */
void syntax_err(fname,ln,text, pcp)
char * fname;
int ln;
char * text;
PARSER_CON * pcp;
{
    fprintf(stderr, "%s\nBizarre. Shouldn't be called at all, never mind from %s:%d.\n",
        text, fname, ln);
    return;
}
/*
 * Obtain the tunnel IP address and create the tunnel device if we're the
 * client
 */
void obtain_tunnel_ip(wdbp)
WEBDRIVE_BASE * wdbp;
{
char * user;
char * passwd;
unsigned char * b = &wdbp->in_buf.buf[0];
unsigned char * top;
int retry_count;
int mess_len;

    strcpy(wdbp->narrative, "obtain_tunnel_ip");
    if ((user = getenv("USER")) == NULL)
         user = "e2vpn";
    if ((passwd = getenv("PASSWORD")) == NULL)
         passwd = "Yeoghe2e2";
    basic_construct(user, passwd, &wdbp->basic_auth[0]);
/*
 * Send a message, get the IP address and session back in a cookie, then
 * create the tunnel device. Assume Basic credentials are necessary.
 */
    if (wdbp->proxy_url != NULL)
        top = b + sprintf(b, "GET %s/ HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
\r\n", wdbp->server_url, wdbp->server_ep.address, wdbp->basic_auth);
    else
        top = b + sprintf(b, "GET / HTTP/1.1\r\n\
Host: %s\r\n\
Authorization: Basic %s\r\n\
Content-Type: application/octet-stream\r\n\
Connection: Keep-Alive\r\n\
Cache-Control: private, no-store, no-cache\r\n\
\r\n", wdbp->server_ep.address, wdbp->basic_auth);
    wdbp->link_det[1].to_ep = &wdbp->server_ep; /* Configured for SSL */
    wdbp->cur_link = &wdbp->link_det[1];
    for (retry_count = 0; retry_count < 5; retry_count++)
    {
        t3drive_connect(wdbp);
        if (wdbp->cur_link->connect_fd != -1)
        {
            if (smart_write(wdbp->cur_link->connect_fd, b, top -b, 1, wdbp)
                 == (top - b))
            {
/*
 * Retrieve the response, extract the IP address and the session
 */
                if ((mess_len = http_read(wdbp->cur_link->connect_fd, b,  wdbp))
                        > 128)
                {
                    if (memcmp(&b[9], "200", 3))
                    {
                        fprintf(stderr,
                           "Failed to connect to remote gateway: %*s\n",
                                  mess_len, b);
                        exit(1);
                    }
                    if ((top = bm_match(wdbp->ehp, b, b+mess_len))
                            != (unsigned char *) NULL)
                    {
                        wdbp->tunnel_ip = (char *) malloc(mess_len -
                            (top - b) - 3);
                        memcpy(wdbp->tunnel_ip,top + 4, mess_len - (top + 4 - b));
                        *(wdbp->tunnel_ip +(mess_len - (top - b) - 4)) = '\0';
                    }
                    fish_things(b, top, wdbp->ebp, wdbp->root_wdbp->session, 49);
                    socket_cleanup(wdbp);
                    break;
                }
            }
        }
    }
    if (retry_count >= 5)
    {
        fprintf(stderr,
             "Failed to connect to remote gateway: %*s\n",
                                  (top -b), b);
        exit(1);
    }
    if (tun_create( wdbp->control_file, wdbp->tunnel_ip, wdbp) < 0)
    {
        fputs("Failed to create tunnel\n", stderr);
        exit(1);
    }
    return;
}
