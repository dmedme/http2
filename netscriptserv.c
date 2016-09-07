/*
 * netscriptserv.c - script handling routines that should function unchanged
 * regardless of the particular protocol, for a TCP-based protocol being
 * driven at the network level.
 ******************************************************************************
 * These are the routines that manage the communication end points and links.
 *
 * SSL is automagically available if it is compiled in, although .NET remoting
 * in particular can't make use of it.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
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
void recognise_end_point(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[22];
END_POINT * xep;

    strcpy(wdbp->narrative, "recognise_end_point");
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
               a->end_point.address,
               &(a->end_point.address[sizeof(a->end_point.address) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields",
             &(wdbp->parser_con));
    a->end_point.cap_port_id = atoi(ret_buf);
    if ((xep = ep_find(a->end_point.address, a->end_point.cap_port_id, wdbp))
         == NULL)
    {
        a->end_point.end_point_id = wdbp->ep_cnt++;
        if (wdbp->ep_cnt > MAXENDPOINTS)
            scarper(__FILE__, __LINE__,
                    "Too many End Points (maximum is MAXENDPOINTS)",
                        &(wdbp->parser_con));
    }
    else
        a->end_point.end_point_id = xep->end_point_id;
    if (nextasc_r((char *) NULL, ':', '\\', &got_to, a->end_point.host,
                  &(a->end_point.host[sizeof(a->end_point.host) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields",
             &(wdbp->parser_con));
    a->end_point.port_id = atoi(ret_buf);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields",
             &(wdbp->parser_con));
    a->end_point.con_flag = ret_buf[0];
    if (a->end_point.con_flag == 'L')
    {
#ifdef USE_SSL
        a->end_point.ssl_spec_id = -1;
        a->end_point.ssl_sess = NULL;
#endif
        if ( ora_web_match(a->end_point.cap_port_id))
            a->end_point.proto_flag = 1;
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
            for (a->end_point.ssl_spec_id = 0;
                     a->end_point.ssl_spec_id < wdbp->ssl_spec_cnt;
                         a->end_point.ssl_spec_id++)
                if (!strcmp(
                     a->end_point.ssl_spec_ref,  wdbp->ssl_specs[
                         a->end_point.ssl_spec_id].ssl_spec_ref))
                    break;
            if ( a->end_point.ssl_spec_id >= wdbp->ssl_spec_cnt)
                 a->end_point.ssl_spec_id = -1;
        }
#endif
    }
    else
    {
        a->end_point.proto_flag = 0;
#ifdef USE_SSL
        a->end_point.ssl_spec_id = -1;
        a->end_point.ssl_spec_ref[0] = '\0';
        a->end_point.ssl_sess = NULL;
#endif
    }
    return;
}
/*
 * Link messages are Link (\M) and Close (\X).
 * They contain:
 * from_host; from_port: to_host;to_port
 */
