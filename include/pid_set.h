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
 *
 * pid_set.h: Abstract set of PIDs.
 */

#ifndef _PID_SET_H_
#define _PID_SET_H_

#include <types.h>

struct pid_set;

// pid_set_create() returns NULL if out of memory
struct pid_set *pid_set_create(void);
void pid_set_destroy(struct pid_set *set);

bool pid_set_empty(struct pid_set *set);
bool pid_set_includes(struct pid_set *set, pid_t pid);

// pid_set_add() returns ENOMEM if out of memory
int pid_set_add(struct pid_set *set, pid_t pid);
void pid_set_remove(struct pid_set *set, pid_t pid);

// Maps <func> once over the set of PIDs, removing
// a PID p if and only if func(p) returns true.  That is,
// pid_set_map(set, func) does the following:
//     FOR pid IN set:
//       IF func(pid) THEN pid_set_remove(set, pid);
void pid_set_map(struct pid_set *set, bool (*func)(pid_t));


#endif /* _PID_SET_H_ */
