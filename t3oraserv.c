/*
 * t3oraserv.c - routines that support driving of BEA T3 and ORACLE
 * WebForms traffic, in addition to straight HTTP, with or without HTTPS
 *
 * ORACLE Forms Notes
 * ==================
 * ORACLE Forms streams are normally scrambled. We cannot edit these in their
 * scrambled states, so they need to be unscrambled first.
 *
 * The driver must either scramble, or pass across unscrambled, depending on
 * how the server responds to GDay. Mate = scrambled, Matf = unscrambled. Thus,
 * we have a single step, unscramble the capture, which we put in the snoop
 * file processor, webdump. Because the traffic may be tunnelled, webextlib.c
 * now has to be more sophisticated (and has been re-written as httpextlib.c),
 * but c'est la vie.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
#ifndef MINGW32
#include <pwd.h>
#endif
char * bind_host;

#ifdef DOTNET
#define WANT_EXCEPTION
#endif
#ifdef NEEDS_ORA_KEEPS
int create_keepalive();
#endif
/*
 * To track session types
 */
static int match_port[5];    /* List of ports to match against          */

static int match_cnt;          /* Number of ports in the list    */
static void ora_web_add(port)
int port;
{
    if (match_cnt < 5)
    {
       match_port[match_cnt] = port;
       match_cnt++;
    }
    return;
}
int ora_web_match(to)
int to;
{
int i;

    for (i = 0; i < match_cnt; i++)
    {
       if (match_port[i] == to)
           return  1;
    }
    return 0;
}
#ifdef USE_SSL
/*
 * Provide a pool of locks, and pointers to pthread functions, for
 * OpenSSL's locking mechanism
 */
static pthread_mutex_t *ssl_lock_cs;
static long *ssl_lock_count;
/*
 * Thread ID
 */
static unsigned long ssl_thread_id(void)
{
pthread_t pth;

    pth = pthread_self();
#ifdef MINGW32
    return (unsigned long) pth.thread_id;
#else
    return (unsigned long) pth;
#endif
}
/*
 * Lock/Unlock
 */
void ssl_locking_callback(int mode, int type, char *file,
	     int line)
{
#ifdef DEBUG
    fprintf(stderr,"thread=%4d mode=%s lock=%s %s:%d\n",
        CRYPTO_thread_id(),
        (mode&CRYPTO_LOCK)?"l":"u",
        (type&CRYPTO_READ)?"r":"w",file,line);
#endif
    if (mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&(ssl_lock_cs[type]));
        ssl_lock_count[type]++;
    }
    else
    {
        pthread_mutex_unlock(&(ssl_lock_cs[type]));
    }
}
/*
 * Initialise lock support data and install callbacks
 */
static void ssl_thread_setup(void)
{
int i;

    ssl_lock_cs = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
    ssl_lock_count = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long));
    for (i=0; i < CRYPTO_num_locks(); i++)
    {
        ssl_lock_count[i] = 0;
        pthread_mutex_init(&(ssl_lock_cs[i]),NULL);
    }
    CRYPTO_set_id_callback((unsigned long (*)())ssl_thread_id);
    CRYPTO_set_locking_callback((void (*)())ssl_locking_callback);
    return;
}
/*
 * Free up the resources allocated to support OpenSSL locking
 */
void ssl_thread_cleanup(void)
{
int i;

    CRYPTO_set_locking_callback(NULL);
    for (i=0; i<CRYPTO_num_locks(); i++)
    {
        pthread_mutex_destroy(&(ssl_lock_cs[i]));
    }
    OPENSSL_free(ssl_lock_cs);
    OPENSSL_free(ssl_lock_count);
}
/**********************************************************************
 * An SSL Spec is the stuff we need for a Session. I assume that we will
 * only need one session per context.
 */ 
