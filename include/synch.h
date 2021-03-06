/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYNCH_H_
#define _SYNCH_H_

/*
 * Header file for synchronization primitives.
 */

#include <spinlock.h>

/*
 * Dijkstra-style semaphore.
 *
 * The name field is for easier debugging. A copy of the name is made
 * internally.
 */
struct semaphore {
	char *sem_name;
	struct wchan *sem_wchan;
	struct spinlock sem_lock;
	volatile int sem_count;
};

struct semaphore *sem_create(const char *name, int initial_count);
void sem_destroy(struct semaphore *);

/*
 * Operations (both atomic):
 *     P (proberen): decrement count. If the count is 0, block until
 *                   the count is 1 again before decrementing.
 *     V (verhogen): increment count.
 */
void P(struct semaphore *);
void V(struct semaphore *);


/*
 * Simple lock for mutual exclusion.
 *
 * When the lock is created, no thread should be holding it. Likewise,
 * when the lock is destroyed, no thread should be holding it.
 *
 * The name field is for easier debugging. A copy of the name is
 * (should be) made internally.
 * 
 * A lock consists of a spinlock, a wait channel, and a thread pointer, lk_holder.
 * This refers to the current holder of the lock.
 * When a thread tries to acquire the lock, it checks whether lk_holder == TID_NULL.  If
 * so, it immediately acquires the lock.  Otherwise, it waits on the
 * wait channel.  When it is awoken, it checks whether the lock is now available.
 * If so, it takes the lock.  Otherwise, it goes back to sleep.
 * Upon releasing the lock, a thread clears lk_holder to pass control to the
 * next thread on the queue.
 */
struct lock {
	char *lk_name;
	struct spinlock lk_metalock;
	struct wchan *lk_wchan; // the core of the lock
	struct thread *volatile lk_holder; // current holder of the lock
};

struct lock *lock_create(const char *name);
void lock_release(struct lock *);

/*
 * Operations:
 *    lock_acquire - Get the lock. Only one thread can hold the lock at the
 *                   same time.
 *    lock_release - Free the lock. Only the thread holding the lock may do
 *                   this.
 *    lock_do_i_hold - Return true if the current thread holds the lock;
 *                   false otherwise.
 *
 * These operations must be atomic. You get to write them.
 */
void lock_acquire(struct lock *);
bool lock_do_i_hold(struct lock *);
void lock_destroy(struct lock *);


/*
 * Condition variable.
 *
 * Note that the "variable" is a bit of a misnomer: a CV is normally used
 * to wait until a variable meets a particular condition, but there's no
 * actual variable, as such, in the CV.
 *
 * These CVs are expected to support Mesa semantics, that is, no
 * guarantees are made about scheduling.
 *
 * The name field is for easier debugging. A copy of the name is
 * (should be) made internally.
 *
 * A condition variable is implemented by a wait channel that
 * logically corresponds to a condition whose value may change.
 * Signals and broadcasts correspond to wakeone and wakeall, respectively.
 */

struct cv {
	char *cv_name;
	struct wchan *cv_wchan;
};

struct cv *cv_create(const char *name);
void cv_destroy(struct cv *);

/*
 * Operations:
 *    cv_wait      - Release the supplied lock, go to sleep, and, after
 *                   waking up again, re-acquire the lock.
 *    cv_signal    - Wake up one thread that's sleeping on this CV.
 *    cv_broadcast - Wake up all threads sleeping on this CV.
 *
 * For all three operations, the current thread must hold the lock passed
 * in. Note that under normal circumstances the same lock should be used
 * on all operations with any particular CV.
 *
 * These operations must be atomic. You get to write them.
 */
void cv_wait(struct cv *cv, struct lock *lock);
void cv_signal(struct cv *cv, struct lock *lock);
void cv_broadcast(struct cv *cv, struct lock *lock);


/*
 * Primitive for mutual exclusion of readers and writers.
 *
 * The name field is for easier debugging. A copy of the name is
 * (should be) made internally.
 *
 * An rw_mutex consists of a lock, two CVs, and two integers representing
 * numbers of readers and writers.  (As there should never be more than
 * one writer, the latter is binary).  Readers wait on the reader CV
 * and writers wait on the writer CV.  A reader can acquire the mutex
 * if there are no writers present.  A writer can only acquire the mutex if
 * there are neither readers nor writers.
 */
struct rw_mutex {
    char            *rw_name;
    struct lock     *rw_lock;
    struct cv       *rw_reader_cv;
    struct cv       *rw_writer_cv;
    volatile int     rw_nreaders;
    volatile int     rw_nwriters;
};

struct rw_mutex *rw_create(const char *name);
void rw_destroy(struct rw_mutex *rw);

/*
 * Operations:
 *    rw_rlock() - Wait for opportunity to read
 *    rw_rdone() - Finish reading
 *
 *    rw_wlock() - Wait for opportunity to write
 *    rw_wdone() - Finish writing
 */
void rw_rlock(struct rw_mutex *rw);
void rw_rdone(struct rw_mutex *rw);
void rw_wlock(struct rw_mutex *rw);
void rw_wdone(struct rw_mutex *rw);


#endif /* _SYNCH_H_ */
