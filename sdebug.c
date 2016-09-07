/*
 * sdebug.c - routines that support interactive control of the script driver.
 *
 * This is through HTTP directives received from and responded to on the
 * Mirror Port.
 *
 * The preliminary version just separated out the existing prod handler
 * from the other sources. We planned to use this as the hook from which to
 * dangle everything else:
 * -   Forwards and backwards movement through the script
 * -   Viewing internal status (eg. the current status of the various
 *     directives)
 * -   Interactive editing of directives, etc. etc.
 * -   Save of script
 *
 * Manipulation of the script file pointer and the look-ahead might have
 * allowed this code to control the driver without otherwise affecting the rest
 * of the driver code.
 *
 * However, we took a different approach. For script development purposes, we
 * work exclusively on a memory-resident version of the script. Nowadays we
 * are never so short of memory that this isn't viable for a single script.
 *
 * At the moment, we can capture, and we can single step. The next stage is to
 * combine these capabilities.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2009";
#include "webdrive.h"
/*
 * Send input or output unformatted.
 */
void unform_plain_send(mirror_out, buf, len, wdbp)
int mirror_out;
unsigned char * buf;
int len;
WEBDRIVE_BASE * wdbp;
{
    smart_write(mirror_out,
    "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/plain\r\n\r\n",
                        64, 0,  wdbp);
    smart_write(mirror_out, buf, len, 0,  wdbp);
    closesocket(mirror_out);
    return;
}
/**************************************************************************
 * Output clear ASCII text when we encounter it.
 *
 * Break up long lines
 */
static unsigned char * asc_copy(out, p, top)
unsigned char ** out;
unsigned char *p;
unsigned char *top;
{
unsigned char *la;
unsigned char tran;

    for (la = p; la < top; la++)
    {
        tran = *la;
        if (!(tran == (unsigned char) '\t'
             || tran == (unsigned char) '\n'
             || tran ==  (unsigned char) '\r'
             || tran ==  (unsigned char) '\f'
             || (tran > (unsigned char) 31 && tran < (unsigned char) 127)))
            break;
    }
    if ((la - p))
    {
    int len=(la -p);
    int olen = 79;
    unsigned char *p1;

        while (len)
        {
            if (olen > len)
                olen = len;
            memcpy(*out, p, olen);
            *out += olen;
            len -= olen;
            if (len)
            {
                if (memchr(p,'\n', olen) == (char *) NULL)
                {
                    *(*out)++ = '\\';
                    *(*out)++ = '\n';
                }
            }
            p += olen;
        }
    }
    return la;
}
/**************************************************************************
 * Output clear UNICODE text when we encounter it.
 *
 * Break up long lines
 */
static unsigned char * uni_copy(out, p, top)
unsigned char ** out;
unsigned char *p;
unsigned char *top;
{
unsigned char *la;
unsigned char tran;

    for (la = p; la < top; la++)
    {
        tran = *la;
        if (!(tran == (unsigned char) '\t'
             || tran == (unsigned char) '\n'
             || tran ==  (unsigned char) '\r'
             || tran ==  (unsigned char) '\f'
             || (tran > (unsigned char) 31 && tran < (unsigned char) 127)))
            break;
        la++;
        if (*la != '\0')
            break;
    }
    if ((la - p))
    {
    int len=(la -p)/2;
    int olen = 79;
    int i;
    unsigned char *p1, *p2;

        *(*out)++ = '"';
        *(*out)++ = 'U';
        *(*out)++ = '"';
        p2 = *out;
        while (len)
        {
            if (olen > len)
                olen = len;
            for (i = olen, p1 = p; i ; i--, (*out)++, p1 += 2)
                **out = *p1;
            len -= olen;
            if (memchr(p2, '\n', olen) == (char *) NULL)
            {
                *(*out)++ = '\\';
                *(*out)++ = '\n';
            }
            p += olen * 2;
        }
    }
    return la;
}
/**************************************************************************
 * Output non-clear text as blocks of Hexadecimal.
 */
