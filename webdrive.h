/************************************************************************
 * webdrive.h - Header for webdrive
 *
@(#) $Name$ $Id$
 */
#ifndef WEBDRIVE_H
#define WEBDRIVE_H
/*
 * scripttree.h includes all the system headers we need
 */
#ifdef TUNDRIVE
#define WORKSPACE 65536
#endif
#include "scripttree.h"
#include <zlib.h>
#include "csexe.h"
#ifdef MINGW32
#include "w32pthread.h"
#include "matchlib.h"
#endif
#ifndef HEADER_DH_H
#include <openssl/dh.h>
#endif
/*
 * Brotli header
 */
#include <decode.h>
/*****************************************************************
 * The data for processing the statement files
 */
#define MAX_SSL_SPEC 2
#ifndef PATHSIZE
#ifndef MAXPATHLEN
#define MAXPATHLEN 256
#endif
#define PATHSIZE MAXPATHLEN
#endif
#ifdef ORA9IAS_1
#define NEEDS_ORA_KEEPS
#endif
#ifdef ORA9IAS_2
#define NEEDS_ORA_KEEPS
#endif
/*
 * Functions that must be defined by the user of t3extlib.c etc.
 */
void scarper();
#ifdef ANCIENT
#ifdef PRODUCT
#define ERROR(fn, ln, ex, fmt, s1, s2) { \
        (void) fprintf(stderr, fmt, s1, s2); \
        (void) fputc('\n', stderr); \
        if (ex) \
                exit(ex); \
}
#else
#define ERROR(fn, ln, ex, fmt, s1, s2) { \
        (void) fprintf(stderr, "\"%s\" Line %d: ", fn, ln); \
        (void) fprintf(stderr, fmt, s1, s2); \
        (void) fputc('\n', stderr); \
        if (ex) \
                exit(ex); \
}
#endif /* !PRODUCT */
#endif

#define NEWARRAY(type, ptr, nel) \
        if ((ptr = (type *) calloc((unsigned) nel, sizeof(type))) == (type *) NULL) \
                ERROR(__FILE__, __LINE__, 1, "can't calloc %d bytes", (nel * sizeof(type)), (char *) NULL)

#define RENEW(type, ptr, nel) \
        if ((ptr = (type *) realloc((char *)ptr, (unsigned)(sizeof(type) * (nel)))) == (type *) NULL) \
                ERROR(__FILE__, __LINE__, 1, "can't realloc %d bytes", (nel * sizeof(type)), (char *) NULL)

#define NEW(type, ptr)  NEWARRAY(type, ptr, 1)

#define FREE(s)         (void) free((char *) s)

#define ZAP(type, s)    (FREE(s), s = (type *) NULL)

#ifndef MAX
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#endif /* !MAX */

#ifndef MIN
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif /* !MIN */

#define PREV(i, n)      (((i) - 1) % (n))
/* #define THIS(i, n)      ((i) % (n)) */
#define NEXT(i, n)      (((i) + 1) % (n))

/************************************************************
 * Macros to get ansi-standard source through various
 * non-ansi compilers. (15th October 2007. It is many years since we have had
 * to deal with one of those. GCC is now absolutely ubiquitous.)
 */
#ifndef ANSI_H
#define ANSI_H

#ifdef __STDC__
#define ANSIARGS(x)     x
#define VOID            void
#endif /* __STDC__ */

#ifdef __ZTC__
#define ANSIARGS(x)     x
#define VOID            void
#endif /* __ZTC__ */

#ifdef __MSC__
#define ANSIARGS(x)     x
#define VOID            void
#endif /* __MSC__ */

#ifndef ANSIARGS
#define ANSIARGS(x)     ()
#define VOID            char
#endif /* !ANSIARGS */
#endif /* !ANSI_H */

/********************************************************************
 * Communications Control Data
 */
#define MAXUSERNAME     32
#define MAXFILENAME     BUFSIZ

#ifndef BUFLEN
#define BUFLEN      2048
#endif
#define MAX_SCAN_SPECS 500
#ifdef TDSDRIVE
#define MAXLINKS 10
#else
#define MAXLINKS 540
#endif
#define MAXENDPOINTS 540
/* prototypes for library functions */
void    weblog          ANSIARGS((int argc, char **argv));
/*********************************************************************
 * Control Record Field Lengths
 */
#ifdef RAW
void hexvartobin ANSIARGS((struct {short int len; char arr[1];} * hexvar,
                           union all_records *ptr));
