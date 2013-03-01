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
 * runprogram() runs a user program invoked from the
 * kernel menu.  As the caller has no process context,
 * a new one must be created and set up.
 * 
 * runprogram() is split into two pieces.
 * runprogram() sets up all of the process that it
 * can without activating the process' address space.
 * Then it spawns a new thread, which calls run_process().
 * run_process() loads the executable and sets up the stack
 * and arguments.
 * runprogram() passes the created process back to the menu so that
 * the menu can wait on the process' ps_waitpid_cv.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <lib.h>
#include <process.h>
#include <copyinout.h>
#include <current.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

// Data needed for a new thread to finish setting up
// the new process
struct new_process_context {
    int             nargs;
    char          **args;
    struct process *proc;
    struct vnode   *executable;
};

// helper function that calls enter_new_process() from a new thread
void run_process(void *ptr, unsigned long num);

// Helper function for setting up standard file descriptors
int setup_inouterr(struct fd_table *fdt);

/*
 * Load program and start running it in usermode
 * in a new thread.
 * This is essentially an amalgam of fork() and execv().
 */
int
runprogram(int nargs, char **args, struct process **created_proc)
{
    if (nargs > ARGNUM_MAX)
        return E2BIG;
    
	struct vnode *v;
	int result;
    struct process *proc;
    
    // copy the string, since vfs_open() will modify it
    char *progname = kstrdup(args[0]);
    if (progname == NULL)
        return ENOMEM;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
    
    // We no longer need the duplicate program name
    kfree(progname);

    // set up new process structure
    proc = process_create(args[0]);
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
    
    // Open FDs for stdin, stdout, and stderr
    result = setup_inouterr(proc->ps_fdt);
    if (result)
    {
        process_destroy(pid);
        vfs_close(v);
        return result;
    }
    
	// Create a new address space
	proc->ps_addrspace = as_create();
	if (proc->ps_addrspace==NULL) {
        process_destroy(pid);
		vfs_close(v);
		return ENOMEM;
	}
    
    struct new_process_context *ctxt = kmalloc(sizeof(struct new_process_context));
    if (ctxt == NULL)
    {
        as_activate(NULL);
        process_destroy(pid);
        return ENOMEM;
    }
    ctxt->nargs = nargs;
    ctxt->args = args;
    ctxt->proc = proc;
    ctxt->executable = v;

	// Start a new thread to warp to user mode
    result = thread_fork("user process",
                         run_process,
                         ctxt, 0, NULL);
    if (result)
    {
        kfree(ctxt);
        as_activate(NULL);
        process_destroy(pid);
        return result;
    }
    
    // pass process to caller and return
    *created_proc = proc;
	return 0;
}

void
run_process(void *ptr, unsigned long num)
{
	vaddr_t entrypoint, stackptr;
	userptr_t uargv[nargs + 1];
    
    // extract and free passed-in context
    struct new_process_context *ctxt = (struct new_process_context *)ptr;
    struct process *proc = ctxt->proc;
    struct vnode *v = ctxt->executable;
    int nargs = ctxt->nargs;
    char **args = ctxt->args;
    kfree(ctxt);
    
    pid_t pid = proc->ps_pid;
    
    // attach process to thread
    curthread->t_proc = proc;
    
	// Activate address space
	as_activate(proc->ps_addrspace);
    
	// Load the executable
	result = load_elf(v, &entrypoint);
	if (result) {
		vfs_close(v);
        kprintf("runprogram failed: %s\n", strerror(result));
        // alert the kernel menu that the process exited
        process_finish(proc, 1);
		return;
	}
    
	// Done with the file now
	vfs_close(v);
    
	// Define the user stack in the address space
	result = as_define_stack(proc->ps_addrspace, &stackptr);
	if (result) {
        kprintf("runprogram failed: %s\n", strerror(result));
        // alert the kernel menu that the process exited
        process_finish(proc, 1);
		return;
	}
	
	// Copy out arguments
    for (int i = 0; i < nargs; i++)
    {
        int aligned_length = WORD_ALIGN(strlen(args[i]) + 1);
        stackptr -= aligned_length;
        uargv[i] = (userptr_t)stackptr;
        size_t arg_len;
        result = copyoutstr(args[i], uargv[i], strlen(args[i]) + 1, &arg_len);
        if (result) {
            kprintf("runprogram failed: %s\n", strerror(result));
            // alert the kernel menu that the process exited
            process_finish(proc, 1);
            return;
        }
    }
    uargv[nargs] =(userptr_t)NULL;
    
    // Copy out the argv array itself
	stackptr -= (nargs + 1) * sizeof(userptr_t);
	result = copyout(uargv, (userptr_t)stackptr,
                     (nargs + 1) * sizeof(userptr_t));
	if (result) {
        kprintf("runprogram failed: %s\n", strerror(result));
        // alert the kernel menu that the process exited
        process_finish(proc, 1);
	    return;
	}
    
    enter_new_process(nargs, (userptr_t)stackptr, stackptr, entrypoint);
    
    // enter_new_process() does not return
	panic("enter_new_process returned\n");
}

int
setup_inouterr(struct fd_table *fdt)
{
    int err;
    struct vnode *v_stdin, *v_stdout, *v_stderr;
    struct file_ctxt *stdin, *stdout, *stderr;
    
    char path[5];
    strcpy(path, "con:");
    
    // open console vnode three times
    // we have to reset the path every time because
    // vfs_open() messes with the contents of the string
    if ((err = vfs_open(path, O_RDONLY, 0, &v_stdin)))
    {
        return err;
    }
    strcpy(path, "con:");
    if ((err = vfs_open(path, O_WRONLY, 0, &v_stdout)))
    {
        vfs_close(v_stdin);
        return err;
    }
    strcpy(path, "con:");
    if ((err = vfs_open(path, O_WRONLY, 0, &v_stderr)))
    {
        vfs_close(v_stdin);
        vfs_close(v_stdout);
        return err;
    }
    
    // create file contexts
    if((stdin = fc_create(v_stdin)) == NULL)
    {
        vfs_close(v_stdin);
        vfs_close(v_stdout);
        vfs_close(v_stderr);
        return ENOMEM;
    }
    if ((stdout = fc_create(v_stdout)) == NULL)
    {
        fc_close(stdin);
        vfs_close(v_stdout);
        vfs_close(v_stderr);
        return ENOMEM;
    }
    if ((stderr = fc_create(v_stderr)) == NULL)
    {
        fc_close(stdin);
        fc_close(stdout);
        vfs_close(v_stderr);
        return ENOMEM;
    }
    
    // insert file contexts
    fdt_replace(fdt, STDIN_FILENO, stdin);
    fdt_replace(fdt, STDOUT_FILENO, stdout);
    fdt_replace(fdt, STDERR_FILENO, stderr);
    
    return 0;
}

