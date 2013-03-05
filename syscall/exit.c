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

#include <types.h>
#include <kern/wait.h>
#include <kern/errno.h>
#include <pid_set.h>
#include <process.h>
#include <current.h>
#include <lib.h>
#include <copyinout.h>
#include <syscall.h>

int
sys__exit(int code)
{
    struct process *proc = curthread->t_proc;    
    
    process_finish(proc, code);
    
    thread_exit();
    
    // should not return from thread_exit()
	panic("thread_exit() returned\n");
	return EINVAL;
}

int sys_waitpid(pid_t pid, userptr_t stat_loc, int options, int *err)
{
    int exit_code;
    
    if (pid > PID_MAX || pid < PID_MIN)
        return false;
    
    struct process *proc = curthread->t_proc;
    struct pid_set *children = proc->ps_children;
    if (!pid_set_includes(children, pid))
    {
        *err = ECHILD;
        return -1;
    }
    
    struct process *child = process_get(pid);
    // The child is in our PID set, so it must exist
    KASSERT(child != NULL);
    
    if (options & WNOHANG)
    {
        exit_code = process_checkon(child);
        if (exit_code == -1)
            return 0;
    }
    else exit_code = process_waiton(child);
    
    pid_set_remove(children, child->ps_pid);
    process_destroy(child->ps_pid);
    
    if ((*err = copyout(&exit_code, stat_loc, sizeof(int))))
        return -1;
    
    return pid;
}
