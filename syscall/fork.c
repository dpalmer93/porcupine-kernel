/*
 * Copyright (c) 2000, 2001
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
#include <errno.h>
#include <machine/trapframe.h>
#include <syscall.h>

// defined in process.c
void process_cleanup(struct process *p);

int
sys_fork(const struct trapframe *parent_tf, int *errno)
{
    struct process *parent = curthread->t_proc;
    
    // set up new process structure
    struct process *child = process_create();
    if (child == NULL)
    {
        *errno = ENOMEM;
        return -1;
    }
    
    // Get a PID for the child.  ENPROC is
    // the error code for "no more processes allowed
    // in the system."
    pid_t child_pid = process_identify(child);
    if (child_pid == 0)
    {
        *errno = ENPROC;
        process_cleanup(child);
        return -1;
    }
    
    // copy the file descriptor table of the parent
    child->ps_fdt = fdt_copy(parent->ps_fdt);
    if (child->ps_fdt == NULL)
    {
        *errno = ENOMEM;
        process_destroy(child_pid);
    }
    
    // copy the address space of the parent
    *errno = as_copy(parent->ps_addrspace,
                     &child->ps_addrspace)
    if (errno)
    {
        process_destroy(child_pid);
        return -1;
    }
    
    // copy trapframe
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    memcpy(child_tf, parent_tf, sizeof(struct trapframe));
    
    // abuse child_tf->TF_RET (which will be set to 0)
    // to pass the process struct to the child thread
    // this cast and assignment will always work,
    // as pointers always fit in machine registers
    child_tf->TF_RET = (uintptr_t)p;
    
    // child thread sets up child return value
    // and ps_thread/t_proc
    *errno = thread_fork("user process",
                         enter_forked_process,
                         child_tf, 0,
                         NULL);
    if (errno)
    {
        process_destroy(child_pid);
    }
    
    
    pid_set_add(parent->ps_children, child_pid);
    return child_pid;
}
