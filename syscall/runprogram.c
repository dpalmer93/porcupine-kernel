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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
    struct process *proc;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	// We should be a new thread
	KASSERT(curthread->t_addrspace == NULL);

    // set up new process structure
    proc = process_create();
    if (proc == NULL)
    {
        vfs_close(v);
        return ENOMEM;
    }
    
    // Get a PID for the process.  ENPROC is
    // the error code for "no more processes allowed
    // in the system."
    pid_t pid = process_identify(proc);
    if (pid == 0)
    {
        process_cleanup(proc);
        vfs_close(v);
        return ENPROC;
    }
    
    // Create a new file descriptor table
    proc->ps_fdt = fdt_create();
    if (proc->ps_fdt == NULL)
    {
        process_destroy(pid);
        vfs_close(v);
        return ENOMEM;
    }
    
    // associate thread and process
    curthread->t_proc = proc;
    proc->ps_thread = curthread;
    
	// Create a new address space
	proc->ps_addrspace = as_create();
	if (proc->ps_addrspace==NULL) {
        process_destroy(pid);
		vfs_close(v);
		return ENOMEM;
	}
    curthread->t_addrspace = proc->ps_addrspace;

	// Activate it
	as_activate(proc->ps_addrspace);

	// Load the executable
	result = load_elf(v, &entrypoint);
	if (result) {
		process_destroy(pid);
		vfs_close(v);
		return result;
	}

	// Done with the file now
	vfs_close(v);

	// Define the user stack in the address space
	result = as_define_stack(proc->ps_addrspace, &stackptr);
	if (result) {
		process_destroy(pid);
		return result;
	}

	// Warp to user mode
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);

	// enter_new_process() does not return.
	panic("enter_new_process returned\n");
	return EINVAL;
}