static unsigned char * bin_copy(out, p,top)
unsigned char ** out;
unsigned char *p;
unsigned char *top;
{
unsigned char tran;
unsigned     char *la;

    for (la = p; la < top; la++)
    {
        tran = *la;
        if ((tran == (unsigned char) '\t'
             || tran == (unsigned char) '\n'
             || tran ==  (unsigned char) '\r'
             || (tran > (unsigned char) 31 && tran < (unsigned char) 127))
             && (((asc_handle(NULL, la, top, 0) - la) > 3)
              ||((uni_handle(NULL, la, top, 0) - la) > 3)))
            break;
    }
    if ((la - p))
    {
    int len=(la -p);
    int olen = 39;
    int i;

        while (len)
        {
            if (olen > len)
                olen = len;
            *(*out)++ = '\'';
            hexin_r(p, olen, *out, *out + olen + olen);
            *out += olen + olen;
            *(*out)++ = '\'';
            *(*out)++ = '\\';
            *(*out)++ = '\n';
            len -= olen;
            p += olen;
        }
    }
    return la;
}
/**************************************************************************
 * Output clear text when we encounter it, otherwise hexadecimal.
 */
static unsigned char * gen_copy(out, p, top)
unsigned char ** out;
unsigned char *p;
unsigned char *top;
{
    while ((p = bin_copy(out, p,top)) < top) 
    {
        if ((uni_handle(NULL, p, top, 0) - p) > 3)
            p = uni_copy(out, p, top);
        else
            p = asc_copy(out, p, top);
    }
    return top;
}
char * edit_if =
    "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/html\r\n\r\n\
<HTML>\n\
<HEAD>\n\
<TITLE>PATH Web Interface</TITLE>\n\
<link rel=\"stylesheet\" type=\"text/css\" href=\"visualise.css\" />\n\
<script src=\"visualise.js\" defer=\"false\"> \n\
</script>\n\
<script>\n\
function drawform() {\n\
get_script(\"E2JSON\");\n\
return true;\n\
}\n\
</script>\n\
</HEAD>\n\
<BODY bgcolor=\"#ffffff\" onLoad=\"drawform()\">\n\
<CENTER>\n\
<TABLE WIDTH=\"98%\" CELLSPACING=1 CELLPADDING=4 BORDER=1>\n\
<TR><TD BGCOLOR=\"#50ffc0\">\n\
<TABLE><TR><TD><A HREF=\"/\"><img\n\
src='data:image/gif;base64,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\n\
BnL1D3gjgZo5HyHUPgnNEN7ntRAAADs=' alt='PATH'></A>\n\
</TD><TD>\n\
<h3>Welcome to PATH 2016!</h3>\n\
<p>Choose an action or press the adjacent button</p>\n\
</TD>\n\
</TR>\n\
</TABLE>\n\
<HR/>\n\
</BODY>\n\
</HTML>";
/*
 * Send input or output unformatted.
 */
void unform_bin_send(mirror_out, buf, len, wdbp)
int mirror_out;
unsigned char * buf;
int len;
WEBDRIVE_BASE * wdbp;
{
unsigned char * outp = (unsigned char *) &(wdbp->msg.buf[1024]);

    smart_write(mirror_out,
    "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/plain\r\n\r\n",
                        64, 0,  wdbp);
    gen_copy(&outp, buf, buf + len);
    smart_write(mirror_out,  (unsigned char *) &(wdbp->msg.buf[1024]),
        (outp - (unsigned char *) &(wdbp->msg.buf[1024])), 0,  wdbp);
    closesocket(mirror_out);
    return;
}
/*
 * Display some fields from the client array
 */
