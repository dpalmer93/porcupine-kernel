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
 * process.h: process system declarations.
 */

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <types.h>
#include <thread.h>
#include <synch.h>
#include <addrspace.h>
#include <pid_set.h>
#include <fdt.h>

typedef enum _pstat_t {
    PS_ACTIVE, 
    PS_ZOMBIE
} pstat_t;

struct process {
    pid_t                ps_pid;
    pstat_t              ps_status;
    int                  ps_ret_val;
    struct thread       *ps_thread;
    struct fd_table     *ps_fdt;
    struct addrspace    *ps_addrspace;
    struct pid_set      *ps_children;
    struct cv           *ps_waitpid_cv;
    struct lock         *ps_waitpid_lock;
};

void process_bootstrap(void);
void process_shutdown(void);

struct process *process_create(void); // set up process struct
void process_destroy(pid_t pid); // remove and free process struct

pid_t process_identify(struct process *p); // assign PID--returns 0 on error
struct process *process_get(pid_t pid); // get process for PID
void process_cleanup(struct process *p); // only for fork() and process.c


#endif /* _PROCESS_H_ */
