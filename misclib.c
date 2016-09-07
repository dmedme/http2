/*
 *    misclib.c - Routines to read and write the streams handled by t3drive.
 *
 *    Copyright (C) E2 Systems 1993, 2000
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995";
#include "webdrive.h"
unsigned char * t3_handle();
#ifndef SD_SEND
#define SD_SEND 1
#endif
static int input_stream_read();

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
 * IN means that the data is in ASCII, and we are decoding the script.
 * The physical reading is done by the input parser. 
 *
 * OUT means that the data is in binary, and we are actually reading it.
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
unsigned char *top = b;
int i;
int mess_len = 0;
enum tok_id tok_id;

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
    {
        mess_len = input_stream_read(f, b, in_out, wdbp);
        if (check_allowed_blocked(wdbp, b, b + mess_len) < 0)
        {
            if (wdbp->verbosity > 2)
            {
                fprintf(stderr, "(Client:%s) Read Message Length: %d Suppressed\n",
                          wdbp->parser_con.pg.rope_seq, mess_len);
                if (mess_len > 0)
                    (void) weboutrec(stderr, b,
                               ((in_out == IN || in_out == OUT) ? IN : ORAIN),
                          mess_len, wdbp);
                    fflush(stderr);
            }
            mess_len = 0;
        }
        else
        if (in_out == IN)
        {
        unsigned char * p1;
/*
 * Save the URL beyond the message
 */
            if ((p1 = memchr( b, ' ', mess_len)) != NULL
             && (i = memcspn(p1 + 1, b + mess_len, 5, " ?\n\r")) !=
               (b  -p1) + ( mess_len -1 ))
            {
                memcpy(b + mess_len, b, i + 1 + (p1 - b));
                *(b + mess_len + (p1 - b) + i + 1) = '\0';
            }
            else
                *(b + mess_len) = '\0';
        }
        else
            *(b + mess_len) = '\0';
    }
    else
    if (in_out == OUT)
        mess_len = http_read(f, b,  wdbp);
    else
    if ( wdbp->cur_link->t3_flag == 1)
        mess_len = t3_read(f, b,  wdbp);
    else
/*   in_out == ORAOUT && ( wdbp->cur_link->from_ep->proto_flag
 *                      ||  wdbp->cur_link->to_ep->proto_flag))
 */
        mess_len = ora_web_read(f, b,  wdbp);
    if (wdbp->debug_level > 2)
    {
        fprintf(stderr, "(Client:%s) Read Message Length: %d\n",
                          wdbp->parser_con.pg.rope_seq, mess_len);
        if (mess_len > 0)
            (void) weboutrec(stderr, b,
               ((in_out == IN || in_out == OUT) ? IN : ORAIN), mess_len, wdbp);
        fflush(stderr);
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
        (void) fprintf(stderr,"(Client:%s) weboutrec(%lx,%lx,%d,%d)\n",
              wdbp->parser_con.pg.rope_seq, (long int) fp,
                        (long int) b, (int) in_out, mess_len);
    if (in_out == OUT || in_out == ORAOUT)
    {
        buf_len =  smart_write((int) fp, b,mess_len, 1, wdbp);
        if (webdrive_base.debug_level > 1)
             (void) fprintf(stderr,
                   "(Client:%s) Message Length %d Sent with return code: %d\n",
                          wdbp->parser_con.pg.rope_seq,
                          mess_len, buf_len);
#ifdef SINGLE_SHOT
        if (in_out != ORAOUT)
            shutdown((int) fp, SD_SEND);
#endif
        if (wdbp->debug_level > 2)
            (void) weboutrec(stderr, b, ((in_out == ORAOUT) ? ORAIN : IN),
                     mess_len, wdbp);
    }
    else
    {
/*
 * Convert the record from binary, and log it.
 */
        if (in_out == ORAIN)
        {
        unsigned char * x;
#ifdef T3_DECODE
            if ( wdbp->cur_link->t3_flag == 1 )
            {

                for (x = b; x < b + mess_len;)
                    x = t3_handle((FILE *) fp, x, b + mess_len, 1);
            }
            else
#else
#ifdef ORA9IAS_1
            if (!wdbp->proxy_port
            &&  ((( wdbp->cur_link->from_ep->proto_flag
               ||  wdbp->cur_link->to_ep->proto_flag))
             && (mess_len < 5 || (memcmp(b,"Mat",3) && memcmp(b,"GDay",4)))))
            {

                for (x = b; x < b + mess_len;)
                    x = forms60_handle((FILE *) fp, x, b + mess_len, 1);
            }
            else
#endif
#endif
                gen_handle((FILE *) fp, b, b + mess_len, 1);
        }
        else
            gen_handle((FILE *) fp, b, b + mess_len, 1);
        buf_len = 1;
    }
    if (webdrive_base.debug_level > 2)
        (void) fprintf(stderr, "(Client:%s) weboutrec() File: %lx\n",
                        wdbp->parser_con.pg.rope_seq, fp);
    return buf_len;
}
/**********************************************************************
 * Read Java stuff serialised by WebLogic T3.
 */
