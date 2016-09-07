/*
 * Simple-minded mapping of POSIX thread stuff to Win32
 *
 * Only implements what is needed by e2tsafe programs
 *
 * We need to watch out for dependency on errno settings, because errno
 * isn't being set.
 */
static char * sccs_id = "Copyright (c) E2 Systems Limited 2004\n\
@(#) $Name$ $Id$";
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "matchlib.h"
#include "w32pthread.h"
#ifndef InterlockedAnd
#define InterlockedAnd InterlockedAnd_Inline
static __inline LONG InterlockedAnd_Inline(LONG volatile *Target,LONG Set)
{
LONG i;
LONG j;

    j = *Target;
    do
    {
        i = j;
        j = InterlockedCompareExchange(Target,i & Set,i);
    }
    while(i != j);
    return j;
}
#endif
/*
 * Self
 */
pthread_t pthread_self()
{
pthread_t ret;

    ret.thread_id = GetCurrentThreadId();
    if (!DuplicateHandle(GetCurrentProcess(),
                         GetCurrentThread(),
                         GetCurrentProcess(),
                         &(ret.hthread),
                         0,
                         1,
                         DUPLICATE_SAME_ACCESS))
        fprintf(stderr, "Failed to duplicate the Thread Handle, error: %x\n",
                       GetLastError());
#ifdef DEBUG
    fprintf(stderr, "pthread_self(%d, %x)\n", ret.thread_id, ret.hthread);
    fflush(stderr);
#endif
    return ret;
}
/*
 * Create a thread
 */
int pthread_create(pthread_t * self, pthread_attr_t * attr,
    void * (*func)(void *), void * arg)
{
    self->hthread = CreateThread((LPSECURITY_ATTRIBUTES) attr,
                                     0,    /* Stack Size (default)   */ 
                                     (LPTHREAD_START_ROUTINE) func,
                                     (LPVOID) arg,
                                     0,    /* Run immediately        */ 
                                    &(self->thread_id));
#ifdef DEBUG
    fprintf(stderr, "pthread_create(%d, %x)\n", self->thread_id, self->hthread);
    fflush(stderr);
#endif
    return 0;
}
#ifdef NO_COND_VARIABLES
/**********************************************************************
 * These worked O.K. when we were just managing a few threads but the
 * underlying calls to PulseEvent() (which doesn't actually work) mean
 * that under pressure it all deadlocks.
 **********************************************************************
 * Set up a condition variable
 */
int pthread_cond_init(pthread_cond_t * self, pthread_condattr_t * attr)
{
    InitializeCriticalSection(&(self->cs));
    self->hcond = CreateEvent(attr, /* LPSECURITY_ATTRIBUTES               */
                        0,    /* Auto-reset (1 for manual)           */
                        0,    /* Start unsignalled (1 for signalled) */
                        NULL);  /* No name; local to this process    */
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_init((tid=%d),%x)\n",
          GetCurrentThreadId(),self->hcond);
    fflush(stderr);
#endif
    if (self->hcond == INVALID_HANDLE_VALUE)
        return -1;
    else
        return 0;
}
/*
 * Signal a condition variable
 */
int pthread_cond_signal(pthread_cond_t *cond)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_signal((tid=%d),%x)\n",
                          GetCurrentThreadId(), cond->hcond);
    fflush(stderr);
#endif
    if (SetEvent(cond->hcond))
        return 0;
    else
    {
        fprintf(stderr,
          "SetEvent((tid=%d),%x) failed, error: %x\n",
                     GetCurrentThreadId(), cond->hcond,
                           GetLastError());
        fflush(stderr);
        return 1;
    }
}
/***********************************************************************
 * Broadcast a condition variable
 ***********************************************************************
 * For this to work:
 * - The event would have to be manual rather than auto
 * - We would use PulseEvent() rather than SetEvent().
 * However, PulseEvent() doesn't actually work.
 * Current versions of Windows support Condition Variables, so this is the
 * way forwards. In the meantime, our applications only ever have one waiter,
 * so we leave it as a duplicate of pthread_cond_signal() for now.
 */
