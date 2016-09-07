/*****************************************************************************
 *  Thread scheduling and future event management
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (c) E2 Systems Limited 2000\n";
#include "webdrive.h"
static pthread_t zeroth;
static pthread_attr_t attr;
#ifdef TUNDRIVE
static struct timeval nohang = {60*SLEEP_FACTOR,0};
#else
static struct timeval nohang = {600*SLEEP_FACTOR,0};
                                   /* Wait 600 seconds timeval structure*/
#endif
static struct timeval close_idle = {60*SLEEP_FACTOR,0};
#define THREAD_MSG_PRINT(pstr, tid, msg) {\
fprintf(stderr, msg, (long) pstr);\
hex_line_out(stderr, (char *) &((pstr)->tid),\
((char *) &((pstr)->tid)) + sizeof((pstr)->tid));\
fputc('\n', stderr);\
}
#ifdef DEBUG
#define THREAD_DEBUG_PRINT(pstr, tid, msg)  THREAD_MSG_PRINT(pstr, tid, msg) 
#else
#define THREAD_DEBUG_PRINT(pstr, tid, msg)
#endif
char * be_a_thread();
/*
 * Tailor the default thread attributes to our usage patterns
 */
void tailor_threads()
{
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    return;
}
/*
 * read_timeout(); interrupt a network read that is taking too long
 */
void read_timeout()
{
#ifdef MINGW32
    if (webdrive_base.own_thread_id.thread_id != 0
     && webdrive_base.own_thread_id.thread_id != GetCurrentThreadId())
#else
    if (!pthread_equal(webdrive_base.own_thread_id, zeroth)
     && !pthread_equal(webdrive_base.own_thread_id, pthread_self()))
#endif
        pthread_kill(webdrive_base.own_thread_id, SIGALRM);
    webdrive_base.alert_flag = 1;   /* Hmmm... what uses this??? */
#ifdef MINGW32
    (void) sigset(SIGALRM, read_timeout);
    (void) sigset(SIGPIPE, read_timeout);
    (void) sigset(SIGIO, read_timeout);
#endif
    return;
}
/*
 * catch_sigio(); receive note to pack up waiting for the network and go home.
 */
void catch_sigio()
{
    fflush(stdout);
    fputs("catch_sigio()\n", stderr);
    fflush(stderr);
#ifdef MINGW32
    (void) sigset(SIGIO, catch_sigio);
#endif
    return;
}
/*
 * Despatch a thread, taking it either from the idle list or if the idle list
 * is empty, launch a new one. Beware:
 * -   There is only one idle list. idle_wdbp must be a grandchild of the root.
 * -   Bad things will happen if wdbp is already being used.
 */
void despatch_a_thread(wdbp, idle_wdbp)
WEBDRIVE_BASE * wdbp;      /* The new task's environment */
WEBDRIVE_BASE * idle_wdbp; /* Home of the idle thread pool */
{
struct idle_thread * itp;

    pthread_mutex_lock(&(idle_wdbp->root_wdbp->idle_thread_mutex));
    wdbp->alert_flag = 0;
    wdbp->root_wdbp = idle_wdbp;   /* Become grandchild */
    if (!pthread_equal(zeroth, wdbp->own_thread_id))
        fprintf(stderr, "Thread allocation for wdbp=%lx screwed up, it isn't free\n",
                        (unsigned long) wdbp);
    if (idle_wdbp->idle_threads->buf_cnt == 0)
    {
        idle_wdbp->thread_cnt++;
/*
 * This isn't supposed to return until the thread ID is allocated, but it seems
 * this may not be the case on 32 bit Ubuntu 12.04 LTS on a uniprocessor ...
 */
        pthread_create(&(wdbp->own_thread_id),
                              &attr, be_a_thread, wdbp);
        THREAD_DEBUG_PRINT(wdbp, own_thread_id, "New thread to own %x - ");
    }
    else
    {
        circbuf_take(idle_wdbp->idle_threads, (char **) &itp);
        itp->wdbp = wdbp;
        wdbp->own_thread_id = itp->thread_id;
        THREAD_DEBUG_PRINT(wdbp, own_thread_id, "Existing thread to own %x - ");
        pthread_cond_signal(&(itp->cond));
    }
    pthread_mutex_unlock(&(idle_wdbp->root_wdbp->idle_thread_mutex));
    return;
}
/*
 * Locate a free slot and use it to proxy
 */
