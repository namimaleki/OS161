#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/* This will serve as the physical memory allocator. Allows the OS to keep track of every physical page, */
/* and allocates and free pages dynamically */

/* At boot, we call ram_getsize() and ram_getfirstfree() to retrieve the usable physical memeory range [lo, hi). 
 * then we construct an array of coremap entries (one per physical page). And this struct basically tracks
 * whether the page is free or allocated. 
 */
struct coremap_entry {
    bool free; /* set to true if this phys page is free (not allocated) */
    unsigned block_size; /* size of the contigous block allocated (set at block head) */
};


/* global variables */
static struct coremap_entry *coremap = NULL; /* pointer to the beginning of our coremap array */

static unsigned total_pages = 0; /* total # of phys pages that come after the core map */
static unsigned coremap_pages = 0; /* number of pages coremap takes up */

static paddr_t first_paddr = 0; /* phys address of the start of the page frame (this comes after the coremap itself )*/

static struct spinlock coremap_lock = SPINLOCK_INITIALIZER; /* spinlock used to avaoid concurrent access to the coremap */

static bool coremap_ready = false; /* flag to indicate core map has been initialized */

/* 
 * this function is called once during system initialization to initialize the virtual memory system. 
 * we build the core map. Now remember we can't use kmalloc() as the heap allocator is not ready so we manually reserve space at beginnign of RAM
 */
 void vm_bootstrap(void){

    paddr_t hi = ram_getsize(); /* upper bound (total RAM available) */
    paddr_t lo = ram_getfirstfree(); /* retrieve first free paddr */


    /* align to page boundaries we round low up and round down high */
    hi = ROUNDDOWN(hi, PAGE_SIZE);
    lo = ROUNDUP(lo, PAGE_SIZE); 


    /* Now we can compute how many pages the coremap needs. */
    unsigned long total_ram_pages = (hi - lo) / PAGE_SIZE;

    /* Next we compute how many pages we need to store the coremap array itself. First we compute the amonut of bytes the coremap is going to take up
     * then we divide by page size to get the number of pages we need for it
     */
    size_t coremap_bytes = total_ram_pages * sizeof(struct coremap_entry);
    coremap_pages = (coremap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    /* we want to place our coremap at the start of physical memory which is at physical address lo */
    /* The kernel cannot directly use this physical address, the CPU needs a kernel va to acess the data so we convert */
    coremap = (struct coremap_entry *)PADDR_TO_KVADDR(lo);
    
    /* need to actually steal memory for the core map */
    ram_stealmem(coremap_pages);

    /* Now the pages used by core map are not usable so we sid the first physical address to be after that region */
    first_paddr = lo + coremap_pages * PAGE_SIZE; 

    /* now we compute the total number of pages available after the core map */
    total_pages = (hi - first_paddr) / PAGE_SIZE;
    
    /* initialize the coremap */
    for (unsigned i = 0; i < total_pages; i++){
        coremap[i].free = true; 
        coremap[i].block_size = 0; 
    }

    coremap_ready = true;
}


 /* Function used to allocate a physical page. Returns the physical address of the allocated page or 0 if out of mem */
 paddr_t alloc_page(void){
    
    /* early boot core map is not ready yet so we use stealmem directly this only happens before vm_boostrap() runs */
    if (!coremap_ready){
        paddr_t pa = ram_stealmem(1);
        if(pa == 0){
            return 0;
        }
        return pa;
    }
    
    /* acquire spinlock for core map first */
    spinlock_acquire(&coremap_lock); 

    /* find first available page */
    for (unsigned i = 0; i < total_pages; i++){
        if (coremap[i].free){
            /* mark the page as used */
            coremap[i].free = false;
            coremap[i].block_size = 1; 

            /* compute the pa of the page by using our base address */
            paddr_t pa = first_paddr + i * PAGE_SIZE; 

            spinlock_release(&coremap_lock); 
            return pa; 
        }
    }

    /* no memory left */
    spinlock_release(&coremap_lock); 
    return 0; 
 }

 
 /* Function used to free a page */
 void free_page(paddr_t pa){

    /* if the core map is not ready we cannot free anything */
    if (!coremap_ready) {
        return;
    }
    
    spinlock_acquire(&coremap_lock); 
    
    /* safety check */
    if (pa < first_paddr){
        spinlock_release(&coremap_lock);
        return; 
    }

    /* compute the corresponding index of the page in the core map and make sure its valid since this is unsigned int if */
    /* the program is trying to free one of the coremap pages that is pa < first_paddr this would give us a gian number */
    unsigned cm_idx = (pa - first_paddr) / PAGE_SIZE;
    KASSERT(cm_idx < total_pages); 

    KASSERT(coremap[cm_idx].block_size == 1); 

    /* free the page */    
    coremap[cm_idx].free = true;
    coremap[cm_idx].block_size = 0; 
    spinlock_release(&coremap_lock);
 }

 

 /* Function used to allocate contiguous physical pages (kernel might need multiple pages) */
 /* we need to search the coremap for npages consecutive free frames and if we find them mark as used and return kva*/
vaddr_t alloc_kpages(unsigned npages)
{

    /* Early boot core map not ready yet we use ram steal mem */
    if (!coremap_ready){
        paddr_t pa = ram_stealmem(npages);
        if(pa == 0){
            return 0;
        }
        return PADDR_TO_KVADDR(pa);
    }
    /* first acquire core map lock to prev concurrent access */
    spinlock_acquire(&coremap_lock);

    /* Search for a contiguous free run of npages frames */
    for (unsigned i = 0; i < total_pages; i++) {

        /* If this frame is not free, skip */
        if (!coremap[i].free){
            continue;
        }

        /* Check if frames [i, i + npages) are all free */
        bool run_ok = true;
        for (unsigned j = 0; j < npages; j++) {

            /* if we run out of bounds or find a used fram break */
            if (i + j >= total_pages || !coremap[i + j].free) {
                run_ok = false;
                break;
            }
        }

        /* Found a suitable run */
        if (run_ok) {
            /* Mark them all allocated */
            coremap[i].free = false; 
            coremap[i].block_size = npages; 

            for (unsigned j = 1; j < npages; j++) {
                coremap[i + j].free = false;
                coremap[i + j].block_size = 0; 
            }

            /* Compute physical address of frame i */
            paddr_t pa = first_paddr + i * PAGE_SIZE;

            spinlock_release(&coremap_lock);

            /* Convert paddr → kseg0 virtual address */
            return PADDR_TO_KVADDR(pa);
        }
    }

    /* No contiguous run available */
    spinlock_release(&coremap_lock);
    return 0;
}


/* function used to free a block of contiguous pages */
void free_kpages(vaddr_t kvaddr)
{

    if (!coremap_ready) return;

    spinlock_acquire(&coremap_lock);

    /* Convert kernel virtual address → physical */
    paddr_t pa = KVADDR_TO_PADDR(kvaddr);

    
    /*
     * Safety check:
     * If pa < first_paddr, it corresponds to RAM used
     * before VM bootstrap or coremap metadata so we can't free
     */
    if (pa < first_paddr) {
        spinlock_release(&coremap_lock);
        return;
    }

    /* Compute index of the frame being freed */
    unsigned index = (pa - first_paddr) / PAGE_SIZE;
    KASSERT(index < total_pages);

    /* make sure the block size isn't 0*/
    unsigned block_ln = coremap[index].block_size;
    KASSERT(block_ln > 0);

    /* Free current frame, then continue freeing frames until we hit one that is already free.
     * This makes free_kpages work even without block-size metadata.
     */

    for(unsigned i = 0; i < block_ln; i++) {
       coremap[index + i].free = true; 
       coremap[index + i].block_size = 0; 
    }

    spinlock_release(&coremap_lock);
}
