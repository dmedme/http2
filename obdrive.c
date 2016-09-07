/*
 *    obdrive.c - Program to drive and collect timings from Web stream
 *
 *    Copyright (C) E2 Systems 1993, 2000
 *
 *    Timestamps are written when the appropriate events are logged.
 *
 * Signal handling
 * ===============
 * SIGTERM - terminate request
 * SIGBUS  - should not happen (evidence of machine stress? very rare)
 * SIGSEGV - should not happen (evidence of programmer error)
 * SIGALRM - used to control typing rate
 * SIGCHLD - watching for death of process
 *
 * Arguments
 * =========
 *   - arg 1 = name of file to output timestamps to
 *   - arg 2 = Id of fdriver
 *   - arg 3 = Id of bundle
 *   - arg 4 = i number within 'rope'
 *   - arg 5 = Input command file
 *   - arg6 ....   Possible additional input files 
 *
 * Signal handling
 * ===============
 * SIGTERM - terminate request
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995";
#ifdef MINGW32
#include <windows.h>
#include <winsock2.h>
#include <process.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SLEEP_FACTOR 1000
#ifdef perror
#undef perror
#endif
#define perror(x) fprintf(stderr,"%s: error: %x\n",x,GetLastError())
#else
#define closesocket close
#define SLEEP_FACTOR 1
#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#ifdef V32
#include <time.h>
#else
#include <sys/time.h>
#endif
#ifdef SEQ
#include <fcntl.h>
#include <time.h>
#else
#ifdef ULTRIX
#include <fcntl.h>
#else
#ifdef AIX
#include <fcntl.h>
#else
#include <sys/fcntl.h>
#endif
#endif
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#ifdef PYR
#include <strings.h>
#endif
#endif
#include "hashlib.h"
#ifndef MINGW32
#include "e2net.h"
#endif
#include "matchlib.h"
#include "circlib.h"
#include "webdrive.h"
#ifndef SD_SEND
#define SD_SEND 1
#endif

extern int errno;
WEBDRIVE_BASE webdrive_base;      /* Global configuration information */
/***********************************************************************
 * Functions in this file
 *
 * Message handling routines
 */
void do_end_point();
int do_delay();
void do_start_timer();
int do_take_time();
void do_link();
void do_close();
void do_send_receive();
END_POINT * ep_find();
LINK * link_find();
static enum tok_id get_tok();
void siggoaway();       /* catch terminate signal */
void scarper();         /* exit, tidying up */
void proc_args();       /* process arguments */
void obdrive_connect(); /* Connect to remote socket */
void obdrive_listen();  /* Set up the socket that listens for link connect
                           requests */

static struct named_token {
char * tok_str;
enum tok_id tok_id;
} known_toks[] = {
{"", E2STR}};

/***********************************************************************
 * Main Program Starts Here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
#ifdef MINGW32
static int glob_argc;
static char** glob_argv;
void peermain(void * p);
#endif
int main(argc,argv,envp)
int argc;
char * argv[];
char * envp[];
{
WEBDRIVE_BASE * wdbp;
int i;
#ifdef SOLAR
    setvbuf(stderr, NULL, _IOFBF, BUFSIZ);
#endif
#ifdef MINGW32
WORD wVersionRequested;
WSADATA wsaData;
wVersionRequested = 0x0101;

    if ( WSAStartup( wVersionRequested, &wsaData ))
    {
        fprintf(stderr, "WSAStartup error: %d", errno);
        exit(1);
    }
#ifdef LCC
    tick_calibrate();
#endif
    glob_argc = argc;
    glob_argv = argv;
    WaitForSingleObject( CreateThread( NULL,  /* LPSECURITY_ATTRIBUTES  */
                                     0,    /* Stack Size (default)   */ 
                                     peermain, /* LPTHREAD_START_ROUTINE */
                                     argv,  /* LPVOID                 */
                                     0,    /* Run immediately        */ 
                                    &argc), INFINITE);
    exit(0);
}
/*
 * Avoid problems with message loops in case this thread has one associated
 * with a window
 */
void peermain(void * p)
{
int argc;
WEBDRIVE_BASE * wdbp;
int i;
char ** argv;
MSG msg;

    argc = glob_argc;
    argv = glob_argv;

    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
                    /* Make sure this thread has a message queue */
#endif
/****************************************************
 *    Initialise
 */
    proc_args(argc,argv);
/*
 * Now have to set up the clones of webdrive_base for each client, and
 * start firing off threads to deal with them.
 *
 * The processing is as follows.
 *
 * We have two circular buffers, each sized in relation to the number
 * of clients that we are handling.
 *
 * One holds a list of times for next processing of the given client.
 *
 * The other holds idle threads.
 *
 * The processing time list is initialised with each client in turn,
 * separated by 'client stagger'.
 *
 * The idle thread list starts empty.
 *
 * During normal running, a thread processes a script until it reaches the end,
 * it encountered an explicit wait, or it comes to a timing point.
 *
 * It computes the interval before the client needs to be active again, and
 * places the next time in the correct place in the time cyclic buffer.
 *
 * If it inserts the new value ahead of an existing value that is being waited
 * for, the alarm time is reduced accordingly.
 *
 * Now it deals with timeouts that have expired.
 *
 * If there are no events at all, expired or non-expired, we must be done. The
 * thread terminates.
 *
 * For each expired timeout except the last:
 * -  If a thread is to be found on the idle list, it is despatched to deal
 *    with the client.
 * -  Otherwise, a new thread is created, and that is despatched instead.
 *
 * The thread executes the last expired event, if there are no further
 * unexpired events, or if there is already a waiter. Otherwise the thread
 * despatches another new or idle thread, and becomes the waiter itself.
 */
    schedule_clients(&webdrive_base);         /* Process the input files */
    for (wdbp = &webdrive_base.client_array[0], i = 0;
            i < webdrive_base.client_cnt;
                 i++, wdbp++)
    {
        free(wdbp->tbuf);
        free(wdbp->sav_tlook);
        wdbp->pg.seqX = wdbp->rec_cnt;
        event_record_r("F", (struct event_con *) NULL, &(wdbp->pg));
                                    /* Announce the finish */
    }
    free(webdrive_base.client_array);
#ifdef MINGW32
    WSACleanup();
#endif
    exit(0);
}
/*****************************************************************************
 * Handle unexpected errors
 */
void unexpected(file_name,line,message)
char * file_name;
int line;
char * message;
{
    (void) fprintf(stderr,"Unexpected Error %s,line %d\n",
                   file_name,line);
    perror(message);
    (void) fprintf(stderr,"UNIX Error Code %d\n", errno);
    (void) fflush(stderr);
    return;
}
#undef select
/*******************
 * Global data
 */