int pthread_cond_broadcast(pthread_cond_t *cond)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_signal((tid=%d),%x)\n",
                          GetCurrentThreadId(), cond->hcond);
    fflush(stderr);
#endif
    if (SetEvent(cond->hcond))
        return 0;
    else
    {
        fprintf(stderr,
          "SetEvent((tid=%d),%x) failed, error: %x\n",
                     GetCurrentThreadId(), cond->hcond,
                           GetLastError());
        fflush(stderr);
        return 1;
    }
}
/*
 * Wait for a condition variable
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
int ret;
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_wait((tid=%d),%x,%x)\n",
                     GetCurrentThreadId(), cond->hcond, *mutex);
    fflush(stderr);
#endif
    if ((ret = SignalObjectAndWait(*mutex, cond->hcond, INFINITE, 0)) !=
                 WAIT_OBJECT_0)
    {
        fprintf(stderr,
          "SignalObjectAndWait((tid=%d),%x, %x) failed, ret: %d error: %x\n",
                     GetCurrentThreadId(), cond->hcond, *mutex,
                           ret, GetLastError());
        fflush(stderr);
    }
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_wait((tid=%d),%x,%x) Now Waiting ...\n",
                     GetCurrentThreadId(), cond->hcond, *mutex);
    fflush(stderr);
#endif
    if ((ret = WaitForSingleObject(*mutex, INFINITE)) != WAIT_OBJECT_0)
    {
        fprintf(stderr,
          "WaitForSingleObject((tid=%d),%x) failed, ret: %d error: %x\n",
                     GetCurrentThreadId(), *mutex,
                           ret, GetLastError());
        fflush(stderr);
    }
    return 0;
}
#else
/*
 * Set up a condition variable
 */
int pthread_cond_init(pthread_cond_t * self, pthread_condattr_t * attr)
{
    InitializeCriticalSection(&(self->cs));
    InitializeConditionVariable(&(self->hcond));
    return 0;
}
/*
 * Signal a condition variable
 */
int pthread_cond_signal(pthread_cond_t *cond)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_signal((tid=%d), %lx)\n",
                          GetCurrentThreadId(), (long int) cond);
    fflush(stderr);
#endif
    WakeConditionVariable(&(cond->hcond));
    return 0;
}
/*
 * Broadcast a condition variable
 */
int pthread_cond_broadcast(pthread_cond_t *cond)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_cond_broadcast((tid=%d),%lx)\n",
                          GetCurrentThreadId(), (long int)cond);
    fflush(stderr);
#endif
    WakeAllConditionVariable(&(cond->hcond));
    return 0;
}
/*
 * Wait for a condition variable
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
int res;

#ifdef DEBUG
    fprintf(stderr, "pthread_cond_wait((tid=%d),%x,%x)\n",
                     GetCurrentThreadId(), cond->hcond, *mutex);
    fflush(stderr);
#endif
    EnterCriticalSection(&cond->cs);
    if (!ReleaseMutex(*mutex))
        fprintf(stderr, "ReleaseMutex((tid=%d),%x) failed error: %x\n",
                     GetCurrentThreadId(), *mutex, GetLastError());
    SleepConditionVariableCS(&cond->hcond, &cond->cs, INFINITE);
    WaitForSingleObject(*mutex, INFINITE);
    LeaveCriticalSection(&cond->cs);
    return 0;
}
#endif
/*
 * Create a mutex
 */
int pthread_mutex_init(pthread_mutex_t  *mutex,
    const pthread_mutexattr_t *mutexattr)
{
    *mutex = CreateMutex(mutexattr,   /* LPSECURITY_ATTRIBUTES */
                         0,           /* Mutex not owned       */ 
                         NULL);       /* Mutex has no name     */
#ifdef DEBUG
    fprintf(stderr, "pthread_mutex_init((tid=%d),%x,%x)\n",
               GetCurrentThreadId(), *mutex, mutexattr);
    fflush(stderr);
#endif
    if (*mutex == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateMutex((tid=%d),%x) failed error: %x\n",
                     GetCurrentThreadId(), *mutex, GetLastError());
        return -1;
    }
    else
        return 0;
}
/*
 * Lock a mutex
 */
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
DWORD res;

