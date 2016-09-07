/***************************************************************************
 * Read a common-or-garden HTTP request or response
 * - Cleaned up implementation
 * - No more 500 line functions
 */
#include "webdrive.h"
static int work_out_close();
#ifndef MINGW32
extern int h_errno;
#endif
static void reject_self();
/*
 * Function to pick up a pre-read header from tbuf
 */
#ifdef SSLSERV
/*
 * At a future point, we will use the pipe_buf message to pass this data in,
 * but since the code that is here works, apart from the race condition issues,
 * in the interests of speed of development, we are just using it to
 * synchronise and overcome the race condition at the moment.
 */
int catch_hand_off(b, wdbp)
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
union all_records * arp = (union all_records *) wdbp->parser_con.tbuf;

    pipe_buf_take(wdbp->pbp, NULL, NULL, NULL, 1); /* Wait for notification */
    wdbp->parser_con.break_flag = 0;
    strcpy(wdbp->narrative, "catch_hand_off");
    if (wdbp->debug_level>2)
    {
            (void) fprintf(stderr,
               "(Client:%s) Caught hand off (len=%u) (%s) to (%lx)\n",
                      wdbp->parser_con.pg.rope_seq,
                     arp->send_receive.message_len,
       (arp->send_receive.message_len > WORKSPACE) ? "Corrupted" :
                      arp->send_receive.msg->buf,
                      (unsigned long) wdbp);
    }
    if (arp->send_receive.message_len > WORKSPACE)
    {
        fprintf(stderr,"(Client:%s) Input data too big: %d\n\
arp = %lx tbuf = %lx arp sample: ",
                      wdbp->parser_con.pg.rope_seq,
                      arp->send_receive.message_len,
                      (unsigned long) arp,
                      (unsigned long) wdbp->parser_con.tbuf);
        gen_handle(stderr, (unsigned char *) arp, ((unsigned char *) arp) + 512,
                       1);
        return -1;
    }
    memcpy(b, arp->send_receive.msg->buf,
              arp->send_receive.message_len);
    return arp->send_receive.message_len;
}
#endif
/*
 * Function that reads up to the end of the header. Request or Response
 */
int http_head_read(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
unsigned char *top = b;
unsigned char * contp;
unsigned char * ncrp;
int ret;
int i;
int mess_len = 0;
enum tok_id tok_id;
/*
 * Standard HTTP
 * =============
 * Read off the socket until we have come to the end of the HTTP header
 * (which is a blank line, that is \r\n\r\n).
 *
 * It is assumed that if the head_start element has been initialised, 
 * b = hp->head_start.element +  hp->head_start.len
 *
 * That is, we are being called with a buffer with some data in it. This may
 * happen if we get HTTP 100 Continue responses.
 */ 
    strcpy(wdbp->narrative, "http_head_read");
    if (hp->head_start.len == 0)
        hp->head_start.element = b;
/*
 * Start by reading (the rest of) the header in its entirety.
 *
 * Return -1 on network error
 */
    for (top = b,
         ncrp = hp->head_start.element,
         mess_len = 8192;
            mess_len > 0;)
    {
/*
 * Pick up a piece off the network
 */
#ifdef USE_SSL
#ifdef SSLSERV
        if (wdbp->parser_con.break_flag == 1)
        {
            i = catch_hand_off(b, wdbp);
            top = b;
            f = wdbp->cur_link->connect_fd;
        }
        else
#endif
        if (wdbp->cur_link->ssl_spec_id != -1)
        {
            i = SSL_read(wdbp->cur_link->ssl,
                     top, mess_len);
            switch(ret = SSL_get_error(wdbp->cur_link->ssl, i))
            {
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                continue;
            case SSL_ERROR_ZERO_RETURN:
                i = 0;
                break;
            case SSL_ERROR_SYSCALL:
                if (i != 0)
                    perror("SSL_read()");
                else
                {
                    (void) fprintf(stderr,
                           "(%s:%d Client:%s) SSL_read() failure (%d:%d) Bogus EOF?\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                i, ret);
                    socket_cleanup(wdbp);
                    return -1;
                }
            default:
                if (wdbp->debug_level > 1)
                    (void) fprintf(stderr,
                             "(%s:%d Client:%s) SSL_read() failure (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                i, ret);
                ERR_print_errors(ssl_bio_error);
                fflush(stderr);
                i = -1;
            }
        }
        else
#endif
            i = recvfrom((int) f, top,
                            mess_len, 0,0,0);
        if (wdbp->debug_level > 2)
        {
            fprintf(stderr,"(Client:%s) HTTP recvfrom(): %d\n",
                      wdbp->parser_con.pg.rope_seq, i);
            if (i > 0)
                asc_handle(stderr, top, top + i, 1);
        }
        if (i <= 0)
        {
#ifdef SSLSERV
            if (wdbp->parser_con.break_flag == 1)
            {
                i = catch_hand_off(b, wdbp);
                top = b;
            }
            else
#endif
            if ((errno != EINTR && errno != EAGAIN && i < 0)
              || (i == 0 && top == b)
              || wdbp->alert_flag)
                return -1;
            else
            if ( i == 0)
            {   /* Not a valid header. Happens if the input is corrupt */
                hp->head_start.len = 0;
                hp->from_wire.element = b;
                hp->from_wire.len = top - b;
                return 0;
            }
            else
                i = 0;
        }
        top += i;
        mess_len -= i;   /* Limit header to 8 K (more) */
        if ((contp = bm_match(wdbp->ehp, ncrp, top)) != (unsigned char *) NULL)
             break;
        else
            for (ncrp = top - 1;
                     ncrp > b && (*ncrp == '\r' || *ncrp == '\n');
                         ncrp--);
    }
    hp->head_start.len = contp - b + 4;
    hp->from_wire.element = contp + 4;
    hp->from_wire.len = top - contp - 4;
#ifdef DEBUG
    fprintf(stderr,"(Client:%s) http_head_read() header: %d body: %d\n",
            wdbp->parser_con.pg.rope_seq, hp->head_start.len,
              hp->from_wire.len);
    asc_handle(stderr, b, top, 1);
#endif
    return 0;
}
/*
 * Function that counts and de-lineates the headers.
 */
int http_head_delineate(b, hp)
unsigned char * b;
struct http_req_response * hp;
{
unsigned char * ncrp;
unsigned char * crp;
unsigned char * colp;
/*
 * Loop - delineate all the headers.
 */
    hp->status = (*b == 'H' && *(b+1) == 'T' && *(b+2) == 'T' && *(b+3) == 'P')
                 ? 0 : -1;
    for (ncrp = hp->head_start.element, hp->element_cnt = 0;
            ncrp < (hp->head_start.element + hp->head_start.len)
               && ((crp = memchr(ncrp,'\r',
          hp->head_start.len + 1- (ncrp - hp->head_start.element)))  != NULL);
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
        }
        else
        {
            hp->headings[hp->element_cnt].label.len = 0;
            hp->headings[hp->element_cnt].value.element = ncrp;
            hp->headings[hp->element_cnt].value.len = (crp - ncrp);
        }
        hp->element_cnt++;
    }
    return hp->element_cnt;
}
/*
 * Function that reads and pre-processes the HTTP headers.
 *
 * It waits for further data in the event of HTTP 100 responses.
 */
int get_http_head(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
/*
 * Loop - until we have an error or we have seen something that isn't an
 * HTTP 100.
 */
    for (hp->head_start.len = 0;;)
    {
        if (http_head_read(f, b, wdbp, hp) == -1)
            return -1;
        if (http_head_delineate(b, hp) < 0)
        {
            fprintf(stderr,
           "(Client:%s) Too Many HTTP Headers (> 64); correct and recompile\n",
                      wdbp->parser_con.pg.rope_seq);
            asc_handle(stderr, hp->head_start.element,
                      hp->head_start.element +hp->head_start.len, 1);
            return -1;
        }
/*
 * If we haven't seen any elements, return
 */
        if (hp->element_cnt == 0)
            return 0;
/*
 * Response and no HTTP status, report an error and return
 */
        if (hp->element_cnt < 1
         || (hp->status != -1
            && (hp->headings[0].label.len != 0
             || hp->headings[0].value.len < 12
             || strncasecmp(hp->headings[0].value.element, "HTTP/1.", 7))))
        {
            fprintf(stderr,"(Client:%s) HTTP Headers Rubbish:\n",
                      wdbp->parser_con.pg.rope_seq);
            asc_handle(stderr, hp->head_start.element,
                      hp->head_start.element +hp->head_start.len, 1);
            return -1;
        }
        if (hp->status != -1
         && !memcmp(hp->headings[0].value.element + 9, "100", 3))
        {
            memmove(hp->head_start.element, hp->from_wire.element,
                        hp->from_wire.len);
            hp->head_start.len = hp->from_wire.len;
        }
        else
            break;
    }
    return 1;
}
/*
 * Get the chunked length, reading more as necessary, and locate the chunk
 * start.
 */
int get_chunk_length(f, wdbp, ep, csp, bound)
long int f;
WEBDRIVE_BASE * wdbp;
struct element_tracker * ep;
unsigned char ** csp;
unsigned char * bound;
{
unsigned char * cp = *csp;
unsigned char * crp = NULL;
int i;
int r;
int loop_detect = 0;
/*
 * -  First we have to locate a carriage return.
 * -  We read more until we have a carriage return.
 * -  If we are actually pointing at a cr we must repeat.
 * -  The length is found by a hexadecimal scan of the current location.
 * -  The chunk starts after the carriage-return/line feed
 * -  Return 0 if we have an error (it could be a close)
 */
    if (wdbp->debug_level > 3 || webdrive_base.verbosity > 3)
    {
        fprintf(stderr,
                 "(Client:%s) get_chunk_length() ep.len=%d\n",
                  wdbp->parser_con.pg.rope_seq, ep->len);
    }
    do
    {
/*
 * Skip over any carriage returns and line feeds
 */ 
        while((cp < ep->element + ep->len) && (*cp == '\n' || *cp == '\r'))
            cp++;
/*
 * If there aren't any further carriage return/line feeds, read more.
 */ 
        while(cp < bound && ((cp >= ep->element + ep->len)
           || ((crp = memchr(cp, '\r', (ep->element + ep->len) - cp)) == NULL)
           || (memchr(crp, '\n', (ep->element + ep->len) - crp) == NULL)))
        {
            if (((ep->element + ep->len) - cp) > 16)
            {
                fprintf(stderr,
      "(Client:%s) get_chunk_length() chunk_length decode malfunction? (%u)\n\
%.*s",
                  wdbp->parser_con.pg.rope_seq, ep->len, ep->len, ep->element);
                gen_handle(stderr, cp, ep->element + ep->len, 1);
                fflush(stderr);
            }
            do
            {
#ifdef USE_SSL
                if (wdbp->cur_link->ssl_spec_id != -1)
                {
                    r = SSL_read(wdbp->cur_link->ssl,
                           ep->element + ep->len, (bound - ep->element - ep->len));
                    switch(i = SSL_get_error(wdbp->cur_link->ssl, r))
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
                                r, i);
                            socket_cleanup(wdbp);
                            return -1;
                        }
                    default:
                        if (wdbp->debug_level > 1)
                            (void) fprintf(stderr,
                               "(%s:%d Client:%s) SSL_read() failure (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, i);
                        ERR_print_errors(ssl_bio_error);
                        fflush(stderr);
                        r = -1;
                    }
                }
                else
#endif
                    r = recvfrom(f, ep->element + ep->len,
                          (bound - ep->element - ep->len), 0,0,0);
                if (r == 0)
                    return 0;
                else
                if (r < 0)
                {
                    loop_detect++;
#ifndef MINGW32
                if (errno == EINTR && loop_detect < 100 && !wdbp->alert_flag)
                        continue;
#endif
                    return r;
                }
                else
                    loop_detect = 0;
            }
            while (r < 0);
            ep->len += r;
        }
        if ((*cp == '\n' || *cp == '\r')
          && crp != NULL
          && cp >= (ep->element + ep->len))
        {
            fprintf(stderr,
                 "(Client:%s) get_chunk_length() Logic Error:cp =%x, crp=%x ep->element+ep->len=%x r=%d\n",
                  wdbp->parser_con.pg.rope_seq, (long) cp, (long) crp, (long)
                        (ep->element + ep->len), r);
            return 0;
        }
    }
    while (*cp == '\r' || *cp == '\n');
    if (crp == NULL || *(crp + 1) != '\n' || sscanf(cp, "%x", &i) != 1)
    {
        fprintf(stderr, "(Client:%s) chunk size missing?|%.*s|\n",
                      wdbp->parser_con.pg.rope_seq,
           (int) ((ep->element + ep->len) - cp),
                      cp);
        return -1;
    }