int find_a_slot(dp, f)
WEBDRIVE_BASE * dp;
int f;
{
int i;
static WEBDRIVE_BASE * alloc_wdbp;
struct idle_thread * itp;
int second_flag = 0;

    dp = dp->root_wdbp;
again:
/*
 * Need to hold a lock to do this.
 */
    pthread_mutex_lock(&(dp->idle_thread_mutex));
    if (alloc_wdbp == NULL)
        alloc_wdbp = &dp->root_wdbp->client_array[0];
    i = dp->root_wdbp->client_cnt;
    for (;i > 0; i--)
    {
        if (pthread_equal(alloc_wdbp->own_thread_id, zeroth))
        {
            fprintf(stderr, "Slot %x is apparently free - allocating\n",
                      (long) alloc_wdbp);
            alloc_wdbp->parser_con.pg.cur_in_file = (FILE *) f;
            alloc_wdbp->alert_flag = 0;
            if (dp->idle_threads->buf_cnt == 0)
            {
                dp->thread_cnt++;
/*
 * This isn't supposed to return until the thread ID is allocated, but it seems
 * this may not be the case on 32 bit Ubuntu 12.04 LTS on a uniprocessor ...
 */
                pthread_create(&(alloc_wdbp->own_thread_id),
                              &attr, be_a_thread, alloc_wdbp);
                THREAD_DEBUG_PRINT(alloc_wdbp, own_thread_id,
                   "New thread to own %x - ");
            }
            else
            {
                circbuf_take(dp->idle_threads, (char **) &itp);
                itp->wdbp = alloc_wdbp;
                alloc_wdbp->own_thread_id = itp->thread_id;
                THREAD_DEBUG_PRINT(alloc_wdbp, own_thread_id,
                       "Existing thread to own %x - ");
                pthread_cond_signal(&(itp->cond));
            }
            dp->active_cnt++;
            break;
        }
        THREAD_DEBUG_PRINT(alloc_wdbp, own_thread_id,
                   "Slot owned by thread %x - ");
        alloc_wdbp++;
        if (alloc_wdbp >=
                &dp->root_wdbp->client_array[dp->root_wdbp->client_cnt - 1])
            alloc_wdbp = (&dp->root_wdbp->client_array[0]);
    }
    alloc_wdbp++;
    if (alloc_wdbp >=
                &dp->root_wdbp->client_array[dp->root_wdbp->client_cnt - 1])
        alloc_wdbp = (&dp->root_wdbp->client_array[0]);
    pthread_mutex_unlock(&(dp->idle_thread_mutex));
    if (i > 0)
        return 1;
    else
    {
/*
 * If we have run out of slots, make the timeout more aggressive and time
 * existing ones out. Can we do this without even worse racing???
 */
        if (second_flag)
            return 0;
        pthread_mutex_lock(&(dp->idle_thread_mutex));
        second_flag = 1;
        if (close_idle.tv_sec > 20)
            close_idle.tv_sec >>= 1;
        if (nohang.tv_sec > 20)
            nohang.tv_sec >>= 1;
        for (alloc_wdbp = &dp->root_wdbp->client_array[0],
             i = dp->root_wdbp->client_cnt;
                 i > 0;
                       alloc_wdbp++, i--)
            if (!pthread_equal(alloc_wdbp->own_thread_id, zeroth)
              && !strcmp(alloc_wdbp->narrative, "smart_read"))
                add_time(dp, alloc_wdbp, 0);
        alloc_wdbp = &dp->root_wdbp->client_array[0];
        pthread_mutex_unlock(&(dp->idle_thread_mutex));
        goto again;
    }
}
#ifdef USE_SSL
#ifndef TUNDRIVE
/*************************************************************************
 * Toggle SSL translation
 * - We need to switch all the current links to the other one.
 * - We need to clean up/shut down existing threads
 *************************************************************************
 * Originally, we had a race condition if anything was in flight, which
 * caused crashes when an HTTP connection turned in to an HTTPS connection.
 * So we need to only toggle when the slot is idle. Thus, we need to lock
 * the idle queue.
 *************************************************************************
 * This was moved here from sdebug.c to gain access to zeroth
 */