#ifdef DEBUG
    fprintf(stderr, "pthread_mutex_lock((tid=%d),%x)\n",
                         GetCurrentThreadId(), *mutex);
    fflush(stderr);
#endif
    res = WaitForSingleObject(*mutex, INFINITE);
    if (res == WAIT_OBJECT_0)
        return 0;
    else
        return 1;
}
/*
 * Try to lock a mutex
 */
int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
DWORD res;

#ifdef DEBUG
    fprintf(stderr, "pthread_mutex_lock((tid=%d),%x)\n",
                         GetCurrentThreadId(), *mutex);
    fflush(stderr);
#endif
    res = WaitForSingleObject(*mutex, 0);
    if (res == WAIT_OBJECT_0)
        return 0;
    else
        return 1;
}
/*
 * Delete a mutex
 */
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
DWORD res;

#ifdef DEBUG
    fprintf(stderr, "pthread_mutex_destroy((tid=%d),%x)\n",
                         GetCurrentThreadId(), *mutex);
    fflush(stderr);
#endif
    return !CloseHandle(*mutex);
}
/*
 * Unlock a mutex
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_mutex_unlock((tid=%d),%x)\n",
                                GetCurrentThreadId(), *mutex);
    fflush(stderr);
#endif
    if (ReleaseMutex(*mutex))
        return 0;
    else
    {
        fprintf(stderr, "ReleaseMutex((tid=%d),%x) failed error: %x\n",
                     GetCurrentThreadId(), *mutex, GetLastError());
        return -1;
    }
}
/*
 * Are two threads the same?
 */
int pthread_equal(pthread_t p1, pthread_t p2)
{
#ifdef DEBUG
    fprintf(stderr, "pthread_equal((%d,%x),(%d,%x))\n",
             p1.thread_id, p1.hthread, p2.thread_id,p2.hthread);
    fflush(stderr);
#endif
    if (p1.thread_id == p2.thread_id)
        return 1;
    else
        return 0;
}
/*
 * Send a signal to a thread. Put the signal in a message, unless a hard kill
 * has been requested.
 */