/*
 * fprintf(stderr, "chunk details: scanned=%d from=%.*s\n", i, (crp - cp), cp);
 */
    *csp =  crp + 2;
    return i;
}
/*
 * Throw away the indicated data
 */
static void discard_read(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
int bite = WORKSPACE;

    while (hp->read_cnt > 0)
    {
        hp->read_cnt -= bite;
        if (hp->read_cnt < 0)
        {
            bite += hp->read_cnt;
            hp->read_cnt = 0;
        }
        (void) smart_read((int) f,b,bite, 1, wdbp);
        if (wdbp->debug_level > 3)
        {
            fprintf(stderr,
   "(Client:%s) discard_read() from_wire.len=%d\n",
                  wdbp->parser_con.pg.rope_seq, bite);
            gen_handle(stderr, b, b+bite, 1);
        }
    }
    return;
}
/***********************************************************************
 * Chunked data - Still perhaps some problems here.
 ***********************************************************************
 * We use free space beyond the original message to accumulate
 * the output with the chunk lengths removed. We do this regardless of
 * whether the data is compressed or not.
 */
int get_chunked_data(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
unsigned char * base = b;
unsigned char * bound = b + WORKSPACE - 1;
unsigned char * datap = hp->from_wire.element;
unsigned char * reserve = (unsigned char *) &(wdbp->msg.buf[sizeof(struct _send_receive) + sizeof(char *)]);
unsigned char * accum = reserve;
unsigned char * lim = (unsigned char *) &(wdbp->msg.buf[WORKSPACE - 1]);
int reserve_len = lim - reserve;   /* Must be > .5 WORKSPACE */
int chunk_len;
/*
 * Save the header always.
 */
    memcpy(accum, b, datap - b);
    accum +=  (datap - b);
/*
 * Loop - process the data a chunk at a time.
 */
    while ((chunk_len =
              get_chunk_length(f, wdbp, &(hp->from_wire), &datap, bound)) > 0)
    {
        chunk_len += 2; /* For the extra cr/lf at the end */
/*
 * Work out what still needs to be read
 */
        hp->read_cnt = chunk_len -((hp->from_wire.element + hp->from_wire.len) -
                            datap);  /* The already read */
                                     /*  datap should be the chunk start */
        if (wdbp->debug_level > 3 || webdrive_base.verbosity > 3)
        {
            fprintf(stderr,
   "(Client:%s) get_chunked_data() from_wire.len=%d chunk_len=%d read_cnt=%d\n",
                  wdbp->parser_con.pg.rope_seq, hp->from_wire.len, chunk_len,
                                     hp->read_cnt);
            if (wdbp->debug_level > 3)
                gen_handle(stderr, hp->from_wire.element, datap, 1);
            fflush(stderr);
        }
/*
 * If we don't have enough buffer space for the whole chunk, and we don't care
 * about it, just read it as cheaply as possible and discard it.
 */
        if ((datap + hp->read_cnt + 16 > bound)
          && !hp->scan_flag && wdbp->verbosity < 2
          && !wdbp->mirror_bin
          && !wdbp->proxy_port)
            discard_read(f, b, wdbp, hp);
        else
        {
/*
 * Advance over the length
 */
            hp->from_wire.len -= (datap - hp->from_wire.element);
            hp->from_wire.element = datap;
/*
 * If there isn't enough space, but we want it, allocate a suitable buffer
 */
            if ((datap + hp->from_wire.len + hp->read_cnt + 16 > bound)
             && (hp->scan_flag || wdbp->verbosity > 1 || wdbp->mirror_bin
                 || wdbp->proxy_port))
            {
                if (base != b)
                    free(base);
                if ((base = (unsigned char *) malloc(hp->read_cnt + 16
                  + hp->from_wire.len)) == NULL)
                    return -1;
                bound = base + hp->read_cnt + 15 + hp->from_wire.len;
                memcpy(base, datap, hp->from_wire.len);
                datap = base;
                hp->from_wire.element = base;
            }
            if (hp->read_cnt > 0)
            {
                (void) smart_read((int) f,
                               hp->from_wire.element + hp->from_wire.len,
                               hp->read_cnt, 1, wdbp);
                hp->from_wire.len += hp->read_cnt;
                hp->read_cnt = 0;
            }
            if (wdbp->debug_level > 3)
            {
                fprintf(stderr,
                  "(Client:%s) complete chunk ... length %d\n",
                          wdbp->parser_con.pg.rope_seq, 
                           chunk_len);
                gen_handle(stderr, hp->from_wire.element,
                            hp->from_wire.element + hp->from_wire.len, 1);
            }
            if (hp->scan_flag || wdbp->verbosity > 2 || wdbp->mirror_bin
                 || wdbp->proxy_port)
            {
                hp->decoded.len = 0;
/*
 * We may now have to decompress the data stream
 */
                if (hp->gzip_flag)
                {
                    if (wdbp->debug_level > 3)
                    {
                        fprintf(stderr,
                        "(Client:%s) about to inflate\n",
                          wdbp->parser_con.pg.rope_seq);
                        fflush(stderr);
                    }
                    if ((hp->gzip_flag == 1 && inf_block(hp, chunk_len - 2) < 0)
                     || (hp->gzip_flag == 2
                       && brot_block(hp, chunk_len - 2) == BROTLI_RESULT_ERROR))
                        fprintf(stderr,
                  "(Client:%s) decompression of %.*s failed for some reason\n",
                          wdbp->parser_con.pg.rope_seq, chunk_len - 2,
                                             hp->from_wire.element);
                    else
                    if (wdbp->debug_level > 3 || webdrive_base.verbosity > 3)
                    {
                        fprintf(stderr,
                        "(Client:%s) after decompression decoded.len=%d\n",
                          wdbp->parser_con.pg.rope_seq, hp->decoded.len);
                    }
                }
                else
                {
                    hp->decoded.element = datap;
/*                    hp->decoded.len = ( hp->from_wire.len -
                              (datap - hp->from_wire.element) - 2); */
                    hp->decoded.len = chunk_len - 2;
                }
#if ORA9IAS_1|ORA9IAS_2
                if (wdbp->pragma_flag
                 && !wdbp->proxy_port
                 && wdbp->f_enc_dec[1] != (unsigned char *) NULL)
                    block_enc_dec(wdbp->f_enc_dec[1],
                          hp->decoded.element,
                          hp->decoded.element,
                          hp->decoded.len);
#endif
                if (hp->decoded.len > 0)
                {
                    if (lim - accum <= hp->decoded.len)
                    {
                        if (reserve_len < WORKSPACE)
                        {
                            lim = (unsigned char *) malloc(reserve_len + reserve_len
                                 + hp->decoded.len);
                            memcpy(lim,reserve, (accum - reserve));
                        }
                        else
                            lim = (unsigned char *) realloc(reserve,
                                            reserve_len + reserve_len
                                 + hp->decoded.len);
                        accum = lim + (accum - reserve);
                        reserve_len += reserve_len + hp->decoded.len;
                        reserve = lim;
                        lim = reserve + reserve_len; 
                    }
                    memcpy(accum, hp->decoded.element, hp->decoded.len);
                    accum += hp->decoded.len;
                }
            }
        }
/*
 * If we have more blocks in the buffer, advance to the next block
 */
        if (datap + chunk_len < hp->from_wire.element + hp->from_wire.len)
        {
            hp->from_wire.len = ( hp->from_wire.element + hp->from_wire.len) -
                                (datap + chunk_len);
            datap += chunk_len;
            hp->from_wire.element = datap;
        }
        else
        {
/*
 * Reset to the start of the buffer so that we can read the next length
 */
            hp->from_wire.element = base;
            hp->from_wire.len = 0;
            datap =  base;
        }
    }
    if (hp->scan_flag)
    {
        scan_incoming_body(wdbp, reserve, accum);
        if (!wdbp->except_flag)
            scan_incoming_error(wdbp, reserve, accum);
    }
    if (accum - reserve <= WORKSPACE)
    {
        memcpy(b, reserve, (accum - reserve));
        if ( reserve != (unsigned char *) &(wdbp->msg.buf[sizeof(struct _send_receive) + sizeof(char *)]))
            free(reserve);
    }
    else
        wdbp->overflow_receive = reserve; /* reserve must be malloc()ed */
    if (base != b)
        free(base);
    return (accum - reserve);
}
/*
 * Read a content-length of data
 */