int t3_read(f, b, wdbp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
unsigned char *top = b;
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;
unsigned char * p1;
/*
 * We make little attempt to recognise anything here at the moment.
 */
    top = b;
    strcpy(wdbp->narrative, "t3read");
    if ((mess_len = smart_read((int) f,top,4, 1, wdbp)) < 4)
        return mess_len;
    if (*top != '\0')
    {
#ifdef DEBUG
        fprintf(stderr, "(Client:%s) t3_read() no count ...\n",
                  wdbp->parser_con.pg.rope_seq);
        (void) gen_handle(stderr,top,top + 4,1);
        fflush(stderr);
#endif
        if ((i = recvfrom((int) f, top + 4, 8192, 0,0,0)) < 1)
            return i;
        mess_len = 4 + i;
    }
    else
    {
        read_cnt = top[3] + (top[2] << 8)
                    + (top[1] << 16) + (top[0] << 24) - 4;
        mess_len = read_cnt + 4;
        if (mess_len > WORKSPACE)
        {
            top = (unsigned char *) malloc(mess_len);
            top[0] = b[0];
            top[1] = b[1];
            top[2] = b[2];
            top[3] = b[3];
        }
        if ((i = smart_read((int) f,top + 4, read_cnt, 1, wdbp)) != read_cnt)
        {
            if (top != b)
                free(top);
            return -1;
        }
/*
 * Check for a new encrypted token
 */
        if ((p1 = bm_match(webdrive_base.ebp,
              top, top + mess_len)) != (char *) NULL)
        {
            if (wdbp->encrypted_token == (char *) NULL)
                wdbp->encrypted_token = (char *) malloc(49);
            memcpy( wdbp->encrypted_token, p1 + 18, 49);
        }
/*
 * Check for a remote reference if we haven't got one yet.
 */
        if (wdbp->remote_ref == (char *) NULL
         && ((p1 = bm_match(webdrive_base.rbp,
              top, top + mess_len)) != (char *) NULL))
        {
            wdbp->remote_ref = (char *) malloc(8);
            memcpy( wdbp->remote_ref, p1 + 19, 8);
        }
/*
 * Check for an EJB Exception if the message length is long enough.
 * We will log the response.
 */
        if (mess_len > 256)
            scan_incoming_error(wdbp, top + 20, top + 256);
        if (top != b)
        {
            if (wdbp->debug_level > 2)
            {
                fprintf(stderr, "Read Message Length: %d\n", mess_len);
                (void) weboutrec(stderr, top, ORAIN, mess_len, wdbp);
                fflush(stderr);
            }
            memcpy(b+4, top + 4, WORKSPACE - 5);
               /* Ensure that first part of returned message is valid */
            free(top);
            return mess_len;
        }
    }
    return mess_len;
}
/*
 * This only applies to ORACLE Forms 6 with encryption disabled.
 * Encryption can't be disabled with later versions.
 */
