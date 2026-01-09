
#include <types.h>        
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <current.h>
#include <proc.h>
#include <addrspace.h>
#include <vm.h>
#include <mips/tlb.h>


/*
 * Our 2-level page table bit layout:
 *
 *  vaddr:  | 31 ........ 22 | 21 ........ 12 | 11 ........ 0 |
 *          |   L1 index     |   L2 index     |   page offset |
 *
 *  - offset:  bits [11:0]
 *  - L2 idx:  bits [21:12]
 *  - L1 idx:  bits [31:22]
 *  L1 index selects pt_l1[l1], which points to a level-2 array of paddr_t.
 *  L2 index selects the specific page entry inside that level-2 array.
 */
#define PT_L1_SHIFT   22
#define PT_L2_SHIFT   12
#define PT_INDEX_MASK 0x3ff  /* 10 bits set */

/* Function that is called to handle page faults (CPU tried to access a va that is not currently in the TLB) */
/* 
 * Steps:
 *   1. Align faultaddress to the page boundary.
 *   2. Check whether the address is inside a valid region / heap / stack.
 *   3. Enforce permissions (e.g., VM_FAULT_READONLY).
 *   4. Look up or create the 2-level page table entry.
 *   5. Allocate a physical page on first access and zero it.
 *   6. Load the mapping into the TLB.
 */
int vm_fault(int faulttype, vaddr_t faultaddress){
    struct addrspace *as; 
    paddr_t paddr; 


    /* align the fault address */
    faultaddress &= PAGE_FRAME; 

    /* retrieve curprocs as */
    as = proc_getas(); 
    if(as == NULL){
        return EFAULT; 
    }

   
    /* Now we will loop over each region (text, data, heap, stack) and for each region we will compute its bounds */
    /* and check if falutaddress belongs to that region or not (if it belongs to non or its a permission fault we return segfault)*/
    /* this prevents processes from reading or writing random memory */
    
    /* introduce boolean that we will set to true if the address is valid that is its inside a region */
    bool in_region = false; 

    /* and a boolean that will be true if the region is writeable */
    bool writeable = false; 

    struct region *r = as->regions;
    while (r != NULL){
         /* compute the start and end addresses of this region */
        vaddr_t start = r->vbase;
        vaddr_t end = r->vbase + r->npages * PAGE_SIZE; 

        /* check if the fault address is within these boundaries */
        if (faultaddress >= start && faultaddress < end){
            in_region = true; 
            /* While loading (load_elf), we temporarily allow writes even to text, so we OR with as->loading.*/
            writeable = r->writeable || as->loading; 
            break;
        }
        
        r = r->next; 

    }
    
    /* If not in any region, check heap and stack ranges. */
    if (!in_region) {
        if (faultaddress >= as->heap_base && faultaddress < as->heap_end) {
            in_region = true;
            writeable = true;
        }

        /* Stack grows downward from stack_base to stack_end */
        else if (faultaddress >= as->stack_end && faultaddress < as->stack_base){
            in_region = true;
            writeable = true; 
        }
    }

  
    /* If we didn't find a region that contains the address then it was an invalid mem access */
    if (!in_region) {
        return EFAULT;
    }

    /* If fault type was readonly and the region is not writeable then we retunr efault */
    if (faulttype == VM_FAULT_READONLY && !writeable){
        return EFAULT; 
    }

    /* At this point we know the address is valid and we have the right permission flags */
    /* so we use 2 level pt to find or create the mapping */
    /* Compute the level 1 and level 2 indicies for page table look up */
    unsigned l1 = (faultaddress >> PT_L1_SHIFT) & PT_INDEX_MASK;
    unsigned l2 = (faultaddress >> PT_L2_SHIFT) & PT_INDEX_MASK;

    /* get or allocate the level 2 table */
    paddr_t *l2_table = as->pt_l1[l1];
     if (l2_table == NULL) {
        l2_table = kmalloc(PT_L2_SIZE * sizeof(paddr_t));
        if (l2_table == NULL) {
            return ENOMEM;
        }
        for (unsigned i = 0; i < PT_L2_SIZE; i++) {
            l2_table[i] = 0;
        }
        as->pt_l1[l1] = l2_table;
    }

    /* Get physical address for this page */
    paddr = l2_table[l2];

    if (paddr == 0) {
        /* First access: allocate a physical frame */
        paddr = alloc_page(); 
        if (paddr == 0) {
            return ENOMEM; 
        }

        /* Zero the new page */
        bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE); 

        /* Install this mapping in the pt */
        l2_table[l2] = paddr; 
    }


    /* Now we must build the TLB entry for this new mapping. Using dumbvm naming convention ehi = vpn bits, elo = physical frame address + valid bit + dirty bit if writable */
    uint32_t ehi = faultaddress; 
    uint32_t elo = paddr | TLBLO_VALID | (writeable ? TLBLO_DIRTY : 0); 
    

    /* Insert this new mapping into TLB */
    int spl = splhigh(); 

    for(int i = 0; i < NUM_TLB; i++){
        uint32_t old_ehi;
        uint32_t old_elo; 
        tlb_read(&old_ehi, &old_elo, i); 

        if (!(old_elo & TLBLO_VALID)){
            /* valid bit is not set so we found empty slot */
            tlb_write(ehi, elo, i); 
            splx(spl); 
            return 0; 
        }   
    }

    /* No empty TLB entry so we need to evict an entry we will just choose a random slot */
    int candidate = random() % NUM_TLB; 
    tlb_write(ehi, elo, candidate); 

    splx(spl); 
    return 0; 

}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}