int toggle_ssl(wdbp)
WEBDRIVE_BASE * wdbp;
{
WEBDRIVE_BASE * xwdbp;
int which;
int i;

    pthread_mutex_lock(&(wdbp->idle_thread_mutex));
    pthread_mutex_lock(&(wdbp->script_mutex));
    if (wdbp->not_ssl_flag == 3)
        which = 1;
    else
        which = 3;
    wdbp->cur_link = &wdbp->link_det[which];
    for (xwdbp = wdbp->client_array, i = wdbp->client_cnt;
          i > 0; i--, xwdbp++)
    {
        if (pthread_equal(xwdbp->own_thread_id, zeroth))
        {
            if (xwdbp->link_det[which].connect_fd != -1)
            {
                add_close(&wdbp->sc, &xwdbp->link_det[which]);
                link_clear( &xwdbp->link_det[which], xwdbp);
            }
            xwdbp->cur_link = &xwdbp->link_det[wdbp->not_ssl_flag];
        }
    }
    pthread_mutex_unlock(&(wdbp->idle_thread_mutex));
    pthread_mutex_unlock(&(wdbp->script_mutex));
    return;
}
#endif
#endif
/***************************************************************************
 * Clock functions
 */
void zap_time(dp, wdbp)
WEBDRIVE_BASE *dp;
WEBDRIVE_BASE *wdbp;
{
int cur_ind;
int next_ind;
struct go_time sav_time;
long new_time;
int bcnt;
int head;
int tail;

    strcpy(wdbp->narrative, "zap_time");
    if (dp->debug_level > 1)
        (void) fprintf(stderr,"zap_time(): wdbp %x\n", (long) wdbp);
    pthread_mutex_lock(&(dp->go_time_mutex));
    bcnt = dp->go_times->top - dp->go_times->base ;
    tail = (dp->go_times->tail - (volatile char**) dp->go_times->base);
    head = (dp->go_times->head - (volatile char**) dp->go_times->base);
/*
 * Get rid of duplicates
 */
    for (cur_ind = tail, next_ind = cur_ind;
            ;
                cur_ind = (cur_ind + 1) % bcnt,
                next_ind = (next_ind + 1) % bcnt)
    {
        while (next_ind != head
         && ((struct go_time *)(dp->go_times->base[next_ind]))->wdbp == wdbp)
        {                   /* Match, increment skip count */
            free(dp->go_times->base[next_ind]);
            next_ind = (next_ind + 1) % bcnt;
            dp->go_times->buf_cnt--;
        }
        if (next_ind == head)
        {               /* Run out, break */
            dp->go_times->head = dp->go_times->base + cur_ind;
            break;
        }
        if (next_ind != cur_ind)
        {                   /* Are we skipping? */
            dp->go_times->base[cur_ind] = dp->go_times->base[next_ind];
                            /* Pull down the next */
        }
    }
    pthread_mutex_unlock(&(dp->go_time_mutex));
    return;
} 
/*
 * add_time();  add a new time for the wdbp. Re-start the clock.
 */