void status_dump(mirror_out, wdbp)
int mirror_out;
WEBDRIVE_BASE * wdbp;
{
WEBDRIVE_BASE * xwdbp;
int i;
char * x = (char *) (&wdbp->ret_msg);

    x += sprintf(x,
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/html\r\n\r\n\
<html><body>\n\
<h1>Current Thread Status (Possible Clients: %d)</h1>\n\
<table><tr><th>Slot</th><th>Thread</th><th>Link</th><th>Last Function</th><tr>",
      wdbp->root_wdbp->client_cnt);
    for (xwdbp = wdbp->root_wdbp->client_array, i = 0;
            i < wdbp->root_wdbp->client_cnt;
                i++, xwdbp++)
    {
        x += sprintf(x,"<tr><td>%d</td><td>%lx</td><td>%d</td><td>%s</td></tr>\n",
            i, (unsigned long)
#ifdef MINGW32
            xwdbp->own_thread_id.hthread,
#else
            xwdbp->own_thread_id,
#endif
            xwdbp->cur_link->connect_fd, xwdbp->narrative);
    }
    x += sprintf(x, "</table><a href=\"/\">Return to menu</a></body></html>\n");
    smart_write(mirror_out, (char *) (&wdbp->ret_msg), x - 
                            (char *) (&wdbp->ret_msg) , 0, wdbp);
    closesocket(mirror_out);
    return;
}
/*
 * Script control console. It needs to be like this to work round
 * cross site scripting problems.
 */