int ora_web_read( f, b, wdbp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
unsigned char * x;
unsigned char * x1;
unsigned char *top = b;
int i;
int loop_detect;
int mess_len = 0;
/*
 * We are making no attempt to recognise anything here at the moment. We will
 * just read until we appear to have seen the end of record marker.
 */
    strcpy(wdbp->narrative, "ora_web_read");
    for (loop_detect = 0; loop_detect < 100;)
    {
        if ((i = recvfrom((int) f, top, 8192, 0,0,0)) <= 0)
        {
            if ((errno == 0 || errno == EINTR) && !wdbp->alert_flag)
            {
                loop_detect++;
                continue;
            }
            break;
        }
        loop_detect = 0;
        mess_len += i;
        top += i;
        x = top - 1;
        x1 = top - 2;
        if (x1 < b)
        {
            mess_len = 1;
            break;
        } 
        else
        {
            if (*b == 'M'
             && *(b + 1) == 'a'
             && *(b + 2) == 't'
             && *(b + 3) == 'f')
                break;
            else
#ifdef FORMS45
            if (*x1 == 0xe0 && (*x < 10))
            {
                if (x1 == b || *(x1 - 1) == 0xf0)
                    break;
            }
#else
            if (*x1 == 0xf0 && *x >= 0x1 && *x <= 0x3)
                break;
#endif
        }
        if (top > (b + WORKSPACE - 8192))
            top = b;
    }
    if (mess_len > WORKSPACE)
        mess_len = WORKSPACE; /* Prevent possible access violations */
    return mess_len;
}
/**********************************************************************
 * Read the ASCII representation of the script
 */