int get_known_data(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
unsigned char * base = b;
unsigned char * bound = b + WORKSPACE - 1;
unsigned char * datap = hp->from_wire.element;
/*
 * If we don't have enough buffer space for the whole chunk, and we don't care
 * about it, just read it as cheaply as possible and discard it.
 */
    if ((hp->from_wire.element + hp->read_cnt > bound)
     && !hp->scan_flag
     && wdbp->verbosity < 2
     && !wdbp->mirror_bin)
    {
        hp->read_cnt -= hp->from_wire.len;
        discard_read(f, b, wdbp, hp);
        return hp->from_wire.len + hp->head_start.len;
    }
    else
    {
/*
 * If there isn't enough space, but we want it, allocate a suitable buffer
 */
        if ((hp->from_wire.element + hp->read_cnt > bound)
         && (hp->scan_flag || wdbp->verbosity > 1 || wdbp->mirror_bin))
        {
            if ((base = (unsigned char *) malloc(hp->read_cnt
                + hp->head_start.len)) ==NULL)
                return -1;
            bound = base + hp->head_start.len + hp->read_cnt -1;
            memcpy(base, hp->head_start.element, hp->head_start.len);
            memcpy(base + hp->head_start.len, hp->from_wire.element,
                     hp->from_wire.len);
            hp->from_wire.element = base + hp->head_start.len;
        }
        hp->read_cnt -= hp->from_wire.len;
        if (hp->read_cnt > 0)
        {
            (void) smart_read((int) f,
                           hp->from_wire.element + hp->from_wire.len,
                           hp->read_cnt, 1, wdbp);
            hp->from_wire.len += hp->read_cnt;
        }
        if (hp->scan_flag || wdbp->verbosity > 2 || wdbp->mirror_bin)
        {
/*
 * We may now have to decompress the data stream
 */
            if (hp->gzip_flag)
            {
                if ((hp->gzip_flag == 1 && inf_block(hp, hp->from_wire.len) < 0)
                  || (hp->gzip_flag == 2
                       && brot_block(hp, hp->from_wire.len)
                                == BROTLI_RESULT_ERROR))
                    fprintf(stderr,
                  "(Client:%s) decompression of %.*s failed for some reason\n",
                          wdbp->parser_con.pg.rope_seq, hp->from_wire.len,
                                             hp->from_wire.element);
            }
            else
            {
                hp->decoded.element = hp->from_wire.element;
                hp->decoded.len = hp->from_wire.len;
            }
#if ORA9IAS_1|ORA9IAS_2
            if (!wdbp->proxy_port)
            {
                if (wdbp->pragma_flag
                  && wdbp->f_enc_dec[1] != (unsigned char *) NULL)
                    block_enc_dec(wdbp->f_enc_dec[1],
                              hp->decoded.element,
                              hp->decoded.element,
                              hp->decoded.len);
                else
                if (wdbp->gday != (unsigned char *) NULL
                  && wdbp->f_enc_dec[0] == (unsigned char *) NULL
                  && (hp->decoded.len) >= 8
                  && !memcmp(hp->decoded.element, "Mate", 4))
                {
                     wdbp->f_enc_dec[0] = ready_code(wdbp->gday,
                                                     hp->decoded.element + 4);
                     wdbp->f_enc_dec[1] = ready_code(wdbp->gday,
                                                     hp->decoded.element + 4);
                     if (hp->decoded.len > 8)
                         block_enc_dec(wdbp->f_enc_dec[1],
                               hp->decoded.element + 8,
                               hp->decoded.element + 8,
                               hp->decoded.len - 8);
                     if (wdbp->debug_level > 2)
                         fputs("Seen Mate\n", stderr);
                }
            }
#endif
            scan_incoming_body(wdbp, hp->decoded.element,
                                 hp->decoded.element + hp->decoded.len);
            if (!wdbp->except_flag)
                scan_incoming_error(wdbp, hp->decoded.element,
                                 hp->decoded.element + hp->decoded.len);
        }
        return work_out_close(b,wdbp,hp,base);
    }
}
static int work_out_close(b,wdbp,hp,base)
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
unsigned char * base;
{
    if ((wdbp->verbosity > 1 || wdbp->mirror_bin || wdbp->except_flag != 0)
      && hp->decoded.element != hp->from_wire.element
      && hp->decoded.element != NULL)
    {
        if (WORKSPACE > hp->decoded.len + hp->head_start.len)
        {
            if (base != b)
                free(base);
            base = b;
            hp->from_wire.element = b + hp->head_start.len;
            memcpy(hp->from_wire.element,
                             hp->decoded.element, hp->decoded.len);
            hp->from_wire.len = hp->decoded.len;
        }
        else
        {
            if (base != b)
                base = (unsigned char *) realloc(base,
                               hp->head_start.len + hp->decoded.len);
            else
            {
                base = (unsigned char *) malloc(hp->head_start.len
                                              + hp->decoded.len);
                memcpy(base, b, hp->head_start.len);  
            }
            hp->from_wire.element = base + hp->head_start.len;
            memcpy(hp->from_wire.element, hp->decoded.element,
                             hp->decoded.len);
            hp->from_wire.len = hp->decoded.len;
        }
    }
    if (base != b)
        wdbp->overflow_receive = base;
    return hp->from_wire.len + hp->head_start.len;
}
/*
 * Read until EOF
 */