void add_time(dp, wdbp, delta)
WEBDRIVE_BASE *dp;
WEBDRIVE_BASE *wdbp;
int delta;
{
int cur_ind;
int next_ind;
time_t t;
struct go_time * sav_time;
struct go_time * nxt_time;
time_t new_time;
int bcnt;
int head;
int tail;

    strcpy(wdbp->narrative, "add_time");
    if (dp->debug_level > 1)
        (void) fprintf(stderr,"add_time(): wdbp %x delta %d\n",
                  (long) wdbp, delta);
    zap_time(dp, wdbp);
    pthread_mutex_lock(&(dp->go_time_mutex));
    bcnt = dp->go_times->top - dp->go_times->base ;
    tail = (dp->go_times->tail - (volatile char **) dp->go_times->base);
    head = (dp->go_times->head - (volatile char **) dp->go_times->base);
    t = time((long *) 0);
    new_time = t + delta;
/*
 * Find the correct place to insert
 */
    for (cur_ind = tail;
            cur_ind != head;
                cur_ind = (cur_ind + 1) % bcnt)
    {
        if (((struct go_time *)(dp->go_times->base[cur_ind]))->go_time >=
                                  new_time)
            break;
    }
/*
 * Need to shift up
 */
    nxt_time = (struct go_time *) malloc(sizeof(struct go_time));
    nxt_time->wdbp = wdbp;
    nxt_time->go_time = new_time;
/*
 * Think about replacing alarm() with something with a higher resolution
 */
    if (cur_ind == tail)
        alarm(delta);
    for (; cur_ind != head ; cur_ind = (cur_ind + 1) % bcnt)
    {
        sav_time = (struct go_time *) (dp->go_times->base[cur_ind]);
        dp->go_times->base[cur_ind] = (char *) nxt_time;
        nxt_time = sav_time;
    }
    dp->go_times->base[head] = (char *) nxt_time;
    dp->go_times->head = dp->go_times->base + ((head + 1) % bcnt);
    dp->go_times->buf_cnt++;
    pthread_mutex_unlock(&(dp->go_time_mutex));
    if (delta == 0)
        pthread_kill(dp->own_thread_id, SIGALRM);
    return;
}
/*
 * Do useful work as a thread
 */
char * be_a_thread(wdbp)
WEBDRIVE_BASE *wdbp;
{
struct idle_thread  *itp;
#ifdef MINGW32
MSG msg;

    (void) PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
                    /* Make sure this thread has a message queue */
#endif
    itp = (struct idle_thread *) malloc(sizeof(*itp));
    pthread_mutex_init(&(itp->mutex), NULL);
    pthread_cond_init(&(itp->cond), NULL);
    itp->thread_id = pthread_self();
    if (wdbp->te2mbp == NULL)
        wdbp->te2mbp = (struct e2malloc_base *) calloc(
                            1, sizeof(struct e2malloc_base));
#ifdef THREADED_MALLOC
    malloc_thread_setup(wdbp->te2mbp);
#endif
    for (;;)
    {
/*
 * Run a script or something else until asked to sleep
 */
        wdbp->progress_client(wdbp);
/*
 * Now dispose of the thread; park it or exit.
 */
        strcpy(wdbp->narrative, "be_a_thread");
        THREAD_DEBUG_PRINT(wdbp,own_thread_id,"Thread for %x for re-assignment - ");
        if (!pthread_equal(itp->thread_id, wdbp->own_thread_id))
        {
            fprintf(stderr, "Thread allocation for wdbp=%lx screwed up? Expected=", (long int) wdbp);
            hex_line_out(stderr, (char *) &((itp)->thread_id),
                    ((char *) &((itp)->thread_id)) + sizeof((itp)->thread_id));
            fputs(" but saw =", stderr);
            hex_line_out(stderr, (char *) &((wdbp)->own_thread_id),
                    ((char *) &((wdbp)->own_thread_id)) +
                            sizeof((wdbp)->own_thread_id));
            fputc('\n', stderr);
        }
        wdbp->own_thread_id = zeroth;
#ifdef THREADED_MALLOC
        malloc_thread_setup(NULL);
#endif
        pthread_mutex_lock(&(wdbp->root_wdbp->idle_thread_mutex));
/*
 * Add ourselves to the idle queue unless there are 25 or more there
 * already.
 */
        if (wdbp->root_wdbp == &webdrive_base)
            wdbp->root_wdbp->active_cnt--;
        if ( wdbp->root_wdbp->idle_threads->buf_cnt < 25)
            circbuf_add(wdbp->root_wdbp->idle_threads, (char *) itp);
        else
        {
            wdbp->root_wdbp->thread_cnt--;
            itp->thread_id = zeroth;
        }
/*
 * If we are the last active script thread, and there is nothing waiting, signal
 * exit.
 */
        if (wdbp->root_wdbp == &webdrive_base
          && wdbp->root_wdbp->active_cnt <= 0
          && wdbp->root_wdbp->go_times->buf_cnt == 0)
        {
            THREAD_DEBUG_PRINT(itp,thread_id,
                       "Thread is the last to finish; shutting down - ");
            wdbp->root_wdbp->go_away = 1;
            assert(wdbp->root_wdbp->proxy_port == 0);
            pthread_kill(wdbp->root_wdbp->own_thread_id, SIGTERM);
                             /* Wake up clockminder to call exit() */
            break;
        }
        if (pthread_equal(itp->thread_id, zeroth))
            break;                         /* Go away */
        THREAD_DEBUG_PRINT(itp,thread_id,
                   "Thread is back on idle queue - ");
        pthread_mutex_lock(&(itp->mutex));
        pthread_mutex_unlock(&(wdbp->root_wdbp->idle_thread_mutex));
/*
 * Wait to be re-allocated. itp->wdbp == NULL protects against spurious wake-ups.
 */
        for (itp->wdbp = NULL; itp->wdbp == NULL;)
            pthread_cond_wait(&(itp->cond), &(itp->mutex));
/*
 * Get ready to go again on another client
 */
        wdbp = itp->wdbp;
        pthread_mutex_unlock(&(itp->mutex));
        if (wdbp->te2mbp == NULL)
            wdbp->te2mbp = (struct e2malloc_base *) calloc(
                            1, sizeof(struct e2malloc_base));
#ifdef THREADED_MALLOC
        malloc_thread_setup(wdbp->te2mbp);
#endif
    }
/*
 * Unlock if the thread is exiting
 */
    pthread_mutex_unlock(&(wdbp->root_wdbp->idle_thread_mutex));
    free(itp);
    THREAD_DEBUG_PRINT(wdbp,own_thread_id,"Thread for %x not needed - ");
    return NULL;
}
/*
 * rem_time(); tidy up the list, removing times from the tail as they
 * expire, and scheduling clients as appropriate.
 */
