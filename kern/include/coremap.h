#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <types.h>


/* Physical memory allocator interface */

/* vm_bootstrap is called once during kernel initialization and its dutyt is to initialize the coremap data structure */
void vm_bootstrap(void);

/* allocate a single page returns physical address of the allocated page */
paddr_t alloc_page(void);

/* frees a single page (takes in the paddr_t returned by alloc page) */
void free_page(paddr_t pa);

/*
 *  Allocate/free a contiguous block of kernel pages.
 *   - alloc_kpages(npages) returns a *kernel virtual address*
 *     (in kseg0) for the start of the block.
 *   - free_kpages() takes that kernel virtual address back.
 */
vaddr_t alloc_kpages(unsigned npages); 
void free_kpages(vaddr_t kvaddr);

#endif
