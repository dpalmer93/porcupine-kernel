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
#include <kern/errno.h>
#include <process.h>
#include <machine/trapframe.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>

// defined in process.c
void process_cleanup(struct process *p);

int
sys_fork(const struct trapframe *parent_tf, int *err)
{
    struct process *parent = curthread->t_proc;
    
    // set up new process structure
    struct process *child = process_create(parent->ps_name);
    if (child == NULL)
    {
        *err = ENOMEM;
        return -1;
    }
    
    // Get a PID for the child.  ENPROC is
    // the error code for "no more processes allowed
    // in the system."
    pid_t child_pid = process_identify(child);
    if (child_pid == 0)
    {
        *err = ENPROC;
        process_cleanup(child);
        return -1;
    }
    
    // copy the file descriptor table of the parent
    child->ps_fdt = fdt_copy(parent->ps_fdt);
    if (child->ps_fdt == NULL)
    {
        *err = ENOMEM;
        process_destroy(child_pid);
        return -1;
    }
    
    // copy the address space of the parent
    *err = as_copy(parent->ps_addrspace,
                   &child->ps_addrspace);
    if (*err)
    {
        process_destroy(child_pid);
        return -1;
    }
    
    // add PID to children now.  That way, if we fail to
    // allocate memory, we have not yet forked a thread
    *err = pid_set_add(parent->ps_children, child_pid)
    if (*err)
    {
        process_destroy(child_pid);
        return -1;
    }
    
    // allocate space for child trapframe in the kernel heap
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL)
    {
        process_destroy(child_pid);
        pid_set_remove(parent->ps_children, child_pid);
        *err = ENOMEM;
        return -1;
    }
    
    // copy trapframe
    memcpy(child_tf, parent_tf, sizeof(struct trapframe));
    
    // abuse child_tf->TF_RET (which will be set to 0)
    // to pass the process struct to the child thread
    // this cast and assignment will always work,
    // as pointers always fit in machine registers
    child_tf->TF_RET = (uintptr_t)child;
    
    // child thread sets up child return value
    // and ps_thread/t_proc
    *err = thread_fork("user process",
                       enter_forked_process,
                       child_tf, 0,
                       NULL);
    if (*err)
    {
        process_destroy(child_pid);
        kfree(child_tf);
        pid_set_remove(parent->ps_children, child_pid);
        return -1;
    }
    
    return child_pid;
}