void rem_time(dp)
WEBDRIVE_BASE *dp;
{
int cur_ind;
int next_ind;
long t;
struct go_time * sav_time;
struct go_time * nxt_time;
long new_time;
int bcnt;
int head;
int tail;
int sleep_int;
time_t cur_time;

    if (dp->debug_level > 1)
        (void) fprintf(stderr,"rem_time(): dp %x\n", (long) dp);
    strcpy(dp->narrative, "rem_time");
    pthread_mutex_lock(&(dp->go_time_mutex));
    bcnt = (dp->go_times->top - dp->go_times->base);
    tail = (dp->go_times->tail - (volatile char **) dp->go_times->base);
    head = (dp->go_times->head - (volatile char **) dp->go_times->base);
    cur_time = time((long *) 0);
    while ( tail != head
       && ((struct go_time *) *(dp->go_times->tail))->go_time <= cur_time )
    {                      /* Loop - process any Clients due an inspection */
    struct go_time * this_time;
    struct idle_thread * itp;

        pthread_mutex_lock(&(dp->idle_thread_mutex));
        circbuf_take(dp->go_times, (char **) &this_time);
/*
 * If a thread owns the WEBDRIVE_BASE associated with this timer expiry, is it
 * blocked? Should we signal it to give up and continue on its merry way?
 * The program fails on LINUX if we send.
 */
        if (!pthread_equal(this_time->wdbp->own_thread_id, zeroth))
        {
            this_time->wdbp->alert_flag = 1;
            pthread_kill(this_time->wdbp->own_thread_id, SIGIO);
            THREAD_DEBUG_PRINT(this_time->wdbp, own_thread_id,
                   "Timout for %x owned by  ");
            pthread_mutex_unlock(&(dp->idle_thread_mutex));
            tail = (tail + 1) % bcnt;
            continue;
        }
        if (dp == &webdrive_base)
            dp->active_cnt++;
        if (dp->idle_threads->buf_cnt == 0)
        {
            dp->thread_cnt++;
            pthread_create(&(this_time->wdbp->own_thread_id),
                              &attr, be_a_thread, this_time->wdbp);
            THREAD_DEBUG_PRINT(this_time->wdbp, own_thread_id,
                   "New thread to own %x - ");
        }
        else
        {
            circbuf_take(dp->idle_threads, (char **) &itp);
            itp->wdbp = this_time->wdbp;
            itp->wdbp->own_thread_id = itp->thread_id;
            THREAD_DEBUG_PRINT(this_time->wdbp, own_thread_id,
                   "Existing thread to own %x - ");
            pthread_cond_signal(&(itp->cond));
        }
        pthread_mutex_unlock(&(dp->idle_thread_mutex));
        tail = (tail + 1) % bcnt;
    }
    if (tail != head)
    {
        (void) sigset(SIGALRM, read_timeout);
        sleep_int = ((struct go_time *) *(dp->go_times->tail))->go_time
                        - cur_time;
        if (dp->debug_level > 1)
            fprintf(stderr,"(Client:%s) alarm(%d)\n", dp->parser_con.pg.rope_seq,
                     sleep_int);
/*
 * Think about replacing alarm() with something with a higher resolution
 */
        (void) alarm(sleep_int);
    }
    else
    {
        if (dp->active_cnt <= 0)
        {
            if (dp->debug_level > 1)
                fprintf(stderr,"(Client:%s) Finished; no more work\n",
                           dp->parser_con.pg.rope_seq);
            assert(dp->proxy_port == 0);
#ifdef MINGW32
            raise(SIGTERM);
#else
            kill(getpid(), SIGTERM);
#endif
        }
    }
    pthread_mutex_unlock(&(dp->go_time_mutex));
    return;
}
/*
 * Launch the clients
 */
