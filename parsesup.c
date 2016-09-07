/*
 * parsesup.c - support routines common to all script parsers
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "scripttree.h"
#ifdef LINUX
/*
 * The code below is needed if signals can interrupt reads from disk files
 * (which on some releases of Linux/glibc does happen).
 */
int e2fgetc(FILE * fp)
{
int ret;

    for (;;)
    {
#ifdef DOESTHISHAPPEN
        if ((ret = fgetc(fp)) != EOF
          || feof(fp))
            return ret;
        fprintf(stderr, "Script input error: %d\n",errno);
        perror("Input");
        if ((errno == EINTR || errno == EAGAIN))
            clearerr(fp);
        else
            return ret;
#else
        if ((ret = fgetc(fp)) != EOF
          || feof(fp)
          ||(errno != EINTR && errno != EAGAIN))
            return ret;
        clearerr(fp);
#endif
    }
}
#else
#define e2fgetc fgetc
#endif
/*
 * Initialise a Parser Control structure
 */
void ini_parser_con(pcp)
PARSER_CON * pcp;
{
    pcp->tbuf = (char *) malloc(WORKSPACE);
    pcp->sav_tlook = (char *) malloc(WORKSPACE);
    pcp->tlook = pcp->sav_tlook;
    pcp->look_status = CLEAR;
    pcp->csmacro.vt = hash(256, string_hh, strcmp);
    cscalc_init(&(pcp->csmacro));
    return;
}
/*
 * Clear a Parser Control structure
 */
void clear_parser_con(pcp)
PARSER_CON * pcp;
{
    if (pcp->tbuf != NULL)
    {
        free(pcp->tbuf);
        pcp->tbuf = NULL;
    }
    if (pcp->sav_tlook != NULL)
    {
        free(pcp->sav_tlook);
        pcp->sav_tlook = NULL;
    }
    pcp->tlook = pcp->sav_tlook;
    pcp->look_status = CLEAR;
    cscalc_zap(&(pcp->csmacro));
    cleanup(pcp->csmacro.vt);
    return;
}
void save_cur_loc(pcp)
PARSER_CON * pcp;
{
    pcp->stack_file[pcp->next_in].ifp = pcp->pg.cur_in_file;
    pcp->stack_file[pcp->next_in].offset = ftell(pcp->pg.cur_in_file);
    pcp->next_in++;
    return;
}
/*
 * Script file nesting support
 * - Save our current location
 * - Go and find whatever it is that is being pushed
 * - Get it ready to go.
 */
void push_file(fname, pcp)
char * fname;
PARSER_CON * pcp;
{
FILE * fin;
char * seg;

    if (pcp->next_in >= MAX_NESTING)
        return;               
    save_cur_loc(pcp);
    if ((seg = getvarval(&pcp->csmacro, fname)) != NULL)
        fseek(pcp->pg.cur_in_file, atol(seg), 0);
    else
    if ((fin = (!strcmp(fname, "-"))? stdin: fopen(fname,"r"))
                      != (FILE *) NULL)
        pcp->pg.cur_in_file = fin;
#ifdef DEBUG
    fprintf(stderr, "(Client:%s) nested echo file: %s no: %d errno: %d\n",
                      pcp->pg.rope_seq,
                      fname, fileno(pcp->pg.cur_in_file), errno); 
#endif
    return;
}
/*
 * Undo the previous push_file()
 */
int pop_file(pcp)
PARSER_CON * pcp;
{
#ifdef DEBUG
    fprintf(stderr, "(Client:%s) pop_file() called; next_in : %d\n",
                      pcp->pg.rope_seq, pcp->next_in);

#endif
    if (pcp->next_in <= 0 )
        return 0;                  /* Exhausted */
    pcp->next_in--;
    if ( pcp->pg.cur_in_file == pcp->stack_file[pcp->next_in].ifp)
    {   /* Script segment pushed */
        fseek(pcp->pg.cur_in_file, pcp->stack_file[pcp->next_in].offset, 0);
    }
    else
    {
        if (pcp->pg.cur_in_file != stdin)
            fclose(pcp->pg.cur_in_file); /* Do not close stdin */
        pcp->pg.cur_in_file = pcp->stack_file[pcp->next_in].ifp;
    }
    return 1;
}
/*
 * Read in a line to tlook, dropping escaped newlines and converting to binary.
 * If the data is too long, allocate a new buffer and return fill that.
 * The returned data is in the buffer pcp->sav_tlook, and pcp->tlook is set to
 * this buffer.
 */
