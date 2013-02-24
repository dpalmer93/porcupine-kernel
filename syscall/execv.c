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

#include <syscall.h>

// helper functions for argument handling
char **copyinargs(const_userptr_t argv, int *argc, int *arg_len);
void copyoutargs(userptr_t stack, const char *kargv, int argc, int arg_len);

int
sys_execv(const_userptr_t path, const_userptr_t argv)
{
    
}

char **
copyinargs(const_userptr_t argv, int *argc, int *arg_len)
{
    *argc = 0;
    
    // allocate space in kernel heap for copying arg pointers
    userptr_t *kargv = (userptr_t)kmalloc(ARGNUM_MAX * sizeof(userptr_t));
    if (kargv == NULL)
        return NULL;
    
    // try to copy the argv array
    if (copyin(argv, (void *)kargv, ARGNUM_MAX * sizeof(userptr_t)))
    {
        kfree(kargv);
        return NULL;
    }
    
    // count arguments
    for (int i = 0; i < ARGNUM_MAX; i++)
    {
        if (kargv[i] == NULL)
        {
            *argc = i + 1;
            break;
        }
    }
    if (i == ARGNUM_MAX)
    {
        kfree(kargv);
        return NULL;
    }
    
    // allocate space for the arguments themselves
    char *kargs = (char *)kmalloc(ARG_MAX);
    
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
        int len;
        if (copyinstr(kargv[i], kargs_cur, ARG_MAX, &len))
        {
            kfree(kargs);
            kfree(kargv);
            return NULL;
        }
        
        // modify the entry in kargv to point to the
        // copied string
        kargv[i] = kargs_cur;
        
        // move on to the next string, padding properly
        kargs_cur += len;
        char *kargs_cur_padded = (kargs_cur + 0x3) & ~0x3;
        while (kargs_cur < kargs_cur_padded)
        {
            *kargs_cur = 0;
            kargs_cur++;
        }
    }
    
    *arg_len = kargs_cur - kargs;
    return (char **)kargv;
}

void
copyoutargs(userptr_t stack, const char **kargv, int argc, int arg_len)
{
    kfree(kargv);
}