void bintohexvar ANSIARGS((union all_records * ptr,
                           struct {short int len; char arr[1];} * hexvar,
                           int len));
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
enum direction_id {IN, OUT, ORAIN, ORAOUT};
struct element_tracker {
    int alloc;
    int len;
    unsigned char * element;
};
struct mon_target {
    int in_use;
    int pipe_out;
    FILE * pipe_out_fp;
    int pipe_in;
    char * target;
    char * host;
    char * pid;
    char * port;
    char * sla;
    char * sample;
    char * dir;
    int out_fd;
    pid_t child_pid;
};
/*
 * Data on Set Up
 */
typedef struct _webdrive_base {
char *control_file; /* Also used for the name of the tunnel device */
int debug_level;
int verbosity;
struct link_det * cur_link;
int ep_cnt;
int msg_seq;
int sav_seq;
int sync_flag;
/*
 * Things that need to be recognised in the input packets and substituted back.
 */
int cookie_cnt;
int head_flag;
int scan_spec_internal;  /* This number of scan specs are handled atypically
                          * Examples would be the negative Pragmas with ORACLE
                          * WebForms
                          */
int scan_spec_used;      /* Encrypted Token Count */
SCAN_SPEC * scan_spec[MAX_SCAN_SPECS];
HASH_CON * ht;           /* Hash-table of named scan_specs */
int except_flag;         /* Output diagnostics regardless of verbosity.
                          * Originally an EJB Exception is present; 
                          * now whatever. */
int pragma_seq;          /* ORACLE POST message sequence */
/*
 * ORACLE Forms Scrambling
 */
int pragma_flag;         /* Indicates that this request is scrambled */
unsigned char * gday;    /* The first part of the handshake          */
unsigned char * f_enc_dec[2];   /* Encrypt/Decrypt control blocks    */
/*
 * These are used by WebLogic T3 Emulation.
 * sbp is also used by ORACLE (we don't use the two concurrently)
 */
char * remote_ref;
char * encrypted_token;
struct bm_table * ebp;
struct bm_table * rbp;
struct bm_table * sbp;
struct bm_table * ehp;
struct bm_table * authbp;  /* Basic auth marker */
struct bm_table * authip;  /* NTLM auth marker */
struct bm_table * authop;  /* NTLM www header */
struct bm_table * authpp;  /* NTLM proxy header */
struct bm_table * authpc;  /* NTLM proxy close */
struct bm_table * authsp;  /* Secure cookie marker */
struct bm_table * cookp;   /* Cookie header */
int proxy_connect_flag;    /* Prevent recursive entry to proxy_connect */
#ifdef NEEDS_ORA_KEEPS
struct bm_table * kap;     /* Used for the ORACLE Keep-alive stuff */
struct bm_table * prp;
struct bm_table * erp;
#endif
char * cookies[100];       /* New handles by tag value           */      
#ifdef USE_SSL
int ssl_spec_cnt;
SSL_SPEC ssl_specs[MAX_SSL_SPEC];
#endif
/*
 * Information about the sockets and sessions.
 */
#ifdef TDSDRIVE
SQLLINK link_det[MAXLINKS];
TDSPARAMINFO *params;
char username[32];
#else
LINK link_det[MAXLINKS];
                             /* list of links in the input file */
END_POINT end_point_det[MAXENDPOINTS];
                             /* list of links in the input file */
END_POINT * ep_cur_ptr;
END_POINT * ep_max_ptr;
#endif
int rec_cnt;          /* Script line counter */
int child_cnt;        /* Child count (used when emulating a server) */
int bind_port;
/*
 * Data used by the script parser
 */
PARSER_CON parser_con;
/*
 * Information used when processing a message. The webdrive script scripts
 * the SENDS. The receives are handled as they arise as responses. The
 * contents or position of a receive in the original capture has no impact
 * on anything (in contrast to ipdrive, where both SENDS and RECEIVES are
 * scripted).
 */
union all_records in_buf;
union all_records ret_msg;
union all_records msg;
unsigned char * overflow_receive;   /* Used to pass over-sized data */
unsigned char * overflow_send;      /* Used to pass over-sized data */
/*
 * The data tracking multiple clients
 */
volatile pthread_t own_thread_id;
volatile int alert_flag;            /* Used to communicate with an interrupted thread */
void (*progress_client)(); /* Function to be executed by be_a_thread()       */
enum tok_id (*webread)();  /* Function providing data for a thread           */
void (*do_send_receive)(); /* Function to handle actual communications       */
/*
 * The data tracking multiple clients
 */
struct _webdrive_base * root_wdbp;
int client_cnt;
volatile int active_cnt;
volatile int thread_cnt;
struct _webdrive_base * client_array;
pthread_mutex_t idle_thread_mutex;
struct circbuf * idle_threads;
pthread_mutex_t go_time_mutex;
struct circbuf * go_times;
pthread_mutex_t encrypt_mutex;
/*
 * Single-step support
 */
int mirror_port;
int proxy_port; /* Also port to listen on when being server tunnel end */
int mirror_fd;  /* Also used for the Tunnel device */
int mirror_bin; /* Flag that mirror output should be treated like binary */ 
struct sockaddr_in mirror_sock;
/*
 * Auto-recognition of substitute variable bounds
 */
char * sep_exp;
int sep_exp_len;
/*
 * Macro support
 */
volatile int go_away;                    /* Exit signal */
int depth;                       /* Stack overflow protection            */
pthread_mutex_t script_mutex;
struct script_control sc;    /* For script capture and editing */
struct {
    char * per_fun;
    double first_run;
    double last_run;
    double periodicity;
    int call_cnt;
} call_filter;
/*
 * Stuff to help with troubleshooting
 */
char narrative[128];
double last_activity;
/*
 * Web Session Credentials etc.
 */
char username[64];
char passwd[64];
char session[64]; /* We actually only need 49 at present */
char basic_auth[130];
struct bm_table * boundaryp;
struct bm_table * traverse_u;
struct bm_table * traverse_w;
struct mon_target mon_target;
/*
 * Proxy and tunnel driving odds and ends
 */
char * out_fifo;
char * in_fifo;
char * server_url;
char * proxy_url;
char * tunnel_ip;
int ssl_proxy_flag;
int not_ssl_flag;     /* Is the target not SSL? */
END_POINT proxy_ep;
END_POINT server_ep;
LINK proxy_link;
int non_priv_uid;     /* Used to drop privileges */
struct e2malloc_base * te2mbp;
/*
 * Inter-thread communications
 */
struct pipe_buf * pbp;
} WEBDRIVE_BASE;
extern WEBDRIVE_BASE webdrive_base;
/*
 * Structure with idle thread details
 */