int get_close_data(f, b, wdbp, hp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
struct http_req_response * hp;
{
int loop_detect = 0;
int r;
int rete;
unsigned char * base = b;
unsigned char * bound = b + WORKSPACE;
int next_size = WORKSPACE*2;
/*
 * Close has been indicated, so read until EOF. We should probably use this
 * for HTTP/1.0 (which we don't even check for any more).
 */
    for (;;)
    {
#ifdef USE_SSL
        if (wdbp->cur_link->ssl_spec_id != -1)
        {
            r = SSL_read(wdbp->cur_link->ssl,
                     hp->from_wire.element + hp->from_wire.len, 8192);
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
                      "(%s:%d Client:%s) SSL_read() on close EOF (rete=%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                rete);
                    socket_cleanup(wdbp);
                    r = 0;
                    break;
                }
            default:
                if (wdbp->debug_level > 1)
                    (void) fprintf(stderr,
                             "(%s:%d Client:%s) SSL_read() failure (%d:%d)\n",
                              __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq,
                                r, rete);
                ERR_print_errors(ssl_bio_error);
                fflush(stderr);
                r = -1;
            }
        }
        else
#endif
            r = recvfrom(f, hp->from_wire.element + hp->from_wire.len, 8192, 0,0,0);
        if (r <= 0)
        {
            if (hp->scan_flag || wdbp->mirror_bin)
            {
/*
 * If hp->gzip_flag is set, need to decompress the data
 */
                if (hp->gzip_flag)
                {
                    if ((hp->gzip_flag == 1 && inf_block(hp, hp->from_wire.len) < 0)
                     || (hp->gzip_flag == 2
                       && brot_block(hp, hp->from_wire.len)
                                == BROTLI_RESULT_ERROR))
                        fprintf(stderr,
                  "(Client:%s) decompression of %.*s failed for some reason\n",
                          wdbp->parser_con.pg.rope_seq, hp->from_wire.len,
                                             hp->from_wire.element);
                }
                else
                {
                    hp->decoded.element = hp->from_wire.element;
                    hp->decoded.len = hp->from_wire.len;
                }
                scan_incoming_body(wdbp, hp->decoded.element,
                   hp->decoded.element + hp->decoded.len);
                if (!wdbp->except_flag)
                    scan_incoming_error(wdbp, hp->decoded.element,
                             hp->decoded.element + hp->decoded.len);
            }
            if (r == 0)
                return work_out_close(b,wdbp,hp,base);
            else
            {
                loop_detect++;
#ifndef MINGW32
                if (errno == EINTR && loop_detect < 100 && !wdbp->alert_flag)
                    continue;
#endif
                return work_out_close(b,wdbp,hp,base);
            }
        }
        else
            loop_detect = 0;
#if ORA9IAS_1|ORA9IAS_2
        if (wdbp->pragma_flag
          && !wdbp->proxy_port
          && wdbp->f_enc_dec[1] != (unsigned char *) NULL)
            block_enc_dec(wdbp->f_enc_dec[1], 
               hp->from_wire.element + hp->from_wire.len, 
               hp->from_wire.element + hp->from_wire.len, r);
#endif
        if ((hp->from_wire.element + hp->from_wire.len + r) >= bound - 8192)
        {
            if (next_size == WORKSPACE*2)
            {
                base = (unsigned char *) malloc(next_size);
                memcpy(base, hp->head_start.element, hp->head_start.len);
                memcpy(base + hp->head_start.len, hp->from_wire.element,
                              hp->from_wire.len);
            }
            else
                base = (unsigned char *) realloc(base, next_size);
            bound = base + next_size;
            next_size += next_size;
            hp->from_wire.element = base + hp->head_start.len;
        }
        hp->from_wire.len += r;
    }
/*
 * Should not come here
 */
    if (base != b)
        wdbp->overflow_receive = base;
    return hp->from_wire.len + hp->head_start.len;
}
/*
 * Process the HTTP Response Read
 */
int http_read(f, b, wdbp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
struct http_req_response hr;
int i;
unsigned char * x;
int mess_len;
int seen_zero;

    seen_zero = 0;
    hr.status = 0;
    hr.decoded.element = NULL;
    hr.decoded.len = 0;
    hr.zreserve_len = 0;
    if (get_http_head(f, b, wdbp, &hr) < 0)
        return -1;
/*
 * The headers are now marked out in an array.
 */
    if (hr.element_cnt > 0)
    {
        hr.status = atoi(& hr.headings[0].value.element[9]);
        if ( hr.headings[0].value.element[9] == '4'
         || hr.headings[0].value.element[9] == '5')
             wdbp->except_flag |= (hr.headings[0].value.element[9] == '5') ?
                            E2_SERVER_ERROR : 
                            E2_HTTP_ERROR;  /* Report HTTP 4xx or 5xx errors */
    }
    else
    {
        hr.status = 400;
        wdbp->except_flag |= E2_HTTP_ERROR; 
    }
    reset_progressive(wdbp);
/*
 * Now loop through the headers
 */
    for (hr.read_cnt = 0, hr.scan_flag = 0, hr.gzip_flag = 0, i = 1;
            i < hr.element_cnt;
                i++)
    {
        switch (hr.headings[i].label.len)
        {
        case 8:
            if (!strncasecmp(hr.headings[i].label.element, "Location", 8))
                scan_incoming_body( wdbp, hr.headings[i].value.element,
                    hr.headings[i].value.element + hr.headings[i].value.len);
            break;
        case 10:
            if (!wdbp->proxy_port
             && !strncasecmp(hr.headings[i].label.element, "Set-Cookie", 10))
                cache_cookie( wdbp, hr.headings[i].value.element,
                    hr.headings[i].value.element + hr.headings[i].value.len);
            else
            if (!strncasecmp(hr.headings[i].label.element, "Connection", 10)
              && !strncasecmp(hr.headings[i].value.element, "close", 5))
            {
                if (wdbp->debug_level > 2)
                    fprintf(stderr,"(Client:%s) Seen a close (read_cnt:%d)\n",
                                    wdbp->parser_con.pg.rope_seq, hr.read_cnt);
                if (hr.read_cnt == 0 && seen_zero == 0)
                    hr.read_cnt = -3; /* Chunked has to take precedence
                                         because we may need to decompress it */
            }
            else
            if (!strncasecmp(hr.headings[i].label.element,
                              "X-Compress", 10)
              && !strncasecmp(hr.headings[i].value.element, "yes", 3))
                hr.gzip_flag = 1;
            break;
        case 12:
            if (!strncasecmp(hr.headings[i].label.element,"Content-Type", 12))
            {
                if (!strncasecmp(hr.headings[i].value.element,"text/", 5))
                {
                    x = hr.headings[i].value.element + 5;
                    if ( !strncasecmp(x, "plain", 5))
                    {
                        wdbp->pragma_flag = 0;
                        hr.scan_flag = 1;
                    }
                    else
                    if (!strncasecmp(x, "javascript", 10))
                        hr.scan_flag = 0;
                    else
                    if (!strncasecmp(x, "css", 3))
                        hr.scan_flag = 0;
                    else
                        hr.scan_flag = 1;
                }
                else
                if (!strncasecmp(hr.headings[i].value.element,"image/", 6))
                    hr.scan_flag = 0;
#if ! ORA9IAS_1|ORA9IAS_2
                else
                if (!strncasecmp(hr.headings[i].value.element,"application/", 12))
                {
                    if (!strncasecmp(hr.headings[i].value.element + 12,"json", 4))
                        hr.scan_flag = 1;
                    else
                        hr.scan_flag = 0;
                }
#endif
                else
                    hr.scan_flag = 1;
            }
            break;
        case 14:
            if (!strncasecmp(hr.headings[i].label.element,"Content-Length", 14)
             && hr.read_cnt != -1)
            {
                hr.read_cnt = atoi(hr.headings[i].value.element);
                if (hr.read_cnt == 0)
                    seen_zero = 1;
            }
            break;
        case 16:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Content-Encoding", 16))
            {
                if (!strncasecmp(hr.headings[i].value.element, "gzip", 4)
                 ||!strncasecmp(hr.headings[i].value.element, "deflate", 7))
                    hr.gzip_flag = 1;
                else
                if (!strncasecmp(hr.headings[i].value.element, "br", 2))
                    hr.gzip_flag = 2;
            }
            break;
        case 17:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Transfer-Encoding", 17)
              && !strncasecmp(hr.headings[i].value.element, "chunked", 7))
                hr.read_cnt = -1;
            break;
        default:
/*
 * All the other headers, which at the moment we ignore
 */
            break;
        }
    }
    if (seen_zero)
        return (hr.from_wire.element - b); 
    hr.declen = -1;          /* Flag that compression isn't initialised */
    reset_progressive(wdbp);
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
    mess_len = hr.from_wire.element - b;
