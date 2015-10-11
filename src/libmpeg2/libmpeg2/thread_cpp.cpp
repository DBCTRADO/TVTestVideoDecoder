/*
 * thread_cpp.cpp
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
    TODO: exception handling
*/

#include "config.h"

// conflict with __declspec(restrict)
#ifdef restrict
#undef restrict
#endif
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <condition_variable>
#include <new>

#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "thread.h"

struct thread_pool_s {
    thread_pool_s (int threads);
    ~thread_pool_s ();
    void enqueue (thread_func_t * task_func, thread_func_t * abort_func,
		  void * param);
    void end ();
    void abort ();
    void wait ();
    void wait_any ();
    int get_thread_num () const { return num_threads; }

private:
    struct worker_t {
	worker_t (thread_pool_t * p);
	void operator() ();

	thread_pool_t * pool;
    };

    struct task_t {
	thread_func_t * task_func;
	thread_func_t * abort_func;
	void * param;
    };

    int num_threads;
    std::vector<std::thread> workers;
    std::deque<task_t> tasks;
    std::mutex queue_lock;
    std::condition_variable queue_condition;
    std::condition_variable finished_condition;
    volatile bool stop;
    int busy_count;

    void clear_queue ();
};

thread_pool_s::thread_pool_s (int threads)
    : stop (false)
    , busy_count (0)
{
    if (threads < 1) {
	threads = std::thread::hardware_concurrency ();
	if (!threads)
	    threads = 1;
    }

    num_threads = threads;

    workers.reserve (threads);

    for (int i = 0; i < threads; i++)
	workers.push_back (std::thread (worker_t (this)));
}

thread_pool_s::~thread_pool_s ()
{
    end ();
}

void thread_pool_s::enqueue (thread_func_t * task_func,
			     thread_func_t * abort_func, void * param)
{
    {
	std::unique_lock<std::mutex> lock (queue_lock);
	task_t task;

	task.task_func = task_func;
	task.abort_func = abort_func;
	task.param = param;

	tasks.push_back (task);
    }

    queue_condition.notify_one ();
}

void thread_pool_s::end ()
{
    clear_queue();

    stop = true;
    queue_condition.notify_all ();

    for (size_t i = 0; i < workers.size (); i++) {
	if (workers[i].joinable ())
	    workers[i].join ();
    }
}

void thread_pool_s::abort ()
{
    clear_queue ();

    wait ();
}

void thread_pool_s::wait ()
{
    std::unique_lock<std::mutex> lock (queue_lock);

    while (!tasks.empty () || busy_count)
	finished_condition.wait (lock);
}

void thread_pool_s::wait_any ()
{
    std::unique_lock<std::mutex> lock (queue_lock);
    const int old_busy_count = busy_count;

    if (old_busy_count) {
	do {
	    finished_condition.wait (lock);
	} while (busy_count >= old_busy_count);
    }
}

void thread_pool_s::clear_queue ()
{
    std::unique_lock<std::mutex> lock (queue_lock);

    while (!tasks.empty ()) {
	task_t &task = tasks.front ();

	if (task.abort_func)
	    task.abort_func (task.param);
	tasks.pop_front ();
    }
}

thread_pool_s::worker_t::worker_t (thread_pool_t * p)
    : pool (p)
{
}

void thread_pool_s::worker_t::operator() ()
{
    for (;;) {
	task_t task;

	{
	    std::unique_lock<std::mutex> lock (pool->queue_lock);

	    while (!pool->stop && pool->tasks.empty ())
	        pool->queue_condition.wait (lock);

	    if (pool->stop)
	        return;

	    task = pool->tasks.front ();
	    pool->tasks.pop_front ();
	    ++(pool->busy_count);
	}

	task.task_func (task.param);

	pool->queue_lock.lock ();
	--(pool->busy_count);
	pool->finished_condition.notify_one ();
	pool->queue_lock.unlock ();
    }
}

struct mutex_s {
    void lock () { mtx.lock (); }
    void unlock () { mtx.unlock (); }

private:
    std::mutex mtx;
};

extern "C" int mpeg2_default_threads (void)
{
    return std::thread::hardware_concurrency ();
}

extern "C" thread_pool_t * mpeg2_thread_pool_create (int num_threads)
{
    return new (std::nothrow) thread_pool_s (num_threads);
}

extern "C" void mpeg2_thread_pool_close (thread_pool_t * pool)
{
    delete pool;
}

extern "C" void mpeg2_thread_pool_enqueue (thread_pool_t * pool,
					   thread_func_t * task_func,
					   thread_func_t * abort_func,
					   void * param)
{
    if (pool)
	pool->enqueue (task_func, abort_func, param);
}

extern "C" void mpeg2_thread_pool_abort (thread_pool_t * pool)
{
    if (pool)
	pool->abort ();
}

extern "C" void mpeg2_thread_pool_wait (thread_pool_t * pool)
{
    if (pool)
	pool->wait ();
}

extern "C" void mpeg2_thread_pool_wait_any (thread_pool_t * pool)
{
    if (pool)
	pool->wait_any ();
}

extern "C" int mpeg2_thread_pool_num_threads (const thread_pool_t * pool)
{
    if (!pool)
	return 0;
    return pool->get_thread_num ();
}

extern "C" mutex_t * mpeg2_mutex_create (void)
{
    return new (std::nothrow) mutex_s;
}

extern "C" void mpeg2_mutex_close (mutex_t * mutex)
{
    delete mutex;
}

extern "C" void mpeg2_mutex_lock (mutex_t * mutex)
{
    if (mutex)
	mutex->lock ();
}

extern "C" void mpeg2_mutex_unlock (mutex_t * mutex)
{
    if (mutex)
	mutex->unlock ();
}
