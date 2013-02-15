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
 * Driver code for the Piazza synch problem.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <generic/random.h>

#include "common.h"

// A Piazza 'answer' consists of ten identical lowercase characters
#define ANSWER_LENGTH 10


/**
 * struct piazza_question - Object representing a question on Piazza.
 */
struct piazza_question {
  char        *pq_answer;
  struct lock *pq_lock;
  struct cv   *pq_student_cv;
  struct cv   *pq_instructor_cv;
  int          pq_nstudents;  // Number of students reading
  int          pq_instructor; // Boolean representing instructor present
};

struct piazza_question *questions[NANSWERS] = { 0 };


// global synchronization of exiting
// and printing
struct lock *print_lock;
struct semaphore *done_sem;

static struct piazza_question *
create_question()
{
  struct piazza_question *q = kmalloc(sizeof(struct piazza_question));
  
  // question creator gets 'first dibs' on edititng
  q->pq_instructor = true;
  q->pq_nstudents = 0;
  
  q->pq_lock = lock_create("question");
  q->pq_student_cv = cv_create("student");
  q->pq_instructor_cv = cv_create("instructor");
  
  q->pq_answer = kmalloc((ANSWER_LENGTH + 1) * sizeof(char));
  for (int i = 0; i < ANSWER_LENGTH; i++)
    q->pq_answer[i] = 'a';
  q->pq_answer[ANSWER_LENGTH] = '\0';
  
  return q;
}

static void
destroy_question(struct piazza_question *q)
{
  KASSERT(q != NULL);
  cv_destroy(q->pq_instructor_cv);
  cv_destroy(q->pq_student_cv);
  lock_destroy(q->pq_lock);
  kfree(q->pq_answer);
  kfree(q);
}

static void
piazza_print(int id)
{
  KASSERT(id < NANSWERS);
  
  lock_acquire(print_lock);
  kprintf("[%2d] %s\n", id, questions[id]->pq_answer);
  lock_release(print_lock);
}

/**
 * student - Piazza answer-reading thread.
 *
 * The student threads repeatedly choose a random Piazza question for which to
 * read the instructors' answer.  Unlike CS 161 students, these students are
 * very slow and after reading each character, need some time to rest and thus
 * deschedule themselves.
 *
 * You may add as much synchronization code as you wish to this function, but
 * you may not change the way the students read.
 */
static void
student(void *p, unsigned long which)
{
  (void)p;

  int i, n;
  char letter, *pos;

  for (i = 0; i < NCYCLES; ++i) {
    // Choose a random Piazza question.
    n = random() % NANSWERS;

    // If the instructors haven't seen the question yet, try again.
    if (questions[n] == NULL) {
      --i;
      continue;
    }
    
    
    lock_acquire(questions[n]->pq_lock);
    while (questions[n]->pq_instructor)
      cv_wait(questions[n]->pq_student_cv, questions[n]->pq_lock);
    questions[n]->pq_nstudents++;
    lock_release(questions[n]->pq_lock);

    pos = questions[n]->pq_answer;
    letter = *pos;

    // Read the answer slowly.
    while (*(++pos) == letter) {
      thread_yield();
    }

    // If the answer changes while we're reading it, panic!  Panic so much that
    // the kernel explodes.
    if (*pos != '\0') {
      panic("[%d:%d] Inconsistent answer!\n", (int)which, n);
    }
    
    lock_acquire(questions[n]->pq_lock);
    questions[n]->pq_nstudents--;
    if (questions[n]->pq_nstudents == 0)
      cv_signal(questions[n]->pq_instructor_cv, questions[n]->pq_lock);
    lock_release(questions[n]->pq_lock);
  }
  
  V(done_sem);
}

/**
 * instructor - Piazza answer-editing thread.
 *
 * Each instructor thread should, for NCYCLES iterations, choose a random
 * Piazza question and then update the answer.  The answer should always
 * consist of a lowercase alphabetic character repeated 10 times, e.g.,
 *
 *    "aaaaaaaaaa"
 *
 * and each update should increment all ten characters (cycling back to a's
 * from z's if a question is updated enough times).
 *
 * After each update, (including the first update, in which you should create
 * the question and initialize the answer to all a's), the instructor should
 * print the answer string using piazza_print().
 *
 * TODO: Implement this.
 */
static void
instructor(void *p, unsigned long which)
{
  (void)p;
  (void)which;
  
  for (int i = 0; i < NCYCLES; ++i) {
    // Choose a random Piazza question.
    int n = random() % NANSWERS;
    
    if (questions[n] == NULL)
      questions[n] = create_question();
    else
    {
      lock_acquire(questions[n]->pq_lock);
      while (questions[n]->pq_nstudents > 0 || questions[n]->pq_instructor > 0)
        cv_wait(questions[n]->pq_instructor_cv, questions[n]->pq_lock);
      questions[n]->pq_instructor = true;
      lock_release(questions[n]->pq_lock);

      // Now we have exclusive access to this question...
      for (char *c = questions[n]->pq_answer; *c != '\0'; c++)
      {
        (*c)++;
        if (*c > 'z')
          *c = 'a';
      }
    }
    
    piazza_print(n);
    
    // Finished editing; now time to tell the world!
    lock_acquire(questions[n]->pq_lock);
    questions[n]->pq_instructor = false;
    cv_broadcast(questions[n]->pq_student_cv, questions[n]->pq_lock);
    cv_signal(questions[n]->pq_instructor_cv, questions[n]->pq_lock);
    lock_release(questions[n]->pq_lock);
  }

  V(done_sem);
}

/**
 * piazza - Piazza synch problem driver routine.
 *
 * You may modify this function to initialize any synchronization primitives
 * you need; however, any other data structures you need to solve the problem
 * must be handled entirely by the forked threads (except for some freeing at
 * the end).
 *
 * Make sure you don't leak any kernel memory!  Also, try to return the test to
 * its original state so it can be run again.
 */
int
piazza(int nargs, char **args)
{
  int i, n;

  (void)nargs;
  (void)args;
  
  print_lock = lock_create("print");
  done_sem = sem_create("done", 0);

  for (i = 0; i < NSTUDENTS; ++i) {
    thread_fork_or_panic("student", student, NULL, i, NULL);
  }
  for (i = 0; i < NINSTRUCTORS; ++i) {
    thread_fork_or_panic("instructor", instructor, NULL, i, NULL);
  }
  
  for (i = 0, n = NSTUDENTS + NINSTRUCTORS; i < n; i++)
    P(done_sem);
    
  for (i = 0; i < NANSWERS; i++)
  {
    if (questions[i] != NULL)
      destroy_question(questions[i]);
  }

  lock_destroy(print_lock);
  sem_destroy(done_sem);

  return 0;
}