int getescline(debug_level, pcp)
int debug_level;
PARSER_CON * pcp;
{
FILE * fp = pcp->pg.cur_in_file;
int p; 
char * bin_lim;
char * cur_pos = pcp->sav_tlook;
char * bound = pcp->sav_tlook + WORKSPACE - 83;
char * long_tlook;
int ltlen;

    ltlen = 0;
skip_blank:
    pcp->tlook = pcp->sav_tlook;
    pcp->look_status = PRESENT;
    p = e2fgetc(fp);
/*
 * Scarper if all done
 */
    if ( p == EOF )
    {
        if (!pop_file(pcp))
            return p;
        fp = pcp->pg.cur_in_file;
        goto skip_blank;
    }
    else
    if (p == '\\')
    {
        *cur_pos++ = (char) p;
        fgets(cur_pos, WORKSPACE - 2, fp);
        if (debug_level > 2)
            fprintf(stderr,"Command: %s\n", cur_pos);
        *(cur_pos + WORKSPACE - 3) = '\n';
        pcp->tlook_len = strlen(cur_pos);
        return pcp->tlook_len;
    }
    else
/*
 * Pick up the next line, stripping out escapes, and converting to binary
 * as appropriate. It will go wrong if there is a super-long token with a
 * load of backslashes located at the boundary.
 */
    {
        bin_lim = cur_pos;
        while (cur_pos < bound)
        {
            if (p == (int) '\\')
            {
                cur_pos = bin_lim + get_bin(bin_lim, bin_lim,
                                                cur_pos - bin_lim);
                bin_lim = cur_pos;
                p = e2fgetc(fp);
                if ( p == EOF )
                    break;
                else
                if (p == '\n')
                    p = e2fgetc(fp);
                else
                if (p == '\r')
                {
                    p = e2fgetc(fp);
                    if (p == '\n')
                        p = e2fgetc(fp);
                    else
                    {
                        *cur_pos++ = '\\';
                        *cur_pos++ = '\r';
                    }
                }
                else
                {
                    *cur_pos++ = '\\';
                    if (p == '\\')
                        continue;
                }
            }
            *cur_pos++ = p;
            if (cur_pos > bound)
            {
                fprintf(stderr,
                 "(Client:%s) Token too big for WORKSPACE; expanding: %.*s\n",
                                pcp->pg.rope_seq, (cur_pos - pcp->sav_tlook),
                                      pcp->sav_tlook);
                fflush(stderr);
                cur_pos = bin_lim + get_bin(bin_lim, bin_lim,
                                                cur_pos - bin_lim);
                if (ltlen == 0)
                {
                    ltlen = (cur_pos - pcp->sav_tlook);
                    long_tlook = (char *) malloc(2 * WORKSPACE);
                    memcpy(long_tlook, pcp->sav_tlook,
                                 ltlen);
                }
                else
                {
                    long_tlook = (char *) realloc(long_tlook,
                         ltlen + 2 * WORKSPACE);
                    memcpy(long_tlook + ltlen, pcp->sav_tlook,
                                 (cur_pos - pcp->sav_tlook));
                    ltlen += (cur_pos - pcp->sav_tlook);
                }
                cur_pos = pcp->sav_tlook;
                bin_lim = cur_pos;
            }
            if (p == (int) '\n')
            {
#ifdef SKIP_BLANK
                if (cur_pos == pcp->sav_tlook + 1)
                {
                    cur_pos = pcp->sav_tlook;
                    goto skip_blank;
                }
#endif
                cur_pos = bin_lim + get_bin(bin_lim, bin_lim,
                                                cur_pos - bin_lim);
                break;
            }
            p = e2fgetc(fp);
            if ( p == EOF )
                p = '\n';
        }
        *cur_pos = '\0';
        if (debug_level > 2)
        {
            fprintf(stderr,"(Client:%s) De-escaped: %s\n",
                      (pcp->pg.rope_seq == NULL)? "unspecified" :
                              pcp->pg.rope_seq, pcp->sav_tlook);
            fflush(stderr);
        }
        pcp->tlook_len =  (cur_pos - pcp->sav_tlook);
        if (pcp->tlook_len > WORKSPACE)
            abort();
        if (ltlen > 0)
        {
            memcpy(long_tlook + ltlen, pcp->sav_tlook,
                                 pcp->tlook_len + 1);
            ltlen += pcp->tlook_len;
            pcp->tlook_len = ltlen;
            free(pcp->sav_tlook);
            pcp->sav_tlook = (char *) realloc(long_tlook,
                                ltlen + 1);
            pcp->tlook = pcp->sav_tlook;
        }
        return pcp->tlook_len;
    }
}
/*
 * Move things from the look-ahead to the live buffer
 */
