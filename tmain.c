/*
 *    tmain.c - Main control for E2 Systems multi-threaded script drivers
 *
 *    Copyright (C) E2 Systems 1993, 2000
 *
 *    Timestamps are written when the appropriate events are logged.
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
 * SIGALRM - used to control typing rate
 ******************************************************************************
 * This source file contains:
 * -  The entry point
 * -  Some logic to deal with Windows message queue bollocks
 * -  Universally applicable code to deal with taking timings etc.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"

WEBDRIVE_BASE webdrive_base;      /* Global configuration information */
#ifdef MINGW32
static int glob_argc;
static char** glob_argv;
void peermain(void * p);
void _invalid_parameter();
#else
#include <execinfo.h>
#endif

/***********************************************************************
 * Main Program Starts Here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
int main(argc,argv,envp)
int argc;
char * argv[];
char * envp[];
{
WEBDRIVE_BASE * wdbp;
int i;
char *x;
#ifdef SOLAR
    setvbuf(stderr, NULL, _IOFBF, BUFSIZ);
#endif
#ifdef LINUX
    setvbuf(stderr, NULL, _IOFBF, BUFSIZ);
#endif
#ifdef MINGW32
WORD wVersionRequested;
WSADATA wsaData;
wVersionRequested = 0x0101; /* 1.1 maximises compatibility with UNIX */

    if ( WSAStartup( wVersionRequested, &wsaData ))
    {
        fprintf(stderr, "WSAStartup error: %d", WSAGetLastError());
        exit(1);
    }
    glob_argc = argc;
    glob_argv = argv;
#if __MSVCRT_VERSION__ >= 0x800
    (void) _set_invalid_parameter_handler(_invalid_parameter);
#endif
    WaitForSingleObject( CreateThread((LPSECURITY_ATTRIBUTES) NULL, 
                                      0,    /* Stack Size (default)   */ 
                                       (LPTHREAD_START_ROUTINE) peermain,
                                      (LPVOID) argv,
                                      0,    /* Run immediately        */ 
                                     &argc), INFINITE);
    exit(0);
}
/*
 * Attempt to void problems with message loops in case this thread has one
 * associated with a window
 */
__stdcall NtSetTimerResolution( unsigned long, char, unsigned long *);
void peermain(void * p)
{
int argc;
char *x;
WEBDRIVE_BASE * wdbp;
int i;
char ** argv;
MSG msg;
ULONG req_timer_res = 0x2710L;
ULONG act_timer_res;

    argc = glob_argc;
    argv = glob_argv;

    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
                    /* Make sure this thread has a message queue */
    NtSetTimerResolution( req_timer_res, 1, &act_timer_res);
#endif
/****************************************************
 *    Initialise
 */
    (void) sigset(SIGINT,SIG_IGN);
#ifdef AIX
#ifndef ANDROID
    (void) sigset(SIGDANGER,SIG_IGN);
#endif
#endif
    (void) sigset(SIGUSR1,siggoaway);
    (void) sigset(SIGSEGV,siggoaway);
    (void) sigset(SIGTERM,siggoaway);       /* In order to exit */
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
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif
#ifndef MINGW32
    (void) sigset(SIGCLD,SIG_DFL);
#endif
#ifndef LCC
    (void) sigset(SIGPIPE,SIG_IGN);   /* So we don't crash out */
#endif
    (void) sigset(SIGHUP,SIG_IGN);    /* So we don't crash out */
    (void) sigset(SIGALRM, read_timeout);
/*    (void) sigset(SIGIO, catch_sigio); */
    (void) sigset(SIGPIPE, read_timeout);
    (void) sigset(SIGIO, read_timeout);
    (void) sigset(SIGUSR2, read_timeout);
                                      /* To give Linux/Solaris compatibility */
    sighold(SIGTERM);                 /* So we stay in control */
    sighold(SIGUSR1);                 /* So we stay in control */
    sighold(SIGUSR2);                 /* So we stay in control */
    sighold(SIGALRM);                 /* So we stay in control */
    sighold(SIGIO);                   /* So we stay in control */
    init_from_environment();
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
    if (webdrive_base.proxy_port == 0)
    {
#ifdef TUNDRIVE
static pthread_attr_t attr;

        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 65536);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
/*
 * The tunnel needs a watchdog thread to handle unsolicited traffic coming
 * back the other way
 */
        if (pthread_create(&(webdrive_base.own_thread_id),
                              &attr,  watchdog_tunnel, &webdrive_base))
        {
            perror("Watchdog thread creation failed");
            exit(1);
        }
#endif
        schedule_clients(&webdrive_base);         /* Process the input files */
    }
    else
    {
#ifndef TUNDRIVE
static pthread_attr_t attr;
#ifdef SSLSERV
/*
 * SSL Server needs a thread for client scheduling
 */
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 65536);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&(webdrive_base.own_thread_id),
                        &attr,  schedule_clients, &webdrive_base))
        {
            perror("Server thread creation failed");
            exit(1);
        }
#else
/*
 * Proxy has debugger thread.
 */
        if (webdrive_base.mirror_port != 0)
        {
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 65536);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            if (pthread_create(&(webdrive_base.own_thread_id),
                              &attr,  mirror_client, &webdrive_base))
            {
                perror("Debug thread creation failed");
                exit(1);
            }
        }
#endif
#endif
/*
 * Listener uses threads as necessary.
 */
        t3drive_listen(&webdrive_base);
    }
/*
 * Do not free() resources because binning the allocated memory can crash
 * other threads that are just attempting to exit. Leave it to the OS on exit().
 */
#ifndef TUNDRIVE
    if (webdrive_base.proxy_port == 0)
        for (wdbp = &webdrive_base.client_array[0], i = 0;
            i < webdrive_base.client_cnt;
                 i++, wdbp++)
        {
            event_record_r("F", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                    /* Announce the finish */
        }
#endif
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
#ifndef MINGW32
unsigned char * calls[100];

    fflush(stderr);
    backtrace_symbols_fd(calls, 100, 2);
#endif
#ifndef TUNDRIVE
    if (webdrive_base.proxy_port == 0)
        for (wdbp = &webdrive_base.client_array[0], i = 0;
            i < webdrive_base.client_cnt;
                i++, wdbp++)
            event_record_r("F", (struct event_con *) NULL, &(wdbp->parser_con.pg));
    else
        snap_script(&webdrive_base);
                               /* Mark the end */
#endif
#ifdef MINGW32
    WSACleanup();
#endif
    fputs("Exiting via siggoaway() - has something bad happened?\n", stderr);
#ifndef TUNDRIVE
    if (webdrive_base.proxy_port == 0)
#endif
    {
        for (wdbp = &webdrive_base.client_array[0], i = 0;
            i < webdrive_base.client_cnt;
                i++, wdbp++)
        {
            fprintf(stderr, "Client: %d was doing %s\n", i, wdbp->narrative);
        }
        fflush(stderr);
    }
    exit(1);
}
/*
 * List out the program arguments
 */
void dump_args(argc, argv)
int argc;
char ** argv;
{
int i;

    for (i = 0; i < argc; i++)
        (void) fprintf(stderr, "Argument %d : %s\n", i, argv[i]);
    return;
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
