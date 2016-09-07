/******************************************************************************
 * webgettok.c - Script tokeniser for Web scripts and relatives (e.g. DOTNET)
 ******************************************************************************
 * We recognise:
 * -   PATH directives on lines by themselves
 * -   Script text, which is in our mixed hexadecimal and clear text format,
 *     which we convert to its binary representation in this routine, but which
 *     is also (mostly) returned line by line.
 * -   Labelled elements, where the first component on the line is the name and
 *     the second component is an argument. We originally used these for
 *     Documentum, where the script produced had a lot of keywords that were
 *     interpreted by the driver, but we also use them to handle HTTP headers.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "scripttree.h"
#ifndef DOTNET
static struct named_token {
char * tok_str;
enum tok_id tok_id;
} known_toks[] = {
#ifdef XGUI
{ "ErrorEvent", 0x0 },
{ "GenericReply", 0x1 },
{ "KeyPressedEvent", 0x2 },
{ "KeyReleasedEvent", 0x3 },
{ "ButtonPressedEvent", 0x4 },
{ "ButtonReleasedEvent", 0x5 },
{ "MotionEvent", 0x6 },
{ "EnterWindowEvent", 0x7 },
{ "LeaveWindowEvent", 0x8 },
{ "FocusInEvent", 0x9 },
{ "FocusOutEvent", 0xa },
{ "KeymapEvent", 0xb },
{ "ExposeEvent", 0xc },
{ "GraphicsExposeEvent", 0xd },
{ "NoExposeEvent", 0xe },
{ "VisibilityEvent", 0xf },
{ "CreateWindowEvent", 0x10 },
{ "DestroyWindowEvent", 0x11 },
{ "UnmapEvent", 0x12 },
{ "MapEvent", 0x13 },
{ "MapRequestEvent", 0x14 },
{ "ReparentEvent", 0x15 },
{ "ConfigureEvent", 0x16 },
{ "GravityEvent", 0x17 },
{ "ResizeRequestEvent", 0x18 },
{ "ConfigureRequestEvent", 0x19 },
{ "CirculateEvent", 0x1a },
{ "CirculateRequestEvent", 0x1b },
{ "PropertyEvent", 0x1c },
{ "SelectionClearEvent", 0x1d },
{ "SelectionRequestEvent", 0x1e },
{ "SelectionEvent", 0x1f },
{ "ColorMapEvent", 0x20 },
{ "ClientMessageEvent", 0x21 },
{ "MappingEvent", 0x22 },
{ "GenericEvent", 0x23 },
{ "Expect", 0x30 },
{ "String", 0x31 },
{ "Sync", 0x32 },
#else
{"Cookie: ", E2COOKIES},
{"cookie: ", E2COOKIES},
{"Content-Encoding: ", E2CONTENT},
{"content-encoding: ", E2CONTENT},
{"X-Compress: ", E2COMPRESS},
{"Transfer-Encoding: ", E2TRANSFER},
{"transfer-encoding: ", E2TRANSFER},
#ifdef NEEDS_ORA_KEEPS
{"Pragma: ", E2PRAGMA},
#endif
{"HEAD ", E2HEAD},
#ifdef T3_DECODE
{"t3 8.1.6", E2T3},
{"t3 6.1.5.0", E2T3},
{"t3 6.1.4.0", E2T3},
{"t3 6.1.3.0", E2T3},
#endif
#endif
{"", E2STR}};
#endif
/*****************************************************************************
 * Read the next token
 * -  There are at most two tokens a line
 * -  Read a full line, taking care of escape characters
 * -  Search to see what the cat brought in
 * -  Be very careful with respect to empty second tokens
 * -  Return  
 *****************************************************************************
 * It is hard to justify what this code does in terms of what it returns to the
 * calling routines. The manipulations with tbuf and tlook are arbitrary and
 * inconsistent.
 */
enum tok_id get_tok( debug_level, pcp)
int debug_level;
PARSER_CON * pcp;
{
FILE * fp = pcp->pg.cur_in_file;
int len;
int str_length;
struct named_token * cur_tok;
/*
 * If no look-ahead present, refresh it
 */
    if (debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) get_tok(%x); LOOK_STATUS=%d\n",
                      (pcp->pg.rope_seq == NULL)? "unspecified" :
                         pcp->pg.rope_seq, (long int) fp, pcp->look_status);
    if (pcp->look_status == KNOWN)
    {
        pcp->look_status = pcp->last_look;
        return pcp->last_tok;
    }
    else
