/*
 *    tdsserv.c - Routines to drive and collect timings from TDS stream
 *
 *    Copyright (C) E2 Systems 1993, 2000, 2006, 2009
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1993, 2009";
#include "webdrive.h"
char * bind_host;

static struct named_token {
char * tok_str;
enum tok_id tok_id;
} known_toks[] = {
{"M_TDS_RPC", E2M_TDS_RPC},
{"M_TDS_SQLBATCH", E2M_TDS_SQLBATCH},
{"T_TDS_BIGCHAR", E2T_TDS_BIGCHAR},
{"T_TDS_BIGVARCHAR", E2T_TDS_BIGVARCHAR},
{"T_TDS_BITN", E2T_TDS_BITN},
{"T_TDS_DATETIMEN", E2T_TDS_DATETIMEN},
{"T_TDS_FLTN", E2T_TDS_FLTN},
{"T_TDS_INTN", E2T_TDS_INTN},
{"T_TDS_MONEYN", E2T_TDS_MONEYN},
{"", E2STR}};
/*
 * This function is empty ...
 */
void init_from_environment()
{
    return;
}
/*
 * Process arguments
 */
void proc_args(argc,argv)
int argc;
char ** argv;
{
int c;
char *x;
WEBDRIVE_BASE * wdbp;
int path_stagger;              /* Interval between client launchings */
int rope;                      /* Which client this is               */
/****************************************************
 * Initialise.
 */
    webdrive_base.parser_con.pg.curr_event = (struct event_con *) NULL;
    webdrive_base.parser_con.pg.abort_event = (struct event_con *) NULL;
    webdrive_base.parser_con.pg.log_output = stdout;
    webdrive_base.parser_con.pg.frag_size = WORKSPACE;
    webdrive_base.verbosity = 0;
    webdrive_base.msg_seq = 1;                /* request sequencer         */
    webdrive_base.client_cnt = 1;             /* Client count              */
    webdrive_base.parser_con.pg.seqX = 0;     /* timestamp sequencer       */
    webdrive_base.progress_client = progress_client;
    webdrive_base.webread = webread;
    while ( ( c = getopt ( argc, argv, "hd:v:m:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'h' :
            (void) fprintf(stderr,"tdsdrive: E2 Systems MS SQL Driver\n\
Options:\n\
 -h prints this message on stderr\n\
 -v sets verbose level (all packets are timestamped and may be logged)\n\
 -m says how many clients we are going to handle\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Output File, Run ID, Bundle ID, Rope, Input File\n");
            fflush(stderr);
            break;
        case 'd' :
            webdrive_base.debug_level = atoi(optarg);
            break;
        case 'v' :
            webdrive_base.verbosity = 1 + atoi(optarg);
            break;
        case 'm' :
            if ((webdrive_base.client_cnt = atoi(optarg)) < 1)
                webdrive_base.client_cnt = 1;    /* Client count              */
            break;
        default:
        case '?' : /* Default - invalid opt.*/
            (void) fprintf(stderr,"Invalid argument; try -h\n");
#ifdef MINGW32
            WSACleanup();
#endif
            exit(1);
        } 
    }
    if ((argc - optind) < (4 + webdrive_base.client_cnt) )
    {
        fprintf(stderr,
            "Insufficient Arguments Supplied; try -h\nSeen: %d Expected: %d\n",
         argc, (4 + webdrive_base.client_cnt + optind));
             dump_args(argc, argv);
#ifdef MINGW32
        WSACleanup();
#endif
        exit(1);
    } 
    if (webdrive_base.verbosity > 1)
        dump_args(argc, argv);
    webdrive_base.parser_con.pg.logfile=argv[optind++];
    webdrive_base.parser_con.pg.fdriver_seq=argv[optind++]; /* Details needed by event   */
    webdrive_base.parser_con.pg.bundle_seq=argv[optind++];  /* recording                 */
    webdrive_base.parser_con.pg.rope_seq=argv[optind++]; 
    rope = atoi(webdrive_base.parser_con.pg.rope_seq);
    webdrive_base.parser_con.pg.think_time = PATH_THINK;  /* default think time */
    webdrive_base.root_wdbp = &webdrive_base;
    webdrive_base.idle_threads = circbuf_cre(webdrive_base.client_cnt + 3,NULL);
    pthread_mutex_init(&(webdrive_base.idle_thread_mutex), NULL);
    webdrive_base.go_times = circbuf_cre(webdrive_base.client_cnt + 3, NULL);
    pthread_mutex_init(&(webdrive_base.go_time_mutex), NULL);
    if ((x = getenv("PATH_STAGGER")) == (char *) NULL
      || ((path_stagger = atoi(x)) < 1))
        path_stagger = 1;             /* Default interval */
/*
 * Now set up the control structures for each client
 */
    webdrive_base.client_array = (WEBDRIVE_BASE *)
                    malloc(sizeof(webdrive_base) * webdrive_base.client_cnt);
    for (c = 0, wdbp = &(webdrive_base.client_array[0]);
             c < webdrive_base.client_cnt;
                  c++, wdbp++)
    {

        webdrive_base.control_file = argv[optind++];
        if (optind > argc
         || (webdrive_base.parser_con.pg.cur_in_file =
             fopen(webdrive_base.control_file,"rb"))
                 == (FILE *) NULL)
        {
            unexpected(__FILE__, __LINE__,"Failed to open control file");
#ifdef MINGW32
            WSACleanup();
#endif
            wdbp->parser_con.pg.rope_seq = (char *) NULL;  /* Mark it out of use */
            continue;
        }
        *wdbp = webdrive_base;
        ini_parser_con(&wdbp->parser_con);
/*
 * Set up the hash table for events
 */
        wdbp->parser_con.pg.poss_events = hash(MAX_EVENT,long_hh,icomp);
/*
 * Schedule the client
 */
        add_time(&webdrive_base, wdbp, (c + 1) * path_stagger);
        
        wdbp->parser_con.pg.rope_seq = (char *) malloc(12);
        sprintf(wdbp->parser_con.pg.rope_seq,"%u", rope);
        wdbp->parser_con.pg.logfile = malloc(2 + strlen(webdrive_base.parser_con.pg.logfile)
                  + strlen(wdbp->parser_con.pg.rope_seq));
        (void) sprintf(wdbp->parser_con.pg.logfile, "%s.%s", webdrive_base.parser_con.pg.logfile,
                       wdbp->parser_con.pg.rope_seq);
        event_record_r("S", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                             /* Announce the start */
        rope++;
    }
    webdrive_base.own_thread_id = pthread_self();
    if (webdrive_base.debug_level > 1)
    {
        (void) fputs("proc_args()\n", stderr);
        (void) fflush(stderr);
        dump_args(argc, argv);
    }
    return;
}
/*
 * Link messages are Link (\I)
 * They contain:  from_port
 */
enum tok_id recognise_link(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char address[32];
int port_id;
struct named_token * cur_tok;
int len;
int str_length;
unsigned char * got_to = (unsigned char *) NULL;
FILE *fp;

    memset((char *) &(a->link),0,sizeof(a->link));
    a->link.record_type = SQLLINK_TYPE;
    a->link.from_port_id = atoi(nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
                      &address[0], &address[sizeof(address) - 1]));
    a->link.cur_link = link_find(a->link.from_port_id, wdbp);
    if (a->link.cur_link->from_port_id == 0)
    {
/*
 * Needs initialising
 */
        if (sqllink_up(a->link.cur_link, wdbp))
            a->link.cur_link->from_port_id =  a->link.from_port_id;
        wdbp->cur_link = a->link.cur_link;
    }
    fp = wdbp->parser_con.pg.cur_in_file;
    if ((len = getescline(fp, wdbp->debug_level, &wdbp->parser_con)) == EOF)
        return;
    for ( cur_tok = known_toks,
         str_length = strlen(cur_tok->tok_str);
             (str_length > 0);
                  cur_tok++,
                  str_length = strlen(cur_tok->tok_str))
        if (!strncmp(wdbp->parser_con.tlook,cur_tok->tok_str,str_length))
            break;
    if (!str_length)
    {
        fprintf(stderr, "(Client:%s) Unrecognised string: %s\n",
                wdbp->parser_con.pg.rope_seq, wdbp->parser_con.tlook);
    }
    else
    {
    unsigned char * x;

        wdbp->last_tok = cur_tok->tok_id;
        wdbp->parser_con.tlook += str_length + 1;
        a->link.msg = &(wdbp->msg);
        if (wdbp->last_tok == E2M_TDS_SQLBATCH)
        {              /* Pick up the whole sequence */
            x = &(wdbp->msg.buf[0]);
            for (;;)
            {
                if ((len = getescline(fp, wdbp->debug_level,
                            &wdbp->parser_con)) == EOF)
                    return E2EOF;     /* Return EOF if no more */
                if (*(wdbp->parser_con.tlook) == '\n')
                    break;
                memcpy(x, wdbp->parser_con.tlook, len);
                x += len;
            }
            *x = '\0';
            a->link.fun = 0;
            a->link.mess_len = x - (unsigned char *) &(wdbp->msg.buf[0]);
        }
        else
        if (wdbp->last_tok == E2M_TDS_RPC)
        {              /* Pick up the whole sequence */

            a->link.mess_len = len - str_length - 2;
            memcpy(&(wdbp->msg.buf[0]),wdbp->parser_con.tlook, 
                a->link.mess_len);
            wdbp->msg.buf[a->link.mess_len] = '\0';
            a->link.fun = 1;
            for (;;)
            {
                if ((len = getescline(fp, wdbp->debug_level,
                            &wdbp->parser_con)) == EOF)
                    return E2EOF;     /* Return EOF if no more */
                if (*(wdbp->parser_con.tlook) == '\n')
                    break;
                wdbp->parser_con.tlook[len - 1] = '\0';
                x = strchr(wdbp->parser_con.tlook,'|');
                if (x != (unsigned char *) NULL)
                {
                    *x = '\0';
                    alloc_param(wdbp,wdbp->parser_con.tlook,
                        len - (x - (unsigned char *) wdbp->parser_con.tlook) - 2, x + 1);
                }
            }
        }
        else
            syntax_err(__FILE__,__LINE__, "Invalid format sql record",
             &(wdbp->parser_con));
    }
    wdbp->parser_con.look_status = CLEAR;
    return SQLLINK_TYPE;
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
 * Skip White Space and Comments
 */
    for (;;)
    {
        tok_id = get_tok( wdbp->debug_level, &wdbp->parser_con);
        if (tok_id == E2EOF)
            return tok_id;
        else
        if (tok_id == E2COMMENT)
        {
            if (wdbp->verbosity)
            {
                fputs(wdbp->tbuf, stderr);
                if (wdbp->parser_con.look_status != CLEAR)
                    fputs(wdbp->parser_con.tlook, stderr);
            }
            wdbp->parser_con.look_status = CLEAR;
        }
        else
        if ((tok_id == E2RESET)
         ||(tok_id == E2STR && wdbp->tbuf[0] == '\n' ))
        {
            wdbp->parser_con.look_status = CLEAR;
        }
        else
            break;
    }
    if (wdbp->parser_con.look_status == CLEAR)
    {
        fprintf(stderr,
         "(Client:%s) Problem Token: %d Value: %s Look: %s sav_tlook: %s\n",
                               wdbp->parser_con.pg.rope_seq, (int) tok_id, wdbp->tbuf,
                                      wdbp->parser_con.tlook, wdbp->parser_con.sav_tlook);
        syntax_err(__FILE__,__LINE__,"There should be a look-ahead token",
             &(wdbp->parser_con));
    }
    a->link.record_type = tok_id;  /* It's in the same position in every
                                         record */
    switch(tok_id)
    {
    case  SQLLINK_TYPE:
        recognise_link(a, wdbp);
        break;
    case  START_TIMER_TYPE:
        recognise_start_timer(a, wdbp);
        break;
    case  TAKE_TIME_TYPE:
        recognise_take_time(a, wdbp);
        break;
    case  PAUSE_TYPE:
        recognise_delay(a, wdbp);
        break;
    case  DELAY_TYPE:
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
enum tok_id rec_type;

    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) progress_client()", wdbp->parser_con.pg.rope_seq);
    while ((rec_type = wdbp->webread(&(wdbp->in_buf), wdbp)) != E2EOF)
    {
        wdbp->rec_cnt++;
        if (wdbp->debug_level > 2)
        {
            (void) fprintf(stderr,"(Client:%s) Control File Service Loop\n\
=====================================\n\
Line: %d Record Type: %d\n",
                               wdbp->parser_con.pg.rope_seq,
                               wdbp->rec_cnt,
                         (int) rec_type);
        }
        switch (rec_type)
        {
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
 * Close a link. We don't?
 */          
            break;
        case SQLLINK_TYPE:
/*
 * Connect a link if this is new.
 */          
            do_link(&(wdbp->in_buf), wdbp);
/*
 * Send the message and receive the response.
 */          
            do_send_receive(&(wdbp->in_buf), wdbp);
            break;
        case E2COMMENT:
            break;
        default:
            fprintf(stderr, "(Client:%s) Token: %d\n",
                            wdbp->parser_con.pg.rope_seq, (int) rec_type);
            syntax_err(__FILE__,__LINE__, "this token invalid at this point",
             &(wdbp->parser_con));
            break;
        }
    }
    return;
}
/***************************************************************************
 * Function to recognise a link definition
 */
void do_link(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_link(%d)\n",
            wdbp->parser_con.pg.rope_seq,
            a->link.from_port_id);
        fflush(stderr);
    }
    wdbp->cur_link = a->link.cur_link;