void recognise_link(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char address[HOST_NAME_LEN];
int port_id;
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];

    strcpy(wdbp->narrative, "recognise_link");
    (void) nextasc_r(wdbp->parser_con.tlook, ';', '\\', &got_to,
                      &address[0], &address[sizeof(address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few link fields",
             &(wdbp->parser_con));
    port_id = atoi(ret_buf);
    if ((a->link.from_ep = ep_find(address,port_id, wdbp)) ==
        (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ';', '\\', &got_to, &address[0],
                      &address[sizeof(address) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few link fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few link fields",
             &(wdbp->parser_con));
    port_id = atoi( ret_buf);
    if ((a->link.to_ep = ep_find(address,port_id, wdbp)) == (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));
    a->link.connect_fd = -1;
    a->link.pair_seq = 0;
#ifdef DOTNET
    a->link.t3_flag = 1;
#else
    a->link.t3_flag = 0;
#endif
    a->link.remote_handle = (char *) NULL;
#ifdef USE_SSL
    a->link.ssl_spec_id = -1;
#endif
    a->link.h2sp = NULL;
    memset((char *) &(a->link.connect_sock),0,sizeof(a->link.connect_sock));
    memset((char *) &(a->link.in_det),0,sizeof(a->link.in_det));
    memset((char *) &(a->link.out_det),0,sizeof(a->link.out_det));
    return;
}
/*
 * Assemble records for processing by the main loop
 */
enum tok_id webread(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
enum tok_id tok_id;
int i;
/*
 * Skip White Space and Comments, and process things we deal with at this
 * level rather than passing them back (where originally things were scheduled
 * and multi-leaved, before we used Operating System Threads). 
 */
    strcpy(wdbp->narrative, "webread");
    for (;;)
    {
        tok_id = get_tok(wdbp->debug_level, &wdbp->parser_con);
        if (tok_id == E2EOF)
            return E2EOF;
        else
        if (tok_id == E2EOS)
        {
            if (!pop_file(&wdbp->parser_con))
                return E2EOF;
            continue;
        }
        else
        switch(tok_id)
        {
        case E2COMMENT:
            if (wdbp->verbosity)
            {
                fputs(wdbp->parser_con.tbuf, stderr);
                if (wdbp->parser_con.look_status != CLEAR)
                    fputs(wdbp->parser_con.tlook, stderr);
            }
            wdbp->parser_con.look_status = CLEAR;
            break;
        case E2RESET:
/*
 * Reset the saved cookies, or whatever passes for session state.
 */          
            clear_cookie_cache(wdbp);
            wdbp->parser_con.look_status = CLEAR;
            break;
/*
 * Perform a conditional branch
 */          
        case E2GOTO:
            recognise_goto(NULL, wdbp);
            wdbp->parser_con.look_status = CLEAR;
            break;
        case E2SCANSPECS:
        {
            unsigned char * got_to = wdbp->parser_con.tlook;
            while (recognise_scan_spec(NULL, wdbp, &got_to) == SCAN_SPEC_TYPE);
            wdbp->parser_con.look_status = CLEAR;
            break;
        }
/*
 * Declare a branch target or a function
 */          
        case E2LABEL:
            recognise_label(NULL, wdbp);
            wdbp->parser_con.look_status = CLEAR;
            break;
/*
 * Open an include file.
 */          
        case E2INCLUDE:
            wdbp->parser_con.look_status = CLEAR;
            if (recognise_include(NULL, wdbp) == E2EOS)
            {
                if (!pop_file(&wdbp->parser_con))
                    return E2EOF;
                continue;
            }
            break;
        default:
            goto will_pass_back;
        }
    }
will_pass_back:
    if (wdbp->parser_con.look_status == CLEAR)
    {
        fprintf(stderr,
         "(Client:%s) Problem Token: %d Value: %s Look: %s sav_tlook: %s\n",
                               wdbp->parser_con.pg.rope_seq, (int) tok_id, wdbp->parser_con.tbuf,
                                      wdbp->parser_con.tlook, wdbp->parser_con.sav_tlook);
        syntax_err(__FILE__,__LINE__,"There should be a look-ahead token",
             &(wdbp->parser_con));
    }
    a->end_point.record_type = tok_id;  /* It's in the same position in every
                                         record */
    switch(tok_id)
    {
    case  END_POINT_TYPE:
        recognise_end_point(a, wdbp);
        break;
    case  E2BEGIN:
        tok_id = recognise_message(a, wdbp);
        break;
    case  E2ABEGIN:
        tok_id = recognise_answer(a, wdbp);
        break;
#ifdef USE_SSL
    case  SSL_SPEC_TYPE:
        recognise_ssl_spec(a, wdbp);
        break;
#endif
    case  LINK_TYPE:
    case  CLOSE_TYPE:
        recognise_link(a, wdbp);
        break;
    case  START_TIMER_TYPE:
        recognise_start_timer(a, wdbp);
        break;
    case  TAKE_TIME_TYPE:
        recognise_take_time(a, wdbp);
        break;
    case  QUIESCENT_TYPE:
        break;
    case  DELAY_TYPE:
        recognise_delay(a, wdbp);
        break;
    case  PAUSE_TYPE:
        recognise_delay(a, wdbp);
        break;
    default:
        fprintf(stderr,"(Client:%s) Token: %d\n",
                            wdbp->parser_con.pg.rope_seq, (int) tok_id);
        syntax_err(__FILE__,__LINE__,"Invalid control file format",
             &(wdbp->parser_con));
    }
    if (wdbp->parser_con.look_status != KNOWN)
        wdbp->parser_con.look_status = CLEAR;
    return tok_id;
}
/***************************************************************************
 * Function to handle control file data.
 */
void progress_client(wdbp)
WEBDRIVE_BASE * wdbp;
{
enum tok_id record_type;

    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) progress_client()", wdbp->parser_con.pg.rope_seq);
    while ((record_type = wdbp->webread(&(wdbp->in_buf), wdbp)) != E2EOF)
    {
        wdbp->rec_cnt++;
        strcpy(wdbp->narrative, "progress_client");
        if (wdbp->debug_level > 2)
        {
            (void) fprintf(stderr,"(Client:%s) Control File Service Loop\n\
=====================================\n\
Line: %d Record Type: %d\n",
                               wdbp->parser_con.pg.rope_seq,
                               wdbp->rec_cnt,
                         (int) record_type);
        }
        switch (record_type)
        {
        case END_POINT_TYPE:
/*
 * Add the end point to the array
 */
            do_end_point(&(wdbp->in_buf), wdbp);
            break;
#ifdef USE_SSL
#ifndef TUNDRIVE
        case SSL_SPEC_TYPE:
/*
 * Initialise SSL
 */
            do_ssl_spec(&(wdbp->in_buf), wdbp);
            break;
#endif
#endif
        case SEND_RECEIVE_TYPE:
/*
 * Send the message and receive the response.
 */          
            wdbp->do_send_receive(&(wdbp->in_buf), wdbp);
            break;
        case QUIESCENT_TYPE:
/*
 * Have to wait for responses before continuing.
 */          
            if (wdbp->do_send_receive == do_send_receive_async)
                do_send_receive_async(NULL, wdbp);
            break;
        case SEND_FILE_TYPE:
/*
 * Send the file. Not used.
 */          
            break;
        case START_TIMER_TYPE:
/*
 * Set up the timer.
 */          
            do_start_timer(&(wdbp->in_buf), wdbp);
            break;
        case TAKE_TIME_TYPE:
/*
 * Record the time.
 */          
            if (wdbp->do_send_receive == do_send_receive_async)
                do_send_receive_async(NULL, wdbp);
            if (do_take_time(&(wdbp->in_buf), wdbp))
                return;     /* The client has been re-scheduled */
            break;
        case PAUSE_TYPE:
/*
 * Wait the allotted span.
 */          
            if (do_pause(&(wdbp->in_buf), wdbp))
                return;     /* The client has been re-scheduled */
            break;
        case DELAY_TYPE:
/*
 * Wait the allotted span.
 */          
            if (do_delay(&(wdbp->in_buf), wdbp))
                return;     /* The client has been re-scheduled */
            break;
        case CLOSE_TYPE:
/*
 * Close a link
 */          
            do_close(&(wdbp->in_buf), wdbp);
            break;
        case LINK_TYPE:
/*
 * Connect a link if this is new.
 */          
            do_link(&(wdbp->in_buf), wdbp);
            break;
        case E2COMMENT:
            break;
        default:
            fprintf(stderr, "(Client:%s) Token: %d\n",
                            wdbp->parser_con.pg.rope_seq, (int) record_type);
            syntax_err(__FILE__,__LINE__, "this token invalid at this point",
             &(wdbp->parser_con));
            break;
        }
    }
    return;
}
