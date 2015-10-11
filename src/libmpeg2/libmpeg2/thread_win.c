/*
 * thread_win.c
 * Copyright (C) 2015      DBCTRADO
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
    TODO: error handling
*/

#include "config.h"

#include <inttypes.h>
#include <windows.h>
#include <stdlib.h>
#include <process.h>

#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "thread.h"

typedef struct task_s {
    thread_func_t * task_func;
    thread_func_t * abort_func;
    void * param;
    struct task_s * next;
} task_t;

typedef struct {
    HANDLE handle;
    unsigned int id;
} thread_t;

struct thread_pool_s {
    int num_threads;
    thread_t * threads;
    task_t * queue_head;
    task_t * queue_tail;
    task_t * free_tasks;
    CRITICAL_SECTION queue_lock;
    HANDLE queue_event;
    HANDLE finished_event;
    volatile int stop;
    int busy_count;
};

struct mutex_s {
    CRITICAL_SECTION critical_section;
};

int mpeg2_default_threads (void)
{
    DWORD_PTR process_mask, system_mask;
    int count;

    if (!GetProcessAffinityMask (GetCurrentProcess (), &process_mask, &system_mask))
	return 0;

    count = 0;
    while (process_mask) {
	if (process_mask & 1)
	    count++;
	process_mask >>= 1;
    }
    return count;
}

static unsigned int __stdcall worker_thread (void * param)
{
    thread_pool_t * pool = (thread_pool_t *) param;

    for (;;) {
	task_t * task;

	EnterCriticalSection (&(pool->queue_lock));
	while (!pool->stop && !pool->queue_head) {
	    LeaveCriticalSection (&(pool->queue_lock));
	    WaitForSingleObject (pool->queue_event, INFINITE);
	    EnterCriticalSection (&(pool->queue_lock));
	}
	if (pool->stop) {
	    LeaveCriticalSection (&(pool->queue_lock));
	    break;
	}
	task = pool->queue_head;
	pool->queue_head = task->next;
	if (!pool->queue_head)
	    pool->queue_tail = NULL;
	++(pool->busy_count);
	ResetEvent (pool->queue_event);
	LeaveCriticalSection (&(pool->queue_lock));

	task->task_func (task->param);

	EnterCriticalSection (&(pool->queue_lock));
	task->next = pool->free_tasks;
	pool->free_tasks = task;
	--(pool->busy_count);
	SetEvent (pool->finished_event);
	LeaveCriticalSection (&(pool->queue_lock));
    }

    return 0;
}

thread_pool_t * mpeg2_thread_pool_create (int num_threads)
{
    thread_pool_t * pool;
    int i;

    pool = (thread_pool_t *) malloc (sizeof(thread_pool_t));
    if (!pool)
	return NULL;

    if (num_threads < 1) {
	num_threads = mpeg2_default_threads ();
	if (num_threads < 1)
	    num_threads = 1;
    }
    pool->num_threads = num_threads;
    pool->threads = (thread_t *) malloc (num_threads * sizeof(thread_t));
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->free_tasks = NULL;
    InitializeCriticalSection (&(pool->queue_lock));
    pool->queue_event = CreateEvent (NULL, TRUE, FALSE, NULL);
    pool->finished_event = CreateEvent (NULL, TRUE, FALSE, NULL);
    pool->stop = 0;
    pool->busy_count = 0;

    for (i = 0; i < num_threads; i++) {
	pool->threads[i].handle = (HANDLE) _beginthreadex(
	    NULL, 0, worker_thread, pool, 0, &pool->threads[i].id);
    }

    return pool;
}

void mpeg2_thread_pool_close (thread_pool_t * pool)
{
    if (pool) {
	int i;
	task_t * task, * next;

	mpeg2_thread_pool_abort (pool);

	pool->stop = 1;
	SetEvent (pool->queue_event);
	for (i = 0; i < pool->num_threads; i++) {
	    WaitForSingleObject (pool->threads[i].handle, INFINITE);
	    CloseHandle (pool->threads[i].handle);
	}
	free (pool->threads);

	task = pool->free_tasks;
	while (task) {
	    next = task->next;
	    free (task);
	    task = next;
	}

	DeleteCriticalSection (&(pool->queue_lock));

	free (pool);
    }
}

void mpeg2_thread_pool_enqueue (thread_pool_t * pool,
				thread_func_t * task_func,
				thread_func_t * abort_func,
				void * param)
{
    if (pool) {
	task_t * task;

	EnterCriticalSection (&(pool->queue_lock));

	task = pool->free_tasks;
	if (task)
	    pool->free_tasks = task->next;
	else
	    task = (task_t *) malloc (sizeof(task_t));

	task->task_func = task_func;
	task->abort_func = abort_func;
	task->param = param;
	task->next = NULL;

	if (pool->queue_tail)
	    pool->queue_tail->next = task;
	else
	    pool->queue_head = task;
	pool->queue_tail = task;

	SetEvent (pool->queue_event);
	LeaveCriticalSection (&(pool->queue_lock));
    }
}

void mpeg2_thread_pool_abort (thread_pool_t * pool)
{
    if (pool) {
	task_t * task, * prev, * next;

	EnterCriticalSection (&(pool->queue_lock));

	task = pool->queue_head;
	prev = pool->free_tasks;
	while (task) {
	    if (task->abort_func)
		task->abort_func (task->param);
	    next = task->next;
	    task->next = prev;
	    pool->free_tasks = task;
	    prev = task;
	    task = next;
	}
	pool->queue_head = NULL;
	pool->queue_tail = NULL;

	LeaveCriticalSection (&(pool->queue_lock));

	mpeg2_thread_pool_wait (pool);
    }
}

void mpeg2_thread_pool_wait (thread_pool_t * pool)
{
    if (pool) {
	EnterCriticalSection (&(pool->queue_lock));
	while (pool->queue_head || pool->busy_count) {
	    ResetEvent (pool->finished_event);
	    LeaveCriticalSection (&(pool->queue_lock));
	    WaitForSingleObject (pool->finished_event, INFINITE);
	    EnterCriticalSection (&(pool->queue_lock));
	}
	LeaveCriticalSection (&(pool->queue_lock));
    }
}

void mpeg2_thread_pool_wait_any (thread_pool_t * pool)
{
    if (pool) {
	int old_busy_count;

	EnterCriticalSection (&(pool->queue_lock));
	old_busy_count = pool->busy_count;
	if (old_busy_count) {
	    do {
		ResetEvent (pool->finished_event);
		LeaveCriticalSection (&(pool->queue_lock));
		WaitForSingleObject (pool->finished_event, INFINITE);
		EnterCriticalSection (&(pool->queue_lock));
	    } while (pool->busy_count >= old_busy_count);
	}
	LeaveCriticalSection (&(pool->queue_lock));
    }
}

int mpeg2_thread_pool_num_threads (const thread_pool_t * pool)
{
    if (!pool)
	return 0;
    return pool->num_threads;
}

mutex_t * mpeg2_mutex_create (void)
{
    mutex_t * mutex;

    mutex = (mutex_t *) malloc (sizeof(mutex_t));
    if (mutex)
	InitializeCriticalSection (&(mutex->critical_section));
    return mutex;
}

void mpeg2_mutex_close (mutex_t * mutex)
{
    free (mutex);
}

void mpeg2_mutex_lock (mutex_t * mutex)
{
    if (mutex)
	EnterCriticalSection (&(mutex->critical_section));
}

void mpeg2_mutex_unlock (mutex_t * mutex)
{
    if (mutex)
	LeaveCriticalSection (&(mutex->critical_section));
}
