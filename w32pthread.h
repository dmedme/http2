/*
 * Simple-minded mapping of POSIX thread stuff to Win32
 *
 * Only implements what is needed by e2tsafe/e2uni programs
 *
 * We need to watch out for dependency on errno settings, because errno
 * isn't being set.
 *
 * Copyright (c) E2 Systems Limited 2004
 * @(#) $Name$ $Id$
 */
#ifndef W32PTHREAD_H
#define W32PTHREAD_H
#include <signal.h>
#ifndef _SIGSET_T_
#define	_SIGSET_T_
typedef long sigset_t;
#endif
struct e2thread {
    DWORD thread_id;
    HANDLE hthread;
};
typedef struct e2thread pthread_t;
typedef HANDLE pthread_mutex_t; 
typedef SECURITY_ATTRIBUTES pthread_attr_t; 
typedef SECURITY_ATTRIBUTES pthread_condattr_t; 
typedef SECURITY_ATTRIBUTES pthread_mutexattr_t; 
typedef struct rtl_condition_variable_ {
  void* p;
} RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE;
typedef RTL_CONDITION_VARIABLE CONDITION_VARIABLE, *PCONDITION_VARIABLE;
void WINAPI InitializeConditionVariable(PCONDITION_VARIABLE);
void WINAPI WakeConditionVariable(PCONDITION_VARIABLE);
void WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE);
BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
#ifdef NO_COND_VARS
struct e2cond {
   HANDLE hcond;
   CRITICAL_SECTION cs;
};
#else
struct e2cond {
   CONDITION_VARIABLE hcond;
   CRITICAL_SECTION cs;
};
#endif
typedef struct e2cond pthread_cond_t;

#define PTHREAD_COND_INITIALIZER {0,0}
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_PROCESS_SHARED 0
#define PTHREAD_CREATE_DETACHED 0
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP 0
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP 0
/*
 * Self
 */
pthread_t pthread_self(void);
/*
 * Create a thread
 */
int pthread_create(pthread_t * self,
    pthread_attr_t * attr,
    void * (*func)(void *),
    void * arg);
/*
 * Set up a condition variable
 */
int pthread_cond_init(pthread_cond_t * self, pthread_condattr_t * attr);
/*
 * Signal a condition variable
 */
int pthread_cond_signal(pthread_cond_t *cond);
/*
 * Signal a condition variable
 */
int pthread_cond_broadcast(pthread_cond_t *cond);
/*
 * Wait for a condition variable
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
/*
 * Initialise a pthread attr. Currently a NO-OP.
 */
int pthread_attr_init(pthread_attr_t *attr);
/*
 * Set the thread stack size. A NO-OP pending investigation as to how we
 * accomplish this with W32.
 */
int pthread_attr_setstacksize(pthread_attr_t *attr, int stack_size );
/*
 * Initialise a mutexattr. Currently a NO-OP.
 */
int pthread_mutexattr_init(pthread_mutexattr_t *mutexattr);
/*
 * Set attributes to be process-shared. A NO-OP pending investigation as to
 * whether or not critical sections are shared between processes on W32.
 */
int pthread_mutexattr_setpshared(pthread_mutexattr_t *mutexattr, int pshared );
/*
 * Create a mutex
 */
int pthread_mutex_init(pthread_mutex_t  *mutex,
    const pthread_mutexattr_t *mutexattr);
/*
 * Lock a mutex
 */
int pthread_mutex_lock(pthread_mutex_t *mutex);
/*
 * Attempt to lock a mutex
 */
int pthread_mutex_trylock(pthread_mutex_t *mutex);
/*
 * Unlock a mutex
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex);
/*
 * Are two threads the same?
 */
int pthread_equal(pthread_t p1, pthread_t p2);
/*
 * Send a signal to a thread. Put the signal in a message.
 */
int pthread_kill(pthread_t t, int signo);
/*
 * Wait for a signal. Actually, just wait to be woken. Pretend it was a
 * timeout.
 */
int sigwait(sigset_t * set, int * sig);
/*
 * Thread-safe version of ctime
 */
char * ctime_r(time_t * t, char * cp);
#endif