/*
 * If this is a HEAD, or a proxy connect, there isn't anything more
 */
    if (wdbp->head_flag)
        wdbp->head_flag = 0;
    else
    if (!wdbp->proxy_connect_flag)
    {
        wdbp->alert_flag = 0; /* No interruptions here */
        if (hr.read_cnt == -1)
/*
 * Chunked data
 */
            mess_len = get_chunked_data(f, b, wdbp, &hr);
        else
        if (hr.read_cnt > 0)
            mess_len = get_known_data(f, b, wdbp, &hr);
        else
        if (hr.read_cnt == -3 || hr.status == 200)
            mess_len = get_close_data(f, b, wdbp, &hr);
        if (hr.gzip_flag
         && (hr.scan_flag || wdbp->verbosity > 2 || wdbp->mirror_bin ))
        {
            if (hr.gzip_flag == 1)
                inf_close(&hr);
            else
            if (hr.gzip_flag == 2)
                brot_close(&hr);
        }
    }
    return mess_len;
}
int is_there_a_host(ln)
struct element_tracker * ln;
{
unsigned char * xp = ln->element;
int left = ln->len;
/*
 * Now delineate the possible host
 */
    for ( xp++, left--; left > 1 && *xp == '/'; left--, xp++);
    if (*xp == '/')
        return 0;
    ln->element = xp;
/*
 * Find the end of the host
 */
    for ( xp++, left--; left > 0 && *xp != '/' && *xp != ' '; left--, xp++);
    ln->len = xp - ln->element;
    return 1;
}
int default_port(host, wdbp)
char * host;
WEBDRIVE_BASE * wdbp;
{
int i;

#ifndef TUNDRIVE
    for (i = 0; i < wdbp->cookie_cnt; i += 2)
    {
        if (!strcmp(wdbp->cookies[i], host))
            return atoi(wdbp->cookies[i+1]);
    }
#ifdef USE_SSL
    if (wdbp->cur_link->ssl_spec_id == -1)
#endif
#endif
        return 80;
#ifndef TUNDRIVE
#ifdef USE_SSL
    else
        return 443;
#endif
#endif
}
/*
 * The element holds a host and optional : port
 */
static int fix_link_to_ep(ep, wdbp)
struct element_tracker * ep;
WEBDRIVE_BASE * wdbp;
{
struct hostent *hep;
in_addr_t num_host;
unsigned char * x;
int i;

    if (memcmp( wdbp->cur_link->to_ep->address,
                    ep->element, ep->len)
         || strlen(wdbp->cur_link->to_ep->address) !=  ep->len)
    {
        if (wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].connect_fd != -1)
        {
            link_clear(&wdbp->link_det[wdbp->root_wdbp->not_ssl_flag], wdbp);
#ifndef TUNDRIVE
            if (wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].to_ep->port_id
                     != wdbp->proxy_port)
            {
                pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
                add_close(&wdbp->root_wdbp->sc,
                    &wdbp->link_det[wdbp->root_wdbp->not_ssl_flag]);
                pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
            }
#endif
        }
        for (i = ep->len, x = ep->element; i > 0; x++, i--)
            if (*x == ':')
                break;
        if (i && !(wdbp->cur_link->to_ep->cap_port_id = atoi(x + 1)))
            i = 0;
        else
        {
            ep->len = x - ep->element;
            wdbp->cur_link->to_ep->port_id = wdbp->cur_link->to_ep->cap_port_id;
        } 
        memcpy(wdbp->cur_link->to_ep->address, ep->element, ep->len);
        wdbp->cur_link->to_ep->address[ep->len] = '\0';
        if ((num_host = inet_addr(wdbp->cur_link->to_ep->address)) != -1)
            strcpy(wdbp->cur_link->to_ep->host, wdbp->cur_link->to_ep->address);
        else
        {
            pthread_mutex_lock(&(wdbp->root_wdbp->encrypt_mutex));
            do
            {
                hep = gethostbyname(wdbp->cur_link->to_ep->address);
            }
            while (hep == (struct hostent *) NULL && h_errno == TRY_AGAIN);
            if (hep != NULL)
            {
                memcpy(&num_host, hep->h_addr_list[0],
                    (hep->h_length < sizeof(num_host)) ?
                    hep->h_length : sizeof(num_host) );
                pthread_mutex_unlock(&(wdbp->root_wdbp->encrypt_mutex));
                e2inet_ntoa_r(num_host, wdbp->parser_con.tlook);
                strcpy(wdbp->cur_link->to_ep->host, wdbp->parser_con.tlook);
            }
            else
            {
                fprintf(stderr,
                     "(Client:%s) Cannot find host '%s', h_errno=%d\n",
                      wdbp->parser_con.pg.rope_seq,
                                    wdbp->cur_link->to_ep->address, h_errno);
                pthread_mutex_unlock(&(wdbp->root_wdbp->encrypt_mutex));
                return 0;
            }
        }
        if (!i)
        {
            wdbp->cur_link->to_ep->cap_port_id =
                    default_port(wdbp->cur_link->to_ep->host, wdbp);
            wdbp->cur_link->to_ep->port_id =
                          wdbp->cur_link->to_ep->cap_port_id;
        }
    }
/*
 * Testing that the host address doesn't re-direct to self, or that incoming
 * connections don't originate with ourselves, would be a lot of code to
 * handle user error. It is much easier to insist that we proxy on a different
 * port to any we might re-direct to.
 */
    if (wdbp->cur_link->to_ep->port_id == wdbp->proxy_port
     /* && !strcmp(wdbp->cur_link->to_ep->host, "127.0.0.1") */ )
    {
        fprintf(stderr,
            "(Client:%s) Packet possibly re-directed to self!?\n",
                      wdbp->parser_con.pg.rope_seq);
        reject_self(wdbp);
        return 0;
    }
    strcpy(wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].to_ep->address,
               wdbp->cur_link->to_ep->address);
    wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].to_ep->cap_port_id =
               wdbp->cur_link->to_ep->cap_port_id;
    strcpy(wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].to_ep->host,
               wdbp->cur_link->to_ep->host);
    wdbp->link_det[wdbp->root_wdbp->not_ssl_flag].to_ep->port_id =
                  (wdbp->root_wdbp->not_ssl_flag == 3) ? 80 :443;
    return 1;
}
/*
 * Redirect a CONNECT tunnelling request to an HTTP URL. This is no longer
 * effective, because of the hard-coded list of websites in the browsers that
 * have designated themselves HTTPS-only.
 */
void redirect_https(fd, hrp, wdbp)
int fd;
struct http_req_response * hrp;
WEBDRIVE_BASE * wdbp;
{
int len;

    strcpy(wdbp->narrative, "redirect_https");
    if (wdbp->debug_level)
        fprintf(stderr,"(Client:%s) attempting re-direct from HTTPS to HTTP\n",
                wdbp->parser_con.pg.rope_seq);
    len = sprintf(wdbp->parser_con.tlook,
    "HTTP/1.1 302 Found\r\nLocation: http://%*s\r\nConnection: close\r\n\r\n",
        hrp->headings[0].label.len-9,
        hrp->headings[0].label.element+9);
    smart_write(fd, wdbp->parser_con.tlook, len, 0,  wdbp);
    return;
}
void ack_mim(fd, wdbp)
int fd;
WEBDRIVE_BASE * wdbp;
{
int len;

    strcpy(wdbp->narrative, "ack_mim");
    if (wdbp->debug_level)
        fprintf(stderr,"(Client:%s) attempting MIM on HTTPS\n",
                wdbp->parser_con.pg.rope_seq);
    smart_write(fd, "HTTP/1.1 200 Connection established\r\n\r\n",
                      39, 0,  wdbp);
    return;
}
/*
 * Redirect a CONNECT tunnelling request to an HTTP URL. This is no longer
 * effective, because of the hard-coded list of websites in the browsers that
 * have designated themselves HTTPS-only.
 */
