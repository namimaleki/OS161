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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <current.h>
#include <current.h>
#include <coremap.h>  
#include <spl.h>
#include <mips/tlb.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

 /* We use:
 *   - A linked list of regions to describe the layout and permissions
 *     of the process's virtual memory (code, data, heap, etc.).
 *   - A 2-level page table to map virtual pages to physical frames:
 *       L1 index = bits [31:22]
 *       L2 index = bits [21:12]
 *
 * The TLB is used as a cache of these mappings; on a TLB miss,
 * vm_fault() looks up the mapping in this 2-level page table.
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/* no regions yet */
	as->regions = NULL; 

	/*
	 * Initialize page table
	 */
	for (int i = 0; i < PT_L1_SIZE; i++){
		as->pt_l1[i] = NULL; 
	}

	/* Initialize heap bounds (initially empty) */
	as->heap_base = 0; 
	as->heap_end = 0; 

	/* Initialize stack bounds */
	as->stack_base = 0; 
	as->stack_end = 0; 

	/* not currently loading from an elf */
	as->loading = false; 
	return as;
}

/* 
 * Function copys an as. This function is used when a process calls fork(). 
 */
/*
 * as_copy:
 * Copy an address space.
 *  - Deep-copy regions (linked list)
 *  - Deep-copy page table: allocate new frames and copy contents
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    /* 1. Allocate a new empty addrspace */
    newas = as_create();
    if (newas == NULL) {
        return ENOMEM;
    }

    /* 2. Deep-copy the region list from old->regions to newas->regions */
    newas->regions = NULL;
    struct region *oldr = old->regions;
    struct region *prev_new = NULL;


    while (oldr != NULL) {

        /* Allocate a new region node */
        struct region *newr = kmalloc(sizeof(struct region));
        if (newr == NULL) {
            as_destroy(newas);
            return ENOMEM;
        }

        /* Copy region fields */
        newr->vbase      = oldr->vbase;
        newr->npages     = oldr->npages;
        newr->readable   = oldr->readable;
        newr->writeable  = oldr->writeable;
        newr->executable = oldr->executable;
        newr->next       = NULL;

        /* Append to new list */
        if (prev_new == NULL) {
            newas->regions = newr;      /* first region in list */
        } else {
            prev_new->next = newr;
        }
        prev_new = newr;
        oldr = oldr->next;
    }

    /* 3. Copy heap + stack metadata */
    newas->heap_base  = old->heap_base;
    newas->heap_end   = old->heap_end;
    newas->stack_base = old->stack_base;
    newas->stack_end  = old->stack_end;
	newas->loading = old->loading; 

    /* 4. Deep-copy the 2-level page table */
    for (int i = 0; i < PT_L1_SIZE; i++) {
        if (old->pt_l1[i] == NULL) {
                        continue;
                }

                paddr_t *old_l2 = old->pt_l1[i];

                /* Allocate new level-2 table */
                paddr_t *new_l2 = kmalloc(PT_L2_SIZE * sizeof(paddr_t));
                if (new_l2 == NULL) {
                        as_destroy(newas);
                        return ENOMEM;
                }

                for (int j = 0; j < PT_L2_SIZE; j++) {
                        paddr_t old_paddr = old_l2[j];

                        if (old_paddr == 0) {
                                new_l2[j] = 0;
                                continue;
                        }

                        /* Allocate new physical frame for this page */
                        paddr_t new_paddr = alloc_page();
                        if (new_paddr == 0) {
                                kfree(new_l2);
                                as_destroy(newas);
                                return ENOMEM;
                        }

                        /* Copy page contents */
                        void *old_kva = (void *)PADDR_TO_KVADDR(old_paddr);
                        void *new_kva = (void *)PADDR_TO_KVADDR(new_paddr);
                        memmove(new_kva, old_kva, PAGE_SIZE);

                        new_l2[j] = new_paddr;
                }

                newas->pt_l1[i] = new_l2;
        }

        *ret = newas;
        return 0;
}




/* Destroy as. Need to free all physical pages that are mapped in the pt then free the whole as */
void
as_destroy(struct addrspace *as)
{
        /* Free all user pages and level-2 tables */
        for (int i = 0; i < PT_L1_SIZE; i++) {
                if (as->pt_l1[i] != NULL) {
                        paddr_t *l2 = as->pt_l1[i];

                        for (int j = 0; j < PT_L2_SIZE; j++) {
                                if (l2[j] != 0) {
                                        free_page(l2[j]);
                                }
                        }

                        kfree(l2);
                        as->pt_l1[i] = NULL;
                }
        }

        /* Free the regions list */
        struct region *r = as->regions;
        while (r != NULL) {
                struct region *next = r->next;
                kfree(r);
                r = next;
        }

        kfree(as);
}



/* Switch to a new as. We flush the TLB so the new process won't read stale entries */
void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Need to disable interrupts to avoid races while modifying TLB */
	int spl = splhigh(); 

	/* Flush the tlb to prevent another process's mappings from remaining (the CPU will refill via vm_fault if needed) */
	for (int i = 0; i < NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); 
	}

	/* restore interrupts */
	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	
	/* regions need to be page aligned so we must perform some bit wise operation first */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME; 
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME; 

	/* calculate the number of pages by using the aligned size */
	size_t npages = sz / PAGE_SIZE; 

	/* allocate the region */
	struct region *r = kmalloc(sizeof(struct region)); 
	if (r == NULL){
		return ENOMEM; 
	}

	/* set the corresponding flags */
	r->vbase = vaddr; 
	r->npages = npages;
	r->readable = readable; 
	r->writeable = writeable; 
	r->executable = executable; 
	r->next = as->regions;
	as->regions = r; 

	/* The heap begins right after the last data/BSS region so we compute this regions end and check if its the region at the end */
	vaddr_t reg_end = r->vbase + r->npages * PAGE_SIZE; 
	if (as->heap_base == 0 || reg_end > as->heap_base){
		as->heap_base = reg_end; 
		as->heap_end = reg_end; 
	}


	return 0;

}

/* called before loading an elf binary into this address space. */
int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as != NULL); 

	/* we set loading to be true so that vm fault treats text as weritable while load elf fills in its contents */
	 as->loading = true; 
	return 0;
}


/* called after loading elf binary is finished */
int
as_complete_load(struct addrspace *as)
{
	KASSERT(as != NULL); 
	/* now we indicate that we're done loading and flush tlb */
	as->loading = false; 
	as_activate(); 
	return 0;
}

/* Define the user stack region */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	
	/* initialize the bounds (stack grows downward from userstack) */
	as->stack_base = USERSTACK; 
	as->stack_end = USERSTACK - (1 * PAGE_SIZE); 

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	return 0;
}