/****************************************************************************
 * get_tok() shouldn't be called if there is a PATH command as the pre-read
 * string; it should already have been processed.
 * However, very rarely, it is.
    if (pcp->look_status == PRESENT)
    {
        mv_look_buf( pcp->tlook_len, pcp);
        pcp->look_status = CLEAR;
        return pcp->last_tok;
    }
    else
 */
    if (pcp->look_status != PRESENT)
    {
        *(pcp->tbuf) = '\0';
        pcp->tbuf_len = 0;
restart:
        if ((len = getescline(debug_level, pcp)) == EOF)
            return E2EOF;            /* Return EOF if no more */
/*
 * Commands are only allowed at the start of a line
 * Insanely, if the returned line starts with a \, the length
 * is one less than the string length.
 */
        if (debug_level > 3)
            fprintf(stderr,"(Client:%s) Input Line: %.*s",
                   (pcp->pg.rope_seq == NULL)? "unspecified" :
                   pcp->pg.rope_seq,
                   pcp->tlook_len,
                   pcp->tlook);
        pcp->last_look = pcp->look_status;
/*
 * getescline() will have returned any line with an initial \.
 * If it is not a command, it may need binary filtering.
 * However, if it does include binary data, there will be an
 * escaped newline at the end of the line, just like a command
 * So, we don't need to get_bin() on a string that doesn't finish with a \.
 */
        if (*(pcp->tlook) == '\\'
          && *(pcp->tlook + pcp->tlook_len - 1) == '\\')
        {        /* Possible PATH Command */
        char c = *(pcp->tlook + 1);
/*
 * Possible commands switch
 */
            switch (c)
            {
/*
 * Answer (usually deleted from the script)
 */
            case 'A':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                if (*(pcp->tlook) == 'B')
                {
                    pcp->last_tok = E2ABEGIN;
                    pcp->tlook += 2;
                    pcp->tlook_len -= 2;
                }
                else
                if (*(pcp->tlook) == 'E')
                    pcp->last_tok = E2AEND;
                else
                {
                    len++;
                    goto notnewline;
                }
                break;
/*
 * Flag that we are going to continue single stepping rather than running on
 */
            case 'B':
                if (*(pcp->tlook + 2) != '\\')
                {
                    len++;
                    goto notnewline;
                }
                pcp->break_flag = 0;
                pcp->last_tok = E2COMMENT;
                break;
/*
 * Flag that we are going to continue single stepping rather than running on
 */
            case 'Q':
                if (*(pcp->tlook + 2) != '\\')
                {
                    len++;
                    goto notnewline;
                }
                pcp->last_tok = QUIESCENT_TYPE;
                break;
/*
 * Comment (discarded) and Place-holder (F), which we may use to mark substitutions.
 * We need to make sure that comments can be embedded in message text.
 */
            case 'C':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->last_tok = E2COMMENT;
                break;
/*
 * Open, End-point, data or Close
 */
            case 'D':
            case 'E':
            case 'M':
            case 'X':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                *(pcp->tbuf) = '\\';
                *(pcp->tbuf+1) = c;
                *(pcp->tbuf+2) = ':';
                *(pcp->tbuf+3) = '\0';
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                pcp->tbuf_len = 3;
                if (c == 'M')
                    pcp->last_tok = LINK_TYPE;
                else
                if (c == 'E')
                    pcp->last_tok = END_POINT_TYPE;
                else
                if (c == 'X')
                    pcp->last_tok = CLOSE_TYPE;
                else
                {
/*
 * Data
 */
                    if (*(pcp->tlook) == 'B')
                        pcp->last_tok = E2BEGIN;
                    else
                    if (*(pcp->tlook) == 'E')
                        pcp->last_tok = E2END;
                    else
                    if (*(pcp->tlook) == 'R')
                        pcp->last_tok = E2RESET;
                    *(pcp->tbuf+3) = *(pcp->tlook);
                    *(pcp->tbuf+4) = '\0';
                    pcp->tlook += 2;
                    pcp->tlook_len -= 2;
                    pcp->tbuf_len++;
                }
                break;
/*
 * Free standing Scan Spec(s)
 */
            case 'F':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 2;
                pcp->tlook_len -= 2;
                pcp->last_tok = E2SCANSPECS;
                break;
/*
 * Goto (Actually, conditional branch and/or program logic execution)
 */
            case 'G':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                pcp->last_tok = E2GOTO;
                break;
/*
 * Details needed to initialise SSL connections.
 */
#ifdef USE_SSL
            case 'H':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                pcp->last_tok = SSL_SPEC_TYPE;
                break;
#endif
/*
 * Include file
 */
            case 'I':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                pcp->last_tok = E2INCLUDE;
                break;
/*
 * Label (Goto Target)
 */
            case 'L':
                if (*(pcp->tlook + 2) != ':')
                {
                    len++;
                    goto notnewline;
                }
                pcp->tlook += 3;
                pcp->tlook_len -= 3;
                pcp->last_tok = E2LABEL;
                break;
/*
 * Start timer
 */
            case 'S':
                *(pcp->tlook + len - 2) = '\0';
                pcp->tlook += 2;
                pcp->tlook_len -= 2;
                pcp->last_tok = START_TIMER_TYPE;
                break;
/*
 * End timer
 */
            case 'T':
                pcp->tlook += 2;
                pcp->tlook_len -= 2;
                pcp->last_tok = TAKE_TIME_TYPE;
                break;
/*
 * Set the think time
 */
            case 'W':
                pcp->tlook += 2;
                pcp->tlook_len -= 2;
                pcp->last_tok = DELAY_TYPE;
                break;
/*
 * Pause in seconds
 */
            case 'P':
                pcp->tlook += 2;
                pcp->tlook_len -= 2;
                pcp->last_tok = PAUSE_TYPE;
                break;
            case '\n':
                goto restart;
            default:
                fprintf(stderr,"(Client:%s) Format problem with line? %s\n",
                   (pcp->pg.rope_seq == NULL)? "unspecified" :
                                         pcp->pg.rope_seq,
                                        pcp->tlook);
                len++;
                goto notnewline;
            }
            if (debug_level > 2)
                fprintf(stderr,"(Client:%s) Token: %d\n",
                   (pcp->pg.rope_seq == NULL)? "unspecified" :
                                pcp->pg.rope_seq, (int) pcp->last_tok);
            return pcp->last_tok;
notnewline:
/*
 * The possible command hasn't been binary filtered yet. We strip the trailing escaped newline.
 */
            pcp->tlook_len = get_bin(pcp->tlook, pcp->tlook, len - 2);
            len = pcp->tlook_len;
        }
    }
    else
        len = pcp->tlook_len;
