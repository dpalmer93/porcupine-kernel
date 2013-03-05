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

#include <limits.h>
#include <process.h>
#include <current.h>
#include <cpu.h>
#include <lib.h>
#include <pid_set.h>

struct process *pid_table[PID_MAX + 1];
struct rw_mutex *pidt_rw;

void
process_bootstrap(void)
{
    pidt_rw = rw_create("Process Table");
}

void
process_shutdown(void)
{
    rw_destroy(pidt_rw);
}


/*
 * Set up everything that must be created anew upon fork().
 * fork() will copy everything else (address space, thread, FD table)
 */
struct process *
process_create(char *name)
{
    struct process *p;
    
    p = kmalloc(sizeof(struct process));
    if (p == NULL)
        return NULL;
    
    if (name == NULL)
        p->ps_name = NULL;
    else
    {
        p->ps_name = kstrdup(name);
        if (p->ps_name == NULL) {
            kfree(p);
            return NULL;
        }
    }
    
    // zero all pointers so that fork() can properly
    // unwind if some structures have been set up
    p->ps_thread = NULL;
    p->ps_fdt = NULL;
    p->ps_addrspace = NULL;
    p->ps_children = NULL;
    p->ps_waitpid_cv = NULL;
    p->ps_waitpid_lock = NULL;
    
    p->ps_children = pid_set_create();
    if (p->ps_children == NULL)
    {
        kfree(p->ps_name);
        kfree(p);
        return NULL;
    }
    
    p->ps_waitpid_lock = lock_create("waitpid");
    if (p->ps_waitpid_lock == NULL)
    {
        pid_set_destroy(p->ps_children);
        kfree(p->ps_name);
        kfree(p);
        return NULL;
    }
    
    p->ps_waitpid_cv = cv_create("waitpid");
    if (p->ps_waitpid_cv == NULL)
    {
        lock_destroy(p->ps_waitpid_lock);
        pid_set_destroy(p->ps_children);
        kfree(p->ps_name);
        kfree(p);
        return NULL;
    }
    
    // process starts out active
    p->ps_status = PS_ACTIVE;
    
    // process exits normally by default.
    p->ps_exit_code = 0;
    
    return p;
}


void
process_finish(struct process *p, int code)
{
    // should only be called from the process' thread
    KASSERT(p == curthread->t_proc);

    // orphan all children
    if (!pid_set_empty(p->ps_children))
        pid_set_map(p->ps_children, process_orphan);
    
    lock_acquire(p->ps_waitpid_lock);
    p->ps_status = PS_ZOMBIE;
    p->ps_exit_code = code;
    
    // disassociate process and thread so that they
    // can be properly destroyed.
    // NOTE: the parent process (or the kernel menu if
    // the process was started from the menu)
    // is responsible for freeing p
    curthread->t_proc = NULL;
    p->ps_thread = NULL;
    
    cv_signal(p->ps_waitpid_cv, p->ps_waitpid_lock);
    lock_release(p->ps_waitpid_lock);
}

// Wait on a process.  For use in runprogram() and waitpid()
int
process_waiton(struct process *p)
{
    int exit_code;
    lock_acquire(p->ps_waitpid_lock);
    while (p->ps_status == PS_ACTIVE)
        cv_wait(p->ps_waitpid_cv, p->ps_waitpid_lock);
    exit_code = p->ps_exit_code;
    lock_release(p->ps_waitpid_lock);
    return exit_code;
}

// returns -1 if the process has not exited
int
process_checkon(struct process *p)
{
    int exit_code = -1;
    lock_acquire(p->ps_waitpid_lock);
    if (p->ps_status == PS_ZOMBIE)
        exit_code = p->ps_exit_code;
    lock_release(p->ps_waitpid_lock);
    return exit_code;
}

/*
 * Assign a PID to a process.
 * Implementation can change, as long as it chooses
 * an unused PID between PID_MIN and PID_MAX (inclusive)
 */
pid_t
process_identify(struct process *p)
{
    KASSERT(p != NULL);
    
    rw_wlock(pidt_rw);
    
    // get lowest unused PID
    for (int i = PID_MIN; i <= PID_MAX; i++)
    {
        if (pid_table[i] == NULL)
        {
            pid_table[i] = p;
            rw_wdone(pidt_rw);
            p->ps_pid = i;
            return i;
        }
    }

    rw_wdone(pidt_rw);
    return 0;
}

/*
 * Tears down the process, removing it from the pid table.
 * Frees ALL associated memory and structures.
 */
void
process_destroy(pid_t pid)
{
    rw_wlock(pidt_rw);
    struct process *p = pid_table[pid];
    
    KASSERT(p->ps_thread == NULL);
    
    pid_table[pid] = NULL;
    rw_wdone(pidt_rw);
    
    // orphan all children if not already done in process_finish()
    // (this is important because kill_curthread() calls
    // process_destroy() on a process that has not properly exited.)
    if (!pid_set_empty(p->ps_children))
        pid_set_map(p->ps_children, process_orphan);
    
    process_cleanup(p);
}

// Only for use here and in fork()
void
process_cleanup(struct process *p)
{
    if (p->ps_name)
        kfree(p->ps_name);
    
    if (p->ps_addrspace)
        as_destroy(p->ps_addrspace);
    
    if (p->ps_fdt)
        fdt_destroy(p->ps_fdt);
    
    if (p->ps_children)
        pid_set_destroy(p->ps_children);
    
    if (p->ps_waitpid_lock)
        lock_destroy(p->ps_waitpid_lock);
    
    if (p->ps_waitpid_cv)
        cv_destroy(p->ps_waitpid_cv);
    
    kfree(p);
}

struct process *
process_get(pid_t pid)
{
    rw_rlock(pidt_rw);
    struct process *p = pid_table[pid];
    rw_rdone(pidt_rw);
    return p;
}

bool
process_orphan(pid_t pid)
{
    struct pid_set *orphan_set = curthread->t_cpu->c_orphans;
    pid_set_add(orphan_set, pid);
    return false;
}

bool
process_check_destroy(pid_t pid)
{
    struct process *p = process_get(pid);
    // check whether the process has exited
    if (process_checkon(p) == -1)
        return false;
    
    process_destroy(p->ps_pid);
    // returning true removes pid from the pid_set
    // during pid_set_map()
    return true;
}
