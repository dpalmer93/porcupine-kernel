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
#include <limits.h>
#include <pid_set.h>
#include <lib.h>

#define SEGBITS 5
#define SEGMASK 0x1F
#define SEGSIZE 32

struct pid_set {
    uint32_t *bits[SEGSIZE];
};

uint32_t *allocate_subset(void);

struct pid_set *
pid_set_create(void)
{
    struct pid_set *set = kmalloc(sizeof(struct pid_set));
    if (set == NULL)
        return NULL;
    for (int i = 0; i < SEGSIZE; i++)
        set->bits[i] = NULL;
    return set;
}

bool
pid_set_includes(struct pid_set *set, pid_t pid)
{
    int index1 = (pid >> (2 * SEGBITS)) & SEGMASK;
    int index2 = (pid >> SEGBITS) & SEGMASK;
    int index3 = pid & SEGMASK;
    
    if (set->bits[index1] == NULL)
        return false;
    else
        return set->bits[index1][index2] & (1 << index3);
}

bool
pid_set_empty(struct pid_set *set)
{
    for (int i = 0; i < SEGSIZE; i++)
    {
        if (set->bits[i] != NULL)
        {
            for (int j = 0; j < SEGSIZE; j++)
            {
                if (set->bits[i][j] != 0)
                    return false;
            }
        }
    }
    return true;
}

bool
pid_set_add(struct pid_set *set, pid_t pid)
{
    int index1 = (pid >> (2 * SEGBITS)) & SEGMASK;
    int index2 = (pid >> SEGBITS) & SEGMASK;
    int index3 = pid & SEGMASK;
    
    if (set->bits[index1] == NULL)
    {
        if ((set->bits[index1] = allocate_subset()) == NULL)
            return false;
    }
    set->bits[index1][index2] |= (1 << index3);
    return true;
}

void pid_set_remove(struct pid_set *set, pid_t pid)
{
    int index1 = (pid >> (2 * SEGBITS)) & SEGMASK;
    int index2 = (pid >> SEGBITS) & SEGMASK;
    int index3 = pid & SEGMASK;
    
    if (set->bits[index1] == NULL)
        return;
    set->bits[index1][index2] &= ~(1 << index3);
}

uint32_t *
allocate_subset(void)
{
    uint32_t *subset = kmalloc(SEGSIZE * sizeof(uint32_t));
    if (subset == NULL)
        return NULL;
    for (int i = 0; i < SEGSIZE; i++)
        subset[i] = 0;
    return subset;
}
