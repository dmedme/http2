/*******************************************************************************
 * webserv.c - functionality required to support a multi-user PATH Web interface
 *******************************************************************************
 * This code comes from webmenu.c originally.
 *
 * As originally conceived, the shell scripts communicated with webmenu over
 * stdin and stdout. This turned out not to work on Windows with the flexibility
 * available on UNIX variants. So it was changed to communicate via name pipes.
 *
 * Incoming data is fed to a named pipe.
 *
 * The controlled program does whatever it pleases, and writes an output file.
 *
 * The name of this output file is passed back.
 *
 * The file is returned to the caller
 *
 * The result looks overly complicated on UNIX, because it uses two programs,
 * e2fifin and e2fifout, which are redundant on UNIX. But they are needed on
 * Windows, and this way the code runs unchanged on both platforms.
 *
 * In the original 'tricky to have more than one user' world, webpath.sh
 * allocates the names of things, using its Process ID. This task now falls to
 * code here. 
 * -  FIFO's are named via the session.
 * -  The proxy and single-step ports need to be allocated dynamically using
 *    the stored values as bases.
 *******************************************************************************
 * Security is rudimentary
 * -  You can't have .. in a file path
 * -  You can't have an absolute path
 * -  You can only update things under the scripts or data directory,
 *    or a few specific elements
 ******************************************************************************
 * Copyright (c) E2 System 2008
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 2008";
#include "webdrive.h"
static pthread_t zeroth;
#ifdef MINGW32
#define strncasecmp strnicmp
#include <winsock2.h>
#include <windef.h>
#include <windows.h>
#ifdef IMPROVE_TIMER_RESOLUTION
#include <intrinsics.h>
#endif
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#define sleep Sleep
#define dup2 _dup2
#ifndef O_NOINHERIT
#define O_NOINHERIT 0x80
#endif
#include "matchlib.h"
char * strtok_r();
static int pipe (int fd[2])
{
    return _pipe(fd, 4096, O_BINARY|O_NOINHERIT);
}
void setlinebuf(FILE * fp)
{
    setvbuf(fp, NULL, _IOLBF, 4096);
    return;
}
#else
/*
 * Non-standard Windows dup2() behaviour
 */
static int _dup2(int oldfd, int newfd)
{
    oldfd = dup2(oldfd, newfd);
    return ((oldfd == newfd) ? 0 : -1);
}
#endif
#ifdef UNIX
/*
 * Find any still-open sockets and close them
 */
static void close_in_child(wdbp)
WEBDRIVE_BASE * wdbp;
{
int i;
LINK * cur_link;

    wdbp = wdbp->root_wdbp;
    i = wdbp->client_cnt;
    wdbp = &wdbp->root_wdbp->client_array[0];
/*
 * The calling routine has locked the script. But we have a problem that isn't
 * resolved here? There is a race condition with access to the end points?
 */
    for (;i > 0; wdbp++, i--)
    {
        for (cur_link = wdbp->link_det;
                cur_link < &wdbp->link_det[MAXLINKS]
              && cur_link->link_id != 0;
                     cur_link++)
        {
            if (cur_link->connect_fd != -1)
                closesocket(cur_link->connect_fd);
        }
    }
    return;
}
long int launch_pipeline(int in_fd[2], int out_fd[2], char *in_command_line, WEBDRIVE_BASE * wdbp)
{
int child_pid;

    pipe(&in_fd[0]);
    pipe(&out_fd[0]);
    if ((child_pid = fork()) == 0)
    {     /* Child pid */
        if (in_fd[0] != 0)
        {
            dup2(in_fd[0], 0);
            close(in_fd[0]);
        }
        if (out_fd[1] != 1)
        {
            dup2(out_fd[1], 1);
            close(out_fd[1]);
        }
        close(out_fd[0]);
        close(in_fd[1]);
        close_in_child(wdbp);
        if (in_command_line == NULL || strlen(in_command_line) == 0)
            execlp("sh","sh",NULL);
        else
            execlp("sh","sh", "-c", in_command_line, NULL);
        perror("execlp() failed");
        exit(1);
    }
    else
    {
        close(in_fd[0]);
        close(out_fd[1]);
    }
    return child_pid;
}
#else
/*
 * Parent sends on in_fd[1], receives on out_fd[0]
 * Pipeline sends on out_fd[1], receives on in_fd[0]
 */
