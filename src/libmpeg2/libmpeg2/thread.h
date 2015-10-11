/*
 * thread.h
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

#ifndef LIBMPEG2_THREAD_H
#define LIBMPEG2_THREAD_H

#ifdef MPEG2_MT

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thread_pool_s thread_pool_t;
typedef struct mutex_s mutex_t;

typedef void thread_func_t (void *param);

int mpeg2_default_threads (void);

thread_pool_t * mpeg2_thread_pool_create (int num_threads);
void mpeg2_thread_pool_close (thread_pool_t * pool);
void mpeg2_thread_pool_enqueue (thread_pool_t * pool,
				thread_func_t * task_func,
				thread_func_t * abort_func, void * param);
void mpeg2_thread_pool_abort (thread_pool_t * pool);
void mpeg2_thread_pool_wait (thread_pool_t * pool);
void mpeg2_thread_pool_wait_any (thread_pool_t * pool);
int mpeg2_thread_pool_num_threads (const thread_pool_t * pool);

mutex_t * mpeg2_mutex_create (void);
void mpeg2_mutex_close (mutex_t * mutex);
void mpeg2_mutex_lock (mutex_t * mutex);
void mpeg2_mutex_unlock (mutex_t * mutex);

#ifdef __cplusplus
}
#endif

#endif /* MPEG2_MT */

#endif /* LIBMPEG2_THREAD_H */