static void reject_self(wdbp)
WEBDRIVE_BASE * wdbp;
{
int len;
int fd = (int) wdbp->parser_con.pg.cur_in_file;

    strcpy(wdbp->narrative, "reject_self");
    if (wdbp->debug_level)
        fprintf(stderr,"(Client:%s) rejecting attempt to proxy to self\n",
                wdbp->parser_con.pg.rope_seq);
    strcpy(wdbp->parser_con.tlook,
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
    smart_write(fd, wdbp->parser_con.tlook, 47, 0,  wdbp);
    return;
}
/*********************************************************************
 * Deal with an incoming request when we are acting as a Web Proxy.
 * -  The buffer usually used for processing the script will hold the
 *    request.
 * -  We know that b == &wdbp->in_buf.buf[0]; we may need to extend it.
 * ********************************************************************
 * Logical structure is all wrong. This does not work in the same way
 * as webread()!
 * ********************************************************************
 * Try to avoid going in to a loop if a message for the proxy is sent
 * to the proxy port ...
 * ********************************************************************
 */
int proxy_read(b, wdbp)
unsigned char * b;
WEBDRIVE_BASE * wdbp;
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
/*
 * Our accept port is in script file position.
 * The incoming socket is always clear. We need to make sure cur_link is
 * set to the non-SSL on a fresh connection to avoid screw-ups in
 * get_http_head().
 */
    strcpy(wdbp->narrative, "proxy_read");
restart_ssl:
    wdbp->cur_link = &wdbp->link_det[wdbp->ssl_proxy_flag];
    f = wdbp->cur_link->connect_fd;
    if (wdbp->proxy_url != NULL)
    {
        wdbp->link_det[wdbp->ssl_proxy_flag].to_ep = &(wdbp->proxy_ep);
        wdbp->link_det[wdbp->ssl_proxy_flag].connect_sock =
                        wdbp->proxy_link.connect_sock;
    }
    hr.status = -1;
    idle_alarm(wdbp);
    if (wdbp->debug_level > 1)
    {
        fprintf(stderr,
                   "(Client:%s) proxy_read() incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, f);
    }
    if (get_http_head(f, b, wdbp, &hr) <= 0)
    {
        alarm_restore(wdbp);
        socket_cleanup(wdbp);
        if (wdbp->debug_level > 1)
        {
            fprintf(stderr,
               "(Client:%s) proxy_read() saw nothing on incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, f);
        }
        return -1;
    }
    alarm_restore(wdbp);
/*
 * See if there is a :
 */
    if (hr.headings[0].label.len > 0)
    {
        if (!strncasecmp(hr.headings[0].label.element, "CONNECT ", 8))
        {
#ifdef USE_SSL
            ack_mim(f, wdbp);
            wdbp->ssl_proxy_flag = 2;
            if (ssl_serv_accept(wdbp))
                goto restart_ssl;  /* Attempt a Man-In-The-Middle attack ... */
#else
            wdbp->ssl_proxy_flag = 0;
/*
 * Deal with HTTPS requests by redirecting to non-HTTPS ... see what happens.
 * Browsers sensitive to users' security expectations, and appropriately
 * configured SSL accelerators don't allow this, so you can get an 'infinite
 * redirection' error. But the user then has the chance to set SSL mode
 * through the UI and try again.
 */
            redirect_https(f, &hr, wdbp);
            closesocket(f);
            if (wdbp->debug_level > 1)
            {
                fprintf(stderr,
                  "(Client:%s) proxy_read() saw HTTPS connect on incoming fd %d\n",
                   wdbp->parser_con.pg.rope_seq, f);
            }
#endif
            return -1;
        }
        for (i = hr.headings[0].label.len - 1,
                 x = hr.headings[0].label.element + i;
                     i > 0 && *x != ' ';
                         x--, i--);
        hr.headings[0].label.len  = i;
        poss_host = hr.headings[0].value;
        host_flag = is_there_a_host(&poss_host);
    }
    else
    {
        host_flag = 0;
        poss_host.element = "";
        poss_host.len = 0;
    }
/*
 * Now loop through the headers. We need to:
 * -  Record the source IP address, and if necessary construct an end point for it
 * -  Get rid of any protocol and host information on the first line, and reserve
 *    the host information.
 * -  If there is no Host: header, add one
 * -  Add a VIA header: 1.0 localhost:port (E2 Systems Script Capture)
 * -  Put the incoming host in the X-Forwarded-For: header
 * -  Look up the IP address for the host, and if necessary construct an
 *    end point for it.
 * -  Construct a link entry for the two end points
 * -  Assemble the rest of the message
 * -  We will if necessary extend the buffer if it isn't big enough
 */
    if (!host_flag || wdbp->proxy_url != NULL)
    {
        memcpy(wdbp->parser_con.tbuf, hr.headings[0].label.element,
             hr.headings[1].label.element - hr.headings[0].label.element);
        x = wdbp->parser_con.tbuf +
             (hr.headings[1].label.element - hr.headings[0].label.element);
    }
    else
    {
        for (x = wdbp->parser_con.tbuf, y =hr.headings[0].label.element; *y != ' '; x++, y++)
            *x = *y; 
        *x++ = ' ';
        y = poss_host.element + poss_host.len;
        if (*y == ' ')
            *x++ = '/';
        memcpy(x, y, hr.headings[0].value.element + hr.headings[0].value.len + 2 - y);
        x +=  hr.headings[0].value.element + hr.headings[0].value.len + 2 - y;
    }
/*
 * Now loop through the headers, copying across as appropriate.
 * - Because we de-chunk and decompress, we remove these headers
 */
    for (hr.read_cnt = 0, hr.scan_flag = 0, hr.gzip_flag = 0, i = 1, host_seen = 0;
            i < hr.element_cnt - 1;
                i++)
    {
        switch (hr.headings[i].label.len)
        {
        case 4:
            if (!strncasecmp(hr.headings[i].label.element, "Host", 4))
            {
/*
 * Find the IP address to call, and fix the EP.
 */
                host_seen = 1;
                if (!fix_link_to_ep(&hr.headings[i].value, wdbp))
                {
                    fprintf(stderr,
                 "(Client:%s) Lookup Host Failed (%.*s) in \n____\n%.*s\n===\n",
                wdbp->parser_con.pg.rope_seq,
                hr.headings[i].value.len, hr.headings[i].value.element,
                hr.headings[hr.element_cnt - 1].value.element - b, b);
                    return -1;
                }
            }
            break;
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
                              "Content-Encoding", 16))
            {
                if (!strncasecmp(hr.headings[i].value.element, "gzip", 4)
                 ||!strncasecmp(hr.headings[i].value.element, "deflate", 7))
                    hr.gzip_flag = 1;
                else
                if (!strncasecmp(hr.headings[i].value.element, "br", 2))
                    hr.gzip_flag = 2;
            }
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
/*
 * Construct a Host header if necessary
 */
    if (host_seen == 0)
    {
        if (!host_flag)
        {
            fprintf(stderr,
                 "(Client:%s) No Host\n%.*s\n",
                wdbp->parser_con.pg.rope_seq,
                (x - (unsigned char *) wdbp->parser_con.tbuf),
                wdbp->parser_con.tbuf);
            return -1;
        }
        if (!fix_link_to_ep(&poss_host, wdbp))
        {
            fprintf(stderr,
                 "(Client:%s) Cannot Lookup Host (%.*s) for \n____\n%.*s\n===\n",
                wdbp->parser_con.pg.rope_seq,
                poss_host.len, poss_host.element,
                (x - (unsigned char *) wdbp->parser_con.tbuf),
                wdbp->parser_con.tbuf);
            return -1;
        }
        memcpy(x, "Host: ", 6);
        x += 6;
        memcpy(x, poss_host.element, poss_host.len);
        x +=   poss_host.len;
        *x++ = '\r';
        *x++ = '\n';
    }
    hr.declen = -1;          /* Flag that compression isn't initialised */
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
        wdbp->cur_link = &wdbp->link_det[0];
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
        {
            if (hr.gzip_flag == 1)
                inf_close(&hr);
            else
            if (hr.gzip_flag == 2)
                brot_close(&hr);
        }
    }
/*
 * Add the VIA header and the content length
 */
    if (!strncmp(wdbp->parser_con.tbuf, "PUT ", 4)
     || !strncmp(wdbp->parser_con.tbuf, "POST ", 5))
        x += sprintf(x, "Content-Length: %d\r\n",
          (cont_len > hr.head_start.len) ? (cont_len - hr.head_start.len) : 0);
/*
 * Bad things seem to be happening with this.
 *
 *  x += sprintf(x, "VIA: 1.0 localhost:%d (E2 Systems Script Capture)\r\n\r\n",
 *                   wdbp->proxy_port);
 */
    *x++ = '\r';
    *x++ = '\n';
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
        fprintf(stderr, "(Client:%s) proxy_read() saw %d on incoming fd %d\n",
                wdbp->parser_con.pg.rope_seq, mess_len, f);
    }
    return mess_len;
}
int proxy_forward(b, len,  wdbp)
unsigned char * b;
int len;
WEBDRIVE_BASE * wdbp;
{
struct http_req_response hr;
unsigned char * p1;
unsigned char * p2;
unsigned char * p3;
unsigned char * p4;
int i;
#ifndef TUNDRIVE
int edit_flag = 0;
struct script_element * sep;
#endif

    strcpy(wdbp->narrative, "proxy_forward");
    if (len <= 0)
        return 0;
#ifndef TUNDRIVE
    pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
    sep = add_answer(&wdbp->root_wdbp->sc, wdbp->cur_link);
    sep->body = (unsigned char *) malloc(len + 25);
                  /* Allows for additional Content-Length: header ?!? */
    pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
#endif
    wdbp->cur_link = &wdbp->link_det[wdbp->ssl_proxy_flag];
#ifndef TUNDRIVE
    hr.head_start.element = b;
    hr.head_start.len = len;
    if ((p1 = bm_match(wdbp->ehp, b, b + len)) != (unsigned char *) NULL)
    {
        hr.head_start.len = p1 - b + 4;
        hr.from_wire.element = p1 + 4;
        hr.from_wire.len = len - hr.head_start.len;
    }
    if (http_head_delineate(b, &hr) < 0)
    {
        fprintf(stderr,
           "(Client:%s) Too Many HTTP Headers (> 64); correct and recompile\n",
                      wdbp->parser_con.pg.rope_seq);
        asc_handle(stderr, hr.head_start.element,
                      hr.head_start.element + hr.head_start.len, 1);
        return -1;
    }
    if (hr.element_cnt == 0)
#endif
    {
        smart_write((int) wdbp->parser_con.pg.cur_in_file,
              b, len, 1, wdbp);
        if (wdbp->debug_level)
        {
            fprintf(stderr, "(Client:%s) <========\n",
                      wdbp->parser_con.pg.rope_seq);
            gen_handle(stderr, b, b + len, 1);
            fputs("============<\n", stderr);
            fflush(stderr);
        }
#ifndef TUNDRIVE
        memcpy(sep->body, b, len);
        sep->body_len = len;
#endif
        return len;
    }
#ifndef TUNDRIVE
    if (wdbp->debug_level)
        fprintf(stderr, "(Client:%s) <========\n",
                      wdbp->parser_con.pg.rope_seq);
    for (p1 = b, p2 = sep->body, i = 1;
            i < hr.element_cnt - 1;
                i++)
    {
        switch (hr.headings[i].label.len)
        {
#ifdef SORTED_THIS_OUT
/*
 * I did not expect this path to be taken when we are SSL Man-In-The-Middle
 * but it is, so have to disable it for now. Something else odd is happening
 * with the formatting and javascript but that isn't obvious either :(
 */
        case 8:
            if (wdbp->ssl_proxy_flag == 0
             && !strncasecmp(hr.headings[i].label.element, "Location", 8)
             && !strncasecmp(hr.headings[i].value.element, "https:", 6))
            {
                 memcpy(p2, p1, (hr.headings[i].value.element - p1));
                 p2 += (hr.headings[i].value.element - p1);
                 memcpy(p2, "http:", 5);
                 if (wdbp->debug_level)
                     fprintf(stderr, "(Client:%s) https: edited out of Location\n",
                          wdbp->parser_con.pg.rope_seq);
                 p2 += 5;               /* We hide https: */
                 p1 = hr.headings[i].value.element + 6;
            }
            break;
#endif
        case 10:
            if (!strncasecmp(hr.headings[i].label.element, "Connection", 10)
              && !strncasecmp(hr.headings[i].value.element, "close", 5))
            {
                 if (p1 != hr.headings[i].label.element)
                 {
                     memcpy(p2, p1, (hr.headings[i].label.element - p1));
                     p2 += (hr.headings[i].label.element - p1);
                 }
                 p1 = hr.headings[i + 1].label.element;
                 link_clear(&wdbp->link_det[wdbp->not_ssl_flag], wdbp);
            }
            else
            if (!strncasecmp(hr.headings[i].label.element, "Set-Cookie", 10))
            {
/*
 * Need to allow for a variable number of spaces associated with semi-colon
 * sub-element separators
 */
                 while ((p3 = bm_match(wdbp->authsp,
                      (p1 > hr.headings[i].value.element) ? p1 :
                      hr.headings[i].value.element, 
                      hr.headings[i + 1].label.element))
                           != (unsigned char *) NULL)
                 {
                     for (p4 = p3 - 1; p4 > p1 && *p4 == ' '; p4--);
                     if (p4 > p1 && *p4 == ';')
                     {
                         memcpy(p2, p1, p4 - p1);
                         p2 += (p4 - p1);
                     }
                     else
                     {
                         memcpy(p2, p1, (p3 + wdbp->authsp->match_len) - p1);
                         p2 += ((p3 + wdbp->authsp->match_len) - p1);
                     }
                     p1 = p3 + wdbp->authsp->match_len;
                 }
                 memcpy(p2, p1, (hr.headings[i + 1].label.element - p1));
                 p2 += (hr.headings[i + 1].label.element - p1);
                 p1 = hr.headings[i + 1].label.element;
            }
            break;
#ifdef SORTED_THIS_OUT
        case 12:
/*
 * I did not expect this path to be taken when we are SSL Man-In-The-Middle
 * but it is, so have to disable it for now. Something else odd is happening
 * with the formatting and javascript but that isn't obvious either :(
 */
            if (wdbp->ssl_proxy_flag == 0
             && !strncasecmp(hr.headings[i].label.element,"Content-Type", 12)
             && !strncasecmp(hr.headings[i].value.element,"text/", 5))
                edit_flag = 1;
            break;
#endif
        case 14:
            if (!strncasecmp(hr.headings[i].label.element,"Content-Length", 14))
            {
                 if (p1 != hr.headings[i].label.element)
                 {
                     memcpy(p2, p1, (hr.headings[i].label.element - p1));
                     p2 += (hr.headings[i].label.element - p1);
                 }
                 p1 = hr.headings[i + 1].label.element;
            }
            break;
        case 16:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Content-Encoding", 16)
              && (!strncasecmp(hr.headings[i].value.element, "gzip", 4)
               ||!strncasecmp(hr.headings[i].value.element, "deflate", 7)
               ||!strncasecmp(hr.headings[i].value.element, "br", 2)))
            {
                if (p1 != hr.headings[i].label.element)
                {
                    memcpy(p2, p1, (hr.headings[i].label.element - p1));
                    p2 += (hr.headings[i].label.element - p1);
                }
                p1 = hr.headings[i + 1].label.element;
            }
            break;
        case 17:
            if (!strncasecmp(hr.headings[i].label.element,
                              "Transfer-Encoding", 17)
              && !strncasecmp(hr.headings[i].value.element, "chunked", 7))
            {
                 if (p1 != hr.headings[i].label.element)
                 {
                     memcpy(p2, p1, (hr.headings[i].label.element - p1));
                     p2 += (hr.headings[i].label.element - p1);
                 }
                 p1 = hr.headings[i + 1].label.element;
            }
            break;
        case 25:
            if (!strncasecmp(hr.headings[i].label.element,
                   "Strict-Transport-Security", 25))
            {
                 if (p1 != hr.headings[i].label.element)
                 {
                     memcpy(p2, p1, (hr.headings[i].label.element - p1));
                     p2 += (hr.headings[i].label.element - p1);
                 }
                 p1 = hr.headings[i + 1].label.element;
            }
            break;
        default:
/*
 * All the other headers, which at the moment we ignore
 */
            break;
        }
    }
