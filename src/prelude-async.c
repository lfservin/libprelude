/*****
*
* Copyright (C) 2001-2005,2006,2007 PreludeIDS Technologies. All Rights Reserved.
* Author: Yoann Vandoorselaere <yoann.v@prelude-ids.com>
*
* This file is part of the Prelude library.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****/

#include "config.h"
#include "libmissing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "prelude-thread.h"
#include "prelude-list.h"
#include "prelude-inttypes.h"
#include "prelude-linked-object.h"
#include "prelude-timer.h"
#include "prelude-log.h"
#include "prelude-io.h"
#include "prelude-async.h"



/*
 * On POSIX systems where clock_gettime() is available, the symbol
 * _POSIX_TIMERS should be defined to a value greater than 0.
 *
 * However, some architecture (example True64), define it as:
 * #define _POSIX_TIMERS
 *
 * This explain the - 0 hack, since we need to test for the explicit
 * case where _POSIX_TIMERS is defined to a value higher than 0.
 *
 * If pthread_condattr_setclock and _POSIX_MONOTONIC_CLOCK are available,
 * CLOCK_MONOTONIC will be used. This avoid possible race problem when
 * calling pthread_cond_timedwait() if the system time is modified.
 *
 * If CLOCK_MONOTONIC is not available, revert to the standard CLOCK_REALTIME
 * way.
 *
 * If neither of the above are avaible, use gettimeofday().
 */
#if _POSIX_TIMERS - 0 > 0
# if defined(HAVE_PTHREAD_CONDATTR_SETCLOCK) && defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK - 0 >= 0)
#  define COND_CLOCK_TYPE CLOCK_MONOTONIC
# else
#  define COND_CLOCK_TYPE CLOCK_REALTIME
# endif
#endif

#ifdef USE_POSIX_THREADS_WEAK
# pragma weak pthread_create
# pragma weak pthread_join
# pragma weak pthread_sigmask

# ifdef HAVE_PTHREAD_ATFORK
#  pragma weak pthread_atfork
# endif

# pragma weak pthread_mutex_init
# pragma weak pthread_mutex_lock
# pragma weak pthread_mutex_unlock
# pragma weak pthread_mutex_destroy

# pragma weak pthread_cond_init
# pragma weak pthread_cond_wait
# pragma weak pthread_cond_signal
# pragma weak pthread_cond_timedwait
# pragma weak pthread_cond_destroy

# pragma weak pthread_condattr_init
# pragma weak pthread_condattr_setclock
#endif


static PRELUDE_LIST(joblist);


static prelude_async_flags_t async_flags = 0;
static prelude_bool_t stop_processing = FALSE;


static pthread_t thread;
static pthread_cond_t cond;
static pthread_mutex_t mutex;

static volatile sig_atomic_t is_initialized = FALSE;



static int timespec_elapsed(struct timespec *end, struct timespec *start)
{
        int diff = end->tv_sec - start->tv_sec;

        if ( end->tv_nsec < start->tv_nsec )
                diff -= 1;

        return diff;
}


static prelude_bool_t timespec_expired(struct timespec *end, struct timespec *start)
{
        return ( timespec_elapsed(end, start) ) ? TRUE : FALSE;
}


static struct timespec *get_timespec(struct timespec *ts)
{
#if _POSIX_TIMERS - 0 > 0
        int ret;

        ret = clock_gettime(COND_CLOCK_TYPE, ts);
        if ( ret < 0 )
                prelude_log(PRELUDE_LOG_ERR, "clock_gettime: %s.\n", strerror(errno));

#else
        struct timeval now;

        gettimeofday(&now, NULL);

        ts->tv_sec = now.tv_sec;
        ts->tv_nsec = now.tv_usec * 1000;
#endif

        return ts;
}