void siggoaway()
{
WEBDRIVE_BASE * wdbp;
int i;

    for (wdbp = &webdrive_base.client_array[0], i = 0;
            i < webdrive_base.client_cnt;
                i++, wdbp++)
        event_record_r("F", (struct event_con *) NULL, &(wdbp->pg));
                               /* Mark the end */
#ifdef MINGW32
    WSACleanup();
#endif
    exit(1);
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
    (void) sigset(SIGINT,SIG_IGN);
#ifdef AIX
    (void) sigset(SIGDANGER,SIG_IGN);
#endif
    (void) sigset(SIGUSR1,siggoaway);
    (void) sigset(SIGTERM,siggoaway);
                            /* Initialise the termination signal catcher */
#ifndef V32
#ifndef MINGW32
    (void) sigset(SIGTTOU,SIG_IGN);
                             /* Ignore silly stops */
    (void) sigset(SIGTTIN,SIG_IGN);
                             /* Ignore silly stops */
    (void) sigset(SIGTSTP,SIG_IGN);
                             /* Ignore silly stops */
#endif
#endif
    (void) sigset(SIGUSR1,siggoaway);       /* In order to exit */
#ifndef MINGW32
    (void) sigset(SIGCLD,SIG_DFL);
#endif
#ifndef LCC
    (void) sigset(SIGPIPE,SIG_IGN);   /* So we don't crash out */
#endif
    (void) sigset(SIGHUP,SIG_IGN);    /* So we don't crash out */
    (void) sigset(SIGALRM, read_timeout);
    (void) sigset(SIGUSR2, read_timeout);
                                      /* To give Linux/Solaris compatibility */
    sighold(SIGTERM);                 /* So we stay in control */
    sighold(SIGUSR1);                 /* So we stay in control */
    sighold(SIGUSR2);                 /* So we stay in control */
    webdrive_base.pg.curr_event = (struct event_con *) NULL;
    webdrive_base.pg.abort_event = (struct event_con *) NULL;
    webdrive_base.pg.log_output = stdout;
    webdrive_base.pg.frag_size = WORKSPACE;
    webdrive_base.verbosity = 0;
    webdrive_base.msg_seq = 1;                /* request sequencer         */
    webdrive_base.client_cnt = 1;             /* Client count              */
    webdrive_base.pg.seqX = 0;                /* timestamp sequencer       */
    while ( ( c = getopt ( argc, argv, "hd:v:m:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'h' :
            (void) fprintf(stderr,"obdrive: E2 Systems Objective Driver\n\
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
    if ((argc - optind) < 5)
    {
        fprintf(stderr,"Insufficient Arguments Supplied; try -h\n");
#ifdef MINGW32
        WSACleanup();
#endif
        exit(1);
    } 
    webdrive_base.pg.logfile=argv[optind++];
    webdrive_base.pg.fdriver_seq=argv[optind++]; /* Details needed by event   */
    webdrive_base.pg.bundle_seq=argv[optind++];  /* recording                 */
    webdrive_base.pg.rope_seq=argv[optind++]; 
    rope = atoi(webdrive_base.pg.rope_seq);
    webdrive_base.pg.think_time = PATH_THINK;  /* default think time */
    webdrive_base.ep_cur_ptr = webdrive_base.end_point_det,
    webdrive_base.ep_max_ptr = &(webdrive_base.end_point_det[MAXENDPOINTS-1]);
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
         || (webdrive_base.pg.cur_in_file =
             fopen(webdrive_base.control_file,"rb"))
                 == (FILE *) NULL)
        {
            unexpected(__FILE__, __LINE__,"Failed to open control file");
#ifdef MINGW32
            WSACleanup();
#endif
            wdbp->pg.rope_seq = (char *) NULL;  /* Mark it out of use */
            continue;
        }
        *wdbp = webdrive_base;
        wdbp->tbuf = (char *) malloc(WORKSPACE);
        wdbp->sav_tlook = (char *) malloc(WORKSPACE);
        wdbp->tlook = wdbp->sav_tlook;
        wdbp->look_status = CLEAR;
/*
 * Set up the hash table for events
 */
        wdbp->pg.poss_events = hash(MAX_EVENT,long_hh,icomp);
/*
 * Initialise the end point management
 */
        wdbp->ep_cur_ptr = wdbp->end_point_det,
        wdbp->ep_max_ptr = &(wdbp->end_point_det[MAXENDPOINTS-1]);
/*
 * Schedule the client
 */
        add_time(&webdrive_base, wdbp, (c + 1) * path_stagger);
        
        wdbp->pg.rope_seq = (char *) malloc(12);
        sprintf(wdbp->pg.rope_seq,"%u", rope);
        wdbp->pg.logfile = malloc(2 + strlen(webdrive_base.pg.logfile)
                  + strlen(wdbp->pg.rope_seq));
        (void) sprintf(wdbp->pg.logfile, "%s.%s", webdrive_base.pg.logfile,
                       wdbp->pg.rope_seq);
        event_record_r("S", (struct event_con *) NULL, &(wdbp->pg));
                                             /* Announce the start */
        rope++;
    }
    webdrive_base.own_thread_id = pthread_self();
    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"proc_args()\n");
        (void) fflush(stderr);
        weblog(argc,argv);
    }
    return;
}
void scarper(fname,ln,text, wdbp)
char * fname;
int ln;
char * text;
WEBDRIVE_BASE * wdbp;
{
    fprintf(stderr, "%s:%d (Client: %s) UNIX error code %d\n\
%s\n",fname,ln, wdbp->pg.rope_seq, errno, text);
#ifdef MINGW32
    WSACleanup();
#endif
    exit(1);       /* Does not Return */
}
void syntax_err(fname,ln,text, wdbp)
char * fname;
int ln;
char * text;
WEBDRIVE_BASE * wdbp;
{
    fprintf(stderr, "Syntax error in control file %s:\n\
%s%s\n\
unexpected around byte offset %d\n",
        wdbp->control_file,
        wdbp->tbuf, (wdbp->look_status == CLEAR) ?"": wdbp->tlook,
               ftell(wdbp->pg.cur_in_file));
        scarper(fname,ln,text, wdbp);       /* Does not Return */
}
/*********************************************************************
 * Recognise the PATH directives in the file
 *
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

    a->end_point.end_point_id = wdbp->ep_cnt++;
    (void) nextasc_r(wdbp->tlook, ':', '\\', &got_to,
               a->end_point.address,
               &(a->end_point.address[sizeof(a->end_point.address) - 1]));
    a->end_point.cap_port_id = atoi(nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]));
    (void) nextasc_r((char *) NULL, ':', '\\', &got_to,a->end_point.host,
                  &(a->end_point.host[sizeof(a->end_point.host) - 1]));
    a->end_point.port_id = atoi(nextasc_r((char *) NULL, ':', '\\',
                                &got_to, &ret_buf[0],
                                 &ret_buf[sizeof(ret_buf) - 1]));
    a->end_point.con_flag = *(nextasc_r((char *) NULL, ':','\\',
                                &got_to, &ret_buf[0],
                                &ret_buf[sizeof(ret_buf) - 1]));
    a->end_point.ora_forms_flag = 0;
    return;
}
/*
 * Start Time (\S) records are:
 * id:number (ignored):description
 */
void recognise_start_timer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];

    (void) nextasc_r(wdbp->tlook, ':', '\\', &got_to,
                    &(a->start_timer.timer_id[0]),
              &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    (void) nextasc_r((char *) NULL, ':', '\\', &got_to, &ret_buf[0],
                 &ret_buf[sizeof(ret_buf) - 1]);
    (void) nextasc_r((char *) NULL, ':', '\\', &got_to,
                 &(a->start_timer.timer_description[0]),
                 &(a->start_timer.timer_description[sizeof(
                   a->start_timer.timer_description) - 1]));
    return;
}
/*
 * Take Time (\T) records are:
 * id:
 */
void recognise_take_time(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;

    (void) nextasc_r(wdbp->tlook, ':', '\\', &got_to,
               &(a->start_timer.timer_id[0]),
          &(a->start_timer.timer_id[sizeof(a->start_timer.timer_id) - 1]));
    return;
}
/*
 * Delay (\W) records are:
 * delay time (floating point number)
 */