long int launch_pipeline(int in_fd[2], int out_fd[2], char *in_command_line, WEBDRIVE_BASE * wdbp)
{
char * command_line;

int h0;
int h1;
int h2;
int len;
DWORD dwCreationFlags = CREATE_NEW_PROCESS_GROUP;
BOOL bInheritHandles = TRUE;
STARTUPINFO si;
PROCESS_INFORMATION pi;
char * prog_name;
/*
 * Check that we actually have a command to execute
 */
    if (in_command_line == NULL)
        return 0;
/*
 * Find the command interpreter
 */
    if ((prog_name = getenv("COMSPEC")) == (char *) NULL)
    {
        if ( _osver & 0x8000 )
            prog_name = "C:\\WINDOWS\\COMMAND.COM";
        else
            prog_name = "C:\\WINDOWS\\SYSTEM32\\CMD.EXE";
/*            prog_name = "C:\\WINNT\\SYSTEM32\\CMD.EXE"; */
    }
    if ((command_line = (char *) malloc(strlen(prog_name) + 10
               + strlen(in_command_line)  + 1))
                  == (char *) NULL)
        return 0;
    sprintf(command_line, "%s /d /q /c %s", prog_name, in_command_line);
    fprintf(stderr, "Launch Command: %s\n", command_line);
/*
 * Default setup information for the spawned process
 */
    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwX = 0;
    si.dwY = 0;
    si.dwXSize = 0;
    si.dwYSize= 0;
    si.dwXCountChars = 0;
    si.dwYCountChars= 0;
    si.dwFillAttribute= 0;
    si.dwFlags = STARTF_USESTDHANDLES /* | STARTF_USESHOWWINDOW */ ;
    si.wShowWindow = 0;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;
/*
 * Create the read pipe
 */
    if (_pipe(&in_fd[0],4096,O_BINARY|O_NOINHERIT))
    {
        fprintf(stderr,"pipe(in_fd...) failed error:%d\n", errno);
        free(command_line);
        return 0;
    }
#ifdef DEBUG
    else
        fprintf(stderr,"pipe(in_fd...) gives files:(%d,%d)\n", in_fd[0],
                   in_fd[1]);
#endif
    if (_pipe(&out_fd[0],4096,O_BINARY|O_NOINHERIT))
    {
        fprintf(stderr,"pipe(out_fd...) failed error:%d\n", errno);
        _close(in_fd[0]);
        _close(in_fd[1]);
        free(command_line);
        return 0;
    }
#ifdef DEBUG
    else
        fprintf(stderr,"pipe(out_fd...) gives files:(%d,%d)\n", out_fd[0],
                   out_fd[1]);
#endif
    if ((h0 = _dup(in_fd[0])) < 0)
    {
        fprintf(stderr,"dup(in_fd[0]) failed error:%d\n", errno);
        _close(in_fd[0]);
        _close(out_fd[0]);
        _close(in_fd[1]);
        _close(out_fd[1]);
        free(command_line);
        return 0;
    }
    _close(in_fd[0]);
    SetHandleInformation((HANDLE) _get_osfhandle(in_fd[1]),
                  HANDLE_FLAG_INHERIT, 0);
/*
 * Set up the pipe handles for inheritance
 */
    if ((h1 = _dup(out_fd[1])) < 0)
    {
        fprintf(stderr,"dup(out_fd[1]) failed error:%d\n", errno);
        _close(out_fd[0]);
        _close(out_fd[1]);
        _close(in_fd[1]);
        _close(h0);
        free(command_line);
        return 0;
    }
    if ((h2 = _dup(2)) < 0)
    {
        fprintf(stderr,"dup(2) failed error:%d\n", errno);
        _close(h1);
        _close(out_fd[0]);
        _close(out_fd[1]);
        _close(in_fd[1]);
        _close(h0);
        free(command_line);
        return 0;
    }
    _close(out_fd[1]);
    SetHandleInformation((HANDLE) _get_osfhandle(out_fd[0]),
                     HANDLE_FLAG_INHERIT, 0);
/*
 * Execute the command
 */
    si.hStdInput = (HANDLE) _get_osfhandle(h0);
    si.hStdOutput = (HANDLE) _get_osfhandle(h1);
    si.hStdError = (HANDLE) _get_osfhandle(h2);
    if (!CreateProcess(prog_name, command_line, NULL, NULL, bInheritHandles,
                  dwCreationFlags, NULL, NULL, &si, &pi))
    {
        fprintf(stderr,"CreateProcess failed error:%d\n", errno);
        _close(out_fd[0]);
        _close(in_fd[1]);
        _close(h0);
        _close(h1);
        _close(h2);
        free(command_line);
        return 0;
    }
    _close(h0);
    _close(h1);
    _close(h2);
#ifdef DEBUG
    fprintf(stderr, "Handle: %x Process: %s\n", pi.hProcess, command_line);
    fflush(stderr);
#endif
    free(command_line);
    CloseHandle(pi.hThread);
    return (long int) pi.hProcess;
}
#endif
void cleanup_session(wdbp)
WEBDRIVE_BASE * wdbp;
{
/*
 * Clean up the session if we cannot contact the child
 */
    memset(wdbp->session,'\0', sizeof(wdbp->session));
    memset(wdbp->username,'\0', sizeof(wdbp->username));
    memset(wdbp->passwd,'\0', sizeof(wdbp->passwd));
    if (wdbp->out_fifo != NULL)
    {
        free(wdbp->out_fifo);
        wdbp->out_fifo = NULL;
    }
    if (wdbp->in_fifo != NULL)
    {
        free(wdbp->in_fifo);
        wdbp->in_fifo = NULL;
    }
    return;
}
/****************************************************************************
 * Routine to check a file.
 * -   path mustn't start with /
 * -   path mustn't contain ../
 * -   if file exists, it must be a regular file
 * -   if file is being written, it must be below the scripts directory or
 *     below the data directory.
 */
int sec_check(fname,  write_flag, wdbp)
char * fname;
int write_flag;
WEBDRIVE_BASE * wdbp;
{
struct stat sbuf;
int ret;
char * bound = fname +  strlen(fname);

    strcpy(wdbp->narrative, "sec_check");
    while (*fname =='/' || *fname =='\\' || *fname == ' ')
        fname++;
    if (bm_match(wdbp->traverse_u, fname, bound) != NULL
     || bm_match(wdbp->traverse_w, fname, bound) != NULL
     || strlen(fname) > PATH_MAX
     || (write_flag
        && strncmp(fname, "scripts/", 8)
/*
 *        && strncmp(fname, "path_web/pathenv.sh", 19)
 *      && strncmp(fname, "web_path_web/pathenv.sh", 23)
 */
        && strncmp(fname, "data/", 5)))
        return 403;
    if ((ret = stat(fname, &sbuf)) && !write_flag)
        return 404;
    if (!ret && (!S_ISREG(sbuf.st_mode)))
        return 403;
    return 0;
}
static void cont_type_lookup(fext, outp)
char * fext;
char * outp;
{
    if (!strcasecmp(fext, "html")
     || !strcasecmp(fext, "htm"))
        strcpy(outp, "text/html");
    else
    if (!strcasecmp(fext, "png"))
        strcpy(outp, "image/png");
    else
    if (!strcasecmp(fext, "gif"))
        strcpy(outp, "image/gif");
    else
    if (!strcasecmp(fext, "jpg")
     || !strcasecmp(fext, "jpeg"))
        strcpy(outp, "image/jpeg");
    else
    if (!strcasecmp(fext, "svg"))
        strcpy(outp, "image/svg+xml");
    else
    if (!strcasecmp(fext, "css"))
        strcpy(outp, "text/css");
    else
    if (!strcasecmp(fext, "js"))
        strcpy(outp, "text/javascript");
    else
    if (!strcasecmp(fext, "txt"))
        strcpy(outp, "text/plain");
    else
    if (!strcasecmp(fext, "msg"))   /* A PATH script */
        strcpy(outp, "text/plain");
    else
    if (!strcasecmp(fext, "pdf"))
        strcpy(outp, "application/pdf");
    else
    if (!strcasecmp(fext, "jar"))
        strcpy(outp, "application/x-java-archive");
    else
    if (!strcasecmp(fext, "doc"))
        strcpy(outp, "application/msword");
    else
    if (!strcasecmp(fext, "xls")
     || !strcasecmp(fext, "csv"))
        strcpy(outp, "application/msexcel");
    else
    if (!strcasecmp(fext, "ppt")
     || !strcasecmp(fext, "pps"))
        strcpy(outp, "application/mspowerpoint");
    else
        strcpy(outp, "application/octet-stream");
    return;
}
/*
 * Send a normal static file
 */