static int wait_timer_and_data(prelude_async_flags_t *flags)
{
        int ret;
        struct timespec ts;
        static struct timespec last_wakeup;
        prelude_bool_t no_job_available = TRUE;

        get_timespec(&last_wakeup);
        last_wakeup.tv_sec--;

        while ( no_job_available ) {
                ret = 0;

                pthread_mutex_lock(&mutex);

                ts.tv_sec = last_wakeup.tv_sec + 1;
                ts.tv_nsec = last_wakeup.tv_nsec;

                while ( (no_job_available = prelude_list_is_empty(&joblist)) &&
                        ! stop_processing && async_flags == *flags && ret != ETIMEDOUT ) {

                        ret = pthread_cond_timedwait(&cond, &mutex, &ts);
                }

                if ( no_job_available && stop_processing ) {
                        pthread_mutex_unlock(&mutex);
                        return -1;
                }

                *flags = async_flags;
                pthread_mutex_unlock(&mutex);

                if ( ret == ETIMEDOUT || timespec_expired(get_timespec(&ts), &last_wakeup) ) {
                        prelude_timer_wake_up();
                        last_wakeup.tv_sec = ts.tv_sec;
                        last_wakeup.tv_nsec = ts.tv_nsec;
                }
        }

        return 0;
}




static int wait_data(prelude_async_flags_t *flags)
{
        pthread_mutex_lock(&mutex);

        while ( prelude_list_is_empty(&joblist) && ! stop_processing && async_flags == *flags )
                pthread_cond_wait(&cond, &mutex);

        if ( prelude_list_is_empty(&joblist) && stop_processing ) {
                pthread_mutex_unlock(&mutex);
                return -1;
        }

        *flags = async_flags;
        pthread_mutex_unlock(&mutex);

        return 0;
}



static prelude_async_object_t *get_next_job(void)
{
        prelude_list_t *tmp;
        prelude_async_object_t *obj = NULL;

        pthread_mutex_lock(&mutex);

        prelude_list_for_each(&joblist, tmp) {
                obj = prelude_linked_object_get_object(tmp);
                prelude_linked_object_del((prelude_linked_object_t *) obj);
                break;
        }

        pthread_mutex_unlock(&mutex);

        return obj;
}



static void *async_thread(void *arg)
{
        int ret;
        prelude_async_object_t *obj;
        prelude_async_flags_t nflags = async_flags;

#ifndef WIN32
        sigset_t set;

        ret = sigfillset(&set);
        if ( ret < 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "sigfillset error: %s.\n", strerror(errno));
                return NULL;
        }

        ret = pthread_sigmask(SIG_BLOCK, &set, NULL);
        if ( ret < 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "pthread_sigmask error: %s.\n", strerror(errno));
                return NULL;
        }
#endif

        while ( 1 ) {

                if ( nflags & PRELUDE_ASYNC_FLAGS_TIMER )
                        ret = wait_timer_and_data(&nflags);
                else
                        ret = wait_data(&nflags);

                if ( ret < 0 ) {
                        /*
                         * On some implementation (namely, recent Linux + glibc version),
                         * calling pthread_exit() from a shared library and joining the thread from
                         * an atexit callback result in a deadlock.
                         *
                         * Appear to be related to:
                         * http://sources.redhat.com/bugzilla/show_bug.cgi?id=654
                         *
                         * Simply returning from the thread seems to fix this problem.
                         */
                        break;
                }

                while ( (obj = get_next_job()) )
                        obj->_async_func(obj, obj->_async_data);
        }

        return NULL;
}



#ifdef HAVE_PTHREAD_ATFORK

static void prepare_fork_cb(void)
{
        pthread_mutex_lock(&mutex);
}



static void parent_fork_cb(void)
{
        pthread_mutex_unlock(&mutex);
}



static void child_fork_cb(void)
{
        is_initialized = FALSE;
        prelude_list_init(&joblist);
}

#endif



static int do_init_async(void)
{
        int ret;
        pthread_condattr_t attr;

        ret = pthread_condattr_init(&attr);
        if ( ret != 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "error initializing condition attribute: %s.\n", strerror(ret));
                return ret;
        }

