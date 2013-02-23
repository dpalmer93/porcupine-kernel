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

#include <limits.h>
#include <kern/errno.h>
#include <synch.h>
#include <process.h>

struct process *pid_table[PID_MAX];
struct rw_mutex *pidt_rw;

void
process_bootstrap(void)
{
    pidt_rw = rw_create("Process Table");
}

struct process *
process_create(void)
{
    struct process *p;
    
    p = kmalloc(sizeof(struct process))
    if (p == NULL)
        return NULL;
    
    p->ps_children = pid_set_create()
    if (p->ps_children == NULL)
    {
        kfree(p);
        return NULL;
    }
    
    p->ps_waitpid_lock = lock_create("waitpid");
    if (p->ps_waitpid_lock == NULL)
    {
        pid_set_destroy(p->ps_children);
        kfree(p);
        return NULL;
    }
    
    p->ps_waitpid_cv = cv_create("waitpid");
    if (p->ps_waitpid_cv == NULL)
    {
        lock_destroy(p->ps_waitpid_lock);
        pid_set_destroy(p->ps_children);
        kfree(p);
        return NULL;
    }
    
    return p;
}

pid_t
process_identify(struct process *p)
{
    rw_wlock(pidt_rw);
    
    for (int i = PID_MIN; i < PID_MAX; i++)
    {
        if (pid_table[i] == NULL)
        {
            pid_table[i] = p;
            rw_wdone(pidt_rw);
            return i;
        }
    }

    rw_wdone(pidt_rw);
    return (pid_t)-1;
}


void
process_destroy(pid_t pid)
{
    rw_wlock(pidt_rw);
    rw_wdone(pidt_rw);
}

struct process *get_process_from_pid(pid_t pid)
{
    rw_rlock(pidt_rw);
    rw_rlock(pidt_rw);
}
