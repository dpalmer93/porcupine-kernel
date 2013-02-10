/*
 * Copyright (c) 2001, 2002, 2009
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

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NMATING 10

struct semaphore *print_lock;

struct threesome
{
    struct semaphore *msem;
    struct semaphore *fsem;
    struct semaphore *mmsem;
    int male;
    int female;
    int generation;
} threesome;

static
void
male(void *p, unsigned long which)
{
	(void)p;
	P(print_lock);
	kprintf("male whale #%ld starting\n", which);
	V(print_lock);

	// Implement this function
	P(threesome.msem);
	threesome.male = 1;
    int my_generation = threesome.generation;
	V(threesome.msem);
	while (threesome.generation == my_generation)
	   P(threesome.msem);
	
	P(print_lock);
	kprintf("female whale #%ld done\n", which);
	V(print_lock);
}

static
void
female(void *p, unsigned long which)
{
	(void)p;
	P(print_lock);
	kprintf("female whale #%ld starting\n", which);
	V(print_lock);

	// Implement this function
	P(threesome.fsem);
	threesome.female = 1;
	int my_generation = threesome.generation;
	V(threesome.fsem);
    while (threesome.generation == my_generation)
       P(threesome.fsem);
    
    P(print_lock);
    kprintf("female whale #%ld done\n", which);
    V(print_lock);
}

static
void
matchmaker(void *p, unsigned long which)
{
	(void)p;
	P(print_lock);
	kprintf("matchmaker whale #%ld starting\n", which);
	V(print_lock);

	// Implement this function
    if (threesome.male == 0 || threesome.female == 0)
    {
        P(threesome.msem);
        P(threesome.fsem);
    }
    threesome.generation++;
    threesome.male = 0;
    threesome.female = 0;
	V(threesome.fsem);
	V(threesome.msem);
	
	P(print_lock);
	kprintf("matchmaker whale #%ld done\n", which);
	V(print_lock);
}


// Change this function as necessary
int
whalemating(int nargs, char **args)
{

	int i, j, err=0;

	(void)nargs;
	(void)args;
	
	print_lock = sem_create("print", 1);
	
	threesome.msem = sem_create("male", 1);
	threesome.fsem = sem_create("female", 1);
	threesome.mmsem = sem_create("matchmaker", 1);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < NMATING; j++) {
			switch(i) {
			    case 0:
				err = thread_fork("Male Whale Thread",
						  male, NULL, j, NULL);
				break;
			    case 1:
				err = thread_fork("Female Whale Thread",
						  female, NULL, j, NULL);
				break;
			    case 2:
				err = thread_fork("Matchmaker Whale Thread",
						  matchmaker, NULL, j, NULL);
				break;
			}
			if (err) {
				panic("whalemating: thread_fork failed: %s)\n",
				      strerror(err));
			}
		}
	}
	
	while (threesome.generation < NMATING);

	return 0;
}