static void web_file_send_static(f, fp, fname, sess_flag, wdbp)
int f;
FILE *fp;
char * fname;
int sess_flag;
WEBDRIVE_BASE * wdbp;
{
char fbuf[16384];
char * xp1;
int n;
int txt_flag;
struct stat sbuf;

    strcpy(wdbp->narrative, "web_file_send_static");
    wdbp->alert_flag = 0;
    if (wdbp->debug_level)
        (void) fprintf(stderr,"web_file_send_static(%d:%s) start\n", f, fname);
    if (fp == NULL)
    {
        smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
        return;
    }
    memcpy(wdbp->parser_con.tbuf, "HTTP/1.1 200 OK\r\nContent-type: ", 31);
    if ((xp1 = strrchr(fname, '.')) == NULL)
        xp1 = fname;
    else
        xp1++;
    cont_type_lookup(xp1, wdbp->parser_con.tbuf + 31);
    txt_flag = (!strncmp(wdbp->parser_con.tbuf + 31, "text/", 5)) ? 1 : 0;
    xp1 = wdbp->parser_con.tbuf + 31 + strlen(wdbp->parser_con.tbuf + 31);
    *xp1++ = '\r';
    *xp1++ = '\n';
    if (sess_flag)
        xp1 += sprintf(xp1,
            "Set-Cookie: E2SESSION=%s;Path=/;HttpOnly;Secure\r\n",
                        wdbp->session);
    fstat(fileno(fp), &sbuf);
/*
 * Now the length
 */
    xp1 += sprintf(xp1, "Content-Length: %lu\r\n\r\n", sbuf.st_size);
    smart_write(f, wdbp->parser_con.tbuf, (xp1 - wdbp->parser_con.tbuf), 1, wdbp); 
    if (wdbp->debug_level && txt_flag)
        fwrite(wdbp->parser_con.tbuf, (xp1 - wdbp->parser_con.tbuf), 1, stderr);
/*
 * An opportunity for LINUX sendfile() or its equivalent on other operating
 * systems...
 */
    while((n = fread(wdbp->parser_con.tbuf,sizeof(char), WORKSPACE, fp)) > 0)
    {
        if (wdbp->debug_level && txt_flag)
            fwrite(wdbp->parser_con.tbuf, n, 1, stderr);
        smart_write(f, wdbp->parser_con.tbuf, n, 1, wdbp); 
    }
    fclose(fp);
    if (wdbp->debug_level)
        (void) fprintf(stderr,"web_file_send_static(%s) complete\n", fname);
    return;
}
/*
 * Naively handle a PUT
 */
