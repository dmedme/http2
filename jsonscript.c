/*
 * jsonscript.c - Routines to convert script fragments into JSON.
 *
 * Everything should be called with script_element arguments.
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
static char * json_recognise_answer();
static char * json_recognise_message();
static void json_recognise_label();
static void json_recognise_include();
static void json_recognise_goto();
void json_do_open();
void json_do_close();
void json_recognise_end_point_pair();
static int json_recognise_scan_spec();
/*********************************************************************
 * Recognise PATH directives that will occur in ALL files.
 *
 * Start Time (\S) records are:
 * id:number (ignored):description
 */
void json_recognise_start_timer(a, sep)
union all_records *a;
struct script_element * sep;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];

    (void) nextasc_r(sep->head + 2, ':', '\\', &got_to,
                    &(a->start_timer.timer_id[0]),
              &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to, &ret_buf[0],
                 &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few Start Timer fields", NULL);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                 &(a->start_timer.timer_description[0]),
                 &(a->start_timer.timer_description[sizeof(
                   a->start_timer.timer_description) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few Start Timer fields", NULL);
    return;
}
/*
 * Take Time (\T) records are:
 * id:
 */
void json_recognise_take_time(a, sep)
union all_records *a;
struct script_element * sep;
{
unsigned char * got_to = (unsigned char *) NULL;

    (void) nextasc_r(sep->head + 2, ':', '\\', &got_to,
               &(a->start_timer.timer_id[0]),
          &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    return;
}
/*
 * Delay (\W) or Pause (\P) records are:
 * delay time (floating point number)
 */
void json_recognise_delay(a, sep)
union all_records *a;
struct script_element * sep;
{
    a->delay.delta = strtod(sep->head+2, (char **) NULL);
    return;
}
/****************************************************************************
 * Routine to stuff things for JSON; backslash and double quote
 */
static void js_stuff_str(in, out)
char * in;
char * out;
{
    while (*in != '\0')
    {
        if (*in == '\n')
        {
            *out++ = '\\';
            *out++ = 'n';
            *out++ = '\\';
        }
        else
        if (*in == '\\' || *in == '"')
            *out++ = '\\';
        *out++ = *in++;
    }
    *out = '\0';
    return;
}
static void js_stuff_cnt(in, out, cnt)
char * in;
char * out;
int cnt;
{
    while (cnt > 0)
    {
        if (*in == '\n')
        {
            *out++ = '\\';
            *out++ = 'n';
            *out++ = '\\';
        }
        else
        if (*in == '\\' || *in == '"')
            *out++ = '\\';
        *out++ = *in++;
        cnt--;
    }
    *out = '\0';
    return;
}
/*
 * Needs workspace, hence tmpbuf and topbuf
 */
static char * js_handle(in, out, len, tmpbuf, topbuf)
char * in;
char * out;
int len;
char * tmpbuf;
char * topbuf;
{
char * p;
char * top = in + len;

    for (p = bin_handle_no_uni_cr(NULL, in,top,0);
            in < top;
                 p = bin_handle_no_uni_cr(NULL, in,top,0))
    {
        if (p > in)
        {
            hexin_r(in, (p - in), tmpbuf, topbuf);
            *out++ = '\'';
            js_stuff_cnt(tmpbuf, out, 2*(p - in));
            out += strlen(out);
            *out++ = '\'';
            *out++ = '\\';
            *out++ = '\\';
            *out++ = '\\';
            *out++ = 'n';
            *out++ = '\\';
            *out++ = '\n';
            in = p;
        }
        p = asc_handle_nocr(NULL, in,top,0);
        if (p > in)
        {
            js_stuff_cnt(in, out, (p - in));
            in = p;
            out += strlen(out);
        }
    }
    *out = '\0';
    return out;
}
#ifdef USE_SSL
/***********************************************************************
 * An SSL Spec is the stuff we need for a Session. I assume that we will
 * only need one session per context.
 ***********************************************************************
 */ 
void json_recognise_ssl_spec(a, sep)
union all_records *a;
struct script_element * sep;
{
unsigned char * got_to = (unsigned char *) NULL;

    a->ssl_spec.ssl_spec_id = 1;
    (void) nextasc_r(sep->head + 3, ':', '\\', &got_to,
               a->ssl_spec.ssl_spec_ref,
           &(a->ssl_spec.ssl_spec_ref[sizeof(a->ssl_spec.ssl_spec_ref) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
               a->ssl_spec.key_file,
             &(a->ssl_spec.key_file[sizeof(a->ssl_spec.key_file) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few SSL Spec fields", NULL);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,a->ssl_spec.passwd,
          &(a->ssl_spec.passwd[sizeof(a->ssl_spec.passwd) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few SSL Spec fields", NULL);
    if (a->ssl_spec.passwd[ strlen(a->ssl_spec.passwd) - 1]
                          == '\n')
        a->ssl_spec.passwd[strlen(a->ssl_spec.passwd )-1] = '\0';
    return;
}
#endif
/********************************************************************
 * End Points (\E) are used to allow replay of scripts captured on
 * one machine with one set of parameters, on another machine with another
 * set.
 *
 * The end_points are:
 * Original Host:Capture Port:Host for Test:Port for Test:Connection Flag
 *
 * The con_flag is 'C' for connect() and 'L' for listen() behaviour.
 */
void json_recognise_end_point(a, sep)
union all_records *a;
struct script_element * sep;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[22];

    a->end_point.end_point_id = 1;
    (void) nextasc_r(sep->head + 3, ':', '\\', &got_to,
               a->end_point.address,
               &(a->end_point.address[sizeof(a->end_point.address) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields",  NULL);
    a->end_point.cap_port_id = atoi(ret_buf);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to, a->end_point.host,
             &(a->end_point.host[sizeof(a->end_point.host) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields", NULL);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields", NULL);
    a->end_point.port_id = atoi(ret_buf);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields", NULL);
    a->end_point.con_flag = ret_buf[0];
    if (a->end_point.con_flag == 'L')
    {
#ifdef USE_SSL
        a->end_point.ssl_spec_id = -1;
#endif
#ifdef USE_SSL
        if ( nextasc_r((char *) NULL, ':', '\\', &got_to,
               a->end_point.ssl_spec_ref,
           &(a->end_point.ssl_spec_ref[sizeof(a->end_point.ssl_spec_ref) - 1]))
                  != NULL)
        {
            if (a->end_point.ssl_spec_ref[ strlen(a->end_point.ssl_spec_ref)
                       - 1] == '\n')
                a->end_point.ssl_spec_ref[strlen(a->end_point.ssl_spec_ref )
                       - 1] = '\0';
        }
#endif
    }
    else
    {
        a->end_point.proto_flag = 0;
#ifdef USE_SSL
        a->end_point.ssl_spec_id = -1;
        a->end_point.ssl_spec_ref[0] = '\0';
#endif
    }
    return;
}
/*
 * Create a JSON representation of a script element.
 */
int generate_json(sep, buf, buf_len, seq)
struct script_element * sep;
char * buf;
int buf_len;
int seq;
{
char * x;
int i;
union all_records a;
char tmpbuf[256];
/*
 * Find out what kind of object it is from the the header.
 * Generate JSON from the header, body and footer
 */
    switch (sep->head[1])
    {
    case 'C':
        js_stuff_str(sep->head, a.buf);
        sprintf(buf, "%s{ id : %d, type : \"COMMENT\", data : \"%s\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10, a.buf);
        break;
    case 'D':
        switch(sep->head[3])
        {
        case 'R':
/*
 * Reset the saved cookies, or whatever passes for session state.
 */          
            sprintf(buf,
                "%s{ id : %d, type : \"RESET\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10);
            break;
        case 'B':
            json_recognise_message(sep, &a, seq, buf, buf_len);
            break;
/*
 * Perform a conditional branch
 */          
        }
        break;
    case 'G':
        json_recognise_goto(sep, &a, seq, buf, buf_len);
        break;
/*
 * Declare a branch target
 */          
    case 'L':
        json_recognise_label(sep, &a, seq, buf, buf_len);
        break;
/*
 * Open an include file.
 */          
    case 'I':
        json_recognise_include(sep, &a, seq, buf, buf_len);
        break;
    case 'E':
        json_recognise_end_point(&a, sep);
        sprintf(buf,
"%s{ id : %d, type : \"END_POINT\", capture_host : \"%s\", capture_port : \"%d\",\n\
map_host : \"%s\", map_port : \"%d\", connect_or_listen : \"%c\", ssl_spec_ref : \"%s\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
                a.end_point.address,
                a.end_point.cap_port_id,
                a.end_point.host,
                a.end_point.port_id,
                a.end_point.con_flag,
                a.end_point.ssl_spec_ref);
        break;
    case 'A':
        json_recognise_answer(sep, &a, seq, buf, buf_len);
        break;
#ifdef USE_SSL
    case 'H':
        json_recognise_ssl_spec(&a, sep);
        x = buf + sprintf(buf, "%s{ id : %d, type : \"SSL_SPEC\",",
                (seq == 0 ) ? "" : ",", seq * 10);
        js_stuff_str(a.ssl_spec.ssl_spec_ref, tmpbuf);
        x += sprintf(x, "ssl_spec_ref : \"%s\",", tmpbuf);
        js_stuff_str(a.ssl_spec.key_file, tmpbuf);
        x += sprintf(x, " key_file : \"%s\",", tmpbuf);
        js_stuff_str(a.ssl_spec.passwd, tmpbuf);
        x += sprintf(x, " password : \"%s\" }\n", tmpbuf);
        break;
#endif
    case 'M':
    case 'X':
        json_recognise_end_point_pair(&a, sep);
        if (sep->head[1] == 'M')
            json_do_open(sep, &a, seq, buf, buf_len);
        else
            json_do_close(sep, &a, seq, buf, buf_len);
        break;
    case 'S':
        json_recognise_start_timer(&a, sep);
        sprintf(buf,
   "%s{ id : %d, type : \"START_TIMER\", event : \"%s\", desc : \"%s\" }\n",
                        (seq == 0 ) ? "" : ",", seq * 10,
                        a.start_timer.timer_id,
                        a.start_timer.timer_description);
        break;
    case 'T':
        json_recognise_take_time(&a, sep);
        sprintf(buf, "%s{ id : %d, type : \"TAKE_TIME\", event : \"%s\" }\n",
                        (seq == 0 ) ? "" : ",", seq * 10,
                        a.start_timer.timer_id);
        break;
    case 'W':
        json_recognise_delay(&a, sep);
        sprintf(buf, "%s{ id : %d, type : \"DELAY\", wait : \"%g\" }\n",
                        (seq == 0 ) ? "" : ",", seq * 10,
                        a.delay.delta);
        break;
    case  PAUSE_TYPE:
        json_recognise_delay(&a, sep);
        sprintf(buf, "%s{ id : %d, type : \"PAUSE\", wait : \"%g\" }\n",
                        (seq == 0 ) ? "" : ",", seq * 10,
                        a.delay.delta);
        break;
    default:
/*
 * Presumably this is a X GUI script, so the header will have an X event in it
 * This of course has structure, but we don't bother with that for now
 */
        sprintf(buf, "%s{ id : %d, type : \"X Event\", \"%s\" }\n",
                        (seq == 0 ) ? "" : ",", seq * 10,
				sep->head);
        break;
    }
    return ++seq;
}
/***************************************************************************
 * Function to follow a script_element chain.
 */
int json_chain(ofp, tp, seq)
FILE * ofp;
struct script_element * tp;
int seq;
{
static char buf[WORKSPACE * 100];
int flag = 0;

    if (seq == 0)
    {
        fputs("[\n", ofp);
        flag = 1;
    }
    for (; tp != NULL; tp = tp->next_track)
    {
        if (tp->head != NULL)
        {
            seq = generate_json(tp, buf, WORKSPACE * 100, seq);
            fputs(buf, ofp);
            fputc('\n', ofp);
        }
        if (tp->child_track != NULL)
            seq = json_chain(ofp, tp->child_track);
    }
    if (flag == 1)
        fputs("]\n", ofp);
    return seq;
}
/*****************************************************************************
 * Take an incoming message in ASCII and make it binary. 
 *
 * End point matches our input fields for data type, though this isn't
 * actually an end point.
 */
void json_recognise_end_point_pair(ap, sep)
union all_records * ap;
struct script_element * sep;
{
char ret_buf[24];
unsigned char * got_to = NULL;
char * x = sep->head +
           ((sep->head[1] == 'A') || (sep->head[1] == 'D') ?  5 : 3);

    (void) nextasc_r(x, ';', '\\', &got_to,
          &ap->end_point.address[0],
          &ap->end_point.address[sizeof(ap->end_point.address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields", NULL);
    ap->end_point.cap_port_id = atoi(ret_buf);
    if (nextasc_r((char *) NULL, ';', '\\', &got_to, &ap->end_point.host[0],
            &ap->end_point.host[sizeof(ap->end_point.host) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields", NULL);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields", NULL);
    ap->end_point.port_id = atoi( ret_buf);
    return;
}
/***************************************************************************
 * Function to recognise a link definition
 */
void json_do_open(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
    sprintf(buf,
"%s{ id : %d, type : \"OPEN\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
         ap->end_point.address,
         ap->end_point.cap_port_id,
         ap->end_point.host,
         ap->end_point.port_id);
    return;
}
/***************************************************************************
 * Function to recognise a socket close definition
 */
void json_do_close(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
    sprintf(buf,
"%s{ id : %d, type : \"CLOSE\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
         ap->end_point.address,
         ap->end_point.cap_port_id,
         ap->end_point.host,
         ap->end_point.port_id);
    return;
}
/*****************************************************************************
 * Take an incoming message in ASCII and make it binary. 
 *
 * End point matches our input fields for data type, though this isn't
 * actually an end point.
 */
static char * json_recognise_message(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
char * x;
char * tmpbuf;
char * topbuf;
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];
int i;

    json_recognise_end_point_pair(ap, sep);
    x = buf + sprintf(buf,
"%s{ id : %d, type : \"MESSAGE\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\", scan_specs : [\n",
                (seq == 0 ) ? "" : ",", seq * 10,
     ap->end_point.address,
     ap->end_point.cap_port_id,
     ap->end_point.host,
     ap->end_point.port_id);
/*
 * Now look for Cookie, Error, Offset or Replace data value sets
 */
    for (i = 0;
            json_recognise_scan_spec(sep, ap, seq, x, buf_len - (x - buf), &x);
                 i++);
    x += sprintf(x, " ], data : \"");
/*
 * Now the data to be sent. There is potentially a real problem here with
 * the size.
 */
    if (sep->body_len > WORKSPACE)
    {
        tmpbuf = (char *) malloc(sep->body_len + sep->body_len);
        topbuf = tmpbuf + sep->body_len + sep->body_len;
    }
    else
    {
        tmpbuf = &ap->buf[0];
        topbuf = &ap->buf[WORKSPACE];
    }
    x = js_handle(sep->body, x, sep->body_len, tmpbuf, topbuf);
    x += sprintf(x, "\" }\n");
    if (tmpbuf != &ap->buf[0])
        free(tmpbuf);
    return x;
}
static char * json_recognise_answer(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
char address[HOST_NAME_LEN];
char * tmpbuf;
char * topbuf;
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];
char * x;

    json_recognise_end_point_pair(ap, sep);
    x = buf + sprintf(buf,
"%s{ id : %d, type : \"ANSWER\", to_host : \"%s\", to_port : \"%d\",\n\
from_host : \"%s\", from_port : \"%d\", data : \"",
                (seq == 0 ) ? "" : ",", seq * 10,
            ap->end_point.address,
            ap->end_point.cap_port_id,
            ap->end_point.host,
            ap->end_point.port_id);
/*
 * Now read the data that was received. There is potentially a real problem
 * here with the size.
 */
    if (sep->body_len > WORKSPACE)
    {
        tmpbuf = (char *) malloc(sep->body_len + sep->body_len);
        topbuf = tmpbuf + sep->body_len + sep->body_len;
    }
    else
    {
        tmpbuf = &ap->buf[0];
        topbuf = &ap->buf[WORKSPACE];
    }
    x = js_handle(sep->body, x, sep->body_len, tmpbuf, topbuf);
    x += sprintf(x, "\" }\n");
    if (tmpbuf != &ap->buf[0])
        free(tmpbuf);
    return x;
}
/*
 * Label (\L) records are:
 *     goto target (string, case sensitive)
 */
static void json_recognise_label(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records *ap;
int seq;
char * buf;
int buf_len;
{
char * x;

    if ((x = strchr(sep->head,'\\')) != NULL)
        *x = '\0';
    js_stuff_str(sep->head + 3, ap->buf);
    sprintf(buf,
        "%s{ id : %d, type : \"LABEL\", label : \"%s\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
             ap->buf);
    return;
}
/*
 * Include (\I) records are:
 *     file name (string, local file system semantics)
 */
static void json_recognise_include( sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
char * x;

    if ((x = strchr(sep->head,'\\')) != NULL)
        *x = '\0';
    js_stuff_str(sep->head + 3, ap->buf);
    sprintf(buf,
        "%s{ id : %d, type : \"INCLUDE\", file : \"%s\" }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
             ap->buf);
    return;
}
/*
 * Goto (\G) records are:
 *     label name  (string, case sensitive) : Expression (evaluates to 0 or ! 0)
 *
 * That is, all gotos are conditional, and execute depending on whether the
 * expression evaluates false (0) or true (any other value).
 *
 * For an unconditional goto just put 1
 * To comment out a goto, put 0
 * To execute code, but not do anything otherwise, use a non-existent label
 * (you will get a warning message, though).
 */
static void json_recognise_goto(sep, ap, seq, buf, buf_len)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char label[ADDRESS_LEN + 1];
char * x;

    (void) nextasc_r(sep->head + 3, ':', '\\', &got_to,
                    &(label[0]),
              &(label[ADDRESS_LEN]));
    got_to++;
    if ((x = strchr(got_to, '\\')) != NULL)
        *x = '\0';
    js_stuff_str(got_to, ap->buf);
    sprintf(buf,
        "%s{ id : %d, type : \"GOTO\", label : \"%s\", expr : \"%s\"  }\n",
                (seq == 0 ) ? "" : ",", seq * 10,
             label, ap->buf);
    return;
}
/*
 * Recognise a pattern spec (a counted list of search strings).
 */
static int json_recognise_pattern_spec(sep, got_to, psp, hex_flag)
struct script_element * sep;
unsigned char ** got_to;
PATTERN_SPEC * psp;
int hex_flag;
{
char val_buf[512];
int i;
int len;

    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &val_buf[0], &val_buf[sizeof(val_buf) - 1]) == NULL)
    {
        fprintf(stderr,"%s:%d PATTERN_SPEC Too Short\n",
                         __FILE__, __LINE__);
        return 0;
    }
    if ((psp->cnt = atoi(val_buf)) < 1 || psp->cnt > MAX_PATTERN_SPECS)
    {
        fprintf(stderr,"%s:%d Invalid Number of PATTERN_SPECS:%d\n",
                         __FILE__, __LINE__, psp->cnt);
        return 0;
    }
    for (i = 0; i < psp->cnt; i++)
    {
        if (nextasc_r((char *) NULL, ':', '\\', got_to,
            &val_buf[0], &val_buf[sizeof(val_buf) - 1]) == NULL)
        {
            fprintf(stderr,"%s:%d PATTERN_SPEC Too Short\n",
                                         __FILE__, __LINE__);
            return 0;
        }
        len = strlen(val_buf);
        if (val_buf[len -1 ] == '\n')
        {
            len--;
            val_buf[len] = '\0';
        }
        if (hex_flag)
        {
            hexout(val_buf, val_buf, len);
            len >>= 1;
        }
        psp->bmp[i] = bm_compile_bin(val_buf, len);
    }
    return 1;
}
/****************************************************************************
 * Convert pattern spec to JSON.
 */
char * pattern_spec_to_json(psp, name, sep, ap, buf)
PATTERN_SPEC * psp;
char * name;
struct script_element * sep;
union all_records * ap;
char * buf;
{
int i;
char * x = buf + sprintf(buf, 
          ", %s : {  pattern_cnt : %d, patterns : [ ", name, psp->cnt);
    for (i = 0; i < psp->cnt; i++)
    {
        js_stuff_cnt( psp->bmp[i]->match_word, buf,
                      psp->bmp[i]->match_len);
        x += sprintf(x, "%s{ match : \"%s\" }\n", (i == 0) ?"":",",
                buf);
    }
    x += sprintf(x, " ] }\n");
    return x;
}
/****************************************************************************
 * Convert scan spec to JSON
 */
char * scan_spec_to_json(ssp, seq, sep, ap, buf)
SCAN_SPEC * ssp;
int seq;
struct script_element * sep;
union all_records * ap;
char * buf;
{
char * x = buf + sprintf(buf, "%s{ name : \"%s\", type : \"%c\"\n",
              (seq == 0) ? "" : ",", ssp->scan_key, ssp->c_e_r_o_flag[0]);
    x = pattern_spec_to_json(&(ssp->ebp), "ins",  sep, ap, x);
    if (ssp->c_e_r_o_flag[0] != 'E'
      && ssp->c_e_r_o_flag[0] != 'D'
      && ssp->c_e_r_o_flag[0] != 'A'
      && ssp->c_e_r_o_flag[0] != 'B'
      && ssp->c_e_r_o_flag[0] != 'W'
      && ssp->c_e_r_o_flag[0] != 'G'
      && ssp->c_e_r_o_flag[0] != 'P')
    {
        x += sprintf(x,", in_offset : %d, in_length : %d\n",
                  ssp->i_offset, ssp->i_len);
        x = pattern_spec_to_json(&(ssp->rbp), "out", sep, ap, x);
        x += sprintf(x,", out_offset : %d, out_length : %d\n",
                  ssp->o_offset, ssp->o_len);
    }
    x += sprintf(x, "}\n");
    return x;
}
/**************************************************************************
 * Recognise the scan specifications tacked on the end of the DB directives
 **************************************************************************
 * The scan specifications are:
 * - A; acceptable host pattern
 * - B; host pattern to be blocked
 * - C; something to be seen in a cookie
 * - D; disastrous error
 * - O; a ('find something, substitute somewhere' pair)
 * - U; as O, but the substitution needs to be URL-escaped
 * - H; as O, but the values are in Hexadecimal
 * - S; suspend a substitution
 * - Y; yes, activate a substitution
 * - R; remove a substitution
 * - E; marker for an error
 * - W; marker for a warning
 * - G; marker for a good item
 * - P; cookie to be preserved in the script (one not set by the server)
 *
 * All scan specifications start with:
 * - The flag
 * - A name, used to identify them.
 *
 * For S, Y, and R operations, this is all that is necessary.
 *
 * The other options additionally have associated with them one or two
 * pattern blocks and offsets.
 *
 * A pattern block consists of:
 * -  A pattern count
 * -  The pattern-count number of strings
 *
 * An offset is:
 * -  The offset to the target from the start of the last string
 *
 * E and P operations have a single pattern block.
 *
 * C, O and U operations have a pair of pattern blocks, each followed
 * by an offset.
 *
 * The scan specifications live in hash table, hashed by name, one table
 * per virtual user.
 *
 * The active specifications are in lists, one list per user.
 *
 * For a given user, there is a maximum of MAX_SCAN_SPECS active at any
 * one time.
 *
 * Scan specifications are created active, but can be de-activated
 * immediately.
 ********************************************************************** 
 */
static int json_recognise_scan_spec(sep, ap, seq, buf, buf_len, xp)
struct script_element * sep;
union all_records * ap;
int seq;
char * buf;
int buf_len;
char ** xp;
{
int i;
int j;
SCAN_SPEC validate_spec;
SCAN_SPEC *sp;
char num_buf[32];
unsigned char * got_to = NULL;

    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.c_e_r_o_flag[0],
        &validate_spec.c_e_r_o_flag[sizeof(validate_spec.c_e_r_o_flag) - 1])
                     == NULL)
        return 0;
/*
 * C for a cookie with an offset
 * E for an exception marker
 * O match and replace at the offset
 * U match and replace at the offset and should be URL-encoded
 * P is a cookie to preserve
 * R means remove the last-added element that matches
 * F means freeze the match
 * T means thaw the match
 * S means suspend the last-added element
 * Y means yes, activate a named element.
 */  
    if (validate_spec.c_e_r_o_flag[0] != 'C'
     && validate_spec.c_e_r_o_flag[0] != 'A'
     && validate_spec.c_e_r_o_flag[0] != 'B'
     && validate_spec.c_e_r_o_flag[0] != 'D'
     && validate_spec.c_e_r_o_flag[0] != 'E'
     && validate_spec.c_e_r_o_flag[0] != 'F'
     && validate_spec.c_e_r_o_flag[0] != 'G'
     && validate_spec.c_e_r_o_flag[0] != 'H'
     && validate_spec.c_e_r_o_flag[0] != 'O'
     && validate_spec.c_e_r_o_flag[0] != 'P'
     && validate_spec.c_e_r_o_flag[0] != 'R'
     && validate_spec.c_e_r_o_flag[0] != 'S'
     && validate_spec.c_e_r_o_flag[0] != 'T'
     && validate_spec.c_e_r_o_flag[0] != 'U'
     && validate_spec.c_e_r_o_flag[0] != 'W'
     && validate_spec.c_e_r_o_flag[0] != 'Y')
    {
        fprintf(stderr,"%s:%d SCAN_SPEC Error :%s:\n",
                   __FILE__, __LINE__,
                        validate_spec.c_e_r_o_flag);
        syntax_err(__FILE__,__LINE__,
"Flag not (A)llow, (B)lock, (C)ookie (Offset), (D)isaster, (E)rror, (F)reeze, (G)ood, (H)exadecimal, (O)ffset,\n(P)reserve, (R)emove, (S)ave, (T)haw, (U)rl-encoded (Offset), (W)arning marker for retry or (Y)ank",
             NULL);
        return 0;
    }
/*
 * Recognise the name
 */
    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.scan_key[0],
        &validate_spec.scan_key[sizeof(validate_spec.scan_key) - 1])
                     == NULL)
    {
        fprintf(stderr,"%s:%d Short SCAN_SPEC:%s\n",
                   __FILE__, __LINE__, sep->head);
        return 0;
    }
    if (validate_spec.scan_key[ strlen(validate_spec.scan_key) - 1]
                          == '\n')
        validate_spec.scan_key[strlen(validate_spec.scan_key )-1] = '\0';
/*
 * The SCAN_SPEC must already exist for these
 */
    if (validate_spec.c_e_r_o_flag[0] == 'R'
     || validate_spec.c_e_r_o_flag[0] == 'S'
     || validate_spec.c_e_r_o_flag[0] == 'F'
     || validate_spec.c_e_r_o_flag[0] == 'T'
     || validate_spec.c_e_r_o_flag[0] == 'Y')
    {
        *xp = buf + sprintf(buf, "%s{ name : \"%s\", type : \"%c\" }\n",
              (seq == 0) ? "" : ",", validate_spec.scan_key,
                      validate_spec.c_e_r_o_flag[0]);
        return 1;
    }
/*
 * All others must have at least one PATTERN_SPEC. Note that the Hexadecimal
 * only relates to the replacement pattern (which will be a session ID
 * or similar being replaced, rather than navigating to a position).
 */
    if (!json_recognise_pattern_spec(sep, got_to, &(validate_spec.ebp), 0))
        return 0;
/*
 * If it isn't an A, B, D, E, G, W or P, there is an offset and a further SCAN_SPEC
 * and offset pair to find
 */
    if (validate_spec.c_e_r_o_flag[0] != 'E'
     && validate_spec.c_e_r_o_flag[0] != 'A'
     && validate_spec.c_e_r_o_flag[0] != 'B'
     && validate_spec.c_e_r_o_flag[0] != 'D'
     && validate_spec.c_e_r_o_flag[0] != 'G'
     && validate_spec.c_e_r_o_flag[0] != 'W'
     && validate_spec.c_e_r_o_flag[0] != 'P')
    {
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"%s:%d Short SCAN_SPEC:%s\n",
                   __FILE__, __LINE__, sep->head);
            return 0;
        }
        validate_spec.i_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"%s:%d Short SCAN_SPEC:%s\n",
                   __FILE__, __LINE__, sep->head);
            return 0;
        }
        validate_spec.i_len = atoi(num_buf);
        if (!json_recognise_pattern_spec(sep, got_to, &(validate_spec.rbp),
               (validate_spec.c_e_r_o_flag[0] == 'H')?1:0))
            return 0;
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"%s:%d Short SCAN_SPEC:%s\n",
                   __FILE__, __LINE__, sep->head);
            return 0;
        }
        validate_spec.o_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"%s:%d Short SCAN_SPEC:%s\n",
                   __FILE__, __LINE__, sep->head);
            return 0;
        }
        validate_spec.o_len = atoi(num_buf);
    }
/*
 * Finally, output the scan_spec as JSON.
 */
    *xp = scan_spec_to_json(sp, seq, sep, ap, buf );
    return 1;
}