struct idle_thread {
    pthread_t thread_id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile WEBDRIVE_BASE * wdbp;
};
/*
 * Structure with timeout details
 */
struct go_time {
    WEBDRIVE_BASE * wdbp;   /* The client */
    long go_time;           /* When to go */
};
struct heading {
    struct element_tracker label;
    struct element_tracker value;
};
/*
 * At the moment, most of these values only relate to responses
 */
struct http_req_response {
    int element_cnt;
    int read_cnt;  /* Flag value indicating what further processing is needed */
    int scan_flag; /* Flag indicating whether the data should be scanned */
    int gzip_flag; /* Flag indicating whether the data is compressed */
    char zreserve[10];
    int zreserve_len;
    int status;    /* Status = -1 flags a Request                    */
    int status_ind;
    int verb_ind;
    int scheme_ind;
    int path_ind;
    int authority_ind;
    struct element_tracker head_start; 
    struct heading headings[64];
    struct element_tracker from_wire;
    struct element_tracker decoded;
    int declen;
    z_stream strm;
    BrotliState bs;
};
/*
 * Structure for managing pipe fragments. Uses a circular buffer of
 * structures including non-descript pointers.
 * -  We have reliable working unsynchronised code in e2net.c. It is
 *    used as is in threadserv.c to manage the idle threads and
 *    timeout list. So why re-implement it?
 * -  The synchronisation within threadserv.c is very poorly
 *    encapsulated.
 * -  We don't want to allocate both the synchronisation gubbins
 *    and the things separately.
 */
struct pipe_mess {
    pthread_mutex_t mutex;     /* Protect updates to elements */
    pthread_mutex_t condmutex; /* For assured delivery */
    pthread_cond_t condcond;
    int clntcnt;               /* Count of recipients */
    unsigned int msglen;
    unsigned char * msgbuf;
    void (*get_rid_cb)();      /* Function to dispose of msgbuf */
};
struct pipe_buf {
    volatile int buf_cnt;
    pthread_mutex_t mutex;     /* Protect updates to elements */
    pthread_mutex_t condmutex; /* In case it fills up delivery */
    pthread_cond_t condcond;
    volatile struct pipe_mess * head;
    volatile struct pipe_mess * tail;
    struct pipe_mess * base;
    struct pipe_mess * top;
};
struct pipe_buf * pipe_buf_cre ANSIARGS((int nelems));
void pipe_buf_des ANSIARGS((struct pipe_buf * buf));
void drain_pipe_buf ANSIARGS((struct pipe_buf * buf));
int pipe_buf_add ANSIARGS(( struct pipe_buf * buf, int clntcnt, unsigned int msglen, unsigned char* msgbuf, void (*get_rid_cb)(), int wait_flag));
int pipe_buf_take ANSIARGS((struct pipe_buf * buf, unsigned int * len, unsigned char  ** msg, void (**get_rid_cb)()));
/***********************************************************************
 * Getopt support
 */
extern int optind;           /* Current Argument counter.      */
extern char *optarg;         /* Current Argument pointer.      */
extern int opterr;           /* getopt() err print flag.       */

#define STRCPY(x,y) {strncpy((x),(y),sizeof(x)-1);*((x)+sizeof(x)-1)='\0';}
#define DEFAULT_SEP_EXP ",?&; \r\n"
int webinrec();
int weboutrec();
enum tok_id webread();
int proxy_read();
char * forms60_handle();
void alarm_preempt();    /* Put in a read timeout, in place of whatever is
                          * currently in the alarm clock
                          */
void alarm_restore();    /* Restore the previous clock value */
void progress_client();  /* Process requests whilst things are still alive */
void proxy_client();  /* Process requests whilst things are still alive */
void mirror_client();  /* Process requests whilst things are still alive */
void schedule_clients(); /* Process requests whilst things are still alive */
int find_a_slot(); /* Allocate a processing slot */
void read_timeout();     /* Support interruptible reads                    */
void catch_sigio();      /* Support interruptible reads                    */
enum tok_id get_tok();
void scan_incoming_cookie();
void scan_incoming_error();
/*
 * Possible flag values in except_flag
 */
#define E2_HTTP_ERROR 1
#define E2_SERVER_ERROR 2
#define E2_ERROR_FOUND 4
#define E2_GOOD_FOUND 8
#define E2_DISASTER_FOUND 16
void scan_incoming_body();
void dump_scan_spec();
void preserve_script_cookies();
void clear_cookie_cache();
void cache_cookie();
int adjust_content_length();
enum tok_id recognise_scan_spec();
void recognise_ssl_spec();
SCAN_SPEC * new_scan_spec();
void set_scan_key();
void reset_progressive();
void update_scan_spec();
SCAN_SPEC * find_scan_spec();
unsigned hash_func();
int comp_func();
void do_end_point();
void do_ssl_spec();
int do_delay();
int do_pause();
void do_start_timer();
int do_take_time();
#ifdef TDSDRIVE
SQLLINK * link_find();
#else
END_POINT * ep_find();
LINK * link_find();
#endif
void do_send_receive();
void do_send_receive_async();
void do_link();
void do_close();
void t3drive_connect(); /* Connect to remote socket */
void t3drive_listen();  /* Set up the socket that listens for link connect
                           requests */
void link_clear();
void socket_cleanup();
void siggoaway();       /* catch terminate signal */
void proc_args();       /* process arguments */
extern WEBDRIVE_BASE webdrive_base;      /* Global configuration information */
extern char * bind_host;
void dump_args();
int ora_web_match();
void push_file();
void add_time();
int pop_file();
void update_target();
void goto_target();
void do_resync();
int http_head_delineate();
int check_allowed_blocked();
int apply_compression();
void log_dotnet_rpc();
void snap_script();
int tun_create();
int ini_server();
int tun_forward();
void watchdog_tunnel();
void tun_client();
int tunnel_read();
void sock_ready();
int url_to_ep();
void tun_write();
void activate_scan_spec();
void suspend_scan_spec();
void remove_scan_spec();
int check_recoverable();
int toggle_ssl();
void sort_out_send();
void ssl_service();
int ssl_serv_read();
char * env_value();
void despatch_a_thread();
void fish_things();
DH *group2_dh_callback ANSIARGS((SSL *s, int is_export, int keylength));
unsigned char * ready_code();
void block_enc_dec();
int wait_for_incoming();
LINK * find_script_link();
void redirect_https();
void ack_mim();
#ifndef MINGW32
long int GetCurrentThreadId();
#ifndef SOLAR
#define fifo_connect(x,y) open(x,O_WRONLY|O_NONBLOCK)
#define fifo_accept(x,y) open(x,O_RDONLY)
#endif
#endif
#endif