void mv_look_buf(len, pcp)
int len;
PARSER_CON * pcp;
{
    if (len >= WORKSPACE - 1)
    {
        free(pcp->tbuf);
        pcp->tbuf = (char *) malloc(len + 1);
    }
    memmove(pcp->tbuf, pcp->tlook, len);
    pcp->tbuf_len =  len;
    *(pcp->tbuf + len) = '\0';
    pcp->tlook += len;
    pcp->tlook_len -= len;
    if (pcp->tlook_len == 0)
        pcp->look_status = CLEAR;
    return;
}
/*
 * Double the space being used to accumulate if we are running out
 */
void double_alloc(sep, bp, xp, boundp)
struct script_element * sep;
unsigned char ** bp;
unsigned char ** xp;
unsigned char ** boundp;
{
    sep->body = realloc(*bp, (*boundp - *bp) * 2);
    *xp = sep->body + (*xp - *bp);
    *boundp = sep->body + (*boundp - *bp) * 2;
    *bp = sep->body;
    return;
}
/*
 * Dump out syntax error information
 */
void syn_err(fname,ln,text, pcp)
char * fname;
int ln;
char * text;
PARSER_CON * pcp;
{
    fprintf(stderr, "%s\nSyntax error in script detected at %s:%d.\n\
TOKEN: %s\nLOOKAHEAD:%s\nSAVED:%s\n\
unexpected around byte offset %d\n",
        text, fname, ln,
        pcp->tbuf, (pcp->look_status == CLEAR) ?"":
                     pcp->tlook,
                pcp->sav_tlook, ftell(pcp->pg.cur_in_file));
        return;
}
/**********************************************************************
 * Read the ASCII representation of the script
 */
void read_body(pcp, debug_level, syn_err_fun, sep)
PARSER_CON * pcp;
int debug_level;
void (*syn_err_fun)();
struct script_element * sep;
{
long int f;
unsigned char * b;
unsigned char * x;
unsigned char *bound;
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;
/*
 * The record is to be read off the input stream. get_tok() gives us a binary
 * buffer.
 */
    pcp->look_status = CLEAR;
    sep->body = (unsigned char *) malloc(WORKSPACE);
    b = sep->body;
    x = b;
    bound = x + WORKSPACE;
    while ((tok_id = get_tok(debug_level, pcp)) != E2END &&
            tok_id != E2AEND && tok_id != E2EOF)
    {
        if (tok_id == E2COMMENT)
            continue;                    /* Ignore comments and place holders */
        while (x + pcp->tbuf_len > bound)
        {
            double_alloc(sep, &b, &x, &bound);
        }
        memcpy(x, pcp->tbuf, pcp->tbuf_len);
        x += pcp->tbuf_len;
    }
    x--;    /* Drop the very last line feed */
/*
 * Free up additional space
 */
    sep->body = realloc(b, (x - b));
    sep->body_len = (x - b);
    if (tok_id == E2EOF)
    {
        fputs("Syntax error: No E2END (\\D:E\\) before EOF\n", stderr);
        fputs(b,stderr);
    }
    return;
}
/*
 * Read in the body of an HTTP Request or Response
 */
void read_http_body(pcp, debug_level, syn_err_fun, sep)
PARSER_CON * pcp;
int debug_level;
void (*syn_err_fun)();
struct script_element * sep;
{
enum tok_id tok_id;
int mess_len = 0;
unsigned char * x;
unsigned char * b;
unsigned char * bound;
int send_flag;
unsigned char * head_end;
unsigned char * dx;
unsigned char * db;
int gzip_flag;
int chunk_flag;