void recognise_delay(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
    a->delay.delta = strtod(wdbp->tlook,(char **) NULL);
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

    (void) nextasc_r(wdbp->tlook, ';', '\\', &got_to,
                      &address[0], &address[sizeof(address) - 1]);
    port_id = atoi(nextasc_r((char *) NULL,':', '\\', &got_to,
                      ret_buf, &ret_buf[sizeof(ret_buf) - 1]));
    if ((a->link.from_ep = ep_find(address,port_id, wdbp)) ==
        (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point", wdbp);
    (void) nextasc_r((char *) NULL, ';', '\\', &got_to, &address[0],
                      &address[sizeof(address) - 1]);
    port_id = atoi(nextasc_r((char *) NULL, ':', '\\', &got_to, ret_buf,
                      &ret_buf[sizeof(ret_buf) - 1]));
    if ((a->link.to_ep = ep_find(address,port_id, wdbp)) == (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point", wdbp);
    a->link.connect_fd = -1;
    a->link.pair_seq = 0;
    if (a->link.remote_handle != (char *) NULL)
    {
        free(a->link.remote_handle);
        a->link.remote_handle = (char *) NULL;
    }
    memset((char *) &(a->link.connect_sock),0,sizeof(a->link.connect_sock));
    memset((char *) &(a->link.in_det),0,sizeof(a->link.in_det));
    memset((char *) &(a->link.out_det),0,sizeof(a->link.out_det));
    return;
}
/*****************************************************************************
 * Take an incoming message in ASCII and make it binary. 
 *
 * Does not assumes only one connection active at a time, but does assume only
 * one message.
 */
enum tok_id recognise_message(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
char address[HOST_NAME_LEN];
int port_id;
END_POINT * from_ep;
END_POINT * to_ep;
enum direction_id in;
unsigned char * got_to = (unsigned char *) NULL;
unsigned char ret_buf[24];

    (void) nextasc_r(wdbp->tlook, ';', '\\', &got_to,
                      &address[0],&address[sizeof(address) - 1]);
    port_id = atoi(nextasc_r((char *) NULL, ':', '\\', &got_to,
                      ret_buf, &ret_buf[sizeof(ret_buf) - 1]));
    if ((from_ep = ep_find(address,port_id, wdbp)) ==
        (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point", wdbp);
    (void) nextasc_r((char *) NULL, ';', '\\', &got_to,
                      &address[0],&address[sizeof(address) - 1]);
    port_id = atoi(nextasc_r((char *) NULL, ':', '\\', &got_to,
                      ret_buf, &ret_buf[sizeof(ret_buf) - 1]));
    if ((to_ep = ep_find(address,port_id, wdbp)) == (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point", wdbp);

    if (wdbp->cur_link->from_ep != from_ep
      || wdbp->cur_link->to_ep != to_ep)
    {
        wdbp->cur_link = link_find(from_ep, to_ep, wdbp);
        if (wdbp->cur_link->from_ep != from_ep
          || wdbp->cur_link->to_ep != to_ep)
            syntax_err(__FILE__, __LINE__, "Message for hitherto unknown link",
                          wdbp);
    }
    in = IN;
    if ((mess_len = obdinrec((long int) wdbp->pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
        syntax_err(__FILE__,__LINE__,
              "Invalid format Objective record");
    a->send_receive.record_type = SEND_RECEIVE_TYPE;
    a->send_receive.msg = &(wdbp->msg);
    a->send_receive.message_len = mess_len;
    wdbp->look_status = CLEAR;
    return SEND_RECEIVE_TYPE;
}
enum tok_id recognise_answer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
enum direction_id in;

    in = IN;
    if ((mess_len = obdinrec((long int) wdbp->pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
        syntax_err(__FILE__,__LINE__,
              "Invalid format Objective record", wdbp);
    wdbp->look_status = CLEAR;
    a->send_receive.record_type = E2COMMENT;
    return E2COMMENT;
}
/*
 * Assemble records for processing by the main loop
 */
enum tok_id obdread(a, wdbp)
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
        tok_id = get_tok(wdbp->pg.cur_in_file, wdbp);
        if (tok_id == E2EOF)
            return tok_id;
        else
        if (tok_id == E2COMMENT)
        {
            if (wdbp->verbosity)
            {
                fputs(wdbp->tbuf,stderr);
                if (wdbp->look_status != CLEAR)
                    fputs(wdbp->tlook,stderr);
            }
            wdbp->look_status = CLEAR;
        }
        else
        if (tok_id == E2RESET)
            wdbp->look_status = CLEAR;
        else
            break;
    }
    if (wdbp->look_status == CLEAR)
        syntax_err(__FILE__,__LINE__,"There should be a look-ahead token",
                            wdbp);
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
    case  DELAY_TYPE:
        recognise_delay(a, wdbp);
        break;
    default:
        fprintf(stderr,"(Client:%s) Token: %d\n", wdbp->pg.rope_seq, (int) tok_id);
        syntax_err(__FILE__,__LINE__,"Invalid control file format", wdbp);
    }
    if (wdbp->look_status != KNOWN)
        wdbp->look_status = CLEAR;
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
        fprintf(stderr,"(Client:%s) progress_client()", wdbp->pg.rope_seq);
    while ((rec_type = obdread(&(wdbp->in_buf), wdbp)) != E2EOF)
    {
        wdbp->rec_cnt++;
        if (wdbp->debug_level > 2)
        {
            (void) fprintf(stderr,"(Client:%s) Control File Service Loop\n\
=====================================\n\
Line: %d Record Type: %d\n",
                               wdbp->pg.rope_seq,
                               wdbp->rec_cnt,
                         (int) rec_type);
        }
        switch (rec_type)
        {
        case END_POINT_TYPE:
/*
 * Add the end point to the array
 */
            do_end_point(&(wdbp->in_buf), wdbp);
            break;
        case SEND_RECEIVE_TYPE:
/*
 * Send the message and receive the response.
 */          
            do_send_receive(&(wdbp->in_buf), wdbp);
            break;
        case SEND_FILE_TYPE:
/*
 * Send the file.
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
            if (do_take_time(&(wdbp->in_buf), wdbp))
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
            syntax_err(__FILE__,__LINE__,"this token invalid at this point");
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
        (void) fprintf(stderr,"(Client:%s) do_link(%s;%d => %s;%d)\n",
            wdbp->pg.rope_seq,
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
            obdrive_connect(wdbp->cur_link);
        else
            obdrive_listen(wdbp->cur_link);
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
        (void) fprintf(stderr,"(Client:%s) do_close(%s;%d => %s;%d)\n",
            wdbp->pg.rope_seq,
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
    if (wdbp->cur_link->connect_fd == -1)
        fprintf(stderr, "Logic Error: closing a non-open connexion\n");
    else
    {
        if (a->link.from_ep->con_flag == 'C')
        {
            shutdown(wdbp->cur_link->connect_fd, 2);
            closesocket(wdbp->cur_link->connect_fd);
            wdbp->cur_link->connect_fd = -1;
            wdbp->cur_link->link_id = CLOSE_TYPE;
            wdbp->cur_link->pair_seq = 0;
            if (wdbp->cur_link->remote_handle != (char *) NULL)
            {
                free(wdbp->cur_link->remote_handle);
                wdbp->cur_link->remote_handle = (char *) NULL;
            }
        }
        else
        {
#ifdef MINGW32
            WSACleanup();
#endif
            exit(0);
        }
    }
    return;
}
/***************************************************************************
 * Function to identify an Communications End Point declaration
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

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,"(Client:%s) do_end_point(%d, %s;%d (%s;%d))\n",
            wdbp->pg.rope_seq,
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
    return;
}
/****************************************************************************
 * Routine to set up a socket address
 */
static void sock_ready(host,port, out_sock)
char * host;
int port;
struct sockaddr_in * out_sock;
{
struct hostent  *connect_host;
long addr;

    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"sock_ready(%s,%d)\n", host, port);
        (void) fflush(stderr);
    }
    connect_host=gethostbyname(host);
    if (connect_host == (struct hostent *) NULL)
        addr = inet_addr(host); /* Assume numeric arguments */
    else
        memcpy((char *) &addr, (char *) connect_host->h_addr_list[0], 
                    (connect_host->h_length < sizeof(addr)) ?
                        connect_host->h_length :sizeof(addr));
/*
 *    Set up the socket address
 */
     memset(out_sock,0,sizeof(*out_sock));
#ifdef OSF
     out_sock->sin_len = connect_host->h_length + sizeof(out_sock->sin_port);
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
        char * x = "getsockname() failed\n"; 
        weblog(1,&x);
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

    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"(Client:%s) ep_find(%s,%d)\n",
                 wdbp->pg.rope_seq, address, cap_port_id);
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

    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"(Client:%s) link_find(%s:%d => %s:%d)\n",
                    wdbp->pg.rope_seq,
                    from_ep->host,
                    from_ep->port_id,
                    to_ep->host,
                    to_ep->port_id);
    for (cur_link = wdbp->link_det;
                cur_link->link_id != 0;
                     cur_link++)
        if ((cur_link->from_ep ==  from_ep
          && cur_link->to_ep == to_ep)
         || (cur_link->from_ep ==  to_ep
          && cur_link->to_ep == from_ep))
            break;
    return cur_link;
}
/************************************************************************
 * Establish a connexion
 * - Fills in the socket stuff.
 * - Sets up a calling socket if it is allowed to.
 */
void obdrive_connect(link)
LINK * link;
{
/*
 *    Initialise - use input parameters to set up listen port, and
 *        address of port to connect to
 *       -    Data Definitions
 */
struct protoent *obdrive_prot;
static int obprot = -1;
int timeout;
int intlen;
int backoff;

    if (link == (LINK *) NULL)
    {
        char * x = "Logic Error: obdrive_connect() called with NULL link";
        weblog(1,&x);
        return;
    }
    if (webdrive_base.debug_level > 1)
        (void) fprintf(stderr,"obdrive_connect(%s;%d => %s;%d)\n",
            link->from_ep->host,
            link->from_ep->port_id,
            link->to_ep->host,
            link->to_ep->port_id);
    if (obprot == -1)
    {
        obdrive_prot = getprotobyname("tcp");
        if ( obdrive_prot == (struct protoent *) NULL)
        { 
        char * x = "Logic Error; no tcp protocol!\n";
            weblog(1,&x);
            return;
        }
        obprot = obdrive_prot->p_proto;
    }
    link->connect_fd = -1;
    sock_ready(link->to_ep->host, link->to_ep->port_id,
                  &(link->connect_sock));
/*
 *    Now create the socket to output on
 */
    for (backoff = 5; ;backoff++)
    {
        if ((link->connect_fd =
              socket(AF_INET,SOCK_STREAM,obprot)) < 0)
        {
        char * x = "Output create failed\n";

            weblog(1,&x);
            perror("Output create failed");
            fflush(stderr);
            sleep (backoff*SLEEP_FACTOR);
            continue;
        }
        else
        {
#ifdef MINW32
            intlen = sizeof(timeout);
            timeout = 10000;
            if (!setsockopt(link->connect_fd,SOL_SOCKET,SO_RCVTIMEO,
                       (char *) &timeout, &intlen))
                fprintf(stderr, "Set Receive Timeout Error: %x\n",
                                 WSAGetLastError());
            intlen = sizeof(timeout);
            timeout = 10000;
            if (!setsockopt(link->connect_fd,SOL_SOCKET,SO_SNDTIMEO,
                       (char *) &timeout, &intlen))
                fprintf(stderr, "Set Send Timeout Error: %x\n",
                                 WSAGetLastError());
            timeout = 0;    /* Make Windows use the application buffer */
            if (!setsockopt(link->connect_fd,SOL_SOCKET,SO_SNDBUF,
                       (char *) &timeout, &intlen))
                fprintf(stderr, "Set Use Application Buffer Error: %x\n",
                                 WSAGetLastError());
            timeout = 1;    /* Enable keepalive (so we will die eventually) */
            if (!setsockopt(link->connect_fd,SOL_SOCKET,SO_KEEPALIVE,
                       (char *) &timeout, &intlen))
                fprintf(stderr, "Set Use Application Buffer Error: %x\n",
                                 WSAGetLastError());
#endif
/*
 * If we need to bind names, use a variation on the following code
 *
 *          for( i = 1050; i < 1489; i++)
 *          {
 *             sock_ready("glaxo1",i,&b);
 *             if (!bind(link->connect_fd, &b, sizeof(b)))
 *                 break;
 *          }
 *
 * Connect to the destination. Leave as blocking
 */
            if (connect(link->connect_fd,
                           (struct sockaddr *) &link->connect_sock,
                               sizeof(link->connect_sock)))
            {
            char * x = "Initial connect() failure\n";

#ifdef MINGW32
                 fprintf(stderr,"%s : error: %x\n", x, WSAGetLastError());
                 if (WSAGetLastError() == 0)
                     return;          /* See if we get more clues later ... */
#endif
                 weblog(1,&x);
                 perror("connect() failed");
                 fflush(stderr);
                 closesocket(link->connect_fd);
                 link->connect_fd = -1;
                 sleep (backoff*SLEEP_FACTOR);
                 continue;
            }
            else
                return;
        }
    }
}
/************************************************************************
 * Listen set up - needed to drive loop-back tests.
 *
 * Note that the source is still the from_ep.
 */
void obdrive_listen(link, wdbp)
LINK * link;
WEBDRIVE_BASE * wdbp;
{
static char nseq[10];
struct protoent *obdrive_prot;
static int obprot = -1;
unsigned int adlen;
int listen_fd;
struct sockaddr_in listen_sock;

    if (webdrive_base.debug_level > 1)
        (void) fprintf(stderr,"obdrive_listen(%s,%d)\n",
          link->from_ep->host,link->from_ep->port_id);
    if (obprot == -1)
    {
        obdrive_prot=getprotobyname("tcp");
        if ( obdrive_prot == (struct protoent *) NULL)
        { 
        char * x = "Logic Error; no tcp protocol!\n";

            weblog(1,&x);
            return;
        }
        obprot = obdrive_prot->p_proto;
    }
/*
 *    Construct the Socket Address
 */
    sock_ready(link->from_ep->host, link->from_ep->port_id, &listen_sock);
    listen_sock.sin_addr.s_addr = INADDR_ANY;
/*
 *    Now create the socket to listen on
 */
    if ((listen_fd=
         socket(AF_INET,SOCK_STREAM,obprot))<0)
    { 
    char * x = "Listen socket create failed\n" ;

        weblog(1,&x);
        perror("Listen socket create failed"); 
    }
/*
 * Bind its name to it
 */
    if (bind(listen_fd,(struct sockaddr *) (&listen_sock),sizeof(listen_sock)))
    { 
    char * x = "Listen bind failed\n"; 

        weblog(1,&x);
        perror("Listen bind failed"); 
    }
    else
    if (webdrive_base.debug_level > 1)
        log_sock_bind(listen_fd);
/*
 *    Declare it ready to accept calls
 */
    if (listen(listen_fd, MAXLINKS))
    { 
    char * x = "Listen() failed\n"; 

        weblog(1,&x);
        perror("listen() failed"); 
        fflush(stderr);
    }
    for (adlen = sizeof(link->connect_sock);
            (link->connect_fd = accept(listen_fd, (struct sockaddr *)
                          &(link->connect_sock), &adlen)) >= 0;
                adlen = sizeof(link->connect_sock))
    {
#ifndef MINGW32
        if (fork() == 0)
#endif
        {
/*
 * Child. One shot. There has to be one file per transaction,
 * because the listener never advances past the listening point..
 */
        char buf[128];

            fclose(wdbp->pg.cur_in_file);
            (void) sprintf(buf,"%s_%s", wdbp->control_file,
                               wdbp->pg.rope_seq);
            closesocket(listen_fd);
            if ((wdbp->pg.cur_in_file = fopen(buf,"rb")) == (FILE *) NULL)
            {
                fprintf(stderr, "No script %s available!\n", buf); 
#ifdef MINGW32
                WSACleanup();
#endif
                exit(1);
            }
            (void) sprintf(buf,"%s_%s",wdbp->pg.logfile,wdbp->pg.rope_seq);
            wdbp->pg.fo = fopen(buf,"wb");
/*
 * Record the other end for possible future reference
 */ 
            sock_ready(link->to_ep->host,link->to_ep->port_id,
                  &(link->connect_sock));
            return;
        }
/*
 * Give the next child a new rope number
 */
        sprintf(nseq,"%d",atoi(wdbp->pg.rope_seq)+1);
        wdbp->pg.rope_seq = nseq;
    }
    perror("accept() failed"); 
#ifdef MINGW32
    WSACleanup();
#endif
    exit(1);
}
/***********************************************************************
 * Process messages. This routine is only called if there is something
 * to do.
 *
 * Objective-specific code is included here. The general solution would
 * provide hooks into which any TCP/IP based message passing scheme could
 * be attached.
 */
void do_send_receive(msg, wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
enum direction_id out;
enum tok_id tok_id;

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Send Receive Message Sequence %d\n",
                       wdbp->pg.rope_seq, wdbp->pg.seqX);
        fflush(stderr);
    }
    out = OUT;
/*
 * Send - Message has been assembled in msg.
 */
    if ((obdoutrec(wdbp->cur_link->connect_fd,
                  msg->send_receive.msg,out, msg->send_receive.message_len,
                   wdbp))
               < 1)
    {
        fflush(wdbp->pg.log_output);
        fflush(stderr);
        perror("Error from do_send_receive obdoutrec()");
        fflush(wdbp->pg.log_output);
        fflush(stderr);
        if ( wdbp->cur_link->from_ep == (END_POINT *) NULL
           ||  wdbp->cur_link->from_ep == (END_POINT *) NULL)
        {
             fprintf(stderr,
                 "(Client: %s) Link (%d) is missing at least one end point\n",
                        wdbp->pg.rope_seq, (wdbp->cur_link - wdbp->link_det));
        }
        else
        fprintf(stderr,
               "(Client:%s) From: %s,%d (%s,%d) To: %s,%d (%s,%d) = %d\n",
            wdbp->pg.rope_seq,
            wdbp->cur_link->from_ep->address,
            wdbp->cur_link->from_ep->cap_port_id,
            wdbp->cur_link->from_ep->host,
            wdbp->cur_link->from_ep->port_id,
            wdbp->cur_link->to_ep->address,
            wdbp->cur_link->to_ep->cap_port_id,
            wdbp->cur_link->to_ep->host,
            wdbp->cur_link->to_ep->port_id,
             wdbp->cur_link->connect_fd);
        (void) obdoutrec(stderr,
                  msg->send_receive.msg, IN, msg->send_receive.message_len,
                     wdbp);
        fflush(stderr);
        event_record_r("X", (struct event_con *) NULL, &(wdbp->pg));
                                                 /* Note the message */
        shutdown(wdbp->cur_link->connect_fd, 2);
        closesocket(wdbp->cur_link->connect_fd);
        wdbp->cur_link->connect_fd = -1;
        wdbp->cur_link->link_id = CLOSE_TYPE;
        wdbp->cur_link->pair_seq = 0;
        if (wdbp->cur_link->remote_handle != (char *) NULL)
        {
            free(wdbp->cur_link->remote_handle);
            wdbp->cur_link->remote_handle = (char *) NULL;
        }
/*
 * Have to give up and start over
 */
        while((tok_id = get_tok(wdbp->pg.cur_in_file, wdbp)) != E2RESET
              &&  tok_id != E2EOF);
        wdbp->look_status = CLEAR;
        return;
    }
    else
    {
/*
 * Have to decide whether to expect single or multiple records returned
 */
        if (!memcmp((((char *) msg->send_receive.msg) + 4),"1103",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"2210",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"2802",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"4312",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"4734",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"4736",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"4810",4)
         || !memcmp((((char *) msg->send_receive.msg) + 4),"4830",4))
            wdbp->encrypted_token[0] = (char *) 1;
    }
    wdbp->except_flag = 0;
    if (wdbp->verbosity)
    {
        event_record_r("T", (struct event_con *) NULL, &(wdbp->pg));
                                         /* Note the message */
        if (wdbp->verbosity > 1)
        {
            fprintf(stderr, "(Client: %s) Out===>\n", wdbp->pg.rope_seq);
            (void) obdoutrec(stderr,
                  msg->send_receive.msg, IN, msg->send_receive.message_len,
                                 wdbp);
        }
    }
/*
 * The logoff message does not get a reply.
 */
    if (!memcmp((((char *) msg->send_receive.msg) + 4),"1302",4))
        return;
/*
 * Receive
 */
    alarm_preempt(wdbp);
    if ( (mess_len = 
            obdinrec((long int) wdbp->cur_link->connect_fd,
                     &(wdbp->ret_msg), out, wdbp)) < 1)
    {
        event_record_r("X", (struct event_con *) NULL, &(wdbp->pg));
                                                     /* Note the message */
        alarm_restore(wdbp);
        shutdown(wdbp->cur_link->connect_fd, 2);
        closesocket(wdbp->cur_link->connect_fd);
        wdbp->cur_link->connect_fd = -1;
        wdbp->cur_link->link_id = CLOSE_TYPE;
        wdbp->cur_link->pair_seq = 0;
        if (wdbp->cur_link->remote_handle != (char *) NULL)
        {
            free(wdbp->cur_link->remote_handle);
            wdbp->cur_link->remote_handle = (char *) NULL;
        }
        return;
    }
    alarm_restore(wdbp);
/*
 * Now do whatever processing is necessary on the received message:
 */
    if (wdbp->verbosity || wdbp->except_flag)
    {
        event_record_r("R", (struct event_con *) NULL, &(wdbp->pg));
                                                          /* Note the message */
        if (wdbp->verbosity > 1)
        {
            fprintf(stderr, "(Client: %s) In<====\n", wdbp->pg.rope_seq);
            (void) obdoutrec(stderr, (char *) &(wdbp->ret_msg),IN, mess_len,
                       wdbp);
        }
    }
/*
 * Take any special action based on the message just processed....
 */
    return;
}
/*
 *  Log the arguments to the global log file.
 */
extern char * ctime();
void
weblog(argc, argv)
int argc;
char    **argv;
{
char    buf[BUFSIZ];
char    cp[25];
char    *x;
time_t    t;
int i;
int left;

    if (webdrive_base.debug_level > 3)
        (void) fprintf(stderr, "weblog(argc=%d, argv[0]=%s)\n", argc, argv[0]);
    (void) fflush(stderr);

    t = time(0);
    x = ctime_r(&t, cp);
    cp[24] = 0;
    (void) sprintf(buf, "obdrive, %s, %d, ", cp,getpid());
    left = sizeof(buf) - strlen(buf);
    for ( i = 0 ; i < argc ; i++)
    {
        if (left <= strlen(argv[i]))
            break; 
        left -= (strlen(argv[i]) + 2);
        (void) strcat(buf, argv[i]);
        (void) strcat(buf, " ");
    }
    (void) strcat(buf, "\n");
    (void) fwrite(buf, sizeof(char), strlen(buf), stderr);
    (void) fflush(stderr);
    return;
}
/*
 * Start the timer running
 */
void do_start_timer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char cur_pos[BUFLEN];
short int x;
HIPT * h;

    wdbp->pg.seqX = wdbp->rec_cnt;
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_start_timer(%s)\n",
            wdbp->pg.rope_seq, a->start_timer.timer_id);
        fflush(stderr);
    }
    if (wdbp->verbosity > 1)
        fprintf(stderr,"(Client: %s) Start:%s:%s\n", wdbp->pg.rope_seq,
                        a->start_timer.timer_id,
                        a->start_timer.timer_description);
    strcpy(cur_pos,a->start_timer.timer_id);
    strcat(cur_pos, ":120:");
    strcat(cur_pos, a->start_timer.timer_description);
    stamp_declare_r(cur_pos, &(wdbp->pg));
    x = (short int) (((int) (cur_pos[0])) << 8) + ((int) (cur_pos[1]));
    if ((h = lookup(wdbp->pg.poss_events, (char *) x)) == (HIPT *) NULL)
    {
        (void) fprintf(stderr,"(%s:%d Client:%s) Error, event define failed for %s\n",
                       __FILE__,__LINE__, wdbp->pg.rope_seq, cur_pos);
        return;       /* Crash out here */
    }
    wdbp->pg.curr_event = (struct event_con *) (h->body);
    if (wdbp->pg.curr_event != (struct event_con *) NULL)
        wdbp->pg.curr_event->time_int = timestamp();
    return;
}
/*
 * Take a time stamp
 */
int do_take_time(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
HIPT * h;
long int event_id;

    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_take_time(%s)\n", wdbp->pg.rope_seq,
                                 a->take_time.timer_id);
        fflush(stderr);
    }
    if (wdbp->verbosity > 1)
        fprintf(stderr,"(Client:%s) End:%s\n", wdbp->pg.rope_seq, a->take_time.timer_id);
    event_id = (((int) (a->take_time.timer_id[0])) << 8)
             + ((int) (a->take_time.timer_id[1]));
    if ((h = lookup(wdbp->pg.poss_events,(char *) event_id)) ==
           (HIPT *) NULL)
    {
        (void) fprintf(stderr,"(%s:%d Client:%s) Error, undefined event %x\n",
                       __FILE__,__LINE__, wdbp->pg.rope_seq, event_id);
        return 0;       /* Crash out here */
    }
    wdbp->pg.curr_event = (struct event_con *) (h->body);
    if (wdbp->pg.curr_event  != (struct event_con *) NULL)
    {
    int sleep_left;

        wdbp->pg.seqX = wdbp->rec_cnt;
        event_record_r(wdbp->pg.curr_event->event_id,
                            wdbp->pg.curr_event, &(wdbp->pg));
        sleep_left = (int) (((double) wdbp->pg.think_time)
                          - wdbp->pg.curr_event->time_int/100.0);
        wdbp->pg.curr_event = (struct event_con *) NULL;
        if (sleep_left > 0)
        {
            add_time(wdbp->root_wdbp, wdbp, sleep_left);
            return 1;
        }
    }
    return 0;
}
extern double floor();
extern double strtod();
/*
 * Use select() to give a high resolution timer
 */
int do_delay(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
    wdbp->pg.think_time = (short int) a->delay.delta;
    if (wdbp->debug_level > 3)
    {
        (void) fprintf(stderr, "(Client:%s) do_delay(%f)\n", wdbp->pg.rope_seq, a->delay.delta);
        fflush(stderr);
    }
    wdbp->pg.think_time = (short int) a->delay.delta;
    if (a->delay.delta >= 1.0)
    {
        add_time(wdbp->root_wdbp, wdbp, (int) a->delay.delta);
        return 1;
    }
    return 0;
}
/*
 * Read in a line to tbuf, dropping escaped newlines.
 */
static int getescline(fp, wdbp)
FILE * fp;
WEBDRIVE_BASE * wdbp;
{
int p; 
char * cur_pos = wdbp->sav_tlook;
char * bound = wdbp->sav_tlook + WORKSPACE - 83;

skip_blank:
    wdbp->tlook = wdbp->sav_tlook;
    wdbp->look_status = PRESENT;
    p = fgetc(fp);
/*
 * Scarper if all done
 */
    if ( p == EOF )
        return p;
    else
    if (p == '\\')
    {
        *cur_pos++ = (char) p;
        fgets(cur_pos, WORKSPACE - 2, fp);
        if (wdbp->debug_level > 2)
            fprintf(stderr,"Command: %s\n", cur_pos);
        *(cur_pos + WORKSPACE - 3) = '\n';
        return strlen(cur_pos);
    }
    else
/*
 * Pick up the next line, stripping out escapes
 */
    {
        for (;;)
        {
            if (p == (int) '\\')
            {
                p = fgetc(fp);
                if ( p == EOF )
                    break;
                else
                if (p == '\n')
                    p = fgetc(fp);
                else
                if (p == '\r')
                {
                    p = fgetc(fp);
                    if (p == '\n')
                        p = fgetc(fp);
                    else
                    {
                        *cur_pos++ = '\\';
                        *cur_pos++ = '\r';
                    }
                }
                else
                    *cur_pos++ = '\\';
            }
            *cur_pos++ = p;
            if (cur_pos > bound)
            {
                fprintf(stderr, "(Client:%s) Token too big for WORKSPACE; discarding: %s\n",
                                wdbp->pg.rope_seq, wdbp->sav_tlook);
                fflush(stderr);
                cur_pos = wdbp->sav_tlook;
            }
            if (p == (int) '\n')
            {
#ifdef SKIP_BLANK
                if (cur_pos == wdbp->sav_tlook + 1)
                {
                    cur_pos = wdbp->sav_tlook;
                    goto skip_blank;
                }
#endif
                break;
            }
            p = fgetc(fp);
            if ( p == EOF )
                p = '\n';
        }
        *cur_pos = '\0';
        if (wdbp->debug_level > 2)
        {
            fprintf(stderr,"(Client:%s) De-escaped: %s\n",
                             wdbp->pg.rope_seq, wdbp->sav_tlook);
            fflush(stderr);
        }
        return (cur_pos - wdbp->sav_tlook);
    }
}
/*
 * Move things from the look-ahead to the live buffer
 */
void mv_look_buf(len, wdbp)
int len;
WEBDRIVE_BASE * wdbp;
{
    memcpy(wdbp->tbuf, wdbp->tlook, len);
    *(wdbp->tbuf + len) = '\0';
    wdbp->tlook += len;
    if (*(wdbp->tlook) == '\0' || *(wdbp->tlook) == '\n')
        wdbp->look_status = CLEAR;
    return;
}
/*
 * Convert a MSB/LSB ordered hexadecimal string into an integer
 */
int hex_to_int(x1, len)
char * x1;
int len;
{
long x;
int x2;

    for (x = (unsigned long) (*x1++ - (char) 48),
         x = (x > 9)?(x - 7):x, len -= 1;
             len;
                 len--, x1++)
    {
        x2 = *x1 - (char) 48;
        if (x2 >  9)
            x2 -=  7;
        x = x*16 + x2;
    }
    return x;
}
/*
 * Read the next token
 * -  There are at most two tokens a line
 * -  Read a full line, taking care of escape characters
 * -  Search to see what the cat brought in
 * -  Be very careful with respect to empty second tokens
 * -  Return  
 */
static enum tok_id get_tok(fp, wdbp)
FILE * fp;
WEBDRIVE_BASE * wdbp;
{
int len;
int str_length;
struct named_token * cur_tok;
/*
 * If no look-ahead present, refresh it
 */
    if (wdbp->debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) get_tok(%x); LOOK_STATUS=%d\n",
                      wdbp->pg.rope_seq, (int) fp,
                      wdbp->look_status);
    if (wdbp->look_status == KNOWN)
    {
        wdbp->look_status = wdbp->last_look;
        return wdbp->last_tok;
    }
    if (wdbp->look_status != PRESENT)
    {
restart:
        if ((len = getescline(fp, wdbp)) == EOF)
            return E2EOF;            /* Return EOF if no more */
/*
 * Commands are only allowed at the start of a line
 */
        if (wdbp->debug_level > 3)
            fprintf(stderr,"(Client:%s) Input Line: %s",
                         wdbp->pg.rope_seq, wdbp->tlook);
        wdbp->last_look = wdbp->look_status;
        if (*(wdbp->tlook) == '\\')
        {        /* Possible PATH Command */
        char c = *(wdbp->tlook + 1);

            switch (c)
            {
/*
 * Comment
 */
            case 'C':
                if (*(wdbp->tlook + 2) != ':')
                    goto notnewline;
                *(wdbp->tbuf) = '\0';
                wdbp->last_tok = E2COMMENT;
                break;
/*
 * Open, End-point or Close
 */
            case 'M':
            case 'E':
            case 'X':
            case 'D':
                if (*(wdbp->tlook + 2) != ':')
                    goto notnewline;
                *(wdbp->tbuf) = '\\';
                *(wdbp->tbuf+1) = c;
                *(wdbp->tbuf+2) = ':';
                *(wdbp->tbuf+3) = '\0';
                wdbp->tlook += 3;
                if (c == 'M')
                    wdbp->last_tok = LINK_TYPE;
                else
                if (c == 'E')
                    wdbp->last_tok = END_POINT_TYPE;
                else
                if (c == 'X')
                    wdbp->last_tok = CLOSE_TYPE;
                else
                {
/*
 * Data
 */
                    if (*(wdbp->tlook) == 'B')
                        wdbp->last_tok = E2BEGIN;
                    else
                    if (*(wdbp->tlook) == 'E')
                        wdbp->last_tok = E2END;
                    else
                    if (*(wdbp->tlook) == 'R')
                        wdbp->last_tok = E2RESET;
                    *(wdbp->tbuf+3) = ':';
                    *(wdbp->tbuf+4) = *(wdbp->tlook);
                    *(wdbp->tbuf+5) = '\0';
                    wdbp->tlook += 2;
                }
                break;
            case 'A':
                if (*(wdbp->tlook + 2) != ':')
                    goto notnewline;
                wdbp->tlook += 3;
                if (*(wdbp->tlook) == 'B')
                    wdbp->last_tok = E2ABEGIN;
                else
                if (*(wdbp->tlook) == 'E')
                    wdbp->last_tok = E2AEND;
                else
                    goto notnewline;
                break;
/*
 * Pause in seconds
 */
            case 'W':
                wdbp->tlook += 2;
                wdbp->last_tok = DELAY_TYPE;
                break;
/*
 * Start timer
 */
            case 'S':
                *(wdbp->tlook + len - 2) = '\0';
                wdbp->tlook += 2;
                wdbp->last_tok = START_TIMER_TYPE;
                break;
/*
 * End timer
 */
            case 'T':
                wdbp->tlook += 2;
                wdbp->last_tok = TAKE_TIME_TYPE;
                break;
            case '\n':
                goto restart;
            default:
                fprintf(stderr,"(Client:%s) Format problem with line: %s\n",
                                         wdbp->pg.rope_seq,
                                        wdbp->tlook);
                break;
            }
            if (wdbp->debug_level > 2)
                fprintf(stderr,"(Client:%s) Token: %d\n",
                                wdbp->pg.rope_seq, (int) wdbp->last_tok);
            return wdbp->last_tok;
        }
    }
    else
        len = strlen(wdbp->tlook);
notnewline:
    for ( cur_tok = known_toks,
         str_length = strlen(cur_tok->tok_str);
             (str_length > 0);
                  cur_tok++,
                  str_length = strlen(cur_tok->tok_str))
        if (!strncmp(wdbp->tlook,cur_tok->tok_str,str_length))
            break;
    if (!str_length)
    {
        wdbp->last_tok = E2STR;
        mv_look_buf( len, wdbp);
        wdbp->look_status = CLEAR;
    }
    else
    {
        wdbp->last_tok = cur_tok->tok_id;
        mv_look_buf( str_length, wdbp);
        wdbp->last_look = wdbp->look_status;
    }
    if (wdbp->debug_level > 2)
        fprintf(stderr, "(Client:%s) Token: %d\n",
                        wdbp->pg.rope_seq, (int) wdbp->last_tok);
    return wdbp->last_tok;
}
/*****************************************************************************
 * - Routines that read or write one of the valid record types
 *   off a FILE.
 *
 * obdinrec()
 *   - Sets up the record in a buffer that is passed to it
 *   - Returns 1 if successful
 *
 * obdoutrec()
 *   - Fills a buffer with the data that is passed to it
 *   - Returns 1 if successful, 0 if not.
 *
 * Code to generate binary from a mixed buffer of ASCII and hexadecimal
 * Note that this is heuristic, not deterministic!?
 */ 
int get_bin(tbuf, tlook, cnt)
unsigned char * tbuf;
unsigned char * tlook;
int cnt;
{
unsigned char * cur_pos;
unsigned char * sav_tbuf = tbuf;
int len;

    while (cnt > 0)
/*
 * Is this a length of hexadecimal?
 */
    {
        if (*tlook == '\''
          && (cur_pos = strchr(tlook+1,'\'')) > (tlook + 1)
          && (((cur_pos - tlook) % 2) ==  1)
          && strspn(tlook+1,"0123456789ABCDEFabcdef") ==
                          (len = (cur_pos - tlook - 1)))
        {
            cnt -= (2 + len);
            tlook++;
            *(tlook + len) = (unsigned char) 0;
            tbuf = hex_in_out(tbuf, tlook);
            tlook = cur_pos + 1;
        }
/*
 * Is this a run of characters?
 */
        else
        if ((len = strcspn(tlook,"'")))
        {
            memcpy(tbuf,tlook,len);
            tlook += len;
            tbuf += len;
            cnt -= len;
        }
/*
 * Otherwise, we have a stray "'"
 */
        else
        {
            *tbuf++ = *tlook++;
            cnt--;
        }
    }
    return tbuf - sav_tbuf;
}
/*
 * Function to get known incoming
 */
static int smart_read(f,buf,len)
int f;
char * buf;
int len;
{
int so_far = 0;
int r;

#ifdef DEBUG
    fprintf(stderr, "smart_read(%d, %x, %d)\n",f, (long) buf, len);
    fflush(stderr);
#endif
    do
    {
        r = recvfrom(f, buf, len, 0,0,0);
        if (r <= 0)
            return r;
#ifdef DEBUG
        (void) gen_handle(stderr,buf,buf + r,1);
        fputc('\n', stderr);
        fflush(stderr);
#endif
        so_far += r;
        len -= r;
        buf+=r;
    }
    while (len > 0);
    return so_far;
}
/*********************************************************************
 * obdinrec - read a record off the input stream
 *
 * -  We build the whole message up. We always know in advance how
 *    much we are expecting.
 * -  We begin by copying the lesser of what we are expecting, and what
 *    is available, to the location in the buffer.
 * -  If we have everything, we then decide what we are going to receive
 *    next
 * -  If we have just read the first 32, we need to set up the number of
 *    fields
 * -  If we have just read the header, or a field:
 *    -  If it is the last one for this record, output the whole record,
 *       and reset it to read another header
 *    -  Otherwise, set up to read another 6 byte length
 * -  Otherwise (we have just read a length)
 *    - set up to read the data
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
int obdinrec(f, b, in_out, wdbp)
long int f;
unsigned char * b;
enum direction_id in_out;
WEBDRIVE_BASE * wdbp;
{
unsigned char * x;
unsigned char * x1;
unsigned char * x2;
unsigned char * x3;
unsigned char *top = b;
int read_cnt;
int i;
int mess_len = 0;
enum tok_id tok_id;

    if ( b == (unsigned char *) NULL)
    {
        (void) fprintf(stderr,
         "(Client:%s) Logic Error: obdinrec() called with NULL parameter(s)\n",
                 wdbp->pg.rope_seq);
        return 0;
    }
    if (wdbp->debug_level > 3 && in_out == IN)
        (void) fprintf(stderr,"(Client:%s) obdinrec(%x,%s,%d)\n", 
                   wdbp->pg.rope_seq, f, b, (int) in_out);
    x = b;
    if (in_out == IN)
    {
/*
 * The record is to be read off the input stream
 */
        wdbp->look_status = CLEAR;
        while ((tok_id = get_tok((FILE *) f, wdbp)) != E2END &&
                tok_id != E2AEND && tok_id != E2EOF)
        {
            mess_len = strlen(wdbp->tbuf);
            mess_len = get_bin(x, wdbp->tbuf, mess_len);
            x += mess_len;
        }
        if (tok_id == E2EOF)
        {
            fputs("Syntax error: No E2END (\\D:E\\) before EOF\n", stderr);
            fputs(b,stderr);
            return 0;
        }
        mess_len = ((unsigned char *) x - b);
/*
 * Now do any substitutions that are necessary
 */ 
        if (wdbp->debug_level > 2)
           (void) gen_handle(stderr,b,x,1);
    }
    else