void schedule_clients(wdbp)
WEBDRIVE_BASE *wdbp;
{
sigset_t msk;
int sgnl = 0;

    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) schedule_clients()\n",
                   wdbp->parser_con.pg.rope_seq);
    strcpy(wdbp->narrative, "schedule_clients");
    (void) sigrelse(SIGTERM);
    (void) sigrelse(SIGUSR1);
    (void) sigrelse(SIGALRM);
    (void) sigemptyset(&msk);
    (void) sigaddset(&msk, SIGTERM);
    (void) sigaddset(&msk, SIGUSR1);
    (void) sigaddset(&msk, SIGALRM);
    while (sgnl != SIGTERM
        && sgnl != SIGUSR1
        && wdbp->root_wdbp->go_away == 0)
    {
        rem_time(wdbp);
        if ((wdbp->alert_flag)
         || sigwait(&msk, &sgnl))
        {
            wdbp->alert_flag = 0;
            sgnl = 0;
        }
    }
    return;
}
/***************************************************************************
 * Routines to temporarily pre-empt the normal clock handling.
 */
void alarm_preempt(wdbp)
WEBDRIVE_BASE *wdbp;
{
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) alarm_preempt()\n", wdbp->parser_con.pg.rope_seq);
    wdbp->alert_flag = 0;
    add_time(wdbp->root_wdbp, wdbp, nohang.tv_sec);
    sigrelse(SIGIO);
    return;
}
void idle_alarm(wdbp)
WEBDRIVE_BASE *wdbp;
{
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) idle_alarm()\n", wdbp->parser_con.pg.rope_seq);
    wdbp->alert_flag = 0;
    add_time(wdbp->root_wdbp, wdbp, close_idle.tv_sec);
    sigrelse(SIGIO);
    return;
}
/*
 * Routine to restore it
 */
void alarm_restore(wdbp)
WEBDRIVE_BASE *wdbp;
{
    if (wdbp->debug_level > 1)
        fprintf(stderr,"(Client:%s) alarm_restore()\n", wdbp->parser_con.pg.rope_seq);
    zap_time(wdbp->root_wdbp, wdbp);
    sighold(SIGIO);
    return;
}
/*
 * Routines to support pipe-like communications between driver threads.
 * - Optional synchronisation, so feeder can block until consumer(s) have
 *   consumed; needed for the hand-off support in sslserv.
 * - Do we have a use for 1 to n communications; a message must be delivered
 *   to more than one thread; registration only needs to bump a counter,
 *   since the thing doesn't care about who the users are? Maybe. Easy, so
 *   do it.
 **************************************************************************
 * Functions to manage circular buffers.
 *
 * Create a circular buffer
 */