static int input_stream_read(f, b, in_out, wdbp)
long int f;
unsigned char * b;
enum direction_id in_out;
WEBDRIVE_BASE * wdbp;
{
unsigned char * x;
unsigned char *top = b;
unsigned char *bound = b + WORKSPACE;
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;
int overflow_len = WORKSPACE;
/*
 * The record is to be read off the input stream. It has been converted to
 * binary on the way in.
 */
    x = b;
    wdbp->overflow_send = (unsigned char *) NULL;
    wdbp->pragma_flag = 0;
    wdbp->parser_con.look_status = CLEAR;
    strcpy(wdbp->narrative, "input_stream_read");
    while ((tok_id = get_tok(wdbp->debug_level, &wdbp->parser_con)) != E2END &&
            tok_id != E2AEND && tok_id != E2EOF)
    {
        if (tok_id == E2COMMENT)
            continue;                    /* Ignore comments and place holders */
        mess_len = wdbp->parser_con.tbuf_len;
        if (wdbp->debug_level > 3)
            (void) fprintf(stderr,"(Client:%s) data(%d)\n%s", 
               wdbp->parser_con.pg.rope_seq, mess_len,wdbp->parser_con.tbuf);
        if ((x -b) + mess_len >= overflow_len - 1)
        {
            sort_out_send(&b, &x, &overflow_len, wdbp->parser_con.tbuf_len,
                    wdbp);
            bound = b + overflow_len;
        } 
/*
 * This first branch covers the cases when the whole message is treated as a
 * unit. This would be BEA WebLogic Server after the session has switched to
 * T3 from HTTP, or ORACLE Forms 4.5 unencrypted, or ORACLE Forms 6 not
 * tunnelled
 */
        if (mess_len +x > bound)
        {
            (void) fprintf(stderr,"(Client:%s) data(%d) too much; exceeds %d\n%s", 
               wdbp->parser_con.pg.rope_seq, mess_len, WORKSPACE,
                         wdbp->parser_con.tbuf);
        }
        if (in_out == ORAIN
          || wdbp->cur_link->t3_flag == 1)
        {
            memcpy(x, wdbp->parser_con.tbuf, mess_len);
            x += mess_len;
        }
/*
 * This is the test for the end of the HTTP headers. We pick up the rest as
 * a single block
 */
        else
        if ((mess_len == 2
            && wdbp->parser_con.tbuf[0] == '\r'
            && wdbp->parser_con.tbuf[1] == '\n')
        ||  (mess_len == 1
            && (wdbp->parser_con.tbuf[0] == '\r'
            || wdbp->parser_con.tbuf[0] == '\n')))
        {
/*
 * Read in everything past the end of the HTTP headers (eg. POST data) as
 * a single block, and scarper.
 */
            *x++ = '\r';
            *x++ = '\n';
            while ((tok_id = get_tok(wdbp->debug_level,
                         &wdbp->parser_con)) == E2COMMENT);
            mess_len = x - b;
            if (tok_id == E2END)
                return mess_len;
            if ((x -b) + mess_len >= overflow_len - 1)
            {
                sort_out_send(&b, &x, &overflow_len, wdbp->parser_con.tbuf_len,
                        wdbp);
                bound = b + overflow_len;
            } 
            if (x + wdbp->parser_con.tbuf_len > bound)
            {
                (void) fprintf(stderr,"(Client:%s) data(%d) too much; exceeds %d\n%s", 
                   wdbp->parser_con.pg.rope_seq, mess_len, WORKSPACE,
                             wdbp->parser_con.tbuf);
                return -1;
            }
            memcpy(x, wdbp->parser_con.tbuf, wdbp->parser_con.tbuf_len);
/*
 * In the ORACLE case, look for the initial hand-shake
 */ 
#if ORA9IAS_1|ORA9IAS_2
            if (wdbp->pragma_flag
              && wdbp->gday == (unsigned char *) NULL
              && wdbp->parser_con.tbuf_len >= 4
              && !memcmp(wdbp->parser_con.tbuf, "GDay", 4))
            {
                if (wdbp->debug_level > 2)
                    fputs("Seen GDay\n", stderr);
                wdbp->gday = (unsigned char *) malloc(4);
                memcpy(wdbp->gday,x + 4, 4);
            }
#endif
            x += wdbp->parser_con.tbuf_len;
/*
 * Pick up the rest of the data in the HTTP request.
 */
            while ((tok_id = get_tok(wdbp->debug_level,
                            &wdbp->parser_con)) != E2END
                 && tok_id != E2AEND
                 && tok_id != E2EOF)
            {
                if (tok_id == E2COMMENT)
                    continue;   /* Ignore comments and place holders */
                if ((x -b) + mess_len >= overflow_len - 1)
                {
                    sort_out_send(&b, &x, &overflow_len, wdbp->parser_con.tbuf_len,
                            wdbp);
                    bound = b + overflow_len;
                } 
                if (x + wdbp->parser_con.tbuf_len > bound)
                {
                    (void) fprintf(stderr,"(Client:%s) data(%d) too much; exceeds %d\n%s", 
                           wdbp->parser_con.pg.rope_seq, mess_len, WORKSPACE,
                             wdbp->parser_con.tbuf);
                    return -1;
                }
                memcpy(x, wdbp->parser_con.tbuf, wdbp->parser_con.tbuf_len);
                x += wdbp->parser_con.tbuf_len;
            }
            x--;    /* Drop the very last line feed */
            if (!wdbp->pragma_flag
              || wdbp->f_enc_dec[0] == (unsigned char *) NULL)
                x += apply_edits(wdbp,  b + mess_len, (x - b - mess_len));
#if ORA9IAS_1|ORA9IAS_2
            else
            if (wdbp->pragma_flag
              && wdbp->f_enc_dec[0] != (unsigned char *) NULL
              && strncmp(b + mess_len, "NULLPOST", 8))
            {
                  if (wdbp->verbosity > 1)
                  {
                  char * xp;

                      fprintf(stderr, "(Client: %s) Pre-scrambled ===>\n",
                             wdbp->parser_con.pg.rope_seq);
                      for (xp = b + mess_len; xp < x;)
                           xp = forms60_handle(stderr, xp, x, 1);
                  }
                  block_enc_dec(wdbp->f_enc_dec[0], b + mess_len,
                            b + mess_len, (x - b - mess_len));
            }
#else
            if (wdbp->pragma_flag)
                x += apply_compression(wdbp,  b + mess_len, (x - b - mess_len),
                       wdbp->parser_con.tbuf);
#endif
            adjust_content_length(wdbp, b, &x);
/*
 * We should take this path for POST
 */
            return (x - b);
        }
/*
 * Below we handle the HTTP headers. We are mostly not interested them at the
 * moment, but they are individually edited.
 */
        else
        {
        unsigned char * p1;
        int j;

            memcpy(x, wdbp->parser_con.tbuf, mess_len);
            mess_len += apply_edits(wdbp,  x, mess_len);
            x += mess_len;
/*
 * Check for corruption by UNIX/DOS format mismatches
 */
#ifdef T3_DECODE
            if (tok_id == E2T3)
                wdbp->cur_link->t3_flag = 1;
            else
#endif
            {
                if (*(x - 1) == '\n' && (mess_len == 1 || *(x -2 ) != '\r'))
                {
                    *(x - 1) =  '\r';
                    *x = '\n';
                    x++;
                }
                if (tok_id == E2COOKIES)
                {
                    wdbp->parser_con.tlook[strlen(wdbp->parser_con.tlook) +
                  apply_edits(wdbp,  wdbp->parser_con.tlook, strlen(wdbp->parser_con.tlook))] = '\0';
                    preserve_script_cookies(wdbp,
                                            wdbp->parser_con.tlook,
                                            wdbp->parser_con.tlook+strlen(wdbp->parser_con.tlook));
                    if (wdbp->cookie_cnt)
                    {          /* Replace the value in the script */
                        wdbp->parser_con.look_status = CLEAR;
                        mess_len = strlen( wdbp->cookies[0]);
                        memcpy(x, wdbp->cookies[0], mess_len);
                        x += mess_len;
                        for (i = 1; i < wdbp->cookie_cnt; i++)
                        {
                            *x = ';';
                            x++;
                            *x = ' ';
                            x++;
                            mess_len = strlen( wdbp->cookies[i]);
                            memcpy(x, wdbp->cookies[i], mess_len);
                            x += mess_len;
                        }
                        *x = '\r';
                        x++;
                        *x = '\n';
                        x++;
                    }
                    else
                    {
                        wdbp->parser_con.look_status = CLEAR;
                        *x = '\r';
                        x++;
                        *x = '\n';
                        x++;
                    }
                }
#if ORA9IAS_1|ORA9IAS_2
                else
                if (tok_id == E2PRAGMA)
                {
                    wdbp->pragma_flag = 1;
                    wdbp->pragma_seq++;
                    if (wdbp->pragma_seq == 2)
                        wdbp->pragma_seq++;
                    wdbp->parser_con.look_status = CLEAR;
                    x += sprintf(x, "%d\r\n", wdbp->pragma_seq);
                }
#endif
                else
                if (tok_id == E2HEAD)
                    wdbp->head_flag = 1;
                else
                if (tok_id == E2COMPRESS)
                    wdbp->pragma_flag = 1;
            }
        }
    }
    if (tok_id == E2EOF)
    {
        fputs("Syntax error: No E2END (\\D:E\\) before EOF\n", stderr);
        fputs(b, stderr);
        return 0;
    }
    mess_len = ((unsigned char *) x - b);
/*
 * I think this is meant to make sure that the headers finish with a carriage
 * return/line feed pair, in case the input file has been buggered up by
 * incompetent editing.
 */
    if (in_out != ORAIN
     && wdbp->cur_link->t3_flag == 0
     && (mess_len < 4 || *(x - 4) != '\r'))
    {
        for (x = x - 1; *x == '\n'; x--);
        x += 2;
        *x = '\r';
        x++;
        *x = '\n';
        x++;
        mess_len = ((unsigned char *) x - b);
    }
    else
#ifdef T3_DECODE
/*
 * Handle the Java RMI headers
 */
    if (in_out == ORAIN && wdbp->cur_link->t3_flag == 1)
    {
    unsigned char * p1;

        mess_len--;       /* Drop the final line feed */
/****************************************************************************
 * Now do the necessary manipulations on the message that has been read in.
 ****************************************************************************
 * Messages consist of:
 * - A four byte inclusive length, sent MSB=>LSB (ie. Big-Endian)
 * - A 19 byte JVMessage header, made up of:
 * - A command code:
 *   - 0 - CMD_UNDEFINED
 *   - 1 - CMD_IDENTIFY_REQUEST
 *   - 2 - CMD_IDENTIFY_RESPONSE
 *   - 3 - CMD_PEER_GONE
 *   - 4 - CMD_ONE_WAY
 *   - 5 - CMD_REQUEST
 *   - 6 - CMD_RESPONSE
 *   - 7 - CMD_ERROR_RESPONSE
 *   - 8 - CMD_INTERNAL
 *   - 9 - CMD_NO_ROUTE_IDENTIFY_REQUEST
 *   - 10 - CMD_TRANSLATED_IDENTIFY_RESPONSE
 *   - 11 - CMD_REQUEST_CLOSE
 * - A 1 byte QOS (always 101, 0x65)
 * - A 1 byte flag:
 *   - 1 - Message has JVMID's
 *   - 2 - Message has Transaction details
 * - A 4 byte request sequence (incremented on requests, matched on responses)
 * - A 4 byte invokable ID (same as request for responses, not understood in
 *   other cases
 * - A 4 byte abbrev offset. This always seems to be the end of the message
 *   for our message
 * - Then, a set of marshalled parameters.
 * - Somewhere there will be the remote reference
 *
 * It is necessary to work out what the remote reference value in the script
 * was. This is done by picking up 8 bytes at offset 26 from the CMD_REQUEST
 * with sequence 3. A match is set up for these 8 bytes, so they can be
 * substituted thereafter.
 */
        if (*(b + 4) == 0x5
         && *(b + 5) == 0x65
         && *(b + 7) == 0
         && *(b + 8) == 0
         && *(b + 9) == 0
         && *(b + 10) == 3)
        {
            if (wdbp->sbp != (struct bm_table *) NULL)
                bm_zap(wdbp->sbp);
            if (wdbp->remote_ref != (char *) NULL
              && !memcmp(wdbp->remote_ref, b + 26, 8))
                wdbp->sbp = (struct bm_table *) NULL;
            else
                wdbp->sbp = bm_compile_bin(b + 26, 8);
        }
   
/*
 * We set the message sequence based on the current values for the link, so
 * we can handle loops. Note that the numbers may be incremented for
 * responses also if these are present in the script.
 */
         if (*b == 0 && *(b + 4) != 4 && *(b + 4) != 8)
         {
             *(b + 7) = (wdbp->cur_link->pair_seq & 0xff000000)>>24;
             *(b + 8) = (wdbp->cur_link->pair_seq & 0xff0000) >> 16;
             *(b + 9) = (wdbp->cur_link->pair_seq & 0xff00) >> 8;
             *(b + 10) = (wdbp->cur_link->pair_seq & 0xff);
             wdbp->cur_link->pair_seq++;
         }
/*
 * Now substitute the encrypted token if appropriate
 */ 
         if (wdbp->encrypted_token != (char *) NULL
           && ((p1 = bm_match(webdrive_base.ebp, 
                b, b + mess_len)) != (char *) NULL))
             memcpy( p1 + 18, wdbp->encrypted_token,  49);
/*
 * Now substitute the remote reference if appropriate
 */ 
         if (wdbp->remote_ref != (char *) NULL
           && wdbp->sbp != (struct bm_table *) NULL
           && ((p1 = bm_match(wdbp->sbp,
                 b, b + mess_len)) != (char *) NULL))
             memcpy( p1, wdbp->remote_ref,  8);
    }
    else
#endif
    if ( wdbp->cur_link->t3_flag == 1 || in_out == ORAIN)
    {
        mess_len--;       /* Drop the final line feed */
    }
    if (wdbp->debug_level > 2)
    {
        if (in_out == IN)
            fwrite(b,1,mess_len,stderr);
        else
            (void) gen_handle(stderr,b,x,1);
    }
    return mess_len;
}