void menu_send(mirror_out, wdbp)
int mirror_out;
WEBDRIVE_BASE * wdbp;
{
int len = sprintf((char *) (&wdbp->ret_msg),
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/html\r\n\r\n\
<html><body><script>\n\
var input_win;\n\
var answer_win;\n\
var input_win_bin;\n\
var answer_win_bin;\n\
function do_get(lwin, lurl, ldesc, lerrm ) {\n\
    try\n\
    {\n\
        if (lwin && !lwin.closed)\n\
            lwin.close();\n\
    lwin = window.open(\"http://127.0.0.1:%d/\" + lurl,ldesc,\"width=800,height=600,scrollbars=yes,resizable=1,toolbar=0,menubar=0\");\n\
        lwin.focus();\n\
    }\n\
    catch(e) {\n\
    alert(lerrm + \" Load Failed: \" + e);\n\
    }\n\
    return false;\n\
}\n\
var req=null;\n\
function handleresp()\n\
{\n\
    return;\n\
}\n\
function do_cont() {\n\
/*\n\
 * Mozilla and WebKit\n\
 */\n\
if (window.XMLHttpRequest)\n\
    req = new XMLHttpRequest();\n\
else\n\
if (window.ActiveXObject)\n\
{\n\
/*\n\
 * Internet Explorer (new and old)\n\
 */\n\
    try\n\
    {\n\
       req = new ActiveXObject(\"Msxml2.XMLHTTP\");\n\
    }\n\
    catch (e)\n\
    {\n\
       try\n\
       {\n\
           req = new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
       }\n\
       catch (e)\n\
       {}\n\
    }\n\
} \n\
req.open(\"GET\", \"E2CONTINUE\", true);\n\
req.onreadystatechange = handleresp;\n\
try\n\
{\n\
req.send(\"\");\n\
}\n\
catch (e)\n\
{\n\
    alert(\"Error: \" + e)\n\
}\n\
return;\n\
}\n\
</script>\n\
<h1>PATH Web Script Controlled Execution Environment</h1>\n\
<p>The script is now running. You control it via the buttons below.</p>\n\
<p>View Input allows you to inspect the next script input before it is submitted. You can change the ASCII rendition and it will be re-submitted at the next step.\n\
The input appears in a new browser window.</p>\n\
<p>View Input is optional. It has no effect on the progress of the script.</p>\n\
<p>Single Step Script causes the next input to be submitted. The output appears in a new window.</p>\n\
<p>There are two versions of each, which differ according to whether the output is seen 'raw' or 'cooked' (an ASCII rendition of binary data, or the HTML or whatever uninterpreted).</p>\n\
<p>Click the E2 Systems logo if you wish to terminate the script prematurely, or when the script has finished.</p>\n\
<p>You may close either or both of the two extra windows as you please.</p>\n\
<p>Right click and Refresh may be useful in the Single step windows, if the Input View suggests that the script is not in fact advancing.</p>\n\
<form><input type=\"button\" value=\"View Script Input\" onClick=\"return do_get(input_win, 'E2INPUT', 'INPUT', 'Input');\">\n\
<input type=\"button\" value=\"View Input (Binary)\" onClick=\"return do_get(input_win_bin, 'E2BINPUT', 'BINPUT', 'Binary Input');\">\n\
<input type=\"button\" value=\"Single Step Script\" onClick=\"return do_get(answer_win, 'E2STEP', 'ANSWER', 'Answer');\">\n\
<input type=\"button\" value=\"Single Step (Binary)\" onClick=\"return do_get(answer_win_bin, 'E2BSTEP', 'BANSWER', 'Binary Answer');\">\n\
<input type=\"button\" value=\"Run On\" onClick=\"return do_cont();\">\n\
</form></body></html>\n", wdbp->mirror_port);
    smart_write(mirror_out, (char *) (&wdbp->ret_msg), len, 0, wdbp);
    closesocket(mirror_out);
    return;
}
/*
 * Script capture console
 */
void proxy_send(mirror_out, wdbp)
int mirror_out;
WEBDRIVE_BASE * wdbp;
{
int len = sprintf((char *) (&wdbp->ret_msg),
      "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/html\r\n\r\n\
<html><body><script>\n\
var req=null;\n\
function handleresp()\n\
{\n\
    if (req.readyState == 4 && (req.status == 0 || req.status == 200))\n\
    {\n\
        document.body.innerHTML = req.responseText;\n\
    }\n\
    return;\n\
}\n\
function do_get(lurl, ldesc, lerrm ) {\n\
/*\n\
 * Mozilla and WebKit\n\
 */\n\
if (window.XMLHttpRequest)\n\
    req = new XMLHttpRequest();\n\
else\n\
if (window.ActiveXObject)\n\
{\n\
/*\n\
 * Internet Explorer (new and old)\n\
 */\n\
    try\n\
    {\n\
       req = new ActiveXObject(\"Msxml2.XMLHTTP\");\n\
    }\n\
    catch (e)\n\
    {\n\
       try\n\
       {\n\
           req = new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
       }\n\
       catch (e)\n\
       {}\n\
    }\n\
} \n\
req.open(\"GET\", lurl, true);\n\
req.onreadystatechange = handleresp;\n\
try\n\
{\n\
req.send(\"\");\n\
/* alert(ldesc + \" submitted\"); */\n\
}\n\
catch (e)\n\
{\n\
    alert(lerrm + \" Error: \" + e)\n\
}\n\
return;\n\
}\n\
</script>\n\
<h1>PATH Web Script Capture Console</h1>\n\
<p>The script capture is now running. You comment it, and terminate it, via the form below.</p>\n\
<form><p>SSL Toggle</p><input type=\"button\" value=\"%s\" onClick=\"return do_get('E2SSL', 'SSL', 'Toggle SSL Input');\">\n\
<p>Comment</p><input type=\"text\" id=\"event\";\">\n\
<input type=\"button\" value=\"Submit Comment\" onClick=\"return do_get('E2SYNC?comment='+escape(document.getElementById('event').value),\n\
'E2SYNC ' +document.getElementById('event').value , 'Script Commentary');\">\n\
<input type=\"button\" value=\"Close Script\" onClick=\"return do_get('E2CLOSE', 'CLOSE', 'Close Script File');\">\n\
<input type=\"button\" value=\"View Thread Status\" onClick=\"return do_get('E2STATUS', 'STATUS', 'View Thread Status');\">\n\
</form></body></html>\n", 
  ((wdbp->not_ssl_flag == 3) ? "Currently non-SSL" : "Currently SSL"));
    smart_write(mirror_out, (char *) (&wdbp->ret_msg), len, 0, wdbp);
    closesocket(mirror_out);
    return;
}
void snap_script(wdbp)
WEBDRIVE_BASE * wdbp;
{
    pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
    dump_script(wdbp->sc.anchor, wdbp->parser_con.pg.logfile,
                         wdbp->debug_level);
    pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
    return;
}
/*
 * Find any still-open sockets and log closes for them
 */