#if defined(HAVE_PTHREAD_CONDATTR_SETCLOCK) && _POSIX_TIMERS - 0 > 0
        ret = pthread_condattr_setclock(&attr, COND_CLOCK_TYPE);
        if ( ret != 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "error setting condition clock attribute: %s.\n", strerror(ret));
                return ret;
        }
#endif

        ret = pthread_cond_init(&cond, &attr);
        if ( ret != 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "error creating condition: %s.\n", strerror(ret));
                return ret;
        }

        ret = pthread_mutex_init(&mutex, NULL);
        if ( ret != 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "error creating mutex: %s.\n", strerror(ret));
                return ret;
        }


#ifdef HAVE_PTHREAD_ATFORK
        {
                static volatile sig_atomic_t fork_handler_registered = FALSE;

                if ( ! fork_handler_registered ) {
                        fork_handler_registered = TRUE;
                        pthread_atfork(prepare_fork_cb, parent_fork_cb, child_fork_cb);
                }
        }
#endif

        ret = pthread_create(&thread, NULL, async_thread, NULL);
        if ( ret != 0 ) {
                prelude_log(PRELUDE_LOG_ERR, "error creating asynchronous thread: %s.\n", strerror(ret));
                return ret;
        }

        /*
         * There is a problem with OpenBSD, where using atexit() from a multithread
         * application result in a deadlock. No workaround has been found at the moment.
         *
         */
#if ! defined(__OpenBSD__)
        return atexit(prelude_async_exit);
#else
        return 0;
#endif
}



/**
 * prelude_async_set_flags:
 * @flags: flags you want to set
 *
 * Sets flags to the asynchronous subsystem.
 *
 */
void prelude_async_set_flags(prelude_async_flags_t flags)
{
        pthread_mutex_lock(&mutex);

        async_flags = flags;
        pthread_cond_signal(&cond);

        pthread_mutex_unlock(&mutex);
}



/**
 * prelude_async_get_flags:
 *
 * Retrieves flags from the asynchronous subsystem
 *
 * Returns: asynchronous flags
 */
prelude_async_flags_t prelude_async_get_flags(void)
{
        return async_flags;
}



/**
 * prelude_async_init:
 *
 * Initialize the asynchronous subsystem.
 *
 * Returns: 0 on success, -1 if an error occured.
 */
int prelude_async_init(void)
{
        if ( ! is_initialized ) {
                assert(_prelude_thread_in_use() == TRUE);

                is_initialized = TRUE;
                stop_processing = FALSE;

                return do_init_async();
        }

        return 0;
}



/**
 * prelude_async_add:
 * @obj: Pointer to a #prelude_async_t object.
 *
 * Adds @obj to the asynchronous processing list.
 */
void prelude_async_add(prelude_async_object_t *obj)
{
        pthread_mutex_lock(&mutex);

        prelude_linked_object_add_tail(&joblist, (prelude_linked_object_t *) obj);
        pthread_cond_signal(&cond);

        pthread_mutex_unlock(&mutex);
}



/**
 * prelude_async_del:
 * @obj: Pointer to a #prelude_async_t object.
 *
 * Deletes @obj from the asynchronous processing list.
 */
void prelude_async_del(prelude_async_object_t *obj)
{
        pthread_mutex_lock(&mutex);
        prelude_linked_object_del((prelude_linked_object_t *) obj);
        pthread_mutex_unlock(&mutex);
}



void prelude_async_exit(void)
{
        prelude_bool_t has_job;

        if ( ! is_initialized )
                return;

        pthread_mutex_lock(&mutex);

        stop_processing = TRUE;
        pthread_cond_signal(&cond);
        has_job = ! prelude_list_is_empty(&joblist);

        pthread_mutex_unlock(&mutex);

        if ( has_job )
                prelude_log(PRELUDE_LOG_INFO, "Waiting for asynchronous operation to complete.\n");

        pthread_join(thread, NULL);
        pthread_cond_destroy(&cond);
        pthread_mutex_destroy(&mutex);

        is_initialized = FALSE;
}
