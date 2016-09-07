/*
 * dotnetlib.c - Routines to read and write the streams handled by dotnetdrive.
 *
 * Copyright (C) E2 Systems 1993, 2010
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995";
#include "webdrive.h"
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
 *
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
        mess_len = input_stream_read(f, b, in_out, wdbp);
    else
        mess_len = dotnet_read(f, b,  wdbp);
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
        (void) fprintf(stderr,"(Client:%s) weboutrec(%x,%.*s,%d,%d)\n",
              wdbp->parser_con.pg.rope_seq, fp, mess_len, b, (int) in_out, mess_len);
    if (in_out == OUT || in_out == ORAOUT)
    {
        buf_len =  smart_write((int) fp, b,mess_len, 0, wdbp);
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
        gen_handle((FILE *) fp, b, b + mess_len, 1);
        buf_len = 1;
    }
    if (webdrive_base.debug_level > 2)
        (void) fprintf(stderr, "(Client:%s) weboutrec() File: %x\n",
                        wdbp->parser_con.pg.rope_seq, fp);
    return buf_len;
}
/**********************************************************************
 * .NET traffic
 */
int dotnet_read(f, b, wdbp)
long int f;
unsigned char * b;
WEBDRIVE_BASE * wdbp;
{
unsigned char *top = b;
unsigned char *bound;
unsigned char *valid;
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;
unsigned char * p1;
/*
 * We make little attempt to recognise anything here at the moment.
 */
    bound = top + WORKSPACE;
/*
 * The minimum message length is 16 bytes; empty headers and no data.
 */
    strcpy(wdbp->narrative, "dotnet_read");
    if ((mess_len = smart_read((int) f,top, 16, 0, wdbp)) < 16)
        return mess_len;
    
    if (*top != '.')
    {
        fprintf(stderr, "(Client:%s) dotnet_read() no header?\n",
                  wdbp->parser_con.pg.rope_seq);
        (void) gen_handle(stderr,top,top + 16,1);
        fflush(stderr);
        if ((i = recvfrom((int) f, top + 16, 8192, 0,0,0)) < 1)
            return i;
        return 16 + i;
    }
    else
    {
        read_cnt = top[10] + (top[11] << 8)
                    + (top[12] << 16) + (top[13] << 24);
        top += 14;
        valid = top + 2;
        for(;;)
        {
            switch(*top)
            {
            case 0:
                if (*(top+1) == 0)
                {
                    top += 2;
                    goto do_rest;
                }
                if (*(top+1) == 2)
                {
                    fprintf(stderr,
                       "(Client:%s) dotnet_read() invalid input signalled\n",
                                wdbp->parser_con.pg.rope_seq);
                    wdbp->except_flag |= E2_ERROR_FOUND;
                    top += 2;
                    goto do_rest;
                }
                fprintf(stderr,
                 "(Client:%s) dotnet_read() invalid header end (%d)\n",
                  wdbp->parser_con.pg.rope_seq, *(top + 1));
                return (valid - b);
            case 4:
            case 6:
                if ((i = smart_read((int) f,top, 8, 0, wdbp)) < 8)
                    return i;
                valid += i;
                i = top[4] + (top[5] << 8)
                    + (top[6] << 16) + (top[7] << 24);
                top += 8;
                if (i + valid > bound)
                {
                    valid = b;
                    top = b;
                    if (i + valid > bound)
                    {
                        fprintf(stderr,
"(Client:%s) dotnet_read() WORKSPACE=%d not big enough for the strings (%d)\n",
                                wdbp->parser_con.pg.rope_seq,
                               WORKSPACE, i );
                        return WORKSPACE;
                    }
                }
                if ((i = smart_read((int) f,top, i, 0, wdbp)) < i)
                    return i;
                top += i;
                break;
            case 1:
                if ((i = smart_read((int) f,top, 7, 0, wdbp)) < 7)
                    return i;
                valid += i;
                i = top[3] + (top[4] << 8)
                    + (top[5] << 16) + (top[6] << 24);
                top += 7;
                if (i + valid > bound)
                {
                    valid = b;
                    top = b;
                    if (i + valid > bound)
                    {
                        fprintf(stderr,
"(Client:%s) dotnet_read() WORKSPACE=%d not big enough for the strings (%d)\n",
                                wdbp->parser_con.pg.rope_seq,
                               WORKSPACE, i );
                        return WORKSPACE;
                    }
                }
                if ((i = smart_read((int) f,top, i, 0, wdbp)) < i)
                    return i;
                top += i;
                break;
            default:
                fprintf(stderr,
                 "(Client:%s) dotnet_read() invalid header key (%d)\n",
                  wdbp->parser_con.pg.rope_seq, *top);
/*
 * Make an attempt to read the rest of it
 */
                if ((i = recvfrom((int) f, valid, 8192, 0,0,0)) < 1)
                    return i;
                wdbp->except_flag |= E2_ERROR_FOUND;
                return valid + i - b;
            }
        } 
do_rest:
        if (valid + read_cnt > bound)
        {
            top = (unsigned char *) malloc(read_cnt);
            mess_len = valid - b;
        }
        else
            mess_len = (valid - b) + read_cnt;
        if ((i = smart_read((int) f,top, read_cnt, 0, wdbp)) != read_cnt)
        {
            if (top != b)
                free(top);
            return -1;
        }
/*
 * Check for the encrypted key and IV.
 * ebp = encryptedIv
 * rbp = value
 * remote_ref will be key, 32 bytes
 * encrypted_token will be IV, 16 bytes.
 * ****************************************************************
 * Should use macros to make these easier to follow.
 * ****************************************************************
 */
        if ((p1 = bm_match(webdrive_base.ebp,
              top, top + mess_len)) !=  NULL
        && (p1 = bm_match(webdrive_base.rbp, p1 + 10, top + mess_len)) !=  NULL)
        {
            p1 += 27;
            if (*p1 != 2)
            {
                fprintf(stderr,
              "(Client:%s) dotnet_read() unexpected encrypted key location?\n",
                  wdbp->parser_con.pg.rope_seq);
            }
            else
            {
                if (wdbp->remote_ref == (char *) NULL)
                    wdbp->remote_ref = (char *) malloc(128);
                if (wdbp->encrypted_token == (char *) NULL)
                    wdbp->encrypted_token = (char *) malloc(128);
                p1++;
                if ( rsa_decryption(wdbp->remote_ref, p1, 128) != 32)
                    fprintf(stderr,
                  "(Client:%s) dotnet_read() RSA key decryption stuffed?\n",
                      wdbp->parser_con.pg.rope_seq);
                else
                {
                    p1 += 137;
                    if (*p1 != 2)
                        fprintf(stderr,
              "(Client:%s) dotnet_read() unexpected encrypted IV location?\n",
                                        wdbp->parser_con.pg.rope_seq);
                    else
                    {
                        p1++;
                        if ( rsa_decryption(wdbp->encrypted_token, p1, 128) != 16)
                            fprintf(stderr,
                  "(Client:%s) dotnet_read() RSA IV decryption stuffed?\n",
                                     wdbp->parser_con.pg.rope_seq);
                    }
                }
            }
        }
/*
 * Normal substitutions
 */
        reset_progressive(wdbp);
        scan_incoming_body(wdbp, top, top + mess_len);
/*
 * Check for an Exception if the message length is long enough.
 * We will log the response.
 */
        if (mess_len > 256)
            scan_incoming_error(wdbp, top + 20, top + mess_len);
        if (top < b || top >= bound)
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
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;
unsigned char * p1;
unsigned char * p2;
unsigned char spn_buf[2];
double ts;
int overflow_len = WORKSPACE;
/*
 * The record is to be read off the input stream. get_tok() gives us a binary
 * buffer. It may be bigger than we have allocated; this deals with the
 * problem.
 */
    x = b;
    wdbp->overflow_send = (unsigned char *) NULL;
    spn_buf[1] ='\0';
    strcpy(wdbp->narrative, "input_stream_read");
    wdbp->pragma_flag = 0;
    wdbp->parser_con.look_status = CLEAR;
    while ((tok_id = get_tok( wdbp->debug_level, &wdbp->parser_con)) != E2END &&
            tok_id != E2AEND && tok_id != E2EOF)
    {
        if (tok_id == E2COMMENT)
            continue;                    /* Ignore comments and place holders */
        if ((x -b) + wdbp->parser_con.tbuf_len >= overflow_len - 1)
            sort_out_send(&b, &x, &overflow_len, wdbp->parser_con.tbuf_len,
                    wdbp);
        memcpy(x, wdbp->parser_con.tbuf, wdbp->parser_con.tbuf_len);
        x += wdbp->parser_con.tbuf_len;
    }
    if (tok_id == E2EOF)
    {
        fputs("Syntax error: No E2END (\\D:E\\) before EOF\n", stderr);
        fputs(b,stderr);
        return 0;
    }
    mess_len = ((unsigned char *) x - b) - 1;
           /* Drop the final line feed */
/****************************************************************************
 * Now do the necessary manipulations on the message that has been read in.
 ****************************************************************************
 * Apply any edits. This needs enormous care if the length of any element is
 * changed, since there will be multiple lengths about the place that will
 * need fixing up. So we want to keep the substitutions the same length.
 */
    mess_len += apply_edits(wdbp, b, mess_len);
    x = b + mess_len;
/*
 * Now check for the presence of data that needs to be encrypted.
 * sbp = EncryptedObject+encryptedBytes
 * ehp = SCasMidTierTypes, Version=1[23].
 * The latter indicates a fragment of Serialization in the Serialization,
 * which marks the presence of the stuff that needs to be encrypted.
 */
    if (wdbp->remote_ref != NULL && wdbp->encrypted_token != NULL)
    {         /* We have a key and an IV */
        if ((p1 = bm_match(webdrive_base.sbp, b, x)) != NULL
         && (p1 = bm_match(webdrive_base.ehp, p1 + 30, x)) !=  NULL)
        {
#ifdef OLD
            p1 -= 21;
            if (*p1-- != 1 || *p1 != 0)
            {
                fprintf(stderr,
"(Client:%s) input_stream_read() didn't find encrypted binary format start?\n",
                  wdbp->parser_con.pg.rope_seq);
            }
#else
            p1 -= 23;
            if (*p1++ != 2)
            {
                fprintf(stderr,
"(Client:%s) input_stream_read() didn't find encrypted binary format start?\n",
                  wdbp->parser_con.pg.rope_seq);
            }
#endif
            else
            {
/*
 * Search from the first possible end point forwards, looking for the padding.
 */
                for (p2 = p1 + 240; p2 < x;)
                {
                    if (*p2 > 0 && *p2 < 16)
                    {
                        spn_buf[0] = *p2;
                        if (strspn(p2 - *p2 + 1, spn_buf) == *p2
                           && *(p2 - *p2) == 0xb)
                        {
                            p2++;
                            break;
                        }
                        else
                            p2++;
                    } 
                    else
                        p2++;
                }
/*
 * If we have successfully (or not! ...) found the end, encrypt it.
 */
                if (p2 <= x)
                {
                    if (wdbp->verbosity > 2)
                    {
                        fprintf(stderr,
                           "(Client:%s) input_stream_read() Encrypting ...\n",
                                wdbp->parser_con.pg.rope_seq);
                        gen_handle(stderr, p1, p2, 1);
                        fputs("_____\n",stderr);
                    }
                    aes_encryption(p1, p1, p2 - p1, wdbp->remote_ref,
                          wdbp->encrypted_token);
                }
            }
        }
    }
    if (wdbp->debug_level > 2)
    {
        if (in_out == IN)
            fwrite(b,1,mess_len,stderr);
        else
            (void) gen_handle(stderr,b,x,1);
    }
/*
 * Save the remote name beyond the message
 */
    if ((p1 = bm_match(webdrive_base.authip, b, x)) != NULL
      && (p1 = bm_match(webdrive_base.authbp, p1 + 24, x)) !=  NULL)
    {
        p1 += 4;
        memcpy(x, p1 + 1, *p1);
        *(x + *p1) = '\0';
    }
    else
        *x = '\0';
/*
 * Look for periodic function, return zero length if it is not appropriate to
 * call it.
 */
    if (p1 != NULL
     && wdbp->call_filter.per_fun != NULL
     && !strcmp(wdbp->call_filter.per_fun, x))
    {
        if (wdbp->call_filter.call_cnt == 0)
        {
            wdbp->call_filter.call_cnt = 1;
            wdbp->call_filter.first_run = timestamp();
            wdbp->call_filter.last_run = 
                    wdbp->call_filter.first_run;
        }
        else
        {
            ts = timestamp();
            if ((ts - wdbp->call_filter.last_run) > 100.0 *
                wdbp->call_filter.periodicity
             || ((ts - wdbp->call_filter.first_run)/
                ((double) (wdbp->call_filter.call_cnt + 1)) > 100.0 *
                wdbp->call_filter.periodicity))
            {
                wdbp->call_filter.call_cnt++;
                wdbp->call_filter.last_run = ts;
            }
            else
                return 0;
        }
    }
    return mess_len;
}