    pcp->look_status = CLEAR;
    sep->body = (unsigned char *) malloc(WORKSPACE);
    b = sep->body;
    x = b;
    if (((long) x) & 0xff00000000000000)
    {
        domain_check(NULL, 1);
        abort();
    }
    bound = x + WORKSPACE;
    send_flag = (sep->head[1] == 'D') ? 1 : 0;
    chunk_flag = 0;
    gzip_flag = 0;
    while ((tok_id = get_tok(debug_level, pcp)) != E2END &&
            tok_id != E2AEND && tok_id != E2EOF)
    {
        if (tok_id == E2COMMENT)
            continue;                    /* Ignore comments and place holders */
        mess_len = pcp->tbuf_len;
        while (x + mess_len > bound)
            double_alloc(sep, &b, &x, &bound);
        if (debug_level > 3)
            (void) fprintf(stderr,"Data(%d)\n%s", mess_len,pcp->tbuf);
/*
 * Read the headers line by line. We are interested in some of them (those
 * telling us the data is compressed or chunked; we decompress and de-chunk).
 */
        if ((mess_len == 2
            && pcp->tbuf[0] == '\r'
            && pcp->tbuf[1] == '\n')
#ifdef FIX_DOS_UNIX_CORRUPTION
        ||  (mess_len == 1
            && (pcp->tbuf[0] == '\r'
            || pcp->tbuf[0] == '\n'))
#endif
                                    )
        {
/*
 * Read in everything past the end of the HTTP headers (eg. POST data) as
 * a single block.
 */
            *x++ = '\r';
            *x++ = '\n';
            head_end = x;
            mess_len = head_end - b;
            while ((tok_id = get_tok(debug_level,
                         pcp)) == E2COMMENT);
            if (tok_id == E2END)
                break;
            while (x + pcp->tbuf_len > bound)
            {
                double_alloc(sep, &b, &x, &bound);
                head_end = b + mess_len;
            }
            memcpy(x, pcp->tbuf, pcp->tbuf_len);
            x += pcp->tbuf_len;
/*
 * Pick up the rest of the data in the HTTP request.
 */
            while ((tok_id = get_tok(debug_level,
                            pcp)) != E2END
                 && tok_id != E2AEND
                 && tok_id != E2EOF)
            {
                if (tok_id == E2COMMENT)
                    continue;   /* Ignore comments and place holders */
                while (x + pcp->tbuf_len > bound)
                {
                    double_alloc(sep, &b, &x, &bound);
                    head_end = b + mess_len;
                }
                memcpy(x, pcp->tbuf, pcp->tbuf_len);
                x += pcp->tbuf_len;
            }
            x--;    /* Drop the very last line feed */
/*
 * We should take this path for POST
 */
            break;
        }
/*
 * Below we handle the HTTP headers. We are mostly not interested them at the
 * moment.
 */
        else
        {
            memcpy(x, pcp->tbuf, mess_len);
            x += mess_len;
#ifdef FIX_DOS_UNIX_CORRUPTION
/*
 * Check for corruption by UNIX/DOS format mismatches
 */
            if (*(x - 1) == '\n' && (mess_len == 1 || *(x -2 ) != '\r'))
            {
                *(x - 1) =  '\r';
                *x = '\n';
                x++;
            }
#endif
            if (tok_id == E2TRANSFER)
            {
                if (!strncasecmp(pcp->tlook, "chunked", 7))
                    chunk_flag = 1;
            }
            else
            if (tok_id == E2CONTENT)
            {
                if (!strncasecmp(pcp->tlook, "gzip", 4)
                || (!strncasecmp(pcp->tlook, "deflate", 7)))
                    gzip_flag = 1;
            }
            else
            if (tok_id == E2COMPRESS)
            {
                if (!strncasecmp(pcp->tlook, "yes", 3))
                    gzip_flag = 1;
            }
        }
    }
/*
 * If it is chunked, we need to de-chunk it
 */
    if (chunk_flag && head_end != NULL)
        x = head_end + dechunk(head_end, x, debug_level);
/*
 * If it is compressed, we need to decompress it
 */
    if (gzip_flag && head_end != NULL)
    {
        mess_len = 4 * (x - head_end);
        if (mess_len < WORKSPACE)
            mess_len = WORKSPACE;
        db = (unsigned char *) malloc(mess_len);
        dx = db + mess_len;
        mess_len = decomp(head_end, (x - head_end), &db, &dx);
        if (mess_len > 0)
        {
            if (mess_len > (bound - head_end))
            {
                sep->body = realloc(b, (head_end - b) + mess_len);
                memcpy(sep->body + (head_end - b), db, mess_len);
            }
            else
            {
                memcpy(head_end, db, mess_len);
                sep->body = realloc(b, (head_end - b) + mess_len);
            }
            sep->body_len = (head_end - b) + mess_len;
        }
        else
        {
            sep->body = realloc(b, (x - b));
            sep->body_len = (x - b);
        }
        free(db);
    }
    else
    {
/*
 * Finally, use realloc() to free up any surplus buffer space
 */
        sep->body = realloc(b, (x - b));
        sep->body_len = (x - b);
    }
    if (tok_id == E2EOF)
        fputs("Syntax error: No E2END (\\D:E\\) before EOF\n", stderr);
    return;
}
/*
 * Create a tree representation of a script.
 */