/*
 * See if we have already encountered this link. If we have not done
 * so, initialise it.
 */
    if (wdbp->cur_link->from_port_id == 0)
    {
/*
 * Needs initialising
 */
        if (sqllink_up(wdbp->cur_link, wdbp))
            wdbp->cur_link->from_port_id =  a->link.from_port_id;
    }
    return;
}
/***************************************************************************
 * Function to recognise a socket close definition
 */
void do_close(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_close(%d)\n",
            wdbp->parser_con.pg.rope_seq,
            a->link.from_port_id);
        fflush(stderr);
    }
    wdbp->cur_link = a->link.cur_link;
/*
 * See if we have already encountered this link. If we have not done
 * so, we have a logic error.
 */
    if (wdbp->cur_link->from_port_id == 0)
        fprintf(stderr,
           "(%s:%d Client:%s) Logic Error: closing a non-open connexion\n",
                  __FILE__, __LINE__, wdbp->parser_con.pg.rope_seq);
    else
    {
        if (sqllink_down(wdbp->cur_link))
            wdbp->cur_link->from_port_id = 0;
    }
    return;
}
/************************************************************************
 * Find the link, given the from
 */
SQLLINK * link_find(from_port_id,  wdbp)
int from_port_id;
WEBDRIVE_BASE * wdbp;
{
SQLLINK * cur_link;

    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"(Client:%s) link_find(%d)\n",
                    wdbp->parser_con.pg.rope_seq,
                    from_port_id);
    for (cur_link = wdbp->link_det;
                cur_link->from_port_id != 0;
                     cur_link++)
        if (cur_link->from_port_id == from_port_id)
            break;
    return cur_link;
}
/***********************************************************************
 * Process messages. This routine is only called if there is something
 * to do.
 *
 * SQLServer-specific code is included here. The general solution would
 * provide hooks into which any TCP/IP based message passing scheme could
 * be attached.
 */
