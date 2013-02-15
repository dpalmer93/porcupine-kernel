/*
 * Copyright (c) 2013
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

/**
 * Driver code for The Fellowship of the Ring synch problem.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>

#include "common.h"


///////////////////////////////////////////////////////////////////////////////
//
//  Name functions for the races of Middle-Earth.
//
//  Your solution should print NFOTRS full fellowships to stdout, each on a
//  separate line.  Each such fellowship should have the form:
//
//    FELLOWSHIP: wizard, man, man, elf, dwarf, hobbit, hobbit, hobbit, hobbit
//
//  where each member of each race is identified by name using these helper
//  routines (e.g., nameof_istari()). The threads can exit once the full
//  fellowship is printed, and should also print out their names as they do so:
//
//    LEAVING: name
//

#define NAMEOF_FUNC(race)   \
  static const char *       \
  nameof_##race(int which)  \
  {                         \
    return race[which];     \
  }

NAMEOF_FUNC(istari);
NAMEOF_FUNC(menfolk);
NAMEOF_FUNC(eldar);
NAMEOF_FUNC(khazad);
NAMEOF_FUNC(hobbitses);

#undef NAMEOF_FUNC


///////////////////////////////////////////////////////////////////////////////
//
//  DESIGN PRINCIPLES
//  =================
//  Correctness conditions:
//  - Each thread must join exactly one fellowship
//  - Each fellowship must comprise exactly one wizard, two men,
//    one elf, one dwarf, and four hobbits
//  - The composition of each fellowship must be printed exactly once
//  - No thread may exit until its fellowship has been printed
//  - The driver thread may not return until all other threads have exited 
//
//  This is a barrier problem.  As such, it can be implemented most easily
//  using condition variables.  As Doeppner describes (pg. 70), a simple barrier
//  can be implemented with a condition variable, a lock, and a "generation"
//  counter that lets threads know when to exit the barrier.  This "compound"
//  barrier will require more-finely-grained synchronization, but the idea is
//  the same.
//

/*
#define WIZARD  (0x1 << 8)
#define MAN     (0x1 << 6)
#define ELF     (0x1 << 5)
#define DWARF   (0x1 << 4)
#define HOBBIT  (0x1 << 0)
#define ALL     (0x1FF   )*/
#define FOTR_SIZE 9

typedef struct
{
  const char  *m_name;
  struct cv   *m_cv;
  struct lock *m_lock;
} member;

static void
init_members(member *mems, int length, const char *name)
{
  struct lock *common_lock = lock_create(name);
  struct cv *common_cv = cv_create(name);
  for (int i = 0; i < length; i++)
  {
    mems[i].m_name = NULL;
    mems[i].m_cv = common_cv;
    mems[i].m_lock = common_lock;
  }
}

// Below, we assume that the array of members
// has been properly initialized, i.e.,
// that
//    mems[i]->m_cv == mems[j]->m_cv
// and
//    mems[i]->m_lock == mems[j]->m_lock
// for all i,j.

static void
destroy_members(member *mems)
{
  lock_destroy(mems[0].m_lock);
  cv_destroy(mems[0].m_cv);
}

//////////////////////////////////////////////
// CENTRAL DATA STRUCTURE
struct fotr_t
{
  // wizard mutex
  struct lock *warlock;

  // data structures to hold members
  member men[2];
  member elf;
  member dwarf;
  member hobbits[4];
  
  // generation count for implementing barrier
  int generation;
} fotr;
//////////////////////////////////////////////

// Join an array of fellowship members.
// The second argument is the length of
// the array.  The third argument is the
// member name.  The return
// value is the current generation of
// the fellowship.
static int
mem_join(member *mems, int length, const char *name)
{
  int i;
  lock_acquire(mems[0].m_lock);
  while (true)
  {
    for (i = 0; i < length; i++)
    {
      if (mems[i].m_name == NULL)
      {
        goto do_join;
      }
    }
    cv_wait(mems[0].m_cv, mems[0].m_lock);
  }
  do_join:
    mems[i].m_name = name;
    int mygen = fotr.generation;
    cv_broadcast(mems[i].m_cv, mems[i].m_lock);
    lock_release(mems[i].m_lock);
    return mygen;
}

static void
mem_wait(member *mem, int mygen)
{
  lock_acquire(mem->m_lock);
  while (fotr.generation == mygen)
    cv_wait(mem->m_cv, mem->m_lock);
  lock_release(mem->m_lock);
}

static void
mem_clear(member *mem)
{
  lock_acquire(mem->m_lock);
  mem->m_name = NULL;
  cv_broadcast(mem->m_cv, mem->m_lock);
  lock_release(mem->m_lock);
}

// global synchronization of exiting
// and printing
struct lock *print_lock;
struct semaphore *done_sem;

// Print name and leave
static void
leave(const char *name)
{
  lock_acquire(print_lock);
  kprintf("LEAVING:\t%s\n", name);
  lock_release(print_lock);
}