static void generate_closes(wdbp)
WEBDRIVE_BASE * wdbp;
{
int i;
struct script_control * scp;
WEBDRIVE_BASE * root_wdbp;
LINK * cur_link;

    root_wdbp = wdbp->root_wdbp;
    scp = &root_wdbp->sc;
    i = root_wdbp->client_cnt;
    wdbp = &root_wdbp->client_array[0];
/*
 * The calling routine has locked the script. But we have a problem that isn't
 * resolved here? There is a race condition with access to the end points?
 */
    for (;i > 0; wdbp++, i--)
    {
        cur_link =  &wdbp->link_det[wdbp->root_wdbp->not_ssl_flag];
        if (cur_link->connect_fd != -1)
            add_close(scp, cur_link);
    }
/*
 * Close the event, add the reset at the end, and add the think time and the
 * SSL spec at the top.
 */
    close_event(scp);
    new_script_element( scp, "\\D:R\\\n", NULL);
#ifdef USE_SSL
    add_ssl_spec(scp, &webdrive_base.ssl_specs[0]);
#endif
    head_script_element(scp, "\\W10\\\n");
    return;
}
/*
 * Set up a connection to mirror what we receive, for single stepping
 * through the application
 */
int mirror_setup(msg, wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
int mess_len;
int mirror_out;
int len;
char * bound_p; /* Prod bound */
char * bound_s; /* Script bound */

    if (wdbp->mirror_fd == -1)
        wdbp->mirror_fd = listen_setup("127.0.0.1", wdbp->mirror_port,
                                   &wdbp->mirror_sock);
    if (wdbp->mirror_fd == -1)
        return -1;
/*
 * We wait for an 'official' prod. This must either be the root,
 * or what we are about to send, or a request to see the input.
 *
 * We want to ignore the requests that just originate from the
 * prodding browser itself, because the just-loaded page includes
 * scripts, images or style-sheets. However, we can't determine the
 * order they are going to come in, so we need to do a heuristic check for
 * likely static content.
 */
    do
    {
        mess_len = sizeof(wdbp->mirror_sock);
/*
 * Cause the accept() to time out after 1 second.
 */
        if (wdbp->parser_con.break_flag)
        {
            add_time(wdbp->root_wdbp, wdbp, 1);
            sigrelse(SIGIO);
        }
        mirror_out = accept(wdbp->mirror_fd, (struct sockaddr *)
                              &(wdbp->mirror_sock), &mess_len);
        if (wdbp->parser_con.break_flag)
        {
            zap_time(wdbp->root_wdbp, wdbp);
            sighold(SIGIO);
            if (mirror_out < 0)
                break;
        }
        else
        if (mirror_out < 0)
            continue;
        if (wdbp->parser_con.break_flag)
            wdbp->parser_con.break_flag = 0; /* Go back to single step */
        mess_len = recvfrom(mirror_out, (char *) &(wdbp->ret_msg),
                                4096, 0,0,0);
        if (mess_len > 0)
            fwrite( (char *) &(wdbp->ret_msg), 1, mess_len, stderr);
        if (mess_len > 12
         && !strncmp((char *) &(wdbp->ret_msg), "GET / HTTP/1.", 13))
        {
            if (wdbp->proxy_port != 0)
                proxy_send(mirror_out, wdbp);
            else
                menu_send(mirror_out, wdbp);
            mirror_out = -1;
        }
        else
        if (mess_len > 19
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2SYNC?comment=", 20))
        {
            pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
            len = strcspn( ((char *) &(wdbp->ret_msg))+20, " \r\n");
            *( ((char *) &(wdbp->ret_msg))+20 + len) = '\0';
            proxy_e2sync(&wdbp->root_wdbp->sc,  ((char *) &(wdbp->ret_msg))+20);
            pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
            proxy_send(mirror_out, wdbp);
            snap_script(wdbp);           /* Could get slow ... ? */
            mirror_out = -1;
        }
        else
        if (mess_len > 13
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2CLOSE H", 14))
        {
            pthread_mutex_lock(&(wdbp->root_wdbp->script_mutex));
            generate_closes(wdbp);
            pthread_mutex_unlock(&(wdbp->root_wdbp->script_mutex));
            snap_script(wdbp);
            unform_plain_send(mirror_out, "Script capture complete\n", 24,
                        wdbp);
            
            exit(0);
        }
        else
        if (mess_len > 13
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2SSL HTT", 14))
        {
            wdbp->not_ssl_flag = (wdbp->not_ssl_flag == 3 ) ? 1 : 3; 
            toggle_ssl(wdbp);
            proxy_send(mirror_out, wdbp);
            mirror_out = -1;
        }
        else
        if (mess_len > 16
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2STATUS HTT", 17))
        {
            status_dump(mirror_out, wdbp);
            mirror_out = -1;
        }
        else
        if (mess_len > 14
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2CONTINUE", 15))
        {
            wdbp->parser_con.break_flag = 1;
            mirror_out = -1;
            break;
        }
        else
        if (mess_len > 11
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2INPUT", 12))
        {
            unform_plain_send(mirror_out, wdbp->in_buf.send_receive.msg->buf,
                          wdbp->in_buf.send_receive.message_len,
                        wdbp);
            mirror_out = -1;
        }
        else
        if (mess_len > 12
         && !strncmp((char *) &(wdbp->ret_msg), "GET /E2BINPUT", 13))
        {
            unform_bin_send(mirror_out, wdbp->in_buf.send_receive.msg->buf,
                          wdbp->in_buf.send_receive.message_len,
                        wdbp);
            mirror_out = -1;
        }
        else
        if (mess_len < 11
         || ((strncmp( (char *) &(wdbp->ret_msg), "GET /E2STEP", 11))
          && (strncmp( (char *) &(wdbp->ret_msg), "GET /E2BSTEP", 12))
           && (msg == NULL
               || ((bound_s = memchr( msg->send_receive.msg->buf,'\r',
                                  msg->send_receive.message_len)) == NULL)
               || ((bound_p = memchr( (char *) &(wdbp->ret_msg), '\r',
                                  mess_len)) == NULL)
               || (((bound_s - (char *) msg->send_receive.msg->buf))
                            < (bound_p - (char *) &(wdbp->ret_msg)))
               || strncmp( (char *) &(wdbp->ret_msg) + 4,
                              bound_s - 
                              (bound_p - (char *) &(wdbp->ret_msg) - 4),
                              (bound_p - (char *) &(wdbp->ret_msg) - 6)))))
        {
            smart_write(mirror_out,
      "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", 46, 0,  wdbp);
            closesocket(mirror_out);
            mirror_out = -1;
        }
        if (mess_len > 11
          && !strncmp( (char *) &(wdbp->ret_msg), "GET /E2BSTEP", 12))
            wdbp->mirror_bin = 1;
        else
            wdbp->mirror_bin = 0;
    }
    while (mirror_out == -1);
    return mirror_out;
}
/*
 * Send our returned data to the mirror port
 */
void mirror_send(mirror_out, mess_len, wdbp)
int mirror_out;
int mess_len;
WEBDRIVE_BASE * wdbp;
{
int so_far = 0;
int r;
int loop_detect;
char * buf;

    if (mess_len > 0)
    {
        if (wdbp->mirror_bin)
            unform_bin_send(mirror_out,
                (char *) ((mess_len > WORKSPACE) ? wdbp->overflow_receive :
                  (unsigned char *) &(wdbp->ret_msg)),
                    mess_len, wdbp);
        else
            smart_write(mirror_out, (char *)
               ((mess_len > WORKSPACE) ? wdbp->overflow_receive :
                  (unsigned char *) &(wdbp->ret_msg)), mess_len,
                 0, wdbp);
    }
    closesocket(mirror_out);
    return;
}
/*
 * Service routine for the debug thread when we are being a proxy.
 */
void mirror_client(wdbp)
WEBDRIVE_BASE * wdbp;
{
int mirror_out;

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Mirror Port\n",
            wdbp->parser_con.pg.rope_seq);
        fflush(stderr);
    }
    wdbp->own_thread_id = pthread_self();
    sigrelse(SIGTERM);
    sigrelse(SIGUSR1);
    while (wdbp->go_away == 0 && (mirror_out = mirror_setup(NULL, wdbp)) > -1)
        closesocket(mirror_out);
    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Mirror Port Thread Done\n",
            wdbp->parser_con.pg.rope_seq);
        fflush(stderr);
    }
    exit(0);
}