int pthread_kill(pthread_t t, int signo)
{
int ret;
#ifdef DEBUG
    fprintf(stderr, "pthread_kill((tid=%d),(%d,%x),%d))\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo);
    fflush(stderr);
#endif
    if (signo == SIGKILL)
        return (!TerminateThread(t.hthread,-9));
reliable:
    while (!PostThreadMessage(t.thread_id,WM_USER,signo,0))
    {
         fprintf(stderr,
            "pthread_kill((tid=%d),(%d,%x),%d)) cannot post message\n\
error: %x\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo,
               GetLastError());
        if (GetLastError() == ERROR_INVALID_THREAD_ID)
            return 0;
        fflush(stderr);
        Sleep(100);
    }
    ret = ResumeThread(t.hthread);
    if (ret < 0)
    {
         fprintf(stderr,
            "pthread_kill((tid=%d),(%d,%x),%d)) cannot resume target\n\
error: %x\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo,
               GetLastError());
        fflush(stderr);
    }
    else
    if (ret == 0)
    {
#ifdef DEBUG
         fprintf(stderr,
            "pthread_kill((tid=%d),(%d,%x),%d)) target not suspended\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo);
        fflush(stderr);
#endif
        if (signo == SIGTERM)
        {
            Sleep(100);    /* Give it a chance to go on */
            goto reliable; /* We need to make sure this gets through */
        }
    }
    else
    if (ret > 1)
    {
         fprintf(stderr,
            "pthread_kill((tid=%d),(%d,%x),%d)) target still suspended\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo);
        fflush(stderr);
    }
#ifdef DEBUG
    else
    {
         fprintf(stderr,
            "pthread_kill((tid=%d),(%d,%x),%d)) target now resumed\n",
               GetCurrentThreadId(), t.thread_id, t.hthread, signo);
        fflush(stderr);
    }
#endif
    return 0;
}
/*
 * Wait for a signal. Actually, just wait to be woken. Pick up the signal
 * from the thread message queue.
 */
int sigwait(sigset_t * set, int * sig)
{
MSG msg;
int ret;

#ifdef DEBUG
    fprintf(stderr, "Entering sigwait((tid=%d),%x))\n",
                GetCurrentThreadId(), *set);
    fflush(stderr);
#endif
    SuspendThread( GetCurrentThread());
#ifdef DEBUG
    fprintf(stderr,
            "sigwait((tid=%d)) target now resumed\n",
               GetCurrentThreadId());
    fflush(stderr);
#endif
    while ((ret = GetMessage (&msg, NULL, 0, 0)) != 0)
    {
        if (ret == -1)
        {
#ifdef DEBUG
            fprintf(stderr,
               "sigwait((tid=%d),%x) GetMessage() error: %x\n",
                        GetCurrentThreadId(), *set, GetLastError());
            fflush(stderr);
#endif
            break;
        }
        TranslateMessage (&msg);
#ifdef DEBUG
        fprintf(stderr, "sigwait((tid=%d),%x)) message loop: %x\n",
                    GetCurrentThreadId(), *set, msg.message);
        fflush(stderr);
#endif
        if (msg.message == WM_TIMER || msg.message == WM_USER)
            break;
#ifdef DEBUG
        fprintf(stderr, "sigwait((tid=%d),%x)) message loop: %x not WM_TIMER or WM_USER - Who sent use this???\n",
                    GetCurrentThreadId(), *set, msg.message);
        fflush(stderr);
#endif
        DispatchMessage (&msg);
    }
    *sig = msg.wParam;
#ifdef DEBUG
    fprintf(stderr, "Leaving sigwait(%x,%d))\n", *set, *sig);
    fflush(stderr);
#endif
    return 0;
}
/*
 * Thread-safe version of ctime; calls a potentially un-thread-safe version
 */
char * ctime_r(time_t * t, char * cp)
{
static long interlock;
static long done;
static CRITICAL_SECTION cs;
char *x;

    if (InterlockedIncrement(&interlock) == 1)
    {
        InitializeCriticalSection(&cs);
        InterlockedIncrement(&done);
    }
    else
    while(InterlockedAnd(&done, 1) != 1)
        Sleep(0);
    EnterCriticalSection(&cs);
    strcpy(cp,ctime(t));
    LeaveCriticalSection(&cs);
    return cp;
}
/*
 * Missing signal handling routines
 */
int sigemptyset(sigset_t * m)
{
    *m = 0;
    return 0;
}
int sigaddset(sigset_t * m, int signo)
{
    *m |= (1 << (signo - 1));
    return 0;
}
/*
 * Create a mutexattr. Currently a NO-OP.
 */
int pthread_mutexattr_init(pthread_mutexattr_t *mutexattr)
{
    return 0;
}
/*
 * Set attributes to be process-shared. A NO-OP pending investigation as to
 * whether or not critical sections are shared between processes on W32.
 */
int pthread_mutexattr_setpshared(pthread_mutexattr_t *mutexattr, int pshared )
{
    return 0;
}
/*
 * Initialise a pthread attr. Currently a NO-OP.
 */
int pthread_attr_init(pthread_attr_t *attr)
{
    return 0;
}
/*
 * Set the thread stack size. A NO-OP pending investigation as to how we
 * accomplish this with W32.
 */
int pthread_attr_setstacksize(pthread_attr_t *attr, int stack_size )
{
    return 0;
}
/*
 * Set the thread detach state. Controls the ability or otherwise to join to
 * a thread, and affects the cleanup of thread state after thread exit on
 * actual pthreads implementations. This is, of course, not one; it the lowest
 * common denominator between Windows threads and pthreads.
 */
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state )
{
    return 0;
}