/*    if (in_out == OUT) */
    {
    int more_flag = 1;
/*
 * Read data off the socket until done.
 */ 
        while (more_flag)
        {
            top = x + 32;
            if (smart_read((int) f, x, 32) == 32)
            {                   /* Read the header */
            int bite = WORKSPACE;

                if (wdbp->verbosity > 1)
                    fwrite(x,sizeof(char),32,stderr);
                if (wdbp->encrypted_token[0] == (char *) NULL
                   || !memcmp(x+4,"0109",4)
                   || !memcmp(x+4,"0031",4))
                {
                    more_flag = 0;
                    if (wdbp->encrypted_token[0] != (char *) NULL)
                        wdbp->encrypted_token[0] = (char *) NULL;
                }
                if (x[24] != '0' || x[25] != '0' || x[26] != '0'|| x[27] != '0')
                {
                    if (wdbp->remote_ref == (char *) NULL)
                        wdbp->remote_ref = (char *) malloc(4);
                    wdbp->remote_ref[0] = x[24];
                    wdbp->remote_ref[1] = x[25];
                    wdbp->remote_ref[2] = x[26];
                    wdbp->remote_ref[3] = x[27];
                }
#ifdef FIELD_BY_FIELD
                read_cnt = (x[23] - 48) +
                     (x[22] - 48)*10 +
                     (x[21] - 48)*100 +
                     (x[20] - 48)*1000;   /* Fields to do */
                while(read_cnt > 0)
                {
                    bite = WORKSPACE;

                    if (smart_read((int) f, x, 6) != 6)
                        break;
                    if (wdbp->verbosity > 1)
                        fwrite(x,sizeof(char),6,stderr);
                    mess_len = (x[5] - 48) +
                         (x[4] - 48)*10 +
                         (x[3] - 48)*100 +
                         (x[2] - 48)*1000 +
                         (x[1] - 48)*10000 +
                         (x[0] - 48)*100000;

                    while (mess_len > 0)
                    {
                        mess_len -= bite;
                        if (mess_len < 0)
                        {
                            bite += mess_len;
                            mess_len = 0;
                        }
                        if (smart_read((int) f,x,bite) != bite)
                            break;
                        top = x + bite;
                        if (wdbp->verbosity > 1)
                            gen_handle(stderr,x,top,1);
                    }
                    read_cnt--;
                }
#else
                mess_len = (x[19] - 48) +
                     (x[18] - 48)*10 +
                     (x[17] - 48)*100 +
                     (x[16] - 48)*1000 +
                     (x[15] - 48)*10000 +
                     (x[14] - 48)*100000 +
                     (x[13] - 48)*1000000 +
                     (x[12] - 48)*10000000;   /* Fields to do */

                while (mess_len > 0)
                {
                    mess_len -= bite;
                    if (mess_len < 0)
                    {
                        bite += mess_len;
                        mess_len = 0;
                    }
                    if (smart_read((int) f,x,bite) != bite)
                        break;
                    top = x + bite;
                    if (wdbp->verbosity > 1)
                        gen_handle(stderr,x,top,1);
                }
#endif
            }
            else
                break;
        }
        mess_len = (unsigned char *) top - b;
    }
    if (wdbp->debug_level > 2)
    {
        fprintf(stderr, "(Client:%s) Read Message Length: %d\n",
                        wdbp->pg.rope_seq, mess_len);
        in_out = IN;
        if (mess_len > 0)
            (void) obdoutrec(stderr, b, in_out, mess_len, wdbp);
        fflush(stderr);
    }
    return mess_len;
}
/****************************************************************************
 * obdoutrec() - write out a record
 *
 * The input data is always in binary format. If IN, it is written out
 * out in ASCII; if OUT, it is written out in binary.
 */