#ifdef DOTNET
    pcp->last_tok = E2STR;
    mv_look_buf( len, pcp);
    pcp->look_status = CLEAR;
#else
    for ( cur_tok = known_toks,
         str_length = strlen(cur_tok->tok_str);
             (str_length > 0);
                  cur_tok++,
                  str_length = strlen(cur_tok->tok_str))
        if (!strncmp(pcp->tlook,cur_tok->tok_str,str_length))
        {
#ifdef NEEDS_ORA_KEEPS
            if (cur_tok->tok_id == E2PRAGMA
              && atoi(pcp->tlook + str_length) == 0)
                continue;
#endif
            break;
        }
    if (!str_length)
    {
        pcp->last_tok = E2STR;
        mv_look_buf( len, pcp);
        pcp->look_status = CLEAR;
    }
    else
    {
        pcp->last_tok = cur_tok->tok_id;
#ifdef T3_DECODE
        if (pcp->last_tok == E2T3)
        {              /* Append the rest of the T3 boot-up sequence */
        char * x = pcp->tbuf;

             while (*(pcp->tlook) != '\n')
             {
                 memcpy(x, pcp->tlook, len);
                 x += len;
                 if ((len = getescline(debug_level, pcp)) == EOF)
                     return E2EOF;     /* Return EOF if no more */
             }
             *x++ = '\n';
             *x = '\0';
             pcp->tbuf_len = (x - pcp->tbuf);
        }
        else
#endif
            mv_look_buf( str_length, pcp);
        pcp->last_look = pcp->look_status;
    }
#endif
    if (debug_level > 2)
        fprintf(stderr,
"(Client:%s) Token: %d LOOK_STATUS=%d lookahead: %d tbuf: %s tlook: %s sav_tlook: %s\n",
                   (pcp->pg.rope_seq == NULL)? "unspecified" :
                pcp->pg.rope_seq, (int) pcp->last_tok,
                pcp->look_status,  pcp->last_look,
                pcp->tbuf,  pcp->tlook, pcp->sav_tlook);
    return pcp->last_tok;
}
