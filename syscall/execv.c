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

#include <kern/errno.h>
#include <syscall.h>
#include <lib.h>
#include <limits.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <addrspace.h>
#include <current.h>
#include <process.h>

// helper functions for argument handling
int copyinargs(const_userptr_t argv, char **kargv, int *argc, size_t *total_len);
int copyoutargs(userptr_t argv, char **kargv, int argc, size_t total_len);
void free_kargv(char **kargv);

int
sys_execv(const_userptr_t path, const_userptr_t argv)
{
    int err;
    struct process *proc = curthread->t_proc;
    
    char *kpath = kmalloc(PATH_MAX);
    if (kpath == NULL)
        return ENOMEM;
    
    // copy in path
    size_t path_len;
    if ((err = copyinstr(path, kpath, PATH_MAX, &path_len)))
    {
        kfree(kpath);
        return err;
    }
    
    // give the process a new name
    char *old_name = proc->ps_name;
    proc->ps_name = kstrdup(kpath);
    if (proc->ps_name == NULL)
    {
        proc->ps_name = old_name;
        kfree(kpath);
        return ENOMEM;
    }
    
    // copy in args
    int argc;
    size_t total_len;
    char *kargv[ARGNUM_MAX + 1];
    err = copyinargs(argv, kargv, &argc, &total_len);
    if (err)
    {
        kfree(proc->ps_name);
        proc->ps_name = old_name;
        kfree(kpath);
        return err;
    }
    
    // Open the file.
    struct vnode *v;
	err = vfs_open(kpath, O_RDONLY, 0, &v);
    kfree(kpath);
	if (err)
    {
        free_kargv(kargv);
        kfree(proc->ps_name);
        proc->ps_name = old_name;
		return err;
    }
    
    // set up new address space
    struct addrspace *old_as = curthread->t_proc->ps_addrspace;
    struct addrspace *as = as_create();
    if (as == NULL)
    {
        vfs_close(v);
        free_kargv(kargv);
        kfree(proc->ps_name);
        proc->ps_name = old_name;
        return ENOMEM;
    }
    proc->ps_addrspace = as;
    
    // Activate the new address space
    as_activate(as);
    
    vaddr_t entrypoint;
    err = load_elf(v, as, &entrypoint);
    if (err)
    {
        proc->ps_addrspace = old_as;
        as_activate(old_as);
        as_destroy(as);
        vfs_close(v);
        free_kargv(kargv);
        kfree(proc->ps_name);
        proc->ps_name = old_name;
        return err;
    }
    
    // close the file now, since we will not be returning
    // here from user mode.
    vfs_close(v);
    
    // set up user stack
    vaddr_t stackptr;
    err = as_define_stack(as, &stackptr);
    if (err)
    {
        proc->ps_addrspace = old_as;
        as_activate(old_as);
        as_destroy(as);
        free_kargv(kargv);
        kfree(proc->ps_name);
        proc->ps_name = old_name;
        return err;
    }
    
    // copy arguments at base of stack
    stackptr -= total_len;
    stackptr -= (argc + 1) * sizeof(userptr_t);
    err = copyoutargs((userptr_t)stackptr, kargv, argc, total_len);
    if (err)
    {
        proc->ps_addrspace = old_as;
        as_activate(old_as);
        as_destroy(as);
        free_kargv(kargv);
        kfree(proc->ps_name);
        proc->ps_name = old_name;
        return err;
    }
    
    // destroy old address space and free memory
    // that we will no longer need
    as_destroy(old_as);
    free_kargv(kargv);
    kfree(old_name);
    
    
    // Warp to user mode
    enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);
    
    // enter_new_process() does not return
	panic("enter_new_process returned\n");
	return EINVAL;
}

// returns error code on failure
int
copyinargs(const_userptr_t argv, char **kargv, int *argc_ret, size_t *total_len)
{
    int err;
    int argc;
    
    userptr_t uargv[ARGNUM_MAX + 1];
    
    // try to copy the argv array
    err = copyin(argv, (void *)uargv, (ARGNUM_MAX + 1) * sizeof(userptr_t));
    if (err)
    {
        return err;
    }
    
    // count arguments
    for (argc = 0; argc < ARGNUM_MAX + 1; argc++)
    {
        if (uargv[argc] == NULL)
            break;
    }
    if (argc == ARGNUM_MAX + 1)
        return E2BIG;
    
    // allocate space for the arguments themselves
    char *kargs = (char *)kmalloc(ARG_MAX * sizeof(char));
    
    // keep track of our position in kargs
    char *kargs_cur = kargs;
    
    // copy in each argument string
    for (int i = 0; i < argc; i++)
    {
        // check bounds
        if (kargs_cur - kargs >= ARG_MAX)
        {
            kfree(kargs);
            return E2BIG;
        }
        
        // actually perform the copy and record the length
        size_t arg_len;
        err = copyinstr(uargv[i], kargs_cur, ARG_MAX, &arg_len);
        if (err)
        {
            kfree(kargs);
            if (err == ENAMETOOLONG)
                return E2BIG;
            else
                return err;
        }
        
        // set the entry in kargv to point to the
        // copied string
        kargv[i] = kargs_cur;
        
        // move on to the next string, padding properly
        kargs_cur += arg_len;
        char *kargs_cur_padded = (char *)WORD_ALIGN((uintptr_t)kargs_cur);
        while (kargs_cur < kargs_cur_padded)
        {
            *kargs_cur = 0;
            kargs_cur++;
        }
    }
    
    *argc_ret = argc;
    *total_len = kargs_cur - kargs;
    return 0;
}

// returns 0 on success, error code on failure
int
copyoutargs(userptr_t argv, char **kargv, int argc, size_t total_len)
{
    int err;
    userptr_t uargv[argc + 1];
    
    // allocate space for argv array and null terminator
    userptr_t start_of_args = argv + (argc + 1) * sizeof(uintptr_t);
    
    // copy strings "in bulk"
    err = copyout(kargv[0], start_of_args, total_len);
    if(err)
    {
        free_kargv(kargv);
        return err;
    }
    
    // get arg user pointers
    for (int i = 0; i < argc; i++)
    {
        uargv[i] = (kargv[i] - kargv[0]) + start_of_args;
    }
    uargv[argc] = NULL;
    
    // copy argv itself
    err = copyout(uargv, argv, (argc + 1) * sizeof(uintptr_t));
    if (err)
    {
        free_kargv(kargv);
        return err;
    }
    
    // free temporary buffer
    free_kargv(kargv);
    
    return 0;
}

void
free_kargv(char **kargv)
{
    kfree(kargv[0]);
}