struct script_element * parse_script(pcp, debug_level, syn_err_fun)
PARSER_CON * pcp;
int debug_level;
void (*syn_err_fun)();
{
enum tok_id tok_id;
int i;
struct script_control script_control;
void (*get_body)();
struct script_element * pp, *cp ;
char * resp;

    if (getenv("E2_WEB_DECOMP") != NULL)
        get_body = read_http_body;
    else
        get_body = read_body;

    memset(&script_control,0,sizeof(script_control));
    while ((tok_id = get_tok(debug_level, pcp)) != E2EOF)
    {
        switch(tok_id)
        {
        case E2COMMENT:
            *(pcp->tlook + pcp->tlook_len) = '\n';
            *(pcp->tlook + pcp->tlook_len + 1) = '\0';
            new_script_element(&script_control, pcp->tlook, NULL);
            break;
        case E2SCANSPECS:
            sprintf(pcp->tbuf, "\\F:%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case E2GOTO:
            sprintf(pcp->tbuf, "\\G:%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case E2INCLUDE:
            sprintf(pcp->tbuf, "\\I:%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case E2LABEL:
            sprintf(pcp->tbuf, "\\L:%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case E2RESET:
            new_script_element(&script_control, "\\D:R\\\n", NULL);
            break;
        case END_POINT_TYPE:
        case LINK_TYPE:
        case CLOSE_TYPE:
            memcpy(pcp->tbuf + pcp->tbuf_len, pcp->tlook, pcp->tlook_len);
            *(pcp->tbuf + pcp->tbuf_len + pcp->tlook_len) = '\n';
            *(pcp->tbuf + pcp->tbuf_len + pcp->tlook_len + 1) = '\0';
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case DELAY_TYPE:
            sprintf(pcp->tbuf, "\\W%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case PAUSE_TYPE:
            sprintf(pcp->tbuf, "\\P%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
#ifdef USE_SSL
        case SSL_SPEC_TYPE:
            sprintf(pcp->tbuf, "\\H:%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
#endif
        case START_TIMER_TYPE:
            sprintf(pcp->tbuf, "\\S%.*s \\\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case TAKE_TIME_TYPE:
            sprintf(pcp->tbuf, "\\T%.*s\n", pcp->tlook_len, pcp->tlook);
            new_script_element(&script_control, pcp->tbuf, NULL);
            break;
        case  E2BEGIN:
/*
 * There is no merge logic here because:
 * - Sometimes messages are one-way
 * - We may use ungz on scripts whose responses have been stripped
 */
            *(pcp->tbuf + pcp->tbuf_len) = ':';
            memcpy(pcp->tbuf + pcp->tbuf_len + 1, pcp->tlook, pcp->tlook_len);
            *(pcp->tbuf + pcp->tbuf_len + pcp->tlook_len + 1) = '\n';
            *(pcp->tbuf + pcp->tbuf_len + pcp->tlook_len + 2) = '\0';
            get_body(pcp, debug_level, syn_err_fun,
                   new_script_element(&script_control, pcp->tbuf, "\\D:E\\\n"));
            break;
        case E2ABEGIN:
/*
 * We need to make sure that this response is adjacent to the original request
 * if possible, otherwise ungz and friends will drop the request.
 */
            sprintf(pcp->tbuf, "\\D:B:%.*s\n", pcp->tlook_len, pcp->tlook);
            pp = search_back(script_control.last, pcp->tbuf); /* Find parent */
            pcp->tbuf[1] = 'A';
            get_body(pcp, debug_level, syn_err_fun,
                   new_script_element(&script_control, pcp->tbuf, "\\A:E\\\n"));
            if (pp != NULL)
            { /* Parent exists */ 
                cp = script_control.last;   /* The one we have just added */
                script_control.last = cp->prev_track; /* Unlink the new one */
                script_control.last->next_track = NULL;
                if (pp->child_track == NULL)
                {
                    cp->prev_track = pp;        /* Link it to the parent */
                    cp->next_track = NULL;
                    pp->child_track = cp;
                }
                else
                {
                    pp = pp->child_track; /* Append it to the existing answer */
                    pp->body = (char *) realloc(pp->body, pp->body_len +
                               cp->body_len);
                    memcpy(pp->body + pp->body_len, cp->body, cp->body_len);
                    pp->body_len += cp->body_len;
                    zap_script_element(cp);
                }
            }
            break;
        default:
            syn_err_fun(__FILE__,__LINE__,"Invalid script format",
                            pcp);
            break;
        }
        pcp->look_status = CLEAR;
    }
    return script_control.anchor;
}
/***************************************************************************
 * Function to handle control file data.
 */
struct script_element * load_script(fname, debug_level)
char * fname;
int debug_level;
{
struct script_element * anchor;
PARSER_CON pc;

    if (debug_level > 1)
        fprintf(stderr,"load_script(%s)\n", fname);
    memset(&pc, 0, sizeof(pc));
    anchor = NULL;
    pc.pg.rope_seq = "load_script";
    if (!strcmp(fname, "-"))
        pc.pg.script_file = stdin;
    else
    if ((pc.pg.script_file = fopen(fname, "rb")) == (FILE *) NULL)
    {                              /*  Script File Open  */
        fprintf(stderr,"load_script() script file open failed %s, error %d\n",
                     fname, errno);
        perror("Cannot open script input file");
        return NULL;
    }
    pc.pg.cur_in_file = pc.pg.script_file;
    ini_parser_con(&pc);
    anchor = parse_script(&pc, debug_level, syn_err);
    clear_parser_con(&pc);
    fclose(pc.pg.script_file);
    return anchor;
}
/*
 * The label/name, optionally followed by : and a block of program text
 */
void update_target(label, pcp)
char * label;
PARSER_CON * pcp;
{
int i;
char * x;
struct symbol * sp;
char num_buf[23];

    if ((x = strchr(label, ':')) != NULL)
        *x++ = '\0';
    if (x == NULL)
    {
        sprintf(num_buf, "%u", ftell(pcp->pg.cur_in_file));
        putvarval(&pcp->csmacro, label, num_buf);
    }
    else
        putvarval(&pcp->csmacro, label, x);
    return;
}
/*
 * This either executes a segment (gosub), or it executes a branch (goto).
 * They are distinguishable ... how?
 */
void do_goto(label, pcp)
char * label;
PARSER_CON * pcp;
{
int i;
char * targ;
long offset;

    if ((targ = getvarval(&pcp->csmacro, label)) != NULL
      && (offset = atol(targ)) > 0)
    {
        if (*label == '+')
            save_cur_loc(pcp);
        fseek(pcp->pg.cur_in_file, offset, 0);
    }
    else
    if (strcasecmp(label,"nowhere"))
        fprintf(stderr,
        "(Client:%s) no label %s before offset %u; forward reference?\n",
               pcp->pg.rope_seq, label, ftell(pcp->pg.cur_in_file));
    return;
}
void scarper(fname, ln, text, pcp)
char * fname;
int ln;
char * text;
PARSER_CON * pcp;
{
    if (pcp != NULL)
        fprintf(stderr, "%s:%d (Client: %s) UNIX error code %d\n\
%s\n",fname,ln, pcp->pg.rope_seq, errno, text);
    else
        fprintf(stderr, "%s:%d:%s\n", fname, ln, text);
#ifdef MINGW32
    WSACleanup();
#endif
    exit(1);       /* Does not Return */
}
void syntax_err(fname, ln, text, pcp)
char * fname;
int ln;
char * text;
PARSER_CON * pcp;
{
    fputs("Syntax error in script input file:\n", stderr);
    if (pcp != NULL)
        fprintf(stderr, "%s:%s:%s\n\
unexpected around byte offset %d\n",
        pcp->tbuf, (pcp->look_status == CLEAR) ?"":
                     pcp->tlook, pcp->sav_tlook,
     (pcp->pg.cur_in_file > 8192) ?  ftell(pcp->pg.cur_in_file): 0);
    scarper(fname, ln, text, pcp);       /* Does not Return */
}