int obdoutrec(fp, b, in_out, mess_len, wdbp)
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
"(Client:%s) Logic Error: obdoutrec(%x, %x, %d) called with NULL parameter(s)\n",
                   wdbp->pg.rope_seq,
                  (unsigned long int) fp,
                  (unsigned long int) b,(unsigned long int) in_out);
        return 0;
    }
    if (wdbp->debug_level > 3)
        (void) fprintf(stderr,"(Client:%s) obdoutrec(%x,%.*s,%d,%d)\n",
                      wdbp->pg.rope_seq, fp,
                       mess_len, b, (int) in_out,
                       mess_len);
    if (in_out == OUT)
    {
    char rlen[9];
    unsigned char * x = b;
    unsigned char * top = b + mess_len;
    int fld_cnt = (x[23] - 48) +
                  (x[22] - 48)*10 +
                  (x[21] - 48)*100 +
                  (x[20] - 48)*1000;   /* Fields to do */
/*
 * Set or clear the session ID
 */
        if (x[24] == '0' && x[25] == '0' && x[26] == '0' && x[27] == '0')
        {
            if (wdbp->remote_ref != (char *) NULL)
            {
                free(wdbp->remote_ref);
                wdbp->remote_ref = (char *) NULL;
            }
        }
        else
        {
            if (wdbp->remote_ref == (char *) NULL)
                wdbp->remote_ref = (char *) malloc(4);
            x[24] = wdbp->remote_ref[0];
            x[25] = wdbp->remote_ref[1];
            x[26] = wdbp->remote_ref[2];
            x[27] = wdbp->remote_ref[3];
        }
        x += 32;
        while (fld_cnt > 0 && x < top)
        {
            mess_len = (x[5] - 48) +
                     (x[4] - 48)*10 +
                     (x[3] - 48)*100 +
                     (x[2] - 48)*1000 +
                     (x[1] - 48)*10000 +
                     (x[0] - 48)*100000;
            x += 6 + mess_len;
            fld_cnt--;
        }
        mess_len = x - b;
        sprintf(rlen,"%08d",(mess_len - 32));
        memcpy(b + 12,rlen,8);
        if (fld_cnt > 0)
            (void) fprintf(stderr,
               "(Client:%s) Message (length %d) Corrupt: %d residual fields\n",
                          wdbp->pg.rope_seq,
                          mess_len, fld_cnt);
/*
 * Validate the format of the message
 */
        buf_len =  sendto((int) fp, b,mess_len, 0, 0 , 0);
        if (webdrive_base.debug_level > 1)
             (void) fprintf(stderr,
             "(Client:%s) Message Length %d Sent with return code: %d\n",
                          wdbp->pg.rope_seq,
                          mess_len, buf_len);
        if (wdbp->debug_level > 2)
        {
            in_out = IN;
            (void) obdoutrec(stderr, b, in_out, mess_len, wdbp);
        }
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
        (void) fprintf(stderr,"(Client:%s) obdoutrec() File: %x\n",
                              wdbp->pg.rope_seq, fp);
    return buf_len;
}
/*
 * Dummies to stop e2net.c pulling in genconv.c
 */
int app_recognise()
{
    return 0;
}
void do_null()
{
    return;
}