/*
 * Write out the rest of the headers up to the final CR/NL/CR/NL
 */
    if (p1 != hr.headings[i].label.element)
    {
        memcpy(p2, p1, (hr.headings[i].label.element - p1));
        p2 += (hr.headings[i].label.element - p1);
    }
/**********************************************************************************************
 * If we are doing HTTPS, need to edit any https: references.
 * But this means that we only edit HTTPS when the page has been served up via HTTPS. There may
 * be a problem when we switch from one to the other.
 **********************************************************************************************
 * Now need to put in the content-length header.
 */
    p2 += sprintf(p2, "Content-Length: %d\r\n\r\n", hr.from_wire.len);
/*
 * Finally, write out the content.
 */
    if (hr.from_wire.len)
    {
        memcpy(p2, hr.from_wire.element, hr.from_wire.len);
        p2 += hr.from_wire.len;
    }
    sep->body_len = (p2 - sep->body);
#ifdef USE_SSL
    if (edit_flag == 1)
    {
        if (wdbp->debug_level)
            fprintf(stderr, "(Client:%s) Before https: edit\n%.*s",
                          wdbp->parser_con.pg.rope_seq,
                          sep->body_len,
                          sep->body);
        sep->body_len += apply_edits(wdbp, sep->body,
                            sep->body_len);
        p1 = sep->body + sep->body_len;
        adjust_content_length(wdbp, sep->body, &p1);
        sep->body_len = p1 - sep->body;
        if (wdbp->debug_level)
            fprintf(stderr, "(Client:%s) After https: edit\n",
                          wdbp->parser_con.pg.rope_seq);
    }
#endif
    smart_write((int) wdbp->parser_con.pg.cur_in_file,
             sep->body, sep->body_len, 1, wdbp);
    if (wdbp->debug_level)
    {
        gen_handle_no_uni(stderr, sep->body, sep->body + sep->body_len, 1);
        fputs("============<\n", stderr);
        fflush(stderr);
    }
    return sep->body_len;
