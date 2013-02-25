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
#include <vfs.h>
#include <addrspace.h>
#include <current.h>
#include <process.h>

// helper functions for argument handling
char **copyinargs(const_userptr_t argv, int *argc, int *total_len);
int copyoutargs(userptr_t argv, char **kargv, int argc, int total_len);
void free_kargv(char **kargv);

int
sys_execv(const_userptr_t path, const_userptr_t argv)
{
    int err;
    
    char *kpath = kmalloc(PATH_MAX);
    if (kpath == NULL)
        return ENOMEM;
    
    // copy in path
    size_t path_len;
    if ((err = copyinstr(path, kpath, PATH_MAX, &path_len)))
        return err;
    
    // copy in args
    int argc;
    int total_len;
    char **kargv = copyinargs(argv, &argc, &total_len);
    if (kargv == NULL)
    {
        kfree(kpath);
        return E2BIG;
    }
    
    // Open the file.
    struct vnode *v;
	err = vfs_open(kpath, O_RDONLY, 0, &v);
	if (err)
    {
        free_kargv(kargv);
        kfree(kpath);
		return err;
    }
    
    // set up new address space
    struct addrspace *old_as = curthread->t_proc->ps_addrspace;
    struct addrspace *as = as_create();
    if (as == NULL)
    {
        vfs_close(v);
        free_kargv(kargv);
        kfree(kpath);
        return ENOMEM;
    }
    
    // Activate the new address space
    as_activate(as);
    
    vaddr_t entrypoint;
    err = load_elf(v, &entrypoint);
    if (err)
    {
        as_activate(old_as);
        as_destroy(as);
        vfs_close(v);
        free_kargv(kargv);
        kfree(kpath);
        return err;
    }
    
    // close the file now, since we will not be returning
    // here from user mode
    vfs_close(v);
    
    // set up user stack
    vaddr_t stackptr;
    err = as_define_stack(as, &stackptr);
    if (err)
    {
        as_activate(old_as);
        as_destroy(as);
        vfs_close(v);
        free_kargv(kargv);
        kfree(kpath);
        return err;
    }
    
    // copy arguments just above stack
    vaddr_t user_argv = stackptr + 4;
    err = copyoutargs((userptr_t)user_argv, kargv, argc, total_len);
    if (err)
    {
        as_activate(old_as);
        as_destroy(as);
        vfs_close(v);
        free_kargv(kargv);
        kfree(kpath);
        return err;
    }
    
    // destroy old address space
    curthread->t_proc->ps_addrspace = as;
    curthread->t_addrspace = as;
    as_destroy(old_as);
    
    // Warp to user mode
    enter_new_process(argc, (userptr_t) user_argv, stackptr, entrypoint);
    
    // enter_new_process() does not return
	panic("enter_new_process returned\n");
	return EINVAL;
}

// returns null on failure
char **
copyinargs(const_userptr_t argv, int *argc, int *total_len)
{
    *argc = 0;
    
    // allocate space in kernel heap for copying arg pointers
    userptr_t *kargv = (userptr_t *)kmalloc((ARGNUM_MAX + 1) * sizeof(userptr_t));
    if (kargv == NULL)
        return NULL;
    
    // try to copy the argv array
    if (copyin(argv, (void *)kargv, (ARGNUM_MAX + 1) * sizeof(userptr_t)))
    {
        kfree(kargv);
        return NULL;
    }
    
    // count arguments
    for (int *argc = 0; *argc < ARGNUM_MAX + 1; *argc++)
    {
        if (kargv[*argc] == NULL)
            break;
    }
    if (*argc == ARGNUM_MAX + 1)
    {
        kfree(kargv);
        return NULL;
    }
    
    // allocate space for the arguments themselves
    char *kargs = (char *)kmalloc(ARG_MAX * sizeof(char));
    
    // keep track of our position in kargs
    char *kargs_cur = kargs;
    
    // copy in each argument string
    for (int i = 0; i < *argc; i++)
    {
        // check bounds
        if (kargs_cur - kargs >= ARG_MAX)
        {
            kfree(kargs);
            kfree(kargv);
            return NULL;
        }
        
        // actually perform the copy and record the length
        int arg_len;
        if (copyinstr(kargv[i], kargs_cur, ARG_MAX, &arg_len))
        {
            kfree(kargs);
            kfree(kargv);
            return NULL;
        }
        
        // modify the entry in kargv to point to the
        // copied string
        kargv[i] = kargs_cur;
        
        // move on to the next string, padding properly
        kargs_cur += arg_len;
        char *kargs_cur_padded = (kargs_cur + 0x3) & ~0x3;
        while (kargs_cur < kargs_cur_padded)
        {
            *kargs_cur = 0;
            kargs_cur++;
        }
    }
    
    *total_len = kargs_cur - kargs;
    return (char **)kargv;
}

// returns 0 on success, error code on failure
int
copyoutargs(userptr_t argv, char **kargv, int argc, int total_len)
{
    // allocate space for argv array and null terminator
    userptr_t start_of_args = argv + argc + 1;
    int err;
    
    // copy strings "in bulk"
    if(err = copyout(kargv[0], start_of_args, total_len))
    {
        free_kargv(kargv);
        return err;
    }
    
    // update pointers
    userptr_t *uargv = (userptr_t)kargv;
    for (int i = 0; i < argc; i++)
    {
        uargv[i] = uargv[i] - uargv[0] + start_of_args;
    }
    
    // copy argv itself
    if (err = copyout(uargv, argv, argc + 1))
    {
        free_kargv(kargv);
        return err;
    }
    
    // free temporary buffers
    free_kargv(kargv);
    
    return 0;
}

void
free_kargv(char **kargv)
{
    kfree(kargv[0]);
    kfree(kargv);
}