/*
 * The wizard completes the fellowship by waiting
 * for the other members to join, then releasing
 * them from the barrier.  The assymetry between
 * the wizard and the other threads prevents a deadlock
 * that could otherwise occur from all of the fellowship members
 * waiting on each other's CVs.
 */
static void
wizard(void *p, unsigned long which)
{
  (void)p;
  
  const char *names[FOTR_SIZE];
  names[0] = nameof_istari(which);
  int name_idx = 1;
  
  // only one Wizard is allowed to be in this 'warlock'
  // critical section at a time
  lock_acquire(fotr.warlock);
  for (member *m = &fotr.men[0]; m <= &fotr.hobbits[3]; m++)
  {
    lock_acquire(m->m_lock);
    while (m->m_name == NULL)
      cv_wait(m->m_cv, m->m_lock);
    names[name_idx] = m->m_name;
    name_idx++;
    lock_release(m->m_lock);
  }
  fotr.generation++;
  
  lock_acquire(print_lock);
  kprintf("FELLOWSHIP:\t%s, %s, %s, %s, %s, %s, %s, %s, %s\n",
          names[0], names[1], names[2], names[3], names[4],
          names[5], names[6], names[7], names[8]);
  lock_release(print_lock);
  
  // The wizard now clears the way for the next fellowship
  for (member *m = &fotr.men[0]; m <= &fotr.hobbits[3]; m++)
    mem_clear(m);
  lock_release(fotr.warlock);
  
  leave(nameof_istari(which));
  V(done_sem);
}

// Each non-Istari member waits to join the fellowship, then
// waits to be allowed to leave the fellowship.

static void
man(void *p, unsigned long which)
{
  (void)p;
  
  int mygen = mem_join(fotr.men, 2, nameof_menfolk(which));
  mem_wait(&fotr.men[0], mygen);
  leave(nameof_menfolk(which));
  V(done_sem);
}

static void
elf(void *p, unsigned long which)
{
  (void)p;
  
  int mygen = mem_join(&fotr.elf, 1, nameof_eldar(which));
  mem_wait(&fotr.elf, mygen);
  leave(nameof_eldar(which));
  V(done_sem);
}

static void
dwarf(void *p, unsigned long which)
{
  (void)p;
  
  int mygen = mem_join(&fotr.dwarf, 1, nameof_khazad(which));
  mem_wait(&fotr.dwarf, mygen);
  leave(nameof_khazad(which));
  V(done_sem);
}

static void
hobbit(void *p, unsigned long which)
{
  (void)p;
  
  int mygen = mem_join(fotr.hobbits, 4, nameof_hobbitses(which));
  mem_wait(&fotr.hobbits[0], mygen);
  leave(nameof_hobbitses(which));
  V(done_sem);
}

/**
 * fellowship - Fellowship synch problem driver routine.
 *
 * You may modify this function to initialize any synchronization primitives
 * you need; however, any other data structures you need to solve the problem
 * must be handled entirely by the forked threads (except for some freeing at
 * the end).  Feel free to change the thread forking loops if you wish to use
 * the same entrypoint routine to implement multiple Middle-Earth races.
 *
 * Make sure you don't leak any kernel memory!  Also, try to return the test to
 * its original state so it can be run again.
 */
int
fellowship(int nargs, char **args)
{
  int i, n;

  (void)nargs;
  (void)args;
  
  print_lock = lock_create("print");
  done_sem = sem_create("done", 0);
  
  fotr.warlock = lock_create("wizard");
  init_members(fotr.men, 2, "men");
  init_members(&fotr.elf, 1, "elf");
  init_members(&fotr.dwarf, 1, "dwarf");
  init_members(fotr.hobbits, 4, "hobbits");
  fotr.generation = 0;

  for (i = 0; i < NFOTRS; ++i) {
    thread_fork_or_panic("wizard", wizard, NULL, i, NULL);
  }
  for (i = 0; i < NFOTRS; ++i) {
    thread_fork_or_panic("elf", elf, NULL, i, NULL);
  }
  for (i = 0; i < NFOTRS; ++i) {
    thread_fork_or_panic("dwarf", dwarf, NULL, i, NULL);
  }
  for (i = 0, n = NFOTRS * MEN_PER_FOTR; i < n; ++i) {
    thread_fork_or_panic("man", man, NULL, i, NULL);
  }
  for (i = 0, n = NFOTRS * HOBBITS_PER_FOTR; i < n; ++i) {
    thread_fork_or_panic("hobbit", hobbit, NULL, i, NULL);
  }
  
  for (i = 0, n = NFOTRS * FOTR_SIZE; i < n; i++)
  {
    P(done_sem);
  }
  
  lock_destroy(fotr.warlock);
  destroy_members(fotr.men);
  destroy_members(&fotr.elf);
  destroy_members(&fotr.dwarf);
  destroy_members(fotr.hobbits);
  
  lock_destroy(print_lock);
  sem_destroy(done_sem);

  return 0;
}