#endif
}
/*
 * Decompress input.
 * -   The data to be de-compressed is located via http_req_response.from_wire
 * -   We will copy it back afterwards. Usage patterns mean that this should
 *     work (WORKSPACE will be big enough).
 * -   When I started, I had no idea whether or not chunks are individually
 *     compressed or whether the compression context spans chunks. However,
 *     having looked at a sample it looks as though it is the stream rather
 *     than the individual chunks that are compressed.
 */
int inf_open(hp, b)
struct http_req_response * hp;
unsigned char b;
{
/*
 * Allocate inflate state
 */ 
    hp->strm.zalloc = Z_NULL;
    hp->strm.zfree = Z_NULL;
    hp->strm.opaque = Z_NULL;
    hp->strm.avail_in = 0;
    hp->strm.next_in = Z_NULL;
    hp->declen = WORKSPACE;
    if ((hp->decoded.element = (unsigned char *) malloc(WORKSPACE)) == NULL)
    {
        fputs("Out of Memory; bad things are about to happen\n", stderr);
        fflush(stderr);
    }
    hp->decoded.len = 0;
    if ((b & 0xf) == Z_DEFLATED)
        return inflateInit(&hp->strm);
    else
        return inflateInit2(&hp->strm, -MAX_WBITS);
}
/*
 * from_wire.element is the pointer to the data to decompress.
 * from_wire.len is the length to decompress.
 * decoded is where the decompressed data ends up.
 */
int inf_block(hp, to_decompress)
struct http_req_response * hp;
int to_decompress;
{
int ret;
int beg = 0;

    if (hp->declen == -1)
    {
/*
 * If we have a gzip header, skip over it. Note that we are only testing the
 * first byte of the two ID bytes; we are not checking for the presence of
 * optional gzip header elements, nor are we determining that the data when
 * we encounter it is going to be deflate data.
 */ 
        if (hp->zreserve_len + to_decompress <= 10)
        {
            memcpy(&hp->zreserve[hp->zreserve_len], hp->from_wire.element,
                     to_decompress); 
            hp->zreserve_len += to_decompress;
            return 0;
        }
        if ( hp->zreserve_len == 0
          && hp->from_wire.element[0] == 0x1f
          && hp->from_wire.element[1] == 0x8b)
        {
            beg = 10;
            to_decompress -= 10;
        }
        else
        if (hp->zreserve[0] == 0x1f
         && (hp->zreserve_len < 2 || hp->zreserve[1] != 0x8b))
        {
            beg = 10 - hp->zreserve_len;
            to_decompress -=  beg;
        }
        else
        if (hp->zreserve_len != 10)
        {
            fprintf(stderr,
 "Unexpected compressed data prior length: %d to_decompress: %d\nReserve: ",
                    hp->zreserve_len, to_decompress);
             gen_handle(stderr, hp->zreserve, hp->zreserve + hp->zreserve_len,1);
             fputs("\nIncoming:\n", stderr);
             gen_handle(stderr, hp->from_wire.element, hp->from_wire.element + hp->from_wire.len,1);
             fputs("===========\n", stderr);
        }
#ifdef NEED_TO_FIND_HEADER
        else
        {
/*
 * If we have screwed up HTTP2 pad length handling, for instance ...
 */
            for (beg = 1;
                    to_decompress > 0
                 && (hp->from_wire.element[beg] != 0x1f
                  || hp->from_wire.element[beg+1] != 0x8b);
                        beg++, to_decompress--);
            beg += 10;
            to_decompress -= 10;
        }
#endif
        if (to_decompress == 0)
            return 0;
        else
        if (to_decompress < 0)
        {
            hp->decoded.element = NULL; /* Not yet allocated */
            hp->decoded.len = 0;
            return -1;
        }
        inf_open(hp, hp->from_wire.element[beg]);
    }
    hp->strm.next_in = &hp->from_wire.element[beg];
    hp->strm.avail_in = to_decompress;
#ifdef DEBUG
    fprintf(stderr, "Decompress input length: %d\n", to_decompress);
    gen_handle(stderr,hp->strm.next_in,hp->strm.next_in + hp->strm.avail_in,1);
#endif
    hp->strm.avail_out = hp->declen;
    hp->strm.next_out = hp->decoded.element;
restart:
    ret = inflate(&hp->strm, Z_NO_FLUSH);
    switch (ret)
    {
    case Z_NEED_DICT:
        ret = Z_DATA_ERROR;     /* and fall through */
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
        (void) inflateEnd(&hp->strm);
        free(hp->decoded.element);
        hp->decoded.element = NULL;
        hp->decoded.len = 0;
        return ret;
    }
    if (hp->strm.avail_out == 0 && hp->strm.avail_in > 0)
    {
        fprintf(stderr, "Ran out of space for decompression, %d uncompressed\n",
                hp->strm.avail_in);
        hp->strm.avail_out = 20 * hp->strm.avail_in;
        hp->decoded.element = realloc(hp->decoded.element, hp->declen +
                   hp->strm.avail_out);
        hp->strm.next_out = hp->decoded.element + hp->declen;
        hp->declen += hp->strm.avail_out;
        goto restart;
    }
    hp->decoded.len = hp->declen - hp->strm.avail_out;
#ifdef DEBUG
    fputs("Decompress output\n", stderr);
    fwrite(hp->decoded.element,1,hp->decoded.len,stderr);
    fputs("\n=====================\n", stderr);
#endif
    return ret;
}
int inf_close(hp)
struct http_req_response * hp;
{
    if (hp->declen != -1)
    {
        if (hp->decoded.element != NULL)
            free(hp->decoded.element);
        (void) inflateEnd(&hp->strm);
        hp->declen = -1;
        hp->zreserve_len = 0;
    }
    return 0;
}
/*
 * Analogous routines for Brotli compression
 */
int brot_open(hp)
struct http_req_response * hp;
{
/*
 * Allocate brotli state
 */ 
    BrotliStateInit(&hp->bs);
    hp->strm.zalloc = Z_NULL;
    hp->strm.zfree = Z_NULL;
    hp->strm.avail_in = 0;
    hp->strm.next_in = Z_NULL;
    hp->declen = WORKSPACE;
    if ((hp->decoded.element = (unsigned char *) malloc(WORKSPACE)) == NULL)
    {
        fputs("Out of Memory; bad things are about to happen\n", stderr);
        fflush(stderr);
    }
    hp->decoded.len = 0;
    return 1;
}
/*
 * from_wire.element is the pointer to the data to decompress.
 * from_wire.len is the length to decompress.
 * decoded is where the decompressed data ends up.
 */
int brot_block(hp, to_decompress)
struct http_req_response * hp;
int to_decompress;
{
BrotliResult ret;
int tot = 0;

    if (hp->declen == -1)
    {
        if (to_decompress == 0)
            return 0;
        else
        if (to_decompress < 0)
        {
            hp->decoded.element = NULL; /* Not yet allocated */
            hp->decoded.len = 0;
            return -1;
        }
        brot_open(hp);
    }
    hp->strm.next_in = &hp->from_wire.element[0];
    hp->strm.avail_in = to_decompress;
#ifdef DEBUG
    fprintf(stderr, "Decompress input length: %d\n", to_decompress);
    gen_handle(stderr,hp->strm.next_in,hp->strm.next_in + hp->strm.avail_in,1);
#endif
    hp->strm.avail_out = hp->declen;
    hp->strm.next_out = hp->decoded.element;
restart:
    if ( BrotliDecompressStream(&hp->strm.avail_in,
                                 &hp->strm.next_in,
                                 &hp->strm.avail_out,
                                 &hp->strm.next_out,
                                 &tot, &hp->bs) ==
        BROTLI_RESULT_ERROR)
    {
        free(hp->decoded.element);
        hp->decoded.element = NULL;
        hp->decoded.len = 0;
        brot_close(hp);
        return ret;
    }
    if (hp->strm.avail_out == 0 && hp->strm.avail_in > 0)
    {
        fprintf(stderr, "Ran out of space for decompression, %d uncompressed\n",
                hp->strm.avail_in);
        hp->strm.avail_out = 20 * hp->strm.avail_in;
        hp->decoded.element = realloc(hp->decoded.element, hp->declen +
                   hp->strm.avail_out);
        hp->strm.next_out = hp->decoded.element + hp->declen;
        hp->declen += hp->strm.avail_out;
        goto restart;
    }
    hp->decoded.len = hp->declen - hp->strm.avail_out;
#ifdef DEBUG
    fputs("Decompress output\n", stderr);
    fwrite(hp->decoded.element,1,hp->decoded.len,stderr);
    fputs("\n=====================\n", stderr);
#endif
    return ret;
}
int brot_close(hp)
struct http_req_response * hp;
{
    if (hp->declen != -1)
    {
        if (hp->decoded.element != NULL)
            free(hp->decoded.element);
        (void) BrotliStateCleanup(&hp->bs);
        hp->declen = -1;
        hp->zreserve_len = 0;
    }
    return 0;
}
