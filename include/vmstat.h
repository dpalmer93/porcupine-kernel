/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008
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

#ifndef _VMSTAT_H_
#define _VMSTAT_H_

#include <types.h>
#include <cdefs.h>
#include <kern/vmstat.h>
#include <spinlock.h>
#include "opt-vmstat.h"

// inline to reduce overhead
#ifndef VMSTAT_INLINE
#define VMSTAT_INLINE INLINE
#endif

// global record of VM statistics
#if OPT_VMSTAT
struct vmstat vs_global;
#endif

// atomic increment and decrement functions

#define VS_DECL(STAT) \
    VMSTAT_INLINE void vs_incr_##STAT(void);    \
    VMSTAT_INLINE void vs_decr_##STAT(void);

#if OPT_VMSTAT
#define VS_IMPL(STAT) \
struct spinlock vs_##STAT##_lock = SPINLOCK_INITIALIZER; \
                                                \
    VMSTAT_INLINE void                          \
    vs_incr_##STAT(void) {                      \
        spinlock_acquire(&vs_##STAT##_lock);    \
        vs_global.vs_##STAT++;                  \
        spinlock_release(&vs_##STAT##_lock);    \
    }                                           \
                                                \
    VMSTAT_INLINE void                          \
    vs_decr_##STAT(void) {                      \
        spinlock_acquire(&vs_##STAT##_lock);    \
        vs_global.vs_##STAT--;                  \
        spinlock_release(&vs_##STAT##_lock);    \
    }

#else
#define VS_IMPL(STAT)       \
    VMSTAT_INLINE void          \
    vs_incr_##STAT(void) {}     \
    VMSTAT_INLINE void          \
    vs_decr_##STAT(void) {}
#endif


void vs_init_ram(size_t npages, size_t nwired);
void vs_init_swap(size_t npages);

// Physical memory statistics
VS_DECL(ram_free);
VS_DECL(ram_active);
VS_DECL(ram_inactive);
VS_DECL(ram_wired);
VS_DECL(ram_dirty);

// Swap statistics
VS_DECL(swap_free);
VS_DECL(swap_ins);
VS_DECL(swap_outs);

// VM system statistics
VS_DECL(faults);
VS_DECL(cow_faults);

#endif /* _VMSTAT_H_ */
