//
//  page_fault.c
//  
//
//  Created by David Palmer on 4/1/13.
//
//

#include <stdio.h>

/*
 * Handle a page fault.
 * The caller must hold the pte, i.e., have called pt_acquire_entry().
 * If pte is NULL, then there was no existing entry.
 *
 */
int
vm_page_fault(vaddr_t vaddr, pt_entry *pte)
{
    
}