/*
 * rendscript.c - Render a script using a browser
 *
 * Read in a 'bothways' (or not) script file, and instead of processing it
 * turn it in to JSON.
 *
 * Originally I imagined putting it in a tree. However, the only reason I
 * can think of for doing this is to support rendition of answer data in the
 * browser, when the browser asks for supporting data (e.g. a style sheet or
 * Javascript file) when the answer is actually a web page; we might find the
 * answer and return it from the structure.
 *
 * However, this is potentially tricky; what do we hash to make sure we return
 * the right thing?
 *
 * It would be much quicker to debug it if we were able to debug this stuff
 * in the browser; we can immediately see what we are doing.
 *
 * Allow bits to be updated by replacing elements of the tree? No, I think not.
 * The script editor is going to be Dynamic HTML, but not AJAX; no recovery
 * option, though we will allow undo and redo?
 * ***************************************************************************
 * The editing works as follows:
 * -   This program generates a JSON representation of the script
 * -   The JSON is uploaded into the browser
 * -   Dynamic HTML is used to allow editing of the script elements
 * -   On save, a new script is reconstituted from the JSON
 ****************************************************************************
 * This hasn't kept up with the latest driver directive tweaks, and has
 * horrible and unnecessary dependencies on the thread handling. It needs to
 * be:
 * -   Hooked in to ungz, which reads the script in to a tree.
 * -   Changed in conjunction with visualise.js to support the integration of
 *     script execution and editing
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
static enum tok_id recognise_answer();
static enum tok_id recognise_message();
static void json_recognise_label();
static void json_recognise_include();
static void json_recognise_goto();
static enum tok_id json_recognise_scan_spec();
/*
 * Things that nothing should call; satisfy link references
 */
int cscalc() { return 0;}
int http_read() { return 0;}
int smart_read() { return 0;}
int smart_write() { return 0;}
char * forms60_handle() { return NULL;}
void block_enc_dec() { return;}
void socket_cleanup() { return;}
/******************************************************************************
 * Things that tmain.c will call
 */
