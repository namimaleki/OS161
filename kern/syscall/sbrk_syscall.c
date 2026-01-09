#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>



/* Syscall implementation of subrk. Per the syscall manuals: sbrks functinoality is to adjust the size of the heap */
/* It does not allocate any pages, and only adjusts the boundarys. The "break" is the end address of a process's heap region */
/* the sbrk call adjusts the "break" by the amount "amount". It returns the old "break" */
int sys_sbrk(intptr_t amount, int32_t *retval){
    struct addrspace *as = proc_getas(); 
    if (as == NULL){
        return ENOMEM; 
    }

    /* our return value is the old break value so save that before applying any further changes */ 
    *retval = as->heap_end; 

    /* there are three cases we deal with amount > 0 we grow the heap, amount < 0 shrink and amount = 0 return heap as is */

    /* first check if amount = 0 */
    if (amount == 0) {
        return 0; /* returning current break */
    }

    /* save the old heap end */
    vaddr_t old_end = as->heap_end; 

    /* compute the new break */
    vaddr_t new_end = old_end + amount; 


    /* need to make sure there aren't unsigned wrap arounds */
    if ((amount > 0 && new_end < old_end) || (amount < 0 && new_end > old_end)){
        return ENOMEM; 
    }

    /* as stated in the manual we need to make sure we are not shrinking the heap past the heap_base or else we're going to */
    /* corrupt the other data regions  */
    if (new_end < as->heap_base){
        return EINVAL;
    }

    /* we also need to make sure we're not growing the heap into the stack region. Now we know that the heap grows upward and stack grow*/
    if(new_end >= as->stack_end) {
        return ENOMEM;
    }

    /* Now we update the heap end */
    as->heap_end = new_end; 
    return 0; 
}