struct pipe_buf * pipe_buf_cre(nelems)
int nelems;
{
struct pipe_buf * buf = (struct pipe_buf *) malloc(sizeof(struct pipe_buf)
                              + (nelems+1) * sizeof(struct pipe_mess));
struct pipe_mess * xpmp;

    if (buf == (struct pipe_buf *) NULL)
        return (struct pipe_buf *) NULL;
    else
    {
        buf->buf_cnt = 0;
        buf->head = (struct pipe_mess *) (buf + 1);
        buf->tail = buf->head;
        buf->base = buf->head;
        buf->top = buf->head + nelems;
        pthread_mutex_init(&(buf->mutex), NULL);
        pthread_mutex_init(&(buf->condmutex), NULL);
        pthread_cond_init(&(buf->condcond), NULL);
        for (xpmp = buf->base; xpmp < buf->top; xpmp++)
        {
            pthread_mutex_init(&(xpmp->mutex), NULL);
            pthread_mutex_init(&(xpmp->condmutex), NULL);
            pthread_cond_init(&(xpmp->condcond), NULL);
            xpmp->clntcnt = 0;
            xpmp->msglen = 0;
            xpmp->msgbuf = NULL;
            xpmp->get_rid_cb = NULL;
        }
        return buf;
    }
}
/*
 * Drain the pipe buffer
 */
void drain_pipe_buf(buf)
struct pipe_buf * buf;
{
unsigned int len;
unsigned char * mp = NULL;
void (*get_rid_cb)();

#ifdef DEBUG
   fprintf(stderr, "drain_pipe_buf(%lx) called\n", (unsigned long) buf);
#endif
   while (pipe_buf_take(buf, &len, &mp, &get_rid_cb, 0) >= 0)
   {
       if (get_rid_cb != NULL && mp != NULL)
           get_rid_cb(len, mp);
       mp = NULL;
   }
   return;
}
/*
 * Destroy a circular buffer
 */
void pipe_buf_des(buf)
struct pipe_buf * buf;
{
   drain_pipe_buf(buf);
   free(buf);
   return;
}
/*
 * Add an element to the buffer. The wait semantics are odd; it only waits
 * if there is a reader waiting. If there are elements backed up, it continues.
 * Protection against hangs.
 */
int pipe_buf_add( buf, clntcnt, msglen, msgbuf, get_rid_cb, wait_flag)
struct pipe_buf *buf;
int clntcnt;
unsigned int msglen;
unsigned char * msgbuf;
void (*get_rid_cb)();
int wait_flag;
{
struct pipe_mess * new_head, *xp;
int occ_cnt;

#ifdef DEBUG
    fprintf(stderr, "pipe_buf_add(%lx,%d,%d,%lx,%lx,%d) called\n",
      (long) buf, clntcnt, msglen, (long) msgbuf, (long) get_rid_cb, wait_flag);
#endif
    pthread_mutex_lock(&(buf->mutex));
/*
 * Get the mutex, and an entry
 */
    for (;;)
    {
        new_head = buf->head;
        new_head++;
        if (new_head == buf->top)
            new_head = buf->base;
        if (new_head == buf->tail)
        {
/*
 * We have to wait for the tail to move.
 */
            occ_cnt = buf->buf_cnt; 
            pthread_mutex_lock(&(buf->condmutex));
            pthread_mutex_unlock(&(buf->mutex));
            while ( occ_cnt == buf->buf_cnt )
                pthread_cond_wait(&(buf->condcond), &(buf->condmutex));
            pthread_mutex_lock(&(buf->mutex));
            pthread_mutex_unlock(&(buf->condmutex));
        }
        else
            break;
    }
/*
 * Swap them round
 */
    xp = new_head;
    new_head =  buf->head;
    buf->head = xp;
    buf->buf_cnt++;
    pthread_mutex_lock(&(new_head->mutex));
    pthread_mutex_unlock(&(buf->mutex));
/*
 * Now have the entry, the header is unlocked (so more can be added or
 * removed) and the entry itself is locked.
 */
    new_head->clntcnt += clntcnt;
    new_head->msglen = msglen;
    new_head->msgbuf = msgbuf;
    new_head->get_rid_cb = get_rid_cb;
    if (new_head->clntcnt != clntcnt)  /* There is at least one waiter */
        pthread_cond_broadcast(&(new_head->condcond));
    if (wait_flag && new_head->clntcnt > 0)
    {
        pthread_mutex_lock(&(new_head->condmutex));
        pthread_mutex_unlock(&(new_head->mutex));
        while ( 0 < new_head->clntcnt )
            pthread_cond_wait(&(new_head->condcond), &(new_head->condmutex));
        pthread_mutex_unlock(&(new_head->condmutex));
    }
    else
        pthread_mutex_unlock(&(new_head->mutex));
#ifdef DEBUG
    fprintf(stderr, "pipe_buf_add(%lx,%d,%lx,%lx,%d) returned %d\n",
      (long) buf, clntcnt, msglen, (long) msgbuf, (long) get_rid_cb, wait_flag,
               buf->buf_cnt);
#endif
    return buf->buf_cnt;
}
/*
 * Take an element from the buffer. Note that because of the 1 to n
 * semantics, the same value can be returned multiple times.
 */