static char * attempt_put(f, buf, bound, mess_len, wdbp)
int f;
char * buf;
char * bound;
int mess_len;
WEBDRIVE_BASE * wdbp;
{
FILE * fp;
int n;
char * cp;
int cont_len;
char * ehp;

/*
 * Prohibit gross breaches of security
 */
    while (*buf == '/' || *buf == '\\')
    {
        buf++;
        mess_len--;
    }
    if (sec_check(buf, 1, wdbp) == 403)
    {
        smart_write(f, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
        return NULL;
    }
    strcpy(wdbp->narrative, "attempt_put");
/*
 * Search for the length; exit if not found
 */
    if ((ehp = bm_match(wdbp->ehp, buf, buf + mess_len) + 4) < (char *) 5)
        return buf;
    if ((cp = bm_match(wdbp->sbp, buf, ehp)) == NULL)
        return buf;
    cont_len = atoi(cp + 16);
/*
 * We have made sure that we have the whole header, so this shouldn't fail ...
 */
    if ((fp = fopen(buf, "wb")) != NULL)
    {
        smart_write(f, "HTTP/1.1 100-Continue\r\n\r\n", 25, 1, wdbp); 
        n = (mess_len - (ehp - buf));
        if (mess_len > n)
        {
            fwrite(ehp, sizeof(char), n, fp);
            cont_len -= n; 
        }
        if (cont_len > WORKSPACE)
            cp = (char *) malloc(cont_len);
        else
            cp = wdbp->parser_con.tbuf;
        if ((cont_len =  smart_read(f,cp,cont_len, 1, wdbp)) > 0)
            fwrite(cp, sizeof(char), cont_len, fp);
        fclose(fp);
        if (cp != wdbp->parser_con.tbuf)
            free(cp);
        memcpy(wdbp->parser_con.tbuf, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n", 41); 
        cont_len = 41 + sprintf(wdbp->parser_con.tbuf + 41, "Location: /%s\r\n\r\n", wdbp->parser_con.sav_tlook); 
        smart_write(f, wdbp->parser_con.tbuf, cont_len, 1, wdbp); 
        return NULL;
    }
    return buf;
}
/*
 * Attempt to handle monitor requests
 *
 * Check if URL is child of one of our monitor's
 */
static char * check_child_url(mtp, url, wdbp)
struct mon_target * mtp;
char * url;
WEBDRIVE_BASE * wdbp;
{
    if (wdbp->debug_level)
        fprintf(stderr, "check_child_url(host: %s url: %s)\n", mtp->host, url);
    if (!strncmp(mtp->host, url, strlen(mtp->host)))
        return (url + strlen(mtp->host) + 1);
    return NULL;
}
/*
 * Kick off the process pipeline that will handle the collection and
 * rendition of statistics
 */
static int spawn_monitor(wdbp)
WEBDRIVE_BASE * wdbp;
{
int out_pipe[2];
int in_pipe[2];
char buf[512];
struct mon_target * mtp = &wdbp->mon_target;
/*
 * Command to run monitors; does it work on Windows?
 * It did not, but the introduction of increp and increp.bat resolves the
 * issue.
 */
    strcpy(wdbp->narrative, "spawn_monitor");
    sprintf(buf, "minitest %.64s %.6s EXEC \"logmon %.64s %.1s\" | increp %s %.6s %.64s",
             mtp->host, mtp->port, mtp->pid, mtp->sample,
             mtp->dir, mtp->sla, mtp->host);
#ifdef OLD
    sprintf(buf, "minitest %.64s %.6s EXEC 'logmon %.64s %.1s' | ( cd %s; fdreport -i %.6s -b -r -o %.64s_mon.html )",
             mtp->host, mtp->port, mtp->pid, mtp->sample,
             mtp->dir, mtp->sla, mtp->host);
#endif
    if (wdbp->debug_level)
        fprintf(stderr, "Monitor Command: %s\n", buf);
/*
 * Cannot launch pipeline; give up
 */ 
    if (!(mtp->child_pid = launch_pipeline(in_pipe, out_pipe, buf, wdbp)))
    {
        close(in_pipe[1]);
        close(out_pipe[0]);
        return 0;
    }
    mtp->pipe_out = in_pipe[1];
    mtp->pipe_in = out_pipe[0];
    if (wdbp->debug_level)
        fprintf(stderr, "Monitor FD's: out: %d in: %d\n", mtp->pipe_out,
                 mtp->pipe_in);
    return 1;
}
/*
 * Serve up a tranche of monitor data.
 */
void serve_monitor(mtp, wdbp)
struct mon_target * mtp;
WEBDRIVE_BASE * wdbp;
{
char fbuf[800];
char ret_buf[256];
FILE * fp;
int len;
int fd = dup(mtp->pipe_in);

    strcpy(wdbp->narrative, "serve_monitor");
    if (wdbp->debug_level)
        fprintf(stderr, "index: %lx host: %s port: %s run: %s sample? %s dir: %d sla: %s in: %d out: %d\n", (unsigned long) wdbp, 
             mtp->host, mtp->port, mtp->pid, mtp->sample,
             mtp->dir, mtp->sla, fd, mtp->out_fd);
    fp = fdopen(fd, "rb");
    setlinebuf(fp);
    while (fgets(&ret_buf[0], sizeof(ret_buf) - 1, fp) == &ret_buf[0])
    {
        len = strlen(ret_buf) - 1;
        ret_buf[len] = '\0';
        if (ret_buf[len - 1] == '\r')
        {
            len--;
            ret_buf[len] = '\0';
        }
        if (len > 5 && !memcmp(&ret_buf[len - 5], ".html", 5))
        {
            sprintf(fbuf, "%.512s/%.256s", mtp->dir, ret_buf);
            web_file_send_static(mtp->out_fd, fopen(fbuf, "rb"), fbuf, 0, wdbp);
            break;
        }
    }
    fclose(fp);
    return;
}
/*
 * Handle a request that needs to go to one of our pipelines
 * 
 * A monitor URL consists of:
 * - monitor
 * - variables
 *   - host
 *   - runid
 *   - directory (needed so that fdreport will run where there is a runout file)
 * 
 * The caller should have stripped leading /'s off the input URL.
 */
static char * async_handle(f, url, wdbp)
int f;
char * url;
WEBDRIVE_BASE * wdbp;
{
int i;
int j;
char fbuf[800];
int first_free;
int last_used;
int child_pid;
char * host;
char * pid;
char * x;
char * sp;
static char * port;
static char * sla;

    strcpy(wdbp->narrative, "async_handle");
    if (port == NULL && (port = getenv("E2_HOME_PORT")) == NULL)
        port = "5000";
    if (sla == NULL && (sla = getenv("E2_SLA_SIG_PCT")) == NULL)
        sla = "95";
/*
 * Clear away all the old monitor stuff
 */
    if (!strcmp(url, "zap"))
    {
        for (i = 0; i < wdbp->root_wdbp->client_cnt; i++)
        {
            if (wdbp->root_wdbp->client_array[i].mon_target.in_use
             && !strcmp(wdbp->root_wdbp->client_array[i].username, wdbp->username)
             && !strcmp(wdbp->root_wdbp->client_array[i].passwd, wdbp->passwd))
            {
                if (wdbp->debug_level)
                    fprintf(stderr, "zapping PID %d\n",
                              wdbp->root_wdbp->client_array[i].mon_target.child_pid);
                wdbp->root_wdbp->client_array[i].session[0] = '\0';
#ifdef UNIX
                kill(wdbp->root_wdbp->client_array[i].mon_target.child_pid, 15);
#endif
                close(wdbp->root_wdbp->client_array[i].mon_target.pipe_out);
                close(wdbp->root_wdbp->client_array[i].mon_target.pipe_in);
                free(wdbp->root_wdbp->client_array[i].mon_target.target);
                free(wdbp->root_wdbp->client_array[i].mon_target.host);
                wdbp->root_wdbp->client_array[i].mon_target.in_use = 0;
            }
        }
        smart_write(f, "HTTP/1.1 200 OK\r\nContent-Length: 59\r\n\r\n\
<html><body><h1>All Monitors Terminated!</h1></body></html>", 98, 1, wdbp); 
        return NULL;
    }
    if (strncmp(url, "monitor/", 8))
        return url;          /* Not one of ours */
    url += 8;
    fprintf(stderr, "Incoming URL: %s\n", url);
/*
 * Look for the url in the list.
 * If found, output a prod.
 * If not found, spawn a handler
 * Then spawn a child to wait for the results.
 */ 
    for (first_free = -1, i = 0; i < wdbp->root_wdbp->client_cnt; i++)
    {
        if (wdbp->root_wdbp->client_array[i].mon_target.in_use)
        {
/*
 * See if this is a monitor refresh
 */
            if (!strcmp(wdbp->root_wdbp->client_array[i].mon_target.target, url))
                break;
/*
 * See if this is a child of our monitor
 */
            if ((x = check_child_url(&wdbp->root_wdbp->client_array[i].mon_target, url)) != NULL)
            {
                if (!strncmp(x, "web_path_web", 12))
                    web_file_send_static(f, fopen(x, "rb"), x, 0, wdbp);
                else
                {
                    sprintf(fbuf, "%.512s/%.256s", wdbp->root_wdbp->client_array[i].mon_target.dir, x);
                    web_file_send_static(f, fopen(fbuf, "rb"), fbuf, 0, wdbp);
                }
                return NULL;
            }
        }
        else
        if (first_free == -1)
            first_free = i;
    } 
    if (i >= wdbp->root_wdbp->client_cnt)
    {
/*
 * A monitor pipeline doesn't exist yet; create one
 */
        if (first_free == -1)
        {
            smart_write(f, "HTTP/1.1 200 OK\r\nContent-Length: 53\r\n\r\n\
<html><body><h1>Too Many Monitors!</h1></body></html>", 92, 1, wdbp); 
                return NULL;
        }
        wdbp->root_wdbp->client_array[first_free].mon_target.target = strdup(url);
        wdbp->root_wdbp->client_array[first_free].mon_target.host = strdup(url);
        wdbp->root_wdbp->client_array[first_free].mon_target.host =
           strtok_r( wdbp->root_wdbp->client_array[first_free].mon_target.host, "/?", &sp);
        if ((wdbp->root_wdbp->client_array[first_free].mon_target.pid =
                    strtok_r(NULL, "/?", &sp)) == NULL
         || (x =  strtok_r(NULL, "&", &sp)) == NULL
         || strncmp(x, "dir=", 4))

        {
            smart_write(f, "HTTP/1.1 200 OK\r\nContent-Length: 55\r\n\r\n\
<html><body><h1>Invalid Monitor URL!</h1></body></html>", 94, 1, wdbp); 
                return NULL;
            free(wdbp->root_wdbp->client_array[first_free].mon_target.target);
            free(wdbp->root_wdbp->client_array[first_free].mon_target.host);
            return NULL;              /* No Run ID or Directory */ 
        }
        wdbp->root_wdbp->client_array[first_free].mon_target.dir = x + 4;
        wdbp->root_wdbp->client_array[first_free].mon_target.sample = "N";
        wdbp->root_wdbp->client_array[first_free].mon_target.port = port;
        wdbp->root_wdbp->client_array[first_free].mon_target.sla = sla;
        if (!spawn_monitor(&wdbp->root_wdbp->client_array[first_free]))
        {
            free(wdbp->root_wdbp->client_array[first_free].mon_target.target);
            free(wdbp->root_wdbp->client_array[first_free].mon_target.host);
            smart_write(f, "HTTP/1.1 200 OK\r\nContent-Length: 57\r\n\r\n\
<html><body><h1>Monitor Launch Failed!</h1></body></html>", 96, 1, wdbp); 
            return NULL;
        }
        wdbp->root_wdbp->client_array[first_free].mon_target.in_use = 1;
        i = first_free;
        if (wdbp->debug_level)
            fprintf(stderr, "Created monitor pipeline for %s:%s\n",
                          wdbp->root_wdbp->client_array[i].mon_target.host,
                          wdbp->root_wdbp->client_array[i].mon_target.pid);
    }
    else
    {
        if (wdbp->debug_level)
            fprintf(stderr, "Prodding existing monitor pipeline for %s:%s\n",
                          wdbp->root_wdbp->client_array[i].mon_target.host,
                          wdbp->root_wdbp->client_array[i].mon_target.pid);
        write(wdbp->root_wdbp->client_array[i].mon_target.pipe_out,"Go\n",3); /* Prod */
    }
/*
 * This doesn't work any more, because we are now SSL and multi-threaded, and
 * so we cannot switch wdbp willy-nilly. We need to pass both the 'active'
 * wdbp and the one associated with the monitor process.
 */ 
    wdbp->root_wdbp->client_array[i].mon_target.out_fd = f;
#ifdef UNIX
/*
 * Tidy up any zombies
 */
    while (waitpid(0,0,WNOHANG) > 0);
#endif
/*
 * At this point, we have a pipeline identified; attempt to kick off a handler
 */
    serve_monitor(& wdbp->root_wdbp->client_array[i].mon_target, wdbp);
    return NULL;
}
/*
 * Naively handle a GET
 */
static char * attempt_get(f, buf, bound, wdbp)
int f;
char * buf;
char * bound;
WEBDRIVE_BASE * wdbp;
{
char * xp1;
FILE * fp;
int n;

    strcpy(wdbp->narrative, "attempt_get");
    while (*buf =='/' || *buf =='\\')
        buf++;
    if ((n = sec_check(buf, 0, wdbp)) == 403)
    {
        smart_write(f, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp);
        return NULL;
    }
    else
    if (n != 404 && (fp = fopen(buf, "rb")) != NULL)
    {
        web_file_send_static(f, fp, buf, 0, wdbp);
        return NULL;
    }
    else
    if (n == 404)
        return async_handle(f, buf, wdbp);
    return buf;
}
/*
 * Naively handle a POST that has to be a single file upload.
 * -  The HTML form 'action' has to actually be the file name.
 * -  Anything other than the apparent contents of the file is simply ignored.
 */
static char * attempt_post(f, buf, bound, mess_len, wdbp)
int f;
char * buf;
char * bound;
int mess_len;
WEBDRIVE_BASE * wdbp;
{
char * ehp;
char * bp;
char * cp;
char * xp;
struct bm_table * boundp;
int cont_len;
int n;
FILE * fp;
char * fbuf;
/*
 * Check that the POST identifies the file name
 */
    while (*buf =='/' || *buf =='\\' || *buf == ' ')
    {
        buf++;
        mess_len--;
    }
    if (sec_check(buf, 1, wdbp) == 403)
    {
        smart_write(f, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp);
        return NULL;
    }
    strcpy(wdbp->narrative, "attempt_post");
/*
 * We have made sure that we have the whole message, so this shouldn't fail ...
 * -  Search for the boundary marker
 * -  Search for the length
 * -  Work out the length
 */
    if ((ehp = bm_match(wdbp->ehp, buf, buf + mess_len) + 4) < (char *) 5
      || (bp = bm_match(wdbp->boundaryp, buf, ehp) + 9) < (char *) 10
     || (cp = bm_match(wdbp->sbp, buf, ehp) + 16) < (char *) 17
     || (cont_len = atoi(cp)) <= 0)
    {
        smart_write(f, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp);
        return NULL;
    }
/*
 * Search for the boundary
 */
    boundp = bm_compile_bin(bp, strchr(bp,'\r') - bp);
    if ((xp = bm_match(boundp, ehp, bound)) == NULL)
    {
        smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
        bm_zap(boundp);
        return NULL;
    }
    bm_zap(boundp);
/*
 * Write out the region between them.
 */
    if ((fp = fopen(buf, "wb")) != NULL)
    {
        while(*xp != '\r')
            xp--;
        fwrite(ehp, sizeof(char), (xp - ehp), fp);
        fclose(fp);
        smart_write(f, "HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n", 54, 1,wdbp);
        return NULL;
    }
    smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
    return NULL;
}
/*
 * Attempt to deal with requests that specify files.
 * -   Return start of URN, minus any http: stuff if can't handle
 * -   Return NULL if dealt with
 * Simple minded file type specification.
 */
char * attempt_request(f, buf, bound, mess_len, wdbp)
int f;
char * buf;
char * bound;
int mess_len;
WEBDRIVE_BASE * wdbp;
{
char * xp;
char * xp1;
FILE * fp;
int n;

    strcpy(wdbp->narrative, "attempt_request");
    if ((xp = strchr(buf, ' ')) == NULL)
        return buf;       /* Should not happen */
    xp++;
    if (!strncmp(xp, "http://",7)
       && (xp = strchr(xp +7, '/')) == NULL)
        xp = buf + 5;
    else
        xp++;
    *bound = '\0';
    if (bound == xp)
        return xp;
    if (!strncmp(buf, "PUT ", 4))
        return attempt_put(f, xp, bound, mess_len - (xp - buf), wdbp);
    else
    if (!strncmp(buf, "GET ", 4) && strcmp(xp, "/"))
        return attempt_get(f, xp, bound, wdbp);
    else
    if (!strncmp(buf, "POST ", 5))
        return attempt_post(f, xp, bound, mess_len - (xp - buf), wdbp);
    return xp;    
}
/*
 * Open an output fifo. dup()'ing to stdout cannot work in this
 * multi-threaded case.
 */
static int open_output(wdbp)
WEBDRIVE_BASE * wdbp;
{
int out_fd;
struct stat sbuf;

    strcpy(wdbp->narrative, "open_output");
#ifndef MINGW32
#ifndef LINUX
    if (stat(wdbp->out_fifo, &sbuf)
     || !(S_ISFIFO(sbuf.st_mode)))
    {
        fputs("No output FIFO\n", stderr);
        return -1;
    }
#endif
#endif
    if ((out_fd = fifo_connect(wdbp->out_fifo, wdbp->out_fifo)) == -1)
    {
        fputs("Failed to connect output FIFO\n", stderr);
        return -1;
    }
#ifdef MINGW32
    _setmode(out_fd,  O_BINARY);
#endif
    return out_fd;
}
/*
 * Function to write if at all possible.
 */
static int sure_write(f,buf,len)
int f;
char * buf;
int len;
{
int so_far = 0;
int r;
int loop_detect = 0;

#ifdef DEBUG
    fprintf(stderr, "sure_write(%d, %x, %d)\n",f, (long) buf, len);
    fflush(stderr);
#endif
    do
    {
        r = write(f, buf, len);
        if (r <= 0)
        {
            loop_detect++;
#ifndef MINGW32
            if (errno == EINTR && loop_detect < 100)
                continue;
#endif
            fprintf(stderr, "sure_write(%d, %x, %d) = %d, error:%d\n",
                 f, (long) buf,  len, r, errno);
            fflush(stderr);
            return r;
        }
        else
            loop_detect = 0;
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
/*
 * Output something, re-opening if it fails. There needs to be a file open and
 * close, so this is encapsulated now.
 */
static int sure_put(obuf, wdbp)
char * obuf;
WEBDRIVE_BASE * wdbp;
{
int out_fd;
int i;
struct timespec ts;

    ts.tv_sec = 1;
    ts.tv_nsec = 0;

    strcpy(wdbp->narrative, "sure_put");
    for (i = 0, out_fd = open_output(wdbp);
            i < 10 ;
                out_fd = open_output(wdbp), i++)
    {
        if (out_fd == -1)
        {
            nanosleep(&ts, NULL);
            continue;
        }
        if (sure_write(out_fd, obuf, strlen(obuf)) < 1) 
        {
        (void) fprintf(stderr,
        "(Client:%s) Cannot write(%s) to command output FIFO, Message Sequence %d: error: %d\n",
            wdbp->parser_con.pg.rope_seq, obuf,
                   wdbp->parser_con.pg.seqX, errno);
            out_fd = -1;
        }
        else
            break;
    }
    if (out_fd > -1)
        close(out_fd);
    return ((i < 10) ? 1 : 0);
}
/*
 * Output a set of variable values extracted from the URL
 */
int output_vars(raw_string, wdbp)
char * raw_string;
WEBDRIVE_BASE * wdbp;
{
char * bound = raw_string + strlen(raw_string);
char * obuf;
char * x;
char * x1;
char * y;
int l;
char * op;

    if (wdbp->debug_level)
        fprintf(stderr, "Output vars input: %s\n", raw_string);
    for ( obuf = (char *) malloc(4 * (bound - raw_string) + 5),
          x = raw_string,
          op = obuf;
              x < bound;)
    {
        x1 = x;
        while (x < bound && *x != '=')
            x++;
        if (x >= bound)
            break;
        if (!strncmp(x1, "Submit", x - x1)
         || !strncmp(x1, "ln_cnt", x - x1))
        {
            for (x++; x < bound && *x != '&'; x++);
            x++;
            continue;
        }
        else
            x++;
        for (y = x; y < bound && *y != '&'; y++);
/*
 * x and y delimit a variable. It will be output:
 * - unless it is called ln_cnt or Submit
 * - Un-URL-escaped
 * - Delimited by '
 * - With any embedded ' marks stuffed
 * - followed by a ' '
 */
        l = url_unescape(x, y - x);
        *op++ = '\'';
        while ( l > 0)
        {
            l--;
            *op++ = *x;
            if (*x == '\'')
            {
                *op++ = '\\';
                *op++ = *x;
                *op++ = *x;
            }
            x++;
        }
        *op++ = '\'';
        *op++ = ' ';
        x = y+1;
    }
    if (op > obuf)
    {
        *op-- = '\0';
        *op = '\n';
    }
    else
    {
        *op++ ='\n';
        *op = '\0';
    }
    if (wdbp->debug_level)
        fprintf(stderr, "Output vars output: %s\n", obuf);
    l = sure_put(obuf, wdbp);
    free(obuf);
    return l;
}
/*
 * Pass a request off to the thread that currently owns the session.
 ****************************************************************************
 * There is nothing to protect the data passed on, which is hooked up to the
 * current wdbp, from being over-written by further activity on this slot.
 ****************************************************************************
 * It turns out we don't need this for now ...
 ****************************************************************************
 * If the incoming data is bigger than WORKSPACE, it is going to leak, methinks.
 */
static int hand_off(msg, sess_wdbp, wdbp)
union all_records * msg;
WEBDRIVE_BASE * sess_wdbp;
WEBDRIVE_BASE * wdbp;
{
/*
 * Race condition. We don't want threads to exit whilst we are trying to
 * hand off to them, but we take no steps to prevent it. It shouldn't be a
 * problem in normal usage, however.
 ***************************************************************************
 * Hook our message up to tbuf
 */
    strcpy(wdbp->narrative, "hand_off");
    *((union all_records *) sess_wdbp->parser_con.tbuf) = *msg;
    msg->send_receive.msg->buf[strlen(msg->send_receive.msg->buf)] = ' ';
/*
 * Set a flag indicating that tbuf holds a message
 */
    sess_wdbp->parser_con.break_flag = 1;
/*
 * Put our socket in the link.
 */
    wdbp->link_det[3] = *(sess_wdbp->cur_link);
    *(sess_wdbp->cur_link) = *(wdbp->cur_link);
    *(wdbp->cur_link) = wdbp->link_det[3];
/*
 * Cannot close the existing socket here whilst the thread we want to interrupt
 * is in SSL_read(), because a side effect of link_clear() is that the SSL
 * structure SSL v3 status structure pointer is cleared, so when the interrupted
 * code goes to update its SSL v3 statistics it crashes. Thus, the clearout
 * needs to happen after the signal (and the target has awoken).
 *
 * Send a time-out signal to the owning thread.
 */
    if (!pthread_equal(sess_wdbp->own_thread_id, zeroth))
    {
        if (wdbp->debug_level>2)
        {
            (void) fprintf(stderr,
                  "(Client:%s) Handing off (%s) to active (%lx)\n",
                      wdbp->parser_con.pg.rope_seq,
                      msg->send_receive.msg->buf,
                      (unsigned long) sess_wdbp);
        }
/*
 * The signal doesn't interrupt the read. Don't know why ...
 */
        pthread_kill(sess_wdbp->own_thread_id, SIGIO);
        if (wdbp->cur_link->connect_fd != -1);
            closesocket(wdbp->cur_link->connect_fd);
/*
 * This waits until the recipient picks up the (zero-length) message
 */
        pipe_buf_add(sess_wdbp->pbp, 1, 0, NULL, NULL, 1);
/*
 *      link_clear(wdbp->cur_link, wdbp);
 * Attempt to carry on regardless
 */
        return 1;
    }
    else
    {
        link_clear(wdbp->cur_link, wdbp);
        if (wdbp->debug_level>2)
        {
            (void) fprintf(stderr,
               "(Client:%s) Handing off (%s) to dormant (%lx) for scheduling\n",
                      wdbp->parser_con.pg.rope_seq,
                      msg->send_receive.msg->buf,
                      (unsigned long) sess_wdbp);
        }
        add_time(wdbp->root_wdbp, sess_wdbp, 0);
    }
/*
 * Release the mutex ... which mutex???
 *
 *  pthread_mutex_unlock(&(wdbp->root_wdbp->idle_thread_mutex));
 */
    return 0;
}
/*
 * Display some fields from the client array
 */
static int thread_dump(out_fd, wdbp)
int out_fd;
WEBDRIVE_BASE * wdbp;
{
WEBDRIVE_BASE * xwdbp;
int i;
char * x = (char *) (&wdbp->ret_msg);
char * bound = x + WORKSPACE;

    x += sprintf(x,
      "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\
Content-Length: 10000\r\n\r\n\
<html><body>\n\
<h1>Current Thread Status (Possible Clients: %d)</h1>\n\
<table><tr><th>Adopt?</th><th>Slot</th><th>Thread</th><th>Session</th><th>User</th><th>Last Function</th><tr>",
      wdbp->root_wdbp->client_cnt);
    for (xwdbp = wdbp->root_wdbp->client_array, i = 0;
            i < wdbp->root_wdbp->client_cnt && x < bound;
                i++, xwdbp++)
    {
        x += sprintf(x,"<tr><td>%s%s%s</a></td><td>%d</td><td>%lx</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
            (xwdbp->session[0] == '\0')?"":"<a href=\"/E2ADOPT?E2SESSION=",
            (xwdbp->session[0] == '\0')?"":xwdbp->session,
            (xwdbp->session[0] == '\0')?"":"\">+</a>",
            i, (unsigned long)
#ifdef MINGW32
            xwdbp->own_thread_id.hthread,
#else
            xwdbp->own_thread_id,
#endif
            xwdbp->session, xwdbp->username, xwdbp->narrative);
    }
/*
 * We have a WORKSPACE worth of over-run, so having gone out of bounds is
 * not a problem
 */
    if (i < wdbp->root_wdbp->client_cnt)
        x += sprintf(x,
      "<tr><td span=5>... not enough buffer to show all of them</td></tr>");
    x += sprintf(x, "</table><a href=\"/\">Return to menu</a></body></html>\n");
    adjust_content_length(wdbp,(char *) (&wdbp->ret_msg), &x);
    smart_write(out_fd, (char *) (&wdbp->ret_msg), x - 
                            (char *) (&wdbp->ret_msg) , 1, wdbp);
    return 1;
}
/*
 * Pick up a session that is currently running
 */
static int adopt_session(out_fd, msg, wdbp)
int out_fd;
union all_records * msg;
WEBDRIVE_BASE * wdbp;
{
char session[64];
char buf[64];
int len;

    fish_things(msg->send_receive.msg->buf,
                &msg->send_receive.msg->buf[msg->send_receive.message_len],
                    wdbp->ebp, session, 49);
    len = sprintf(wdbp->in_buf.buf,
           "HTTP/1.1 302 Moved Temporarily\r\nLocation: /\r\nSet-Cookie: E2SESSION=%s;HttpOnly;Secure\r\nContent-length: 0\r\n\r\n", session);
    smart_write(out_fd, wdbp->in_buf.buf, len, 1, wdbp);
    return 1;
}
/*
 * Process a request.
 * Request to be processed is msg->send_receive.msg,
 *                            msg->send_receive.message_len
 */
int do_web_request(msg, wdbp, sess_wdbp)
union all_records * msg;
WEBDRIVE_BASE * wdbp;
WEBDRIVE_BASE * sess_wdbp;
{
int in_fd = -1;
unsigned int u;
char * bound_p;
char * ret_p;
int f = (int) wdbp->parser_con.pg.cur_in_file;
FILE * ffp;
int sess_flag;
#ifdef TUNDRIVE
char ipbuf[128];
int len;
#endif

    if (wdbp->debug_level > 1)
    {
        (void) fprintf(stderr,
        "(Client:%s) Processing Web Request Message Sequence %d\n",
            wdbp->parser_con.pg.rope_seq,
                   wdbp->parser_con.pg.seqX);
        fflush(stderr);
    }
    strcpy(wdbp->narrative, "do_web_request");
    if (msg->send_receive.message_len > 8 && !strncmp(msg->send_receive.msg->buf, "OPTIONS ", 8))
    {
        smart_write(f,
"HTTP/1.1 200 OK\r\nAccess-Control-Allow-Methods: PUT, GET, POST\r\nAccess-Control-Allow-Origin: *\r\nContent-length: 0\r\n\r\n", 116, 1, wdbp); 
        return 1;
    }
    else
    {
        if (msg->send_receive.message_len < 16
         || (strncmp(msg->send_receive.msg->buf,  "GET ", 4)
          && strncmp(msg->send_receive.msg->buf,  "PUT ", 4)
          && strncmp(msg->send_receive.msg->buf,  "POST ", 5))
         || ((bound_p = memchr( msg->send_receive.msg->buf, '\r',
                             msg->send_receive.message_len)) == NULL)
         || bound_p < (&msg->send_receive.msg->buf[5]))
        {
            smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
            return 1;
        }
        if (!strcmp(msg->send_receive.msg->buf, "GET /E2STATUS"))
            return thread_dump(f, wdbp);
#ifdef TUNDRIVE
/*
 * Have to dole out an IP address
 */
        if (! strncmp(wdbp->in_buf.buf, "GET /about HTTP", 15))
        {
            smart_write((int) wdbp->parser_con.pg.cur_in_file, "HTTP/1.1 200 OK\r\n\
Content-type: text/html\r\n\
Connection: close\r\n\
\r\n\
<html><body><p>Why does President Hollande only have one egg for breakfast?</p>\n\
<p>Because one egg is 'un oeuf'.</p>\n\
<p>Did you hear about the three french cats swimming the channel?</p>\n\
<p>Un, deux, trois, cat sank :(</p>\n\
</body>\n\
</html>\n", 302, 1, wdbp);
            socket_cleanup(wdbp);
            wdbp->cur_link->connect_fd = -1;
            return 0; /* Mustn't use this socket again */
        }
        else
        if (wdbp->out_fifo == NULL)
        {
            if (wdbp == sess_wdbp)
            {
                if (tun_create( wdbp->control_file, /* Tunnel device */
                        wdbp->tunnel_ip, wdbp) < 0)
                {
                    smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
                    return 1;
                }
/*
 * Increment the IP address for the other end. We are limited to 254.
 */
                wdbp->out_fifo = strdup(wdbp->tunnel_ip);
                u = inet_addr(wdbp->tunnel_ip);
                u += 0x1000000; /* Little endian assumed */
/*
 * Increment the tunnel device number
 */
                len =  strlen(wdbp->control_file);
                if (*(wdbp->control_file + len - 1) < 57)
                    *(wdbp->control_file + len - 1) += 1;
                else
                {
                    wdbp->control_file = realloc(wdbp->control_file,len + 2);
                    for (ret_p = wdbp-> control_file + len - 1;
                            ret_p >= wdbp-> control_file
                          && *ret_p >= 48
                          && *ret_p <= 57;
                             ret_p--);
                    ret_p++;
                    len = atoi(ret_p) + 1;
                    sprintf(ret_p, "%u", len);
                }
                e2inet_ntoa_r(u, &ipbuf[0]);  /* Value to be returned */
                len = sprintf(wdbp->parser_con.tbuf,
"HTTP/1.1 200 OK\r\n\
Content-type: text/plain\r\n\
Set-Cookie: E2SESSION=%s;Path=/;HttpOnly;Secure\r\n\
Connection: close\r\n\
\r\n\
%s",    wdbp->session, ipbuf);
                u -= 0x1000000; /* Little endian assumed */
                u += 0x10000;
                e2inet_ntoa_r(u, &ipbuf[0]);  /* Value to be used next */
                if (strlen(ipbuf) <= strlen(wdbp->tunnel_ip))
                    strcpy(wdbp->tunnel_ip,ipbuf);
                else
                {
                    free(wdbp->tunnel_ip);
                    wdbp->tunnel_ip = strdup(ipbuf);
                }
                if (smart_write(f, wdbp->parser_con.tbuf, len, 1, wdbp) == len)
                {
                    socket_cleanup(wdbp);
                    wdbp->cur_link->connect_fd = -1;
                }
                return 0; /* Mustn't use this socket again */
            }
        }
/*
 * Send to tunnel if necessary
 */
        if ((ret_p = bm_match(wdbp->ehp,
                              msg->send_receive.msg->buf,
                              msg->send_receive.msg->buf +
                              msg->send_receive.message_len))
               != (unsigned char *) NULL
            && ret_p < ( msg->send_receive.msg->buf +
                              msg->send_receive.message_len - 4))
            weboutrec(sess_wdbp->mirror_fd, ret_p + 4, OUT,
              ((msg->send_receive.msg->buf +
                 msg->send_receive.message_len) - (ret_p + 4)), wdbp);
/*
 * Read the tunnel data and return it
 */
        smart_write(f, wdbp->in_buf.buf,
            webinrec(sess_wdbp->mirror_fd, wdbp->in_buf.buf, IN, sess_wdbp),
              1, wdbp);
    }
    return 1;
#else
        else
        if ((ret_p = attempt_request(f, msg->send_receive.msg->buf, bound_p - 9,
                     msg->send_receive.message_len, wdbp)) == NULL)
            return 1;
        else
        if (!strcmp(msg->send_receive.msg->buf, "GET /E2STATUS"))
            return thread_dump(f, wdbp);
        else
        if (!strncmp(msg->send_receive.msg->buf, "GET /E2ADOPT?", 13))
            return adopt_session(f, msg, wdbp);
        if (wdbp->out_fifo == NULL)
        {
            if (wdbp == sess_wdbp)
            {
                if (!webshell_launch(wdbp))
                {
                    smart_write(f, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 1, wdbp); 
                    return 1;
                }
                sess_flag = 1;
            }
            else
            {
/*
 * We have a request for a specific session that has arrived on a fresh socket,
 * probably. We need to pass the socket details and the new message over to
 * the session that it relates to. We don't need a queue; there cannot be
 * multiple requests stacked up.
 *
 * If the session still has an owning thread, we need it to abandon whatever
 * it is doing and take over.
 *
 * Otherwise, we submit a request for a thread to process it via the clock
 * minder.
 *
 * Which means we need a clock minder.
 *
 * -   We grab a mutex to indicate we are messing around with members of the
 *     parser_con structure.
 * -   We hook our message up to tbuf
 * -   We grab the existing 
 * -   We place our socket details in to the new session
 *
 * If it no longer has an owning session, we want to set it up, and despatch
 * a thread to deal with it, whilst exiting ourselves.
 *
 * See if we can get it to work without the hand-off logic.
 *
 *              return hand_off(msg, sess_wdbp, wdbp);
 */
                sess_flag = 0;
            }
        }
        else
            sess_flag = 0;
    }
/*****************************************************************************
 * Hand off the request to the output pipe
 */ 
    *(bound_p -9) = '\0';
    if (!output_vars(ret_p, sess_wdbp))
    {
        cleanup_session(sess_wdbp);
        smart_write(f, 
           "HTTP/1.1 302 Moved Temporarily\r\nLocation: /\r\nContent-length: 0\r\n\r\n",  66, 1, wdbp);
        return 1;
    }
    if (wdbp->debug_level > 1)
        (void) fprintf(stderr,"do_web_request(%s) ===>\n",
                       &msg->send_receive.msg->buf[5]);
/*
 * Get back the response and send it on to the caller
 */
#ifndef LINUX
    if ((in_fd = fifo_listen(sess_wdbp->in_fifo)) == -1)
        fputs("Failed to listen on input FIFO", stderr);
#endif
reconnect_fifo:
    if ((in_fd = fifo_accept(sess_wdbp->in_fifo, in_fd)) == -1)
    {
        fputs("Failed to accept input FIFO", stderr);
        smart_write(f, "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n", 48, 1, wdbp); 
    }
    else
    if ((ffp = fdopen(in_fd, "rb")) == NULL)
    {
        fputs("Failed to accept input FIFO", stderr);
        smart_write(f, "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n", 48, 1, wdbp); 
        close(in_fd);
    }
    else
    {
        if ( fgets(&wdbp->ret_msg.buf[0], WORKSPACE - 1, ffp) ==
                   &wdbp->ret_msg.buf[0])
        {
            if (wdbp->debug_level > 1)
                (void) fprintf(stderr,"file name to send(%s)\n",
                   &wdbp->ret_msg.buf[0]);
            wdbp->ret_msg.buf[strlen(&wdbp->ret_msg.buf[0]) - 1] ='\0';
            web_file_send_static(f, 
                   fopen(&wdbp->ret_msg.buf[0], "rb"),
                   &wdbp->ret_msg.buf[0], sess_flag, wdbp);
            unlink( &wdbp->ret_msg.buf[0]);
            fclose(ffp);
        }
        else
        {
            if (wdbp->debug_level > 1)
                    (void) fprintf(stderr,"Input read failed error: %d\n", 
                               errno);
            fclose(ffp);
            goto reconnect_fifo;
        }
    }
#endif
    return 1;
}
#ifndef TUNDRIVE
/*****************************************************************************
 * Launch the session script
 */
int webshell_launch(wdbp)
WEBDRIVE_BASE * wdbp;
{
int in_fd = (int) wdbp->parser_con.pg.cur_in_file;
char * npp = getenv("NAMED_PIPE_PREFIX");
char * cmdline = getenv("E2_WEBSHELL_TEMPLATE");
/*
 * Construct the name of the output FIFO
 */
    if (npp == NULL)
        npp = "";
    sprintf(wdbp->ret_msg.buf, "%sscript_in_fifo.%s", npp, wdbp->session);
    wdbp->out_fifo = strdup(wdbp->ret_msg.buf);
#ifndef MINGW32
    if (mkfifo(wdbp->out_fifo, 0600) < 0)
    {
        smart_write(in_fd, "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n", 48, 1, wdbp); 
        fputs("Failed to create output FIFO\n", stderr);
        free(wdbp->out_fifo);
        wdbp->out_fifo = NULL;
        return 0;
    }
#endif
    sprintf(wdbp->ret_msg.buf, "%sweb_fifo.%s", npp, wdbp->session);
    wdbp->in_fifo = strdup(wdbp->ret_msg.buf);
#ifndef MINGW32
    if (mkfifo(wdbp->in_fifo, 0600) < 0)
    {
        smart_write(in_fd, "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n", 48, 1, wdbp); 
        fputs("Failed to create input FIFO\n", stderr);
        free(wdbp->out_fifo);
        wdbp->out_fifo = NULL;
        free(wdbp->in_fifo);
        wdbp->in_fifo = NULL;
        return 0;
    }
#endif
    if (cmdline == NULL)
        sprintf(wdbp->ret_msg.buf, "webpath.sh %s %d >sessions/%s.log 2>&1 &", wdbp->session,
             (wdbp - &wdbp->root_wdbp->client_array[0]), wdbp->session);
    else
        sprintf(wdbp->ret_msg.buf, cmdline, wdbp->session,
             (wdbp - &wdbp->root_wdbp->client_array[0]), wdbp->session);
    system(wdbp->ret_msg.buf);
    return 1;
}
#endif