void do_send_receive(msg, wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
unsigned char * p1;
int rc;

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Send Receive Message Sequence %d\n",
            wdbp->parser_con.pg.rope_seq,
                   wdbp->parser_con.pg.seqX);
        fflush(stderr);
    }
    if (wdbp->cur_link->from_port_id == 0)
    {
        fprintf(stderr,
                   "(Client:%s) From: %d Not Connected!?\n",
                        wdbp->parser_con.pg.rope_seq,
                        wdbp->cur_link->from_port_id);
        if (sqllink_up(wdbp->cur_link, wdbp))
            wdbp->cur_link->from_port_id = msg->link.from_port_id;
    }
    if (wdbp->verbosity)
        event_record_r("T", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                         /* Note the message */
    wdbp->alert_flag = 0;  /* To distinguish random and purposeful interrupts */
    alarm_preempt(wdbp);
    if (msg->link.fun)
    {
        rc = sqllink_rpc(wdbp->cur_link, msg->link.msg, wdbp->params);
        wdbp->params = NULL;
    }
    else
        rc = sqllink_sql(wdbp->cur_link, msg->link.msg);
    alarm_restore(wdbp);
    if (rc)
        event_record_r("X", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                                         /* Note the message */
    else
    if (wdbp->verbosity )
        event_record_r("R", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                                          /* Note the message */
    return;
}
/*
 * Read the next token
 * -  There are at most two tokens a line
 * -  Read a full line, taking care of escape characters
 * -  Search to see what the cat brought in
 * -  Be very careful with respect to empty second tokens
 * -  Return  
 */
enum tok_id get_tok( debug_level, pcp)
int debug_level;
WEBDRIVE_BASE * pcp;
{
int len;
int str_length;
FILE * fp = pcp->pg.cur_in_file;
/*
 * If no look-ahead present, refresh it
 */
    if (debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) get_tok(%x); LOOK_STATUS=%d\n",
                      pcp->parser_con.pg.rope_seq, (long int) fp,
                      pcp->look_status);
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
        mv_look_buf( strlen(pcp->tlook), pcp);
        pcp->look_status = CLEAR;
        return pcp->last_tok;
    }
    else
 */
    if (pcp->look_status != PRESENT)
    {
restart:
        if ((len = getescline(fp, debug_level, pcp)) == EOF)
            return E2EOF;            /* Return EOF if no more */
/*
 * Commands are only allowed at the start of a line
 */
        if (debug_level > 3)
            fprintf(stderr,"(Client:%s) Input Line: %s",
                   pcp->parser_con.pg.rope_seq,
                   pcp->tlook);
        pcp->last_look = pcp->look_status;
        if (*(pcp->tlook) == '\\')
        {        /* Possible PATH Command */
        char c = *(pcp->tlook + 1);

            switch (c)
            {
/*
 * Comment
 */
            case 'C':
                *(pcp->tbuf) = '\0';
                pcp->last_tok = E2COMMENT;
                break;
/*
 * Save a username
 */
            case 'U':
                *(pcp->tbuf) = '\0';
		memcpy(pcp->username, pcp->tlook +3,len -4);
                pcp->last_tok = E2COMMENT;
                break;
/*
 * SQL
 */
            case 'I':
                pcp->tlook += 8;
                pcp->last_tok = SQLLINK_TYPE;
                break;
/*
 * Think time in seconds
 */
            case 'W':
                pcp->tlook += 2;
                pcp->last_tok = DELAY_TYPE;
                break;
/*
 * Pause in seconds
 */
            case 'P':
                pcp->tlook += 2;
                pcp->last_tok = PAUSE_TYPE;
                break;
/*
 * Start timer
 */
            case 'S':
                *(pcp->tlook + len - 2) = '\0';
                pcp->tlook += 2;
                pcp->last_tok = START_TIMER_TYPE;
                break;
/*
 * End timer
 */
            case 'T':
                pcp->tlook += 2;
                pcp->last_tok = TAKE_TIME_TYPE;
                break;
            case '\n':
                goto restart;
            default:
                fprintf(stderr,"(Client:%s) Format problem with line: %s\n",
                                         pcp->parser_con.pg.rope_seq,
                                        pcp->tlook);
                break;
            }
            if (debug_level > 2)
                fprintf(stderr,"(Client:%s) Token: %d\n",
                                pcp->parser_con.pg.rope_seq, (int) pcp->last_tok);
            return pcp->last_tok;
        }
    }
    else
        len = strlen(pcp->tlook);
    pcp->last_tok = E2STR;
    mv_look_buf( len, pcp);
    pcp->look_status = CLEAR;
    if (debug_level > 2)
        fprintf(stderr,
"(Client:%s) Token: %d LOOK_STATUS=%d lookahead: %d tbuf: %s tlook: %s sav_tlook: %s\n",
                pcp->parser_con.pg.rope_seq, (int) pcp->last_tok,
                pcp->look_status,  pcp->last_look,
                pcp->tbuf,  pcp->tlook, pcp->sav_tlook);
    return pcp->last_tok;
}