int pipe_buf_take(buf, len, x, get_rid_cb, wait_flag)
struct pipe_buf * buf;
unsigned int * len;
unsigned char ** x;
void (**get_rid_cb)();
int wait_flag;
{
struct pipe_mess * cur_tail;
int occ_cnt;

#ifdef DEBUG
    fprintf(stderr, "pipe_buf_take(%lx,%d,%lx,%lx,%d) called\n",
              (long) buf, len, (long) x, (long) get_rid_cb, wait_flag);
#endif
retry:
    pthread_mutex_lock(&(buf->mutex));
    if (buf->buf_cnt <= 0)
    {   /* There is no work for us */
        if (wait_flag)
        {   /* We need to wait for work */
            pthread_mutex_lock(&(buf->head->mutex));
            cur_tail = buf->head;
            cur_tail->clntcnt--;
            occ_cnt = cur_tail->clntcnt;
            pthread_mutex_lock(&(buf->head->condmutex));
            pthread_mutex_unlock(&(buf->head->mutex));
            pthread_mutex_unlock(&(buf->mutex));
            while ( cur_tail->clntcnt <= occ_cnt )
                pthread_cond_wait(&(cur_tail->condcond),
                            &(cur_tail->condmutex));
            pthread_mutex_unlock(&(cur_tail->condmutex));
            goto retry;
        }
        else
        {
            pthread_mutex_unlock(&(buf->mutex));
#ifdef DEBUG
    fprintf(stderr, "pipe_buf_take(%lx,%d,%lx,%lx,%d) returned -1\n",
              (long) buf, len, (long) x, (long) get_rid_cb, wait_flag);
#endif
            return -1;
        }
    }
    cur_tail = buf->tail;
    pthread_mutex_lock(&(cur_tail->mutex));
    if (len != NULL)
        *len  = cur_tail->msglen;
    if (x != NULL)
        *x = cur_tail->msgbuf;
    cur_tail->clntcnt--;
    if (cur_tail->clntcnt <= 0)
    {
        if (get_rid_cb != NULL)
            *get_rid_cb = cur_tail->get_rid_cb;
        pthread_cond_broadcast(&(cur_tail->condcond));
                             /* In case the adder is still waiting */
        pthread_mutex_unlock(&(cur_tail->mutex));
        cur_tail++;
        if (cur_tail == buf->top)
            cur_tail = buf->base;
        buf->tail = cur_tail;
        buf->buf_cnt--;
        pthread_cond_broadcast(&(buf->condcond));
                 /* In case someone is waiting; who, how, why? */
    }
    else
    {
        if (get_rid_cb != NULL)
            *get_rid_cb = NULL;
        pthread_mutex_unlock(&(cur_tail->mutex));
    }
    pthread_mutex_unlock(&(buf->mutex));
#ifdef DEBUG
    fprintf(stderr, "pipe_buf_take(%lx,%d,%lx,%lx,%d) returned %d\n",
              (long) buf, len, (long) x, (long) get_rid_cb, wait_flag,
               buf->buf_cnt);
#endif
    return buf->buf_cnt;
}