void init_from_environment()
{
    tailor_threads();                     /* Initialise thread attributes */
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
    webdrive_base.progress_client = progress_client;
    webdrive_base.msg_seq = 1;                /* request sequencer         */
    webdrive_base.client_cnt = 1;             /* Client count              */
    webdrive_base.parser_con.pg.seqX = 0;                /* timestamp sequencer       */

    while ( ( c = getopt ( argc, argv, "hd:v:m:s:" ) ) != EOF )
    {
        switch ( c )
        {
        case 'h' :
            (void) fputs("jsonify: Convert E2 Systems Web Driver Scripts to JSON\n\
Options:\n\
 -h prints this message on stderr\n\
 -v sets verbosity level\n\
 -m says how many scripts we are going to handle\n\
 -d set the debug level (between 0 and 4)\n\
Arguments: Output File Seed, Input File(s)\n", stderr);
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
    if ((argc - optind) < (1 + webdrive_base.client_cnt) )
    {
        fprintf(stderr,
            "Insufficient Arguments Supplied; try -h\nSeen: %d Expected: %d\n",
         argc, (1 + webdrive_base.client_cnt + optind));
             dump_args(argc, argv);
#ifdef MINGW32
        WSACleanup();
#endif
        exit(1);
    } 
    if (webdrive_base.verbosity > 1)
        dump_args(argc, argv);
    webdrive_base.parser_con.pg.logfile=argv[optind++];
    webdrive_base.parser_con.pg.fdriver_seq="1"; /* Details needed by event   */
    webdrive_base.parser_con.pg.bundle_seq="1";  /* recording                 */
    webdrive_base.parser_con.pg.rope_seq="1"; 
    rope = atoi(webdrive_base.parser_con.pg.rope_seq);
    webdrive_base.ep_cur_ptr = webdrive_base.end_point_det,
    webdrive_base.ep_max_ptr = &(webdrive_base.end_point_det[MAXENDPOINTS-1]);
    webdrive_base.scan_spec_internal = 0;
    webdrive_base.sbp = bm_casecompile("Content-Length: ");
    webdrive_base.ehp = bm_compile("\r\n\r\n");
/*
 * Set up thread control. We are multi-thraded for performance reasons ...
 */
    webdrive_base.root_wdbp = &webdrive_base;
    webdrive_base.idle_threads = circbuf_cre(webdrive_base.client_cnt + 3,NULL);
    pthread_mutex_init(&(webdrive_base.idle_thread_mutex), NULL);
    webdrive_base.go_times = circbuf_cre(webdrive_base.client_cnt + 3, NULL);
    pthread_mutex_init(&(webdrive_base.go_time_mutex), NULL);
    pthread_mutex_init(&(webdrive_base.encrypt_mutex), NULL);
    path_stagger = 0;             /* Default interval */
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
 * Set up the hash table for scan specifications
 */
        wdbp->ht = hash(MAX_SCAN_SPECS, hash_func, comp_func);
/*
 * Initialise the end point management
 */
        wdbp->ep_cur_ptr = wdbp->end_point_det,
        wdbp->ep_max_ptr = &(wdbp->end_point_det[MAXENDPOINTS-1]);
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
        rope++;
    }
    webdrive_base.own_thread_id = pthread_self();
    if (webdrive_base.debug_level > 1)
    {
        (void) fprintf(stderr,"proc_args()\n");
        (void) fflush(stderr);
        dump_args(argc,argv);
    }
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
 * BEWARE: Destructive of wdbp->parser_con.tbuf.
 */
static void js_handle(in, out, len, wdbp)
char * in;
char * out;
int len;
WEBDRIVE_BASE * wdbp;
{
char * p;
char * top = in + len;

    for (p = bin_handle_no_uni_cr(NULL, in,top,0);
            in < top;
                 p = bin_handle_no_uni_cr(NULL, in,top,0))
    {
        if (p > in)
        {
            hexin_r(in, (p - in), wdbp->parser_con.tbuf, wdbp->parser_con.tbuf + WORKSPACE);
            *out++ = '\'';
            js_stuff_cnt(wdbp->parser_con.tbuf, out, 2*(p - in));
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
    return;
}
#ifdef USE_SSL
/***********************************************************************
 * An SSL Spec is the stuff we need for a Session. I assume that we will
 * only need one session per context.
 ***********************************************************************
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

    a->end_point.end_point_id = wdbp->ep_cnt++;
    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
               a->end_point.address,
               &(a->end_point.address[sizeof(a->end_point.address) - 1]));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few end point fields", 
             &(wdbp->parser_con));
    a->end_point.cap_port_id = atoi(ret_buf);
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
    memset((char *) &(a->link.connect_sock),0,sizeof(a->link.connect_sock));
    memset((char *) &(a->link.in_det),0,sizeof(a->link.in_det));
    memset((char *) &(a->link.out_det),0,sizeof(a->link.out_det));
    return;
}
/*
 * Create a JSON representation of a script.
 */
void generate_json(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
enum tok_id tok_id;
int i;
    while ((tok_id = get_tok( wdbp->debug_level, &wdbp->parser_con)) != E2EOF)
    {
        a->end_point.record_type = tok_id;
                                      /* It's in the same position in every
                                         record */
        switch(tok_id)
        {
        case E2COMMENT:
            js_stuff_str(wdbp->parser_con.tlook, wdbp->parser_con.tbuf);
            fprintf(wdbp->parser_con.pg.fo,
                "%s{ id : %d, type : \"COMMENT\", data : \"%s\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                         wdbp->parser_con.tbuf);
            break;
        case E2RESET:
/*
 * Reset the saved cookies, or whatever passes for session state.
 */          
            fprintf(wdbp->parser_con.pg.fo,
                "%s{ id : %d, type : \"RESET\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10);
            break;
        case E2SCANSPECS:
        {
        unsigned char * got_to = got_to = wdbp->parser_con.tlook;

            fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"SCAN_SPEC_BUNDLE\", scan_specs : [\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10);
/*
 * Now look for Cookie, Error, Offset or Replace data value sets
 */
            for (i = 0;
                json_recognise_scan_spec(a, wdbp, &got_to, i) == SCAN_SPEC_TYPE;
                 i++);
    fputs(" ] }\n", wdbp->parser_con.pg.fo);
            break;
        }
/*
 * Perform a conditional branch
 */          
        case E2GOTO:
            json_recognise_goto(NULL, wdbp);
            break;
/*
 * Declare a branch target
 */          
        case E2LABEL:
            json_recognise_label(NULL, wdbp);
            break;
/*
 * Open an include file.
 */          
        case E2INCLUDE:
            json_recognise_include(NULL, wdbp);
            break;
/*
 * The message types below this point have populated 'a'
 */
        case  END_POINT_TYPE:
            recognise_end_point(a, wdbp);
            fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"END_POINT\", capture_host : \"%s\", capture_port : \"%d\",\n\
map_host : \"%s\", map_port : \"%d\", connect_or_listen : \"%c\", ssl_spec_ref : \"%s\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                a->end_point.address,
                a->end_point.cap_port_id,
                a->end_point.host,
                a->end_point.port_id,
                a->end_point.con_flag,
                a->end_point.ssl_spec_ref);
            do_end_point(a, wdbp);
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
            fprintf(wdbp->parser_con.pg.fo, "%s{ id : %d, type : \"SSL_SPEC\",",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10);
            js_stuff_str(a->ssl_spec.ssl_spec_ref, wdbp->parser_con.tbuf);
            fprintf(wdbp->parser_con.pg.fo, "ssl_spec_ref : \"%s\",", wdbp->parser_con.tbuf);
            js_stuff_str(a->ssl_spec.key_file, wdbp->parser_con.tbuf);
            fprintf(wdbp->parser_con.pg.fo, " key_file : \"%s\",", wdbp->parser_con.tbuf);
            js_stuff_str(a->ssl_spec.passwd, wdbp->parser_con.tbuf);
            fprintf(wdbp->parser_con.pg.fo, " password : \"%s\" }\n", wdbp->parser_con.tbuf);
            break;
#endif
        case  LINK_TYPE:
        case  CLOSE_TYPE:
            recognise_link(a, wdbp);
            if (tok_id == LINK_TYPE)
                do_link(a, wdbp);
            else
                do_close(a, wdbp);
            break;
        case  START_TIMER_TYPE:
            recognise_start_timer(a, wdbp);
            fprintf(wdbp->parser_con.pg.fo,
   "%s{ id : %d, type : \"START_TIMER\", event : \"%s\", desc : \"%s\" }\n",
                        (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                        a->start_timer.timer_id,
                        a->start_timer.timer_description);
            break;
        case  TAKE_TIME_TYPE:
            recognise_take_time(a, wdbp);
            fprintf(wdbp->parser_con.pg.fo,
   "%s{ id : %d, type : \"TAKE_TIME\", event : \"%s\" }\n",
                        (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                        a->start_timer.timer_id);
            break;
        case  DELAY_TYPE:
            recognise_delay(a, wdbp);
            fprintf(wdbp->parser_con.pg.fo,
   "%s{ id : %d, type : \"DELAY\", wait : \"%g\" }\n",
                        (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                        a->delay.delta);
            break;
        case  PAUSE_TYPE:
            recognise_delay(a, wdbp);
            fprintf(wdbp->parser_con.pg.fo,
   "%s{ id : %d, type : \"PAUSE\", wait : \"%g\" }\n",
                        (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
                        a->delay.delta);
            break;
        default:
            fprintf(stderr,
         "(Client:%s) Problem Token: %d Value: %s Look: %s sav_tlook: %s\n",
                               wdbp->parser_con.pg.rope_seq, (int) tok_id, wdbp->parser_con.tbuf,
                                      wdbp->parser_con.tlook, wdbp->parser_con.sav_tlook);
            syntax_err(__FILE__,__LINE__,"Invalid script format",
             &(wdbp->parser_con));
        }
        wdbp->parser_con.look_status = CLEAR;
        wdbp->rec_cnt++;
    }
    return;
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
/*
 * Use the file variable provided for timings to do output
 */
    if ((wdbp->parser_con.pg.fo = fopen(wdbp->parser_con.pg.logfile, "wb")) == (FILE *) NULL)
    {                              /*  Open the JSON output file  */
        fprintf(stderr,"progress_client:Attempting to open JSON file %s\n",
                     wdbp->parser_con.pg.logfile);
        perror("Cannot JSON output file");
        return;
    }
    fputs("[\n", wdbp->parser_con.pg.fo);
    generate_json(&(wdbp->in_buf), wdbp);
    fputs("]\n", wdbp->parser_con.pg.fo);
    fclose(wdbp->parser_con.pg.fo);
    wdbp->parser_con.pg.fo = (FILE *) NULL;
    return;
}
/***************************************************************************
 * Function to recognise a link definition
 */
void do_link(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
    wdbp->cur_link = link_find(a->link.from_ep, a->link.to_ep, wdbp);
/*
 * See if we have already encountered this link. If we have not done
 * so, initialise it.
 */
    if (wdbp->cur_link->link_id != LINK_TYPE)
        *(wdbp->cur_link) = a->link; 
    fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"OPEN\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
         wdbp->cur_link->from_ep->host,
         wdbp->cur_link->from_ep->port_id,
         wdbp->cur_link->to_ep->host,
         wdbp->cur_link->to_ep->port_id);
    return;
}
/***************************************************************************
 * Function to recognise a socket close definition
 */
void do_close(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
    wdbp->cur_link = link_find(a->link.from_ep, a->link.to_ep, wdbp);
    fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"CLOSE\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
         wdbp->cur_link->from_ep->host,
         wdbp->cur_link->from_ep->port_id,
         wdbp->cur_link->to_ep->host,
         wdbp->cur_link->to_ep->port_id);
    wdbp->cur_link->link_id = CLOSE_TYPE;
    wdbp->cur_link->pair_seq = 0;
    return;
}
/***************************************************************************
 * Function to process a Communications End Point declaration
 */
void do_end_point(a, wdbp)
union all_records * a;
WEBDRIVE_BASE * wdbp;
{
int ep;
/*
 * Add the end point to the array
 */
    if ((ep = a->end_point.end_point_id) < 0 || ep > MAXENDPOINTS)
                       /* Ignore if out of range */
        return;
    wdbp->end_point_det[ep] = a->end_point;
    return;
}
/*****************************************************************************
 * Take an incoming message in ASCII and make it binary. 
 *
 * Does not assumes only one connection active at a time, but does assume only
 * one message.
 */
static enum tok_id recognise_message(a, wdbp)
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
int i;

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
            syntax_err(__FILE__, __LINE__, "Message for hitherto unknown link",
             &(wdbp->parser_con));
    }
    if ( (wdbp->cur_link->t3_flag == 1)
     ||  wdbp->cur_link->from_ep->proto_flag
     ||  wdbp->cur_link->to_ep->proto_flag)
        in = ORAIN;
    else
        in = IN;
    fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"MESSAGE\", from_host : \"%s\", from_port : \"%d\",\n\
to_host : \"%s\", to_port : \"%d\", scan_specs : [\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
     wdbp->cur_link->from_ep->host,
     wdbp->cur_link->from_ep->port_id,
     wdbp->cur_link->to_ep->host,
     wdbp->cur_link->to_ep->port_id);
/*
 * Now look for Cookie, Error, Offset or Replace data value sets
 */
    for (i = 0;
             json_recognise_scan_spec(a, wdbp, &got_to, i) == SCAN_SPEC_TYPE;
                 i++);
    fputs(" ], data : \"", wdbp->parser_con.pg.fo);
/*
 * Now read the data to be sent.
 */
    if ((mess_len = webinrec((long int) wdbp->parser_con.pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
        syntax_err(__FILE__,__LINE__, "Invalid format web record",
             &(wdbp->parser_con));
    js_handle( &(wdbp->msg.buf[0]), wdbp->parser_con.tlook, mess_len, wdbp);
    fputs(wdbp->parser_con.tlook, wdbp->parser_con.pg.fo);
    fputs("\" }\n", wdbp->parser_con.pg.fo);
    a->send_receive.record_type = SEND_RECEIVE_TYPE;
    a->send_receive.msg = &(wdbp->msg);
    a->send_receive.message_len = mess_len;
    wdbp->parser_con.look_status = CLEAR;
    return SEND_RECEIVE_TYPE;
}
static enum tok_id recognise_answer(a, wdbp)
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
int i;

    (void) nextasc_r(wdbp->parser_con.tlook, ';', '\\', &got_to,
                      &address[0],&address[sizeof(address) - 1]);
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few answer fields",
             &(wdbp->parser_con));
    port_id = atoi(ret_buf);
    if ((from_ep = ep_find(address,port_id, wdbp)) ==
        (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ';', '\\', &got_to, &address[0],
                      &address[sizeof(address) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few answer fields",
             &(wdbp->parser_con));
    if (nextasc_r((char *) NULL, ':', '\\', &got_to,
                  ret_buf, &ret_buf[sizeof(ret_buf) - 1]) == NULL)
        syntax_err(__FILE__,__LINE__,"Too few answer fields",
             &(wdbp->parser_con));
    port_id = atoi( ret_buf);
    if ((to_ep = ep_find(address,port_id, wdbp)) == (END_POINT *) NULL)
        syntax_err(__FILE__,__LINE__,"Missing End Point",
             &(wdbp->parser_con));

    if (wdbp->cur_link->from_ep != from_ep
      || wdbp->cur_link->to_ep != to_ep)
    {
        wdbp->cur_link = link_find(from_ep, to_ep, wdbp);
        if (wdbp->cur_link->from_ep != to_ep
          || wdbp->cur_link->to_ep != from_ep)
            syntax_err(__FILE__, __LINE__, "Answer on hitherto unknown link",
             &(wdbp->parser_con));
    }
    if ( (wdbp->cur_link->t3_flag == 1)
     ||  wdbp->cur_link->from_ep->proto_flag
     ||  wdbp->cur_link->to_ep->proto_flag)
        in = ORAIN;
    else
        in = IN;
    fprintf(wdbp->parser_con.pg.fo,
"%s{ id : %d, type : \"ANSWER\", to_host : \"%s\", to_port : \"%d\",\n\
from_host : \"%s\", from_port : \"%d\", data : \"",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
     wdbp->cur_link->from_ep->host,
     wdbp->cur_link->from_ep->port_id,
     wdbp->cur_link->to_ep->host,
     wdbp->cur_link->to_ep->port_id);
/*
 * Now read the data that was received.
 */
    if ((mess_len = webinrec((long int) wdbp->parser_con.pg.cur_in_file,
                              &(wdbp->msg.buf[0]),in, wdbp)) < 1)
        syntax_err(__FILE__,__LINE__, "Invalid format web record",
             &(wdbp->parser_con));
    js_handle( &(wdbp->msg.buf[0]), wdbp->parser_con.tlook, mess_len, wdbp);
    fputs(wdbp->parser_con.tlook, wdbp->parser_con.pg.fo);
    fputs("\" }\n", wdbp->parser_con.pg.fo);
    a->send_receive.record_type = SEND_RECEIVE_TYPE;
    a->send_receive.msg = &(wdbp->msg);
    a->send_receive.message_len = mess_len;
    wdbp->parser_con.look_status = CLEAR;
    return SEND_RECEIVE_TYPE;
}
/*
 * Label (\L) records are:
 *     goto target (string, case sensitive)
 */
static void json_recognise_label(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char * x;

    if ((x = strchr(wdbp->parser_con.tlook,'\\')) != NULL)
        *x = '\0';
    js_stuff_str(wdbp->parser_con.tlook, wdbp->parser_con.tbuf);
    fprintf(wdbp->parser_con.pg.fo,
        "%s{ id : %d, type : \"LABEL\", label : \"%s\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
             wdbp->parser_con.tbuf);
    return;
}
/*
 * Include (\I) records are:
 *     file name (string, local file system semantics)
 */
static void json_recognise_include(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
char * x;

    if ((x = strchr(wdbp->parser_con.tlook,'\\')) != NULL)
        *x = '\0';
    js_stuff_str(wdbp->parser_con.tlook, wdbp->parser_con.tbuf);
    fprintf(wdbp->parser_con.pg.fo,
        "%s{ id : %d, type : \"INCLUDE\", file : \"%s\" }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
             wdbp->parser_con.tbuf);
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
static void json_recognise_goto(a, wdbp)
union all_records *a;
WEBDRIVE_BASE * wdbp;
{
unsigned char * got_to = (unsigned char *) NULL;
unsigned char label[ADDRESS_LEN + 1];
char * x;

    (void) nextasc_r(wdbp->parser_con.tlook, ':', '\\', &got_to,
                    &(label[0]),
              &(label[ADDRESS_LEN]));
    got_to++;
    if ((x = strchr(got_to, '\\')) != NULL)
        *x = '\0';
    js_stuff_str(got_to, wdbp->parser_con.tbuf);
    fprintf(wdbp->parser_con.pg.fo,
        "%s{ id : %d, type : \"GOTO\", label : \"%s\", expr : \"%s\"  }\n",
                (wdbp->rec_cnt == 0 ) ? "" : ",", wdbp->rec_cnt * 10,
             label, wdbp->parser_con.tbuf);
    return;
}
/*
 * Recognise a pattern spec (a counted list of search strings).
 */
static int recognise_pattern_spec(wdbp, got_to, psp, hex_flag)
WEBDRIVE_BASE * wdbp;
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
        fprintf(stderr,"(Client:%s) %s:%d PATTERN_SPEC Too Short\n",
                        wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
        return 0;
    }
    if ((psp->cnt = atoi(val_buf)) < 1 || psp->cnt > MAX_PATTERN_SPECS)
    {
        fprintf(stderr,"(Client:%s) Invalid Number of PATTERN_SPECS:%d\n",
                        wdbp->parser_con.pg.rope_seq, psp->cnt);
        return 0;
    }
    for (i = 0; i < psp->cnt; i++)
    {
        if (nextasc_r((char *) NULL, ':', '\\', got_to,
            &val_buf[0], &val_buf[sizeof(val_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d PATTERN_SPEC Too Short\n",
                                        wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
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
void pattern_spec_to_json(psp, name, wdbp)
PATTERN_SPEC * psp;
char * name;
WEBDRIVE_BASE * wdbp;
{
int i;

    fprintf(wdbp->parser_con.pg.fo, 
          ", %s : {  pattern_cnt : %d, patterns : [ ", name, psp->cnt);
    for (i = 0; i < psp->cnt;i++)
    {
        js_stuff_cnt( psp->bmp[i]->match_word, wdbp->parser_con.tbuf,
                      psp->bmp[i]->match_len);
        fprintf(wdbp->parser_con.pg.fo, "%s{ match : \"%s\" }\n", (i == 0) ?"":",",
                wdbp->parser_con.tbuf);
    }
    fputs( " ] }\n", wdbp->parser_con.pg.fo);
    return;
}
/****************************************************************************
 * Convert scan spec to JSON
 */
void scan_spec_to_json(ssp, seq, wdbp)
SCAN_SPEC * ssp;
int seq;
WEBDRIVE_BASE * wdbp;
{
    fprintf(wdbp->parser_con.pg.fo, "%s{ name : \"%s\", type : \"%c\"\n",
              (seq == 0) ? "" : ",", ssp->scan_key, ssp->c_e_r_o_flag[0]);
    pattern_spec_to_json(&(ssp->ebp), "ins",  wdbp);
    if (ssp->c_e_r_o_flag[0] != 'E'
      && ssp->c_e_r_o_flag[0] != 'D'
      && ssp->c_e_r_o_flag[0] != 'A'
      && ssp->c_e_r_o_flag[0] != 'B'
      && ssp->c_e_r_o_flag[0] != 'W'
      && ssp->c_e_r_o_flag[0] != 'G'
      && ssp->c_e_r_o_flag[0] != 'P')
    {
        fprintf(wdbp->parser_con.pg.fo,", in_offset : %d, in_length : %d\n",
                  ssp->i_offset, ssp->i_len);
        pattern_spec_to_json(&(ssp->rbp), "out", wdbp);
        fprintf(wdbp->parser_con.pg.fo,", out_offset : %d, out_length : %d\n",
                  ssp->o_offset, ssp->o_len);
    }
    fputs("}\n", wdbp->parser_con.pg.fo);
    return;
}
/**************************************************************************
 * Recognise the scan specifications tacked on the end of the DB directives
 **************************************************************************
 * The scan specifications are:
 * - C; something to be seen in a cookie
 * - O; a ('find something, substitute somewhere' pair)
 * - U; as O, but the substitution needs to be URL-escaped
 * - H; as O, but the values are in Hexadecimal
 * - S; suspend a substitution
 * - Y; yes, activate a substitution
 * - R; remove a substitution
 * - E; marker for an error
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
static enum tok_id json_recognise_scan_spec(a, wdbp, got_to, seq)
union all_records *a;
WEBDRIVE_BASE * wdbp;
unsigned char ** got_to;
int seq;
{
int i;
int j;
SCAN_SPEC validate_spec;
SCAN_SPEC *sp;
char num_buf[32];

    if (wdbp->scan_spec_used >= MAX_SCAN_SPECS)
    {
        fprintf(stderr,"(Client:%s) Too Many Active SCAN_SPECS:%d\n",
                        wdbp->parser_con.pg.rope_seq, wdbp->scan_spec_used);
        return E2EOF;
    }
    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.c_e_r_o_flag[0],
        &validate_spec.c_e_r_o_flag[sizeof(validate_spec.c_e_r_o_flag) - 1])
                     == NULL)
        return E2EOF;
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
        fprintf(stderr,"(Client:%s) SCAN_SPEC Error :%s:\n", wdbp->parser_con.pg.rope_seq,
                        validate_spec.c_e_r_o_flag);
        syntax_err(__FILE__,__LINE__,
"Flag not (A)llow, (B)lock, (C)ookie (Offset), (D)isaster, (E)rror, (F)reeze, (G)ood, (H)exadecimal, (O)ffset,\n(P)reserve, (R)emove, (S)ave, (T)haw, (U)rl-encoded (Offset), (W)arning marker for retry or (Y)ank",
             &(wdbp->parser_con));
        return E2EOF;
    }
/*
 * Recognise the name
 */
    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.scan_key[0],
        &validate_spec.scan_key[sizeof(validate_spec.scan_key) - 1])
                     == NULL)
    {
        fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n", wdbp->parser_con.pg.rope_seq,
                   __FILE__, __LINE__);
        return E2EOF;
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
        if ((sp = find_scan_spec(wdbp,&validate_spec))
             ==  (SCAN_SPEC *) NULL)
        {
            fprintf(stderr,"(Client:%s) SCAN_SPEC:%c: but %s does not exist\n",
                       wdbp->parser_con.pg.rope_seq, validate_spec.c_e_r_o_flag[0],
                       validate_spec.scan_key);
            return E2EOF;
        }
        switch (validate_spec.c_e_r_o_flag[0])
        {
        case 'R':
            break;
        case 'S':
            break;
        case 'Y':
            break;
        case 'F':
            sp->frozen = 1;
            break;
        case 'T':
            sp->frozen = 0;
            break;
        }
        fprintf(wdbp->parser_con.pg.fo, "%s{ name : \"%s\", type : \"%c\" }\n",
              (seq == 0) ? "" : ",", validate_spec.scan_key,
                      validate_spec.c_e_r_o_flag[0]);
        return SCAN_SPEC_TYPE;
    }
/*
 * All others must have at least one PATTERN_SPEC. Note that the Hexadecimal
 * only relates to the replacement pattern (which will be a session ID
 * or similar being replaced, rather than navigating to a position).
 */
    if (!recognise_pattern_spec(wdbp, got_to, &(validate_spec.ebp), 0))
        return E2EOF;
/*
 * If it isn't an E, G or P, there is an offset and a further SCAN_SPEC
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
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.i_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.i_len = atoi(num_buf);
        if (!recognise_pattern_spec(wdbp, got_to, &(validate_spec.rbp),
               (validate_spec.c_e_r_o_flag[0] == 'H')?1:0))
            return E2EOF;
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.o_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.o_len = atoi(num_buf);
        validate_spec.encrypted_token = (char *) NULL;
        validate_spec.frozen = 0;
    }
/*
 * Check to see if the scan_spec is already known. If it is, we update
 * it, otherwise we create a new one
 */
    if ((sp = find_scan_spec(wdbp,&validate_spec))
         !=  (SCAN_SPEC *) NULL)
        update_scan_spec(sp, &validate_spec);
    else
        sp = new_scan_spec(wdbp, &validate_spec);
/*
 * Finally, output the scan_spec as JSON.
 */
    scan_spec_to_json(sp, seq, wdbp);
    return SCAN_SPEC_TYPE;
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
                        wdbp->parser_con.pg.rope_seq, address, cap_port_id);
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
                    wdbp->parser_con.pg.rope_seq,
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
void mirror_client()
{
}
void snap_script()
{
}
void t3drive_listen()
{
}
void link_clear()
{
}