void recognise_ssl_spec(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;

    a->ssl_spec.ssl_spec_id = wdbp->ssl_spec_cnt;
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
               a->ssl_spec.ssl_spec_ref,
             &(a->ssl_spec.ssl_spec_ref[sizeof(a->ssl_spec.ssl_spec_ref) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
               a->ssl_spec.key_file,
             &(a->ssl_spec.key_file[sizeof(a->ssl_spec.key_file) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few SSL Spec fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,a->ssl_spec.passwd,
                &(a->ssl_spec.passwd[sizeof(a->ssl_spec.passwd) - 1])) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few SSL Spec fields", 
             &(wdbp->parser_con));
    if (a->ssl_spec.passwd[ strlen(a->ssl_spec.passwd) - 1]
                          == '\n')
        a->ssl_spec.passwd[strlen(a->ssl_spec.passwd )-1] = '\0';
    return;
}
#endif
void init_from_environment()
{
char * x;
int i;

    tailor_threads();              /* Initialise thread attributes */
    bind_host = getenv("E2_BIND_HOST");
/*
 * Identify any ORACLE Forms ports
 */
#ifdef DOTNET
    if ((x = getenv("E2_DOTNET_PORTS")) != (char *) NULL)
#else
    if ((x = getenv("E2_ORA_WEB_PORTS")) != (char *) NULL)
#endif

    {
        for (x = strtok(x," "); x != (char *) NULL; x = strtok(NULL, " "))
        {
            if ((i = atoi(x)) > 0 && i < 65536)   
                ora_web_add(i);
        }
    }
#ifdef DOTNET
/*
 * Monitor frequency of periodic function calls
 */
    webdrive_base.call_filter.call_cnt = 0;
    webdrive_base.call_filter.first_run = 0.0;
    webdrive_base.call_filter.last_run = 0.0;
    if ((webdrive_base.call_filter.per_fun = getenv("E2_DOTNET_PER_FUN"))
            == (char *) NULL
     || (x = getenv("E2_DOTNET_PERIODICITY")) == NULL
     || (webdrive_base.call_filter.periodicity = strtod(x, NULL)) <= 0.0)
        webdrive_base.call_filter.per_fun = NULL;
#endif
#ifdef USE_SSL
/*
 * Global SSL Initialisation
 */
#ifdef DEBUG
    fputs("Starting OPENSSL initialisation\n", stderr);
#endif
#ifdef MINGW32
    CRYPTO_malloc_init();
#endif
    SSL_library_init();
    ssl_thread_setup();
    SSL_load_error_strings();
    ssl_bio_error = BIO_new_fp(stderr, BIO_NOCLOSE);
#ifdef DEBUG
    fputs("Finished OPENSSL initialisation\n", stderr);
#endif
#endif
#ifndef TUNDRIVE
    ini_huffman_decodes();
#endif
    return;
}
#ifndef DOTNET
#ifndef TUNDRIVE
/*
 * Lock down minitest (which usually provides a password-free method of
 * executing commands as root on a target machine).
 */
static void set_port_fixup(host_ports)
char * host_ports;
{
char * x;
/*
 * Generate a vector of valid hosts and fixed ports
 */
    for (webdrive_base.cookie_cnt = 0, x=strtok(host_ports,": ");
             webdrive_base.cookie_cnt < 100 && x != (char *) NULL;
                  x = strtok(NULL, ": "))
    {
        if (inet_addr(x) == -1)
        {
            webdrive_base.cookies[webdrive_base.cookie_cnt] = x;
            if ((x=strtok(host_ports,": ")) != (char *) NULL)
            {
                webdrive_base.cookies[webdrive_base.cookie_cnt + 1] = x;
                webdrive_base.cookie_cnt += 2;
            }
        }
    }
    return;
}
#endif
/*
 * Set up that relates to the proxy function (script capture or the HTTP
 * tunnel)
 */
void proxy_vars(argc, argv)
int argc;
char ** argv;
{
char *x;
int i;

#ifdef USE_SSL
/*
 * Horrendous logic. We later need to complete the set-up for the
 * webdrive_base clones, so the count will be reset!
 */
    webdrive_base.ssl_spec_cnt = 2;
    webdrive_base.ssl_specs[0].ssl_spec_id = 0;
    strcpy(webdrive_base.ssl_specs[0].ssl_spec_ref, "1");
    strcpy(webdrive_base.ssl_specs[0].key_file, "client.pem");
    strcpy(webdrive_base.ssl_specs[0].passwd, "password");
#endif
    webdrive_base.verbosity = 0;
    webdrive_base.mirror_bin = 1;
    webdrive_base.end_point_det[0].end_point_id = 0;
    strcpy(webdrive_base.end_point_det[0].address, "127.0.0.1");
    webdrive_base.end_point_det[0].cap_port_id = webdrive_base.proxy_port;
    strcpy(webdrive_base.end_point_det[0].host, "127.0.0.1");
    webdrive_base.end_point_det[0].port_id = webdrive_base.proxy_port;
    webdrive_base.end_point_det[0].con_flag = 'L';
    webdrive_base.end_point_det[0].proto_flag = 0;
#ifdef USE_SSL
    webdrive_base.end_point_det[0].ssl_spec_id = -1;
    webdrive_base.end_point_det[0].ssl_spec_ref[0] = '\0';
#endif
    webdrive_base.end_point_det[1].end_point_id = 1;
    strcpy(webdrive_base.end_point_det[1].address, "127.0.0.1");
    webdrive_base.end_point_det[1].cap_port_id = 10101; /* Arbitrary */
    strcpy(webdrive_base.end_point_det[1].host, "127.0.0.1");
    webdrive_base.end_point_det[1].port_id = 10101; /* Arbitrary */
    webdrive_base.end_point_det[1].con_flag = 'C';
    webdrive_base.end_point_det[1].proto_flag = 0;
#ifdef USE_SSL
    webdrive_base.end_point_det[1].ssl_spec_id = -1;
    webdrive_base.end_point_det[1].ssl_spec_ref[0] = '\0';
#endif
    webdrive_base.end_point_det[2].end_point_id = 2;
    strcpy(webdrive_base.end_point_det[2].address, "127.0.0.1");
    webdrive_base.end_point_det[2].cap_port_id = webdrive_base.proxy_port;
    strcpy(webdrive_base.end_point_det[2].host, "127.0.0.1");
    webdrive_base.end_point_det[2].port_id = webdrive_base.proxy_port;
    webdrive_base.end_point_det[2].con_flag = 'L';
    webdrive_base.end_point_det[2].proto_flag = 0;
#ifdef USE_SSL
    webdrive_base.end_point_det[2].ssl_spec_id = 0;
    strcpy(webdrive_base.end_point_det[2].ssl_spec_ref, "1");
#endif
/*
 * This is for the Man-In-The-Middle, tundrive and sslserv.
 */
    webdrive_base.end_point_det[3].end_point_id = 3;
    strcpy(webdrive_base.end_point_det[3].address, "127.0.0.1");
    webdrive_base.end_point_det[3].cap_port_id = 10102; /* Arbitrary */
    strcpy(webdrive_base.end_point_det[3].host, "127.0.0.1");
    webdrive_base.end_point_det[3].port_id = 10102; /* Arbitrary */
    webdrive_base.end_point_det[3].con_flag = 'L';
    webdrive_base.end_point_det[3].proto_flag = 0;
#ifdef USE_SSL
    webdrive_base.end_point_det[3].ssl_spec_id = 1;
    strcpy(webdrive_base.end_point_det[3].ssl_spec_ref, "0");
    webdrive_base.ssl_specs[1].ssl_spec_id = 1;
    strcpy(webdrive_base.ssl_specs[1].ssl_spec_ref, "0");
    strcpy(webdrive_base.ssl_specs[1].key_file, "server.pem");
    strcpy(webdrive_base.ssl_specs[1].passwd, "password");
#endif
    webdrive_base.ep_cnt = 4;
/*
 * Used for incoming without SSL
 */
    webdrive_base.link_det[0].from_ep = &webdrive_base.end_point_det[1];
    webdrive_base.link_det[0].to_ep = &webdrive_base.end_point_det[0];
    webdrive_base.link_det[0].connect_fd = -1;
    webdrive_base.link_det[0].pair_seq = 0;
    webdrive_base.link_det[0].link_id = LINK_TYPE;
    webdrive_base.link_det[0].t3_flag = 0;
    webdrive_base.link_det[0].remote_handle = (char *) NULL;
#ifdef USE_SSL
    webdrive_base.link_det[0].ssl_spec_id = -1;
#endif
/*
 * Used for outgoing with SSL
 */
    webdrive_base.link_det[1].from_ep = &webdrive_base.end_point_det[1];
    webdrive_base.link_det[1].to_ep = &webdrive_base.end_point_det[2];
    webdrive_base.link_det[1].connect_fd = -1;
    webdrive_base.link_det[1].pair_seq = 0;
    webdrive_base.link_det[1].link_id = LINK_TYPE;
    webdrive_base.link_det[1].t3_flag = 0;
    webdrive_base.link_det[1].remote_handle = (char *) NULL;
#ifdef USE_SSL
    webdrive_base.link_det[1].ssl_spec_id = 0;
#endif
/*
 * Used for incoming with SSL
 */
    webdrive_base.link_det[2].from_ep = &webdrive_base.end_point_det[1];
    webdrive_base.link_det[2].to_ep = &webdrive_base.end_point_det[3];
    webdrive_base.link_det[2].connect_fd = -1;
    webdrive_base.link_det[2].pair_seq = 0;
    webdrive_base.link_det[2].link_id = LINK_TYPE;
    webdrive_base.link_det[2].t3_flag = 0;
    webdrive_base.link_det[2].remote_handle = (char *) NULL;
#ifdef USE_SSL
    webdrive_base.link_det[2].ssl_spec_id = 1;
#endif
/*
 * Used for outgoing without SSL
 */
    webdrive_base.link_det[3].from_ep = &webdrive_base.end_point_det[1];
    webdrive_base.link_det[3].to_ep = &webdrive_base.end_point_det[0];
    webdrive_base.link_det[3].connect_fd = -1;
    webdrive_base.link_det[3].pair_seq = 0;
    webdrive_base.link_det[3].link_id = LINK_TYPE;
    webdrive_base.link_det[3].t3_flag = 0;
    webdrive_base.link_det[3].remote_handle = (char *) NULL;
#ifdef USE_SSL
    webdrive_base.link_det[3].ssl_spec_id = -1;
#endif
    webdrive_base.not_ssl_flag = 3;
    for (i = 0; i < 4; i++)
    {
        memset((char *) &(webdrive_base.link_det[i].connect_sock),
                         0,sizeof(webdrive_base.link_det[i].connect_sock));
        memset((char *) &(webdrive_base.link_det[i].in_det),
                         0,sizeof(webdrive_base.link_det[i].in_det));
        memset((char *) &(webdrive_base.link_det[i].out_det),
                         0,sizeof(webdrive_base.link_det[i].out_det));
    }
    webdrive_base.cur_link = &webdrive_base.link_det[0];
#ifndef TUNDRIVE
#ifndef SSLSERV
    webdrive_base.parser_con.pg.rope_seq = "Debugger Thread";
    webdrive_base.parser_con.pg.logfile=argv[optind]; /* Script file */
    if ((webdrive_base.sc.anchor = load_script(argv[optind],
            webdrive_base.debug_level)) != NULL)
    {   /* If the script exists already, we will append to it */
         for (webdrive_base.sc.last = webdrive_base.sc.anchor;
              webdrive_base.sc.last->next_track != NULL;
              webdrive_base.sc.last = webdrive_base.sc.last->next_track);
    }
    if ((x = getenv("E2PORT_FIXUPS")) != NULL)
        set_port_fixup(x);
#endif
#endif
    return;
}
#endif
/*
 * Process arguments
 */
void proc_args(argc,argv)
int argc;
char ** argv;
{
int c;
char *x;
int ssl_spec_flag = 0;
WEBDRIVE_BASE * wdbp;
int path_stagger;              /* Interval between client launchings */
int rope = 0;                  /* Which client this is               */
int thread0_verbosity = 0;
#ifdef DOTNET
    char call_marker[]  = {'\0','\0','\0',(char )18};
#endif
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
#ifdef TUNDRIVE
    webdrive_base.progress_client = tun_client;
    webdrive_base.webread = tunnel_read;      /* Default tunnel reader     */
#else
    webdrive_base.progress_client = progress_client;
    webdrive_base.webread = webread;          /* Default script reader     */
#endif
    webdrive_base.root_wdbp = &webdrive_base;
    webdrive_base.mirror_port = 0;
    webdrive_base.do_send_receive = do_send_receive;
    webdrive_base.proxy_port = 0;
    webdrive_base.mirror_fd = -1;
    pthread_mutex_init(&(webdrive_base.encrypt_mutex), NULL);
    if ((webdrive_base.sep_exp = getenv("E2_DEFAULT_SEP_EXP")) == (char *) NULL)
        webdrive_base.sep_exp = DEFAULT_SEP_EXP;
    webdrive_base.sep_exp_len = strlen( webdrive_base.sep_exp);
    while ( ( c = getopt ( argc, argv, "2hd:v:0:m:s:p:l:u:c:e:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'h' :
#ifdef TUNDRIVE
            (void) fputs("tundrive: E2 Systems HTTP Tunnel Driver\n\
Options:\n\
 -h prints this message on stderr\n\
 -v sets verbosity level (all packets are timestamped and may be logged)\n\
 -l sets a local proxy server URL\n\
 -0 sets verbosity level separately for thread 0\n\
 -2 says use HTTP/2 for communication with target.\n\
 -m says how many threads we are going to handle\n\
 -p says provide service via supplied port\n\
 -c certificate details for SSL (ignored)\n\
 -u non-privileged user to drop privileges to\n\
 -e environmental settings file\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Tunnel Device, Tunnel IP/Tunnel URL\n", stderr);
#else
#ifdef SSLSERV
            (void) fputs("sslserv: E2 Systems PATH Server\n\
Options:\n\
 -h prints this message on stderr\n\
 -v sets verbosity level (all packets are timestamped and may be logged)\n\
 -0 sets verbosity level separately for thread 0\n\
 -m says how many clients we are going to handle\n\
 -l proxy for proxy capture to proxy\n\
 -c certificate details for SSL (ignored)\n\
 -p says proxy using supplied port\n\
 -e environmental settings file\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Output File, Run ID, Bundle ID, Rope, Input Files\n", stderr);
#else
            (void) fputs("http2drive: E2 Systems Web Driver\n\
Options:\n\
 -h prints this message on stderr\n\
 -v sets verbosity level (all packets are timestamped and may be logged)\n\
 -0 sets verbosity level separately for thread 0\n\
 -m says how many clients we are going to handle\n\
 -s says single step using supplied port\n\
 -l proxy for proxy capture to proxy\n\
 -c certificate details for SSL (ignored)\n\
 -p says proxy using supplied port\n\
 -e environmental settings file\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Output File, Run ID, Bundle ID, Rope, Input Files\n", stderr);
#endif
#endif
            fflush(stderr);
            break;
        case 'd' :
            webdrive_base.debug_level = atoi(optarg);
            break;
        case 'e' :
            read_config(optarg);
            break;
        case 'v' :
            webdrive_base.verbosity = 1 + atoi(optarg);
            break;
        case '0' :
            thread0_verbosity = 1 + atoi(optarg);
            break;
#ifndef SSLSERV
#ifndef TUNDRIVE
        case '2' :
            webdrive_base.do_send_receive = do_send_receive_async;
            break;
#endif
#endif
        case 'c' :
            webdrive_base.parser_con.pg.rope_seq = "SSL set-up";
            webdrive_base.parser_con.tlook = (char *) malloc(WORKSPACE);
            strcpy( webdrive_base.parser_con.tlook, optarg);
            recognise_ssl_spec(&webdrive_base.msg, &webdrive_base);
            free(webdrive_base.parser_con.tlook);
            ssl_spec_flag = 1;
            break;
        case 'l' :
            webdrive_base.proxy_url = strdup(optarg);
            break;
#ifndef TUNDRIVE
#ifndef SSLSERV
        case 's' :
            webdrive_base.mirror_port = atoi(optarg);
            break;
#endif
#endif
        case 'p' :
#ifdef DOTNET
            webdrive_base.proxy_port = 0;
#else
            webdrive_base.proxy_port = atoi(optarg);
            if (webdrive_base.proxy_port > 0)
            {
#ifdef SSLSERV
                webdrive_base.progress_client = ssl_service;
                webdrive_base.webread = ssl_serv_read;
#else
#ifdef TUNDRIVE
                webdrive_base.progress_client = ssl_service;
                webdrive_base.webread = ssl_serv_read;
#else
                webdrive_base.progress_client = proxy_client;
                webdrive_base.webread = proxy_read;
#endif
#endif
            }
#ifdef TUNDRIVE
            else
            {
                webdrive_base.progress_client = tun_client;
                webdrive_base.webread = tunnel_read;
            }
#endif
#endif
            break;
        case 'm' :
            if ((webdrive_base.client_cnt = atoi(optarg)) < 1)
                webdrive_base.client_cnt = 1;    /* Client count              */
            break;
#ifndef MINGW32
        case 'u' :
        {
        struct passwd * p;

            if ((p = getpwnam(optarg)) == (struct passwd *) NULL)
            {
                (void) fprintf(stderr,"No such user %s!\n", optarg);
                exit(1);
            }
            webdrive_base.non_priv_uid = p->pw_uid;
            break;
        }
#endif
        default:
        case '?' : /* Default - invalid opt.*/
            (void) fprintf(stderr,"Invalid argument; try -h\n");
#ifdef MINGW32
            WSACleanup();
#endif
            exit(1);
        } 
    }
#ifdef TUNDRIVE
    if ((argc - optind) < 2 )
#else
#ifdef SSLSERV
    if (webdrive_base.proxy_port == 0)
#else
    if ((webdrive_base.proxy_port == 0
     && (argc - optind) < (4 + webdrive_base.client_cnt) )
    || (webdrive_base.proxy_port != 0
     && (argc - optind) < 1 ))
#endif
#endif
    {
        fprintf(stderr,
            "Insufficient Arguments Supplied; try -h\nSeen: %d Expected: %d\n",
              argc,
#ifdef TUNDRIVE
              3
#else
    ((webdrive_base.proxy_port == 0) ?  (4 + webdrive_base.client_cnt) : 0)
#endif
               );
        dump_args(argc, argv);
#ifdef MINGW32
        WSACleanup();
#endif
        exit(1);
    } 
#ifdef TUNDRIVE
    if (webdrive_base.proxy_port != 0 && webdrive_base.non_priv_uid != 0)
    {
        fputs("Cannot drop privileges if serving. Try -h.\n", stderr);
        dump_args(argc, argv);
#ifdef MINGW32
        WSACleanup();
#endif
        exit(1);
    }
#endif
#ifndef DOTNET
#ifndef TUNDRIVE
    if (webdrive_base.proxy_port != 0)
#endif
    {
#ifndef TUNDRIVE
#ifndef SSLSERV
        if (webdrive_base.mirror_port == 0)
        {
            fputs("Must provide a Mirror port if Proxy requested. Try -h.\n", stderr);
            dump_args(argc, argv);
#ifdef MINGW32
            WSACleanup();
#endif
            exit(1);
        } 
#endif
#endif
/*
 * This order is important. The input SSL spec for script capture must be
 * the second of the SSL specs
 */
        proxy_vars(argc, argv);
        webdrive_base.parser_con.pg.rope_seq = "SSL set-up";
        do_ssl_spec(&(webdrive_base.ssl_specs[0]), &webdrive_base);
        do_ssl_spec(&(webdrive_base.ssl_specs[1]), &webdrive_base);
        webdrive_base.proxy_ep.ssl_spec_id = 1;
    }
#endif
/*
 * Single-stepping implies only one client. But we have no control over the
 * browser, which will feed us requests over multiple sockets asynchronously.
 * So, we need a pool of pre-allocated slots to handle the requests, and a
 * separate thread to handle the mirror requests. This is really our debugger
 * interface. Rather than write out the script as we go, perhaps it would be
 * better to build up the script tree in memory. Then the user can go
 * backwards and forwards through the list, and we have the opportunity to
 * single step through an existing script.
 */
#ifdef SSLSERV
    if (webdrive_base.proxy_port != 0)
#else
#ifdef TUNDRIVE
    if (webdrive_base.proxy_port != 0)
#else
    if (webdrive_base.mirror_port != 0)
#endif
#endif
        webdrive_base.client_cnt = 100;
    if (webdrive_base.verbosity > 1)
        dump_args(argc, argv);
#ifdef TUNDRIVE
    webdrive_base.control_file = strdup(argv[optind++]); /* Tunnel device */
    webdrive_base.tunnel_ip = strdup(argv[optind]);      /* Tunnel IP */
#endif
#ifndef DOTNET
#ifdef TUNDRIVE
    webdrive_base.parser_con.pg.rope_seq = "Watchdog Thread";
#endif
    if (webdrive_base.proxy_port == 0)
#endif
    {
#ifdef TUNDRIVE
        webdrive_base.server_url = argv[optind++]; /* Remote URL */
        if (!ini_server(&webdrive_base))
            exit(1);
#else
        webdrive_base.parser_con.pg.logfile=argv[optind++];
        webdrive_base.parser_con.pg.fdriver_seq=argv[optind++]; /* Details needed by event   */
        webdrive_base.parser_con.pg.bundle_seq=argv[optind++];  /* recording                 */
        webdrive_base.parser_con.pg.rope_seq=argv[optind++]; 
        rope = atoi(webdrive_base.parser_con.pg.rope_seq);
#endif
    }
    webdrive_base.parser_con.pg.think_time = PATH_THINK;  /* default think time */
    webdrive_base.ep_cur_ptr = webdrive_base.end_point_det,
    webdrive_base.ep_max_ptr = &(webdrive_base.end_point_det[MAXENDPOINTS-1]);
    webdrive_base.scan_spec_internal = 0;
#ifdef DOTNET
    webdrive_base.authip = bm_compile("application/octet-stream");
    webdrive_base.authbp = bm_compile_bin(&call_marker[0], 4);
#else
    webdrive_base.authip = bm_casecompile("-Authenticate: NTLM");
                                                    /* WWW or Proxy */
    webdrive_base.authbp = bm_casecompile("-Authenticate: Basic");
                                                    /* WWW or Proxy */
    webdrive_base.authop = bm_casecompile("Authorization: "); /* WWW */
    webdrive_base.authpp = bm_casecompile("Proxy-Authorization: "); /* Proxy */
    webdrive_base.proxy_connect_flag = 0; /* In Proxy Connect */
    webdrive_base.authpc = bm_casecompile("Connection: close"); /* Proxy close */
    webdrive_base.authsp = bm_casecompile("Secure");
    webdrive_base.cookp = bm_casecompile("Cookie: ");
#ifndef SSLSERV
    ntlm_init();
#endif
#ifdef T3_DECODE
    webdrive_base.ebp = bm_compile("ENCRYPTED_TOKENt");
                           /* Where we get the encrypted token from */
    webdrive_base.rbp = bm_compile("StellarApp1Managed1xxx");
                           /* How we find the remote reference      */
/*
 * Having found a remote reference, it is necessary to work out what the
 * remote reference value in the script was. This is done by picking up
 * 8 bytes at offset 26 from the CMD_REQUEST with sequence 3.
 *******************************************************************
 */ 
#else
#ifndef DOTNET
    webdrive_base.ebp = bm_casecompile(" E2SESSION=");
    webdrive_base.rbp = bm_casecompile(" Basic ");
#endif
#endif
    webdrive_base.sbp = bm_casecompile("Content-Length: ");
    webdrive_base.ehp = bm_compile("\r\n\r\n");
    webdrive_base.traverse_u = bm_compile("../");
    webdrive_base.traverse_w = bm_compile("..\\");
    webdrive_base.boundaryp = bm_casecompile("boundary=");
#ifdef NEEDS_ORA_KEEPS
    webdrive_base.prp = bm_compile("Pragma: ");
#ifdef ORA9IAS_2
    webdrive_base.kap = bm_compile("ifError:11/");
    webdrive_base.erp = bm_compile("ifError:");
#endif
#ifdef ORA9IAS_1
    webdrive_base.kap = bm_compile("ifError:8/");
#endif
#endif
#endif
/*
 * Do the proxy set-up if appropriate
 */
    if (!ini_prox(&webdrive_base))
        exit(1);
    webdrive_base.idle_threads = circbuf_cre(webdrive_base.client_cnt + 3,NULL);
    pthread_mutex_init(&(webdrive_base.idle_thread_mutex), NULL);
    webdrive_base.go_times = circbuf_cre(webdrive_base.client_cnt + 3, NULL);
    pthread_mutex_init(&(webdrive_base.go_time_mutex), NULL);
    if ((x = getenv("PATH_STAGGER")) == (char *) NULL
      || ((path_stagger = atoi(x)) < 1))
        path_stagger = 1;             /* Default interval */
#ifdef TUNDRIVE
    if (webdrive_base.proxy_port == 0)
        obtain_tunnel_ip(&webdrive_base);
#endif
/*
 * Now set up the control structures for each client
 */
    webdrive_base.client_array = (WEBDRIVE_BASE *)
                    malloc(sizeof(webdrive_base) * webdrive_base.client_cnt);
    for (c = 0, wdbp = &(webdrive_base.client_array[0]);
             c < webdrive_base.client_cnt;
                  c++, wdbp++)
    {
        if (webdrive_base.proxy_port == 0)
        {
#ifdef TUNDRIVE
            wdbp->parser_con.pg.rope_seq = (char *) NULL;
                                 /* Mark it out of use */
#else
            webdrive_base.control_file = argv[optind++];
            if (optind > argc
             || (webdrive_base.parser_con.pg.cur_in_file =
                    (!strcmp(webdrive_base.control_file, "-"))? stdin : 
                 fopen(webdrive_base.control_file,"rb"))
                     == (FILE *) NULL)
            {
                unexpected(__FILE__, __LINE__,"Failed to open control file");
#ifdef MINGW32
                WSACleanup();
#endif
                wdbp->parser_con.pg.rope_seq = (char *) NULL;
                                 /* Mark it out of use */
                continue;
            }
#endif
        }
        *wdbp = webdrive_base;
/*
 * Set a (higher?) level of verbosity for thread 0
 */
        if (thread0_verbosity)
        {
            wdbp->verbosity = thread0_verbosity;
            thread0_verbosity = 0;
        }
#ifdef TUNDRIVE
        wdbp->parser_con.tbuf = (char *) malloc(WORKSPACE);
        wdbp->parser_con.sav_tlook = (char *) malloc(WORKSPACE);
        wdbp->parser_con.tlook = wdbp->parser_con.sav_tlook;
#else
        pthread_mutex_init(&(wdbp->script_mutex), NULL);
        ini_parser_con(&wdbp->parser_con);
        if (bind_host != (char *) NULL)
            wdbp->bind_port = 1050 + ((50 * c) % 30031); 
#endif
/*
 * Set up the hash table for macro variables. The proxy may use it to
 * deal with user sign-ons.
 */
        wdbp->parser_con.csmacro.wdbp = wdbp;
        wdbp->ht = hash(MAX_SCAN_SPECS, hash_func, comp_func);
#ifdef SSLSERV
        wdbp->pbp = pipe_buf_cre(3); /* Do we need more than 1??? */
#endif
#ifndef TUNDRIVE
        if (wdbp->proxy_port == 0)
        {
/*
 * Set up the hash table for events
 */
            wdbp->parser_con.pg.poss_events = hash(MAX_EVENT,long_hh,icomp);
/*
 * Set up the hash table for scan specifications
 */
#ifdef DOTNET
/*
 * Encrypted Login Handling. We adapt the routines that support Stellar
 * login in t3drive.
 *****************************************************************************
 * dotnetdump needs to decrypt these things, because we will certainly
 * have to make substitutions within them.
 *****************************************************************************
 * But dotnetdump can't work out the decryption from the client, because we
 * don't know the client private key. It has to get the values from SQL Server
 * traffic.
 *****************************************************************************
 * dotnetdrive has to pick up the key and IV running live, and then recognise
 * the script stuff that has to be encrypted.
 *
 * We have the following variables to play with.
 *****************************************************************************
 * char * remote_ref;      => Use for the session key
 * char * encrypted_token; => Use for the session IV
 * struct bm_table * ebp;  => Use to locate the encrypted session key.
 * struct bm_table * rbp;  => Use to locate the encrypted session IV.
 * struct bm_table * sbp;  => Use to locate where to put an encrypted
 *                            block.
 * struct bm_table * ehp;  => Use to locate where to put an encrypted
 *                            block ... if we need multiple ones.
 */
            webdrive_base.ebp = bm_compile("encryptedIv");
            webdrive_base.rbp = bm_compile("value");
                           /* Positioning to just before the key and IV. */
                           /* How we find the remote reference      */
            webdrive_base.sbp = bm_casecompile("EncryptedObject+encryptedBytes");
/* Old version        webdrive_base.ehp = bm_compile("SCasMidTierTypes, Version=12."); */
            webdrive_base.ehp = bm_compile("TCasMidTierTypes, Version=13.");
                           /* The second fragment is actually in the piece that
                            * will be encrypted. Find its end by locating the
                            * padding bytes ...
                            */
#else
#ifdef ORA9IAS_1
            internal_scan_spec(wdbp, "C","JServSessionIdroot=", 19
                                 "JServSessionIdroot=", 19);
#else
#ifdef ORA9IAS_2
            internal_scan_spec(wdbp, "C","jsessionid=", 11, "jsessionid=", 11);
            internal_scan_spec(wdbp, "O","jsessionid=", 11, "jsessionid=", 11);
#endif
#endif
#endif
#ifdef WANT_EXCEPTION
#ifdef DOTNET
            internal_scan_spec(wdbp, "E","System.Exception",16, NULL, 0);
#else
            internal_scan_spec(wdbp, "E","Exception=",10, NULL, 0);
#endif
#endif
        }
#ifndef DOTNET
        else
        {
/*
 * Proxy - create a global edit to deal with https -> http and allow us to
 * see which thread handled each connection.
 */
#ifdef TUNDRIVE
            pthread_mutex_init(&(wdbp->script_mutex), NULL);
#else
            internal_scan_spec(wdbp, "O", "https:", 0, "https:", 0);
            wdbp->scan_spec[0]->encrypted_token = strdup("http:");
            wdbp->scan_spec[0]->o_len = 6;
            wdbp->scan_spec[0]->frozen = 1;
            internal_scan_spec(wdbp, "O", "https%25", 0, "https%25", 0);
            wdbp->scan_spec[1]->encrypted_token = strdup("http%25");
            wdbp->scan_spec[1]->o_len = 8;
            wdbp->scan_spec[1]->frozen = 1;
            wdbp->end_point_det[1].cap_port_id += c; /* Distinguish Threads */
            wdbp->end_point_det[1].port_id += c;     /* Distinguish Threads */
#endif
        }
#endif
#endif
/*
 * Initialise the end point management
 */
        wdbp->ep_cur_ptr = wdbp->end_point_det,
        wdbp->ep_max_ptr = &(wdbp->end_point_det[MAXENDPOINTS-1]);
/*
 * Schedule the client
 */
        wdbp->parser_con.pg.rope_seq = (char *) malloc(12);
        sprintf(wdbp->parser_con.pg.rope_seq,"%u", rope);
        if (wdbp->proxy_port == 0)
        {
#ifndef TUNDRIVE
            wdbp->parser_con.pg.logfile = malloc(2
                  + strlen(webdrive_base.parser_con.pg.logfile)
                  + strlen(wdbp->parser_con.pg.rope_seq));
            (void) sprintf(wdbp->parser_con.pg.logfile, "%s.%s",
                       webdrive_base.parser_con.pg.logfile,
                       wdbp->parser_con.pg.rope_seq);
#endif
            add_time(&webdrive_base, wdbp, (c + 1) * path_stagger);
#ifndef TUNDRIVE
            event_record_r("S", (struct event_con *) NULL,
                      &(wdbp->parser_con.pg));  /* Announce the start */
#endif
        }
#ifndef DOTNET
#ifndef TUNDRIVE
        else
#endif
        {
            wdbp->link_det[0].from_ep = &wdbp->end_point_det[1];
            wdbp->link_det[0].to_ep = &wdbp->end_point_det[0];
            wdbp->link_det[1].from_ep = &wdbp->end_point_det[1];
            wdbp->link_det[1].to_ep = &wdbp->end_point_det[2];
            wdbp->cur_link = &wdbp->link_det[0];
            wdbp->ssl_spec_cnt = 0;
#ifndef SSLSERV
            do_ssl_spec(&(webdrive_base.ssl_specs[0]), wdbp);
            do_ssl_spec(&(webdrive_base.ssl_specs[1]), wdbp);
#endif
        }
#endif
        rope++;
    }
    webdrive_base.own_thread_id = pthread_self();
/*
 * Prevent exit when otherwise idle if being a proxy
 */
#ifndef DOTNET
#ifdef TUNDRIVE
/*
 * We use the script mutex to serialise access to the tun device
 */
    if (webdrive_base.proxy_port == 0)
        pthread_mutex_init(&(webdrive_base.script_mutex), NULL);
#else
    if (webdrive_base.proxy_port != 0)
#endif
    {
        webdrive_base.active_cnt = webdrive_base.client_cnt + 1;
#ifndef TUNDRIVE
        pthread_mutex_init(&(webdrive_base.script_mutex), NULL);
#endif
    }
#endif
    if (webdrive_base.debug_level > 1)
    {
        (void) fputs("proc_args()\n", stderr);
        (void) fflush(stderr);
        dump_args(argc,argv);
    }
    return;
}
#ifndef TUNDRIVE
/*****************************************************************************
 * Take an incoming message in ASCII and make it binary. 
 *
 * Does not assume only one connection active at a time, but does assume only
 * one message. This is a valid assumption for HTTP2.
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

    strcpy(wdbp->narrative, "recognise_message");
    (void) nextasc_r(wdbp->parser_con.tlook, ';', '\\', &got_to,
                      &address[0],&address[sizeof(address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields",
             &(wdbp->parser_con));
    port_id = atoi(ret_buf);
    if ((from_ep = ep_find(address,port_id, wdbp)) ==
        (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ';', '\\', &got_to, &address[0],
                      &address[sizeof(address) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few message fields",
             &(wdbp->parser_con));
    port_id = atoi( ret_buf);
    if ((to_ep = ep_find(address,port_id, wdbp)) == (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));

    if (wdbp->cur_link->from_ep != from_ep
      || wdbp->cur_link->to_ep != to_ep)
    {
        wdbp->cur_link = link_find(from_ep, to_ep, wdbp);
        if (wdbp->cur_link->from_ep != from_ep
          || wdbp->cur_link->to_ep != to_ep)
        {
        union all_records al;
            
            memset((unsigned char *) &al.link, 0, sizeof(al.link));
            al.link.link_id = LINK_TYPE;
            al.link.connect_fd = -1;
#ifdef DOTNET
            al.link.t3_flag = 1;
#endif
#ifdef USE_SSL
            al.link.ssl_spec_id = -1;
#endif
            al.link.from_ep = from_ep;
            al.link.to_ep = to_ep;
            do_link(&al, wdbp);
        }
    }
    if ( (wdbp->cur_link->t3_flag == 1)
     ||  wdbp->cur_link->from_ep->proto_flag
     ||  wdbp->cur_link->to_ep->proto_flag)
        in = ORAIN;
    else
        in = IN;
/*
 * Now look for Cookie, Error, Offset or Replace data value sets
 */
    while (recognise_scan_spec(a, wdbp, &got_to) == SCAN_SPEC_TYPE);
/*
 * Now read the data to be sent.
 */
    if ((mess_len = webinrec((long int) wdbp->parser_con.pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
    {
        if (mess_len == 0)
            return E2COMMENT;
        syntax_err(__FILE__,__LINE__, "Invalid format web record",
             &(wdbp->parser_con));
    }
    a->send_receive.record_type = SEND_RECEIVE_TYPE;
    a->send_receive.msg = &(wdbp->msg);
    a->send_receive.message_len = mess_len;
    wdbp->parser_con.look_status = CLEAR;
    return SEND_RECEIVE_TYPE;
}
enum tok_id recognise_answer(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
enum direction_id in;

    if ( (wdbp->cur_link->t3_flag == 1)
     ||  wdbp->cur_link->from_ep->proto_flag
     ||  wdbp->cur_link->to_ep->proto_flag)
        in = ORAIN;
    else
        in = IN;
    strcpy(wdbp->narrative, "recognise_answer");
    if ((mess_len = webinrec((long int) wdbp->parser_con.pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
        syntax_err(__FILE__,__LINE__,
              "Invalid format web record",
             &(wdbp->parser_con));
    wdbp->parser_con.look_status = CLEAR;
    a->send_receive.record_type = E2COMMENT;
    return E2COMMENT;
}
#ifdef NEEDS_ORA_KEEPS
/*
 * Handle errors returned from the ORACLE protocol
 */
int create_keepalive(wdbp, b, x)
WEBDRIVE_BASE * wdbp;
unsigned char * b;
unsigned char *x;
{
unsigned char *lenp, * pragp;
char prag_buf_before[32];
char prag_buf_after[32];
int new_len;

    if ((pragp = bm_match(wdbp->prp, b, x)) == (unsigned char *) NULL)
        return 0;    /* No Pragma do nothing */
    strcpy(wdbp->narrative, "create_keepalive");
    pragp += wdbp->prp->match_len;
    if ((lenp = bm_match(wdbp->sbp, pragp, x)) == (unsigned char *) NULL)
        return 0;    /* No content length, do nothing */
    lenp += wdbp->sbp->match_len;
    strcpy(lenp, "0\r\n\r\n");
    x = lenp + 5;
    sprintf(prag_buf_before, "%d", wdbp->pragma_seq);
    wdbp->pragma_seq++;
    sprintf(prag_buf_after, "%d", wdbp->pragma_seq);
    new_len = 1 + strlen(prag_buf_after) - strlen(prag_buf_before);
    memmove(pragp + new_len, pragp, (x - pragp));
    pragp += sprintf(pragp,"-%s",prag_buf_after);
    *pragp = '\r';
    wdbp->pragma_flag = 1;
    return (x + new_len) - b;
}
#endif
#endif
/*******************************************************************************
 * Process messages. This routine is only called if there is something
 * to do.
 *******************************************************************************
 * Web-specific code is included here. The general solution would
 * provide hooks into which any TCP/IP based message passing scheme could
 * be attached.
 *******************************************************************************
 * Evolutionary development over many years has given us:
 * -  A lump of context; the WEBDRIVE_BASE structure, that contains info for
 *    the communications end points, the session status, substitutions, reading
 *    of script input files, macro processing and what have you
 * -  A thread management framework, that creates, destroys and despatches
 *    threads to do work on the contextual things
 * -  A so far limited separation of the code to accept input, and progress the
 *    the context.
 * -  No separation at all of this, the routine that does most of the work
 *    on a request.
 *******************************************************************************
 * We really want this stuff to be in loadable modules, so that:
 * -  We don't need all the different #ifdef's spread through the code
 * -  Supporting new applications simply requires the development of a limited
 *    number of well defined routines.
 *******************************************************************************
 * This is where the webtest HTTP server code will get hooked in.
 */
void do_send_receive(msg, wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
enum direction_id out;
unsigned char * p1;
int resend_cnt = 0;
int mirror_out = -1;
char * user = NULL;                    /* Values used for authentication */
char * password = NULL;
char * machine = NULL;
char * domain = NULL;
double ttime;
double rtime;
struct script_element * sep;
char name_buf[256];

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Send Receive Message Sequence %d\n",
            wdbp->parser_con.pg.rope_seq,
                   wdbp->parser_con.pg.seqX);
        fflush(stderr);
    }
/*
 * If we are single stepping, accept the prompt.
 */
    strcpy(wdbp->narrative, "do_send_receive");
#ifndef TUNDRIVE
    if (wdbp->mirror_port != 0 && wdbp->proxy_port == 0)
        mirror_out = mirror_setup(msg, wdbp);
    if (wdbp->parser_con.pg.curr_event != NULL
#ifdef DIFF_TIME
      && wdbp->verbosity > 2
#endif
       )
    {
        ttime = timestamp();
        wdbp->last_activity = ttime;
    }
    strncpy (name_buf,  msg->send_receive.msg->buf + msg->send_receive.message_len, 255);
    name_buf[255] = '\0';
#endif
    if (wdbp->cur_link->connect_fd == -1)
    {
        if (wdbp->debug_level > 2)
            fprintf(stderr,
                   "(Client:%s) From: %s,%d (%s,%d) To: %s,%d (%s,%d) Not Connected!?\n",
                        wdbp->parser_con.pg.rope_seq,
                        wdbp->cur_link->from_ep->address,
                        wdbp->cur_link->from_ep->cap_port_id,
                        wdbp->cur_link->from_ep->host,
                        wdbp->cur_link->from_ep->port_id,
                        wdbp->cur_link->to_ep->address,
                        wdbp->cur_link->to_ep->cap_port_id,
                        wdbp->cur_link->to_ep->host,
                        wdbp->cur_link->to_ep->port_id);
        t3drive_connect(wdbp);
    }
    if ( (wdbp->cur_link->t3_flag == 1)
     ||  wdbp->cur_link->from_ep->proto_flag
     ||  wdbp->cur_link->to_ep->proto_flag)
        out = ORAOUT;
    else
        out = OUT;

#ifndef DOTNET
#ifndef TUNDRIVE
/*
 * Script capture
 */
    if (wdbp->proxy_port != 0)
    {
        pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
        sep = add_message(&wdbp->root_wdbp->sc, wdbp->cur_link);
        sep->body = (unsigned char *) malloc(msg->send_receive.message_len);
        memcpy(sep->body, msg->send_receive.msg->buf,
                      msg->send_receive.message_len);
        sep->body_len = msg->send_receive.message_len;
        pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
        if (wdbp->cur_link->connect_fd == -1)
        {
            smart_write((int) wdbp->parser_con.pg.cur_in_file,
                 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n",
                         45, 0, wdbp); 
            pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
            sep = add_answer(&wdbp->root_wdbp->sc, wdbp->cur_link);
            sep->body = strdup(
                 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            sep->body_len = 45;
            pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
            return;
        }
    }
#endif
#endif
resend:
    if (resend_cnt > 5)
        return;
/*
 * Send - Message has been assembled in msg.
 */
    if (wdbp->cur_link->connect_fd < 0
     || (msg->send_receive.message_len > 0
        && weboutrec(wdbp->cur_link->connect_fd, msg->send_receive.msg->buf,
                      out,
                     msg->send_receive.message_len, wdbp) < 1))
    {
        fflush(wdbp->parser_con.pg.log_output);
        fflush(stderr);
        perror("Error from do_send_receive weboutrec()");
        fflush(wdbp->parser_con.pg.log_output);
        fflush(stderr);
        if ( wdbp->cur_link->from_ep == (END_POINT *) NULL
           ||  wdbp->cur_link->from_ep == (END_POINT *) NULL)
        {
             fprintf(stderr,
               "(Client: %s) Link (%d) is missing at least one end point\n",
                        wdbp->parser_con.pg.rope_seq, (wdbp->cur_link - wdbp->link_det));
        }
        else
            fprintf(stderr,
                   "(Client:%s) From: %s,%d (%s,%d) To: %s,%d (%s,%d) = %d\n",
                        wdbp->parser_con.pg.rope_seq,
                        wdbp->cur_link->from_ep->address,
                        wdbp->cur_link->from_ep->cap_port_id,
                        wdbp->cur_link->from_ep->host,
                        wdbp->cur_link->from_ep->port_id,
                        wdbp->cur_link->to_ep->address,
                        wdbp->cur_link->to_ep->cap_port_id,
                        wdbp->cur_link->to_ep->host,
                        wdbp->cur_link->to_ep->port_id,
                        wdbp->cur_link->connect_fd);
        if (out == ORAOUT)
            (void) weboutrec(stderr, msg->send_receive.msg->buf, ORAIN,
                               msg->send_receive.message_len, wdbp);
        else
            (void) weboutrec(stderr, msg->send_receive.msg->buf, IN,
                               msg->send_receive.message_len, wdbp);
        fflush(stderr);
        if (wdbp->proxy_port == 0)
#ifdef TUNDRIVE
        {
/*
 * Can only resend if the send/receive is with respect to a socket
 */
            socket_cleanup(wdbp);
            t3drive_connect(wdbp);
            resend_cnt++;
            goto resend;
        }
#else
            event_record_r("X", (struct event_con *) NULL,
                                 &(wdbp->parser_con.pg));
                                                 /* Note the message */
        else
        {
            pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
            add_close(&wdbp->root_wdbp->sc, wdbp->cur_link);
            pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
        }
        socket_cleanup(wdbp);
        if ( (wdbp->cur_link->t3_flag == 1)
/*         ||  wdbp->cur_link->from_ep->proto_flag
         ||  wdbp->cur_link->to_ep->proto_flag */
         ||  resend_cnt > 3)
        {
            if (wdbp->proxy_port == 0 && wdbp->cur_link->t3_flag == 1)
                do_resync(wdbp);
        }
        else
        {
            t3drive_connect(wdbp);
            resend_cnt++;
            goto resend;
        }
        if (mirror_out > -1)
            closesocket(mirror_out);
        if ( msg->send_receive.message_len > WORKSPACE)
            free(wdbp->overflow_send);
        return;     /* Web session; plough on regardless. */
#endif
    }
    wdbp->except_flag = 0;
    if (wdbp->verbosity)
    {
#ifndef TUNDRIVE
        if (wdbp->verbosity > 4 && wdbp->proxy_port == 0)
            event_record_r("T", (struct event_con *) NULL,
                                &(wdbp->parser_con.pg));
                                         /* Note the message */
#endif
        if (wdbp->verbosity > 1)
        {
            fprintf(stderr, "(Client: %s) Out===>\n",
                      wdbp->parser_con.pg.rope_seq);
            if (out == ORAOUT)
                (void) weboutrec(stderr, msg->send_receive.msg->buf, ORAIN,
                                    msg->send_receive.message_len, wdbp);
            else
                (void) weboutrec(stderr, msg->send_receive.msg->buf, IN,
                                    msg->send_receive.message_len, wdbp);
        }
    }
#ifndef TUNDRIVE
#ifdef DOTNET
     if (msg->send_receive.msg->buf[6] == 1)
#else
    if (out == ORAOUT
      && msg->send_receive.msg->buf[0] == 0
      && (msg->send_receive.msg->buf[4] == 4
       || msg->send_receive.msg->buf[4] == 8))
#endif
    {
        if (wdbp->verbosity)
            fputs("One way message: No response expected\n",stderr);
        if (mirror_out > -1)
            closesocket(mirror_out);
        if ( msg->send_receive.message_len > WORKSPACE)
            free(wdbp->overflow_send);
        return;
    }
#endif
/*
 * Receive
 */
    wdbp->alert_flag = 0;  /* To distinguish random and purposeful interrupts */
#ifdef TUNDRIVE
    if (wdbp->proxy_port != 0)
        out = IN;          /* Indicate that the output is to the tunnel */
#endif
    alarm_preempt(wdbp);
    do
    {
        if ( (mess_len = 
            webinrec((long int) wdbp->cur_link->connect_fd,
                     &(wdbp->ret_msg), out, wdbp)) < 1)
        {
#ifndef TUNDRIVE
            if (wdbp->proxy_port == 0)
                event_record_r("X", (struct event_con *) NULL,
                                   &(wdbp->parser_con.pg));
                                                         /* Note the message */
#endif
            alarm_restore(wdbp);
#ifndef TUNDRIVE
            socket_cleanup(wdbp);
/*
 * Is this really necessary?
 */ 
            if ( wdbp->cur_link->t3_flag == 1
/* ||  wdbp->cur_link->from_ep->proto_flag
             ||  wdbp->cur_link->to_ep->proto_flag */ )
            {
                if (wdbp->proxy_port == 0)
                    do_resync(wdbp);
                       /* Application server; have to give up and start over */
            }
            else
            {
                t3drive_connect(wdbp);
                resend_cnt++;
                goto resend;
            }
            if (mirror_out > -1)
                closesocket(mirror_out);
            if ( msg->send_receive.message_len > WORKSPACE)
                free(wdbp->overflow_send); /* Free the overflow buffer */
            return;
#endif
        }
#ifdef DEBUG
#ifndef TUNDRIVE
        else
        if (out == ORAOUT)
        {
            fprintf(stderr, "(Client: %s) Send:[0]=%x [7]=%x [8]=%x [9]=%x [10]=%x Recv:[7]=%x [8]=%x [9]=%x [10]=%x\n",
                      wdbp->parser_con.pg.rope_seq,
                msg->send_receive.msg->buf[0],
                msg->send_receive.msg->buf[7],
                msg->send_receive.msg->buf[8],
                msg->send_receive.msg->buf[9],
                msg->send_receive.msg->buf[10],
                wdbp->ret_msg.buf[7],
                wdbp->ret_msg.buf[8],
                wdbp->ret_msg.buf[9],
                wdbp->ret_msg.buf[10]);
            fflush(stderr);
        }
#endif
#endif
    }
    while (out == ORAOUT
     && msg->send_receive.msg->buf[0] == 0
     && memcmp(&(wdbp->ret_msg.buf[7]), &(msg->send_receive.msg->buf[7]), 4));
            /* Oracle forms type-ahead rules */
    alarm_restore(wdbp);
#ifndef TUNDRIVE
    if (wdbp->parser_con.pg.curr_event != NULL
#ifdef DIFF_TIME
      && wdbp->verbosity > 2
#endif
       )
    {
        rtime = timestamp();
        wdbp->last_activity = rtime;
        log_dotnet_rpc(&(wdbp->parser_con.pg), name_buf, ttime, (rtime - ttime));
#ifndef DIFF_TIME
        wdbp->parser_con.pg.curr_event->time_int += (rtime - ttime);
#endif
        ttime = rtime;
    }
#endif
/*
 * Now do whatever processing is necessary on the received message.
 * If proxy, we have got rid of gzip and chunking, so we need to adjust
 * the headers. We don't do any further processing in this case; it is
 * up to the real user. 
 */
#ifndef DOTNET
    if (wdbp->proxy_port != 0)
    {
        proxy_forward(  (mess_len > WORKSPACE) ?
                            wdbp->overflow_receive : (char *) &(wdbp->ret_msg),
                                          mess_len, wdbp);
        if ( msg->send_receive.message_len > WORKSPACE)
            free(wdbp->overflow_send); /* Free the overflow buffer */
        if ( mess_len > WORKSPACE && wdbp->overflow_receive != NULL)
        {
            free(wdbp->overflow_receive); /* Free the overflow buffer */
            wdbp->overflow_receive = NULL;
        }
        return;
    }
    else
#endif
#ifndef TUNDRIVE
    if (wdbp->verbosity || wdbp->except_flag)
    {
        if (wdbp->verbosity > 4 && wdbp->proxy_port == 0)
            event_record_r("R", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                                          /* Note the message */
        if (wdbp->except_flag & E2_GOOD_FOUND)
            event_record_r("G", (struct event_con *) NULL, &(wdbp->parser_con.pg));
                                    /* Announce success */
        else
        if (wdbp->except_flag &
                 (E2_HTTP_ERROR | E2_SERVER_ERROR | E2_ERROR_FOUND
                     | E2_DISASTER_FOUND))
            event_record_r("Z", (struct event_con *) NULL, &(wdbp->parser_con.pg));
        if (wdbp->parser_con.pg.curr_event != NULL)
        {
            if (wdbp->except_flag & E2_HTTP_ERROR)
                wdbp->parser_con.pg.curr_event->flag_cnt[0]++;
            if (wdbp->except_flag & E2_SERVER_ERROR)
                wdbp->parser_con.pg.curr_event->flag_cnt[1]++;
            if (wdbp->except_flag & E2_ERROR_FOUND)
                wdbp->parser_con.pg.curr_event->flag_cnt[2]++;
            if (wdbp->except_flag & E2_GOOD_FOUND)
                wdbp->parser_con.pg.curr_event->flag_cnt[3]++;
        }
        if (wdbp->verbosity > 1 || (wdbp->except_flag != 0
          && !(wdbp->except_flag & E2_GOOD_FOUND)))
        {
            if (wdbp->verbosity < 2)
            {    /* Otherwise we have output it already */
                fprintf(stderr, "(Client: %s) Out===> except_flag: 0x%x\n",
                      wdbp->parser_con.pg.rope_seq, wdbp->except_flag);
                if (out == ORAOUT)
                    (void) weboutrec(stderr, msg->send_receive.msg->buf, ORAIN,
                                    msg->send_receive.message_len, wdbp);
                else
                    (void) weboutrec(stderr, msg->send_receive.msg->buf, IN,
                                        msg->send_receive.message_len, wdbp);
            }
            fprintf(stderr, "(Client: %s) In<==== except_flag: 0x%x\n",
                      wdbp->parser_con.pg.rope_seq, wdbp->except_flag);
            if (((wdbp->except_flag & E2_HTTP_ERROR) == 0)
             ||  (strncmp(&wdbp->ret_msg.buf[9], "401", 3)
             &&  strncmp(&wdbp->ret_msg.buf[9], "407", 3)))
            {
                (void) weboutrec(stderr, (char *)
                 ((mess_len > WORKSPACE) ? wdbp->overflow_receive :
                                         &(wdbp->ret_msg)),
                           (out == ORAOUT)? ORAIN : IN,
                                          mess_len, wdbp);
            }
        }
    }
/*
 * Take any special action based on the message just processed ...
 * If we have the keepalive response, resend
 */
#ifdef NEEDS_ORA_KEEPS
    if (bm_match(wdbp->kap,
        (unsigned char *) &(wdbp->ret_msg),
        ((unsigned char *) &(wdbp->ret_msg)) +
             (( mess_len < WORKSPACE) ? mess_len : WORKSPACE))
                     != (unsigned char *) NULL)
    {
        if (msg->send_receive.message_len = create_keepalive(wdbp,
                 msg->send_receive.msg,
                 ((unsigned char *) (msg->send_receive.msg->buf))
                           + msg->send_receive.message_len))
            goto resend;
    }
#endif
    if ((wdbp->except_flag & E2_ERROR_FOUND)
      && (resend_cnt < 3)
      && (check_recoverable(wdbp,
    (unsigned char *) &(wdbp->ret_msg),
    ((unsigned char *) &(wdbp->ret_msg)) +
         (( mess_len < WORKSPACE) ? mess_len : WORKSPACE))
                 != (unsigned char *) NULL))
    {
/*
 * We have received instructions to Rerun the transaction ...
 */
        resend_cnt++;
        goto resend;
    }
#endif
#ifndef DOTNET
/*
 * This code makes use of the knowledge that we won't have needed an overflow
 * buffer if the response was an authentication request
 */
    if (wdbp->except_flag & E2_HTTP_ERROR
     && (!strncmp(&wdbp->ret_msg.buf[9], "401", 3)  /* Web Server authenticate */
      ||!strncmp(&wdbp->ret_msg.buf[9], "407", 3))) /* Proxy authenticate */
    {
    char * user_var;
    char * passwd_var;
    char * domain_var;

        if (wdbp->ret_msg.buf[11] == '1')  /* Web Server authenticate */
        {
            user_var = "USER";
            passwd_var = "PASSWORD";
            domain_var = "DOMAIN";
        }
        else
        {
            user_var = "PROXY_USER";
            passwd_var = "PROXY_PASSWORD";
            domain_var = "PROXY_DOMAIN";
        }
        if ((p1 = bm_match(wdbp->authip,
        (unsigned char *) &(wdbp->ret_msg),
        ((unsigned char *) &(wdbp->ret_msg)) +
             (( mess_len < WORKSPACE) ? mess_len : WORKSPACE)))
                     != (unsigned char *) NULL)
        {
        int len;

/*
 * NTLM Authentication
 *
 * This won't work if the NTLM responses are chunked, because the input
 * message buffer will have been destroyed ...
 *
 * We should use the the wdbp->msg buffer instead; the space beyond
 * the occupied space, which is pretty large.
 */

            if (mess_len > WORKSPACE - 512)
                fprintf(stderr,
"(Client: %s) Authorisation required but Incoming (%d) too long to allow for NTLM decoding; increase WORKSPACE in webdrive.h\n",
                      wdbp->parser_con.pg.rope_seq, mess_len);
            else
            {
/*
 * Find out if it is a blank authorisation (in which case, inject Type 1)
 * or a Type 2 (in which case, inject a Type 3), then re-transmit
 */
                p1 += wdbp->authip->match_len;
/*
 * Use the space beyond the returned message in the buffer to assemble
 * the authorisation details
 */
                memcpy( ((unsigned char *) &(wdbp->ret_msg)) + mess_len, "NTLM ",5);
/*
 * We need to create a type 1 or type 3, and edit it in to the header
 */
                if (*p1 == '\r')
                {
                    if (machine == NULL
                    && ((machine = getvarval(&wdbp->parser_con.csmacro,"MACHINE"))
                         == NULL))
                        machine = "EDC-NS15";
                    if (domain == NULL
                    && ((domain = getvarval(&wdbp->parser_con.csmacro,domain_var))
                               == NULL))
                        domain = "RMJM";
                    len = ntlm_construct_type1(machine, domain,
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 5);
                }
                else
                {
                    if (user == NULL
                     && ((user = getvarval(&wdbp->parser_con.csmacro,user_var))
                             == NULL))
                        user = "perftest";
                    if (password == NULL
                     && ((password = getvarval(&wdbp->parser_con.csmacro,
                                               passwd_var))
                             == NULL))
                        password = "passw0rd1";
                    len = ntlm_construct_type3(machine, domain,
                               user, password, NULL, p1 + 1,
                   (((unsigned char *) strchr(p1 + 1, '\r')) - p1) + 1,
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 5);
                }
                if (len < 1)
                    fprintf(stderr, "(Client: %s) NTLM authorisation failed\n",
                      wdbp->parser_con.pg.rope_seq);
                else
                {
                    if (wdbp->debug_level > 1)
                    {
                        fprintf(stderr, "(Client: %s) NTLM string %.*s\n",
                          wdbp->parser_con.pg.rope_seq, len, 
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 5);
                        fflush(stderr);
                    }
                    msg->send_receive.message_len = edit_header(wdbp,
                           (struct bm_table *) ((*user_var == 'P') ?
                                    (unsigned long) wdbp->authpp :
                                     (unsigned long) wdbp->authop),
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len,
                          len + 5, msg->send_receive.msg->buf,
                          msg->send_receive.message_len);
                    if (bm_match(wdbp->authpc,
                        (unsigned char *) &(wdbp->ret_msg),
                        ((unsigned char *) &(wdbp->ret_msg)) +
                       (( mess_len < WORKSPACE) ? mess_len : WORKSPACE))
                            != (unsigned char *) NULL)
                    {
                        socket_cleanup(wdbp);
                        t3drive_connect(wdbp);
                    }
                    resend_cnt++;
                    goto resend;
                }
            }
        }
        else
        if ((p1 = bm_match(wdbp->authbp,
        (unsigned char *) &(wdbp->ret_msg),
        ((unsigned char *) &(wdbp->ret_msg)) +
             (( mess_len < WORKSPACE) ? mess_len : WORKSPACE)))
                     != (unsigned char *) NULL)
        {
        int len;
/*
 * Basic Authentication
 *
 * This won't work if the responses are chunked, because the input
 * message buffer will have been destroyed ...
 */
            if (mess_len > WORKSPACE - 512)
                fprintf(stderr,
"(Client: %s) Authorisation required but Incoming (%d) too long; increase WORKSPACE in webdrive.h\n",
                      wdbp->parser_con.pg.rope_seq, mess_len);
            else
            {
/*
 * Use the buffer beyond the returned data. 
 */
                memcpy( ((unsigned char *) &(wdbp->ret_msg)) + mess_len,
                       "Basic ",6);
                if (user == NULL
                 && ((user = getvarval(&wdbp->parser_con.csmacro,user_var)) == NULL))
                    user = "perftest";
                if (password == NULL
                 && ((password = getvarval(&wdbp->parser_con.csmacro,passwd_var))
                         == NULL))
                    password = "passw0rd1";
                len = basic_construct(user, password,
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 6);
            }
            if (len < 1)
                fprintf(stderr, "(Client: %s) Basic authorisation failed\n",
                      wdbp->parser_con.pg.rope_seq);
            else
            {
                if (wdbp->debug_level > 1)
                {
                    fprintf(stderr, "(Client: %s) Basic string %.*s\n",
                          wdbp->parser_con.pg.rope_seq, len, 
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len + 6);
                    fflush(stderr);
                }
                msg->send_receive.message_len = edit_header(wdbp,
                                     wdbp->authop,
                         ((unsigned char *) &(wdbp->ret_msg)) + mess_len,
                          len + 6, msg->send_receive.msg->buf,
                          msg->send_receive.message_len);
                resend_cnt++;
                goto resend;
            }
        }
    }
#ifdef TUNDRIVE
    else
    {
        tun_forward( (char *) &(wdbp->ret_msg),
                           (mess_len > WORKSPACE) ?
                                         WORKSPACE : mess_len, wdbp);
        return;
    }
#else
/*
 * Send to the mirror port as required
 */
    if (mirror_out > -1)
        mirror_send(mirror_out, mess_len, wdbp);
    if (wdbp->except_flag & E2_DISASTER_FOUND
#ifdef ORA9IAS_2
      || (bm_match(wdbp->erp, (unsigned char *) &(wdbp->ret_msg),
        ((unsigned char *) &(wdbp->ret_msg)) +
             (( mess_len < WORKSPACE) ? mess_len : WORKSPACE))
                     != (unsigned char *) NULL)
#endif
               )
    {    /* Seen a Disastrous Error; have to give up and start over */
#ifndef DONT_TRUST_RESYNC
    enum tok_id tok_id;
#endif

#ifdef ORA9IAS_2
        if ( !wdbp->proxy_port)
        {
            fprintf(stderr, "(Client: %s) In<==== Pragma: %d\n",
                                 wdbp->parser_con.pg.rope_seq,
                                 wdbp->pragma_seq);
            (void) weboutrec(stderr, (char *) &(wdbp->ret_msg),IN,
                                        mess_len, wdbp);
        }
#endif
        if (wdbp->proxy_port == 0)
            event_record_r("X", (struct event_con *) NULL,
                                &(wdbp->parser_con.pg));
                                               /* Note the message */
        alarm_restore(wdbp);
        socket_cleanup(wdbp);
#ifndef DONT_TRUST_RESYNC
        do_resync(wdbp);
#else
        wdbp->parser_con.pg.think_time = 1;
#endif
    }
#endif
#endif
    if ( msg->send_receive.message_len > WORKSPACE)
        free(wdbp->overflow_send); /* Free the overflow buffer */
    if ( mess_len > WORKSPACE && wdbp->overflow_receive != NULL)
    {
        free(wdbp->overflow_receive); /* Free the overflow buffer */
        wdbp->overflow_receive = NULL;
    }
    return;
}
