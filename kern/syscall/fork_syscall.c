#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <thread.h>
#include <copyinout.h>
#include <syscall.h>
#include <file_table.h>
#include <synch.h>
#include <mips/trapframe.h>

/* sys fork system call. Creates a new process that is a copy of the current process. */
/* Returns the child's PID to the parent and 0 to the child. Both the child and parent should have unique PIDs assigned to them */
int sys_fork(struct trapframe *parent_tf, pid_t *retval){
    
    /* 1. We first gotta create the new process for the child. */
    /* to do this we will use proc create and since the child is a copy we jut use the current procs name */
    struct proc *child = proc_create(curproc->p_name);
    if (child == NULL){
        return ENOMEM; /* Out of memory */
    }

    /* 2. We then need to copy the address space */
    /* Each process has its own address space so we create a new address space first and then we */
    /* copy the contents of the parent address space over using as_copy*/
    struct addrspace *parent_as = proc_getas(); 
    struct addrspace *child_as = NULL; 

    int result = as_copy(parent_as, &child_as); 
    if (result){
        /* if the copy failed then we free the child proc and return the error */
        proc_destroy(child);
        return result; 
    }
    child->p_addrspace = child_as; 

     spinlock_acquire(&curproc->p_lock);
struct vnode *pcwd = curproc->p_cwd;
if (pcwd != NULL) {
    VOP_INCREF(pcwd);          /* take a ref for the child */
}
spinlock_release(&curproc->p_lock);

child->p_cwd = pcwd;
    
    /* 3. Now we need to copy the file table of the parent. Need to be careful about this as the file table content is copied over */
    /* but the child and parent will share the open file table handles (what each fd points to) */
    child->file_table = copy_file_table(curproc->file_table);
    if (child->file_table == NULL){
        as_destroy(child_as);
        proc_destroy(child);
        return ENOMEM;
    }


    /* Note that each process needs to know who its parent is so that later when the child calls _exit(), the parent can use waitpid() to collect */
    /* its exit status. Without this link, the parent would have no way of knowing which child exited or when */
    child->p_parent = curproc->p_pid; 

    /* 4. Copy parents tf onto child. The tf represents the exact cpu reg state when the parent process entered the kernel to execute the fork() sys call */
    /* this includes pc, sp, and other general purpose registers. We make a copy of it for the child since the parents tf lives on its kernel stack and will */
    /* soon be reused when returning to user mode. If we don't copy it both the parent and chile processes will share the same reg snapshots and this is bad */

    /* so first we allocate a heap copy of the parents tf that will survive after sys_fork() returns. And then we pass this heap copy into thread_fork()*/
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe)); 
    if (child_tf == NULL){
        destroy_file_table(child->file_table); 
        as_destroy(child_as); 
        proc_destroy(child); 
        return ENOMEM; 
    }
    memcpy(child_tf, parent_tf, sizeof(struct trapframe));

    /* 5. copy the kernel thread that is we need to create a new thread to execute inside the child process */
    /* we do this by calling thread_fork() to create a kernel thread that belongs to the childs proc. Then this new thread will start running  */
    /* enter_forked_process(), which sets up the CPU registers so the child appears to return from fork() w a ret val of 0 and then we trans back to user mode */
    /* note that we're passing the copied tf which allows the child's thread to resume from same point as the parent */
    result = thread_fork(curthread->t_name, child, (void (*)(void *, unsigned long))enter_forked_process, (void *)child_tf, 0);

    if (result){
        kfree(child_tf);
        destroy_file_table(child->file_table); 
        as_destroy(child_as); 
        proc_destroy(child); 
        return result; 
    }

    /* 6. All that's left now is to return control to set the retval to the child pid and return control to the parent process. The parent will continue execution in kernel mode after the fork() */
    /* sys call returns. It received the childs PID as the return val and the child (in enter-forked_prcoess) receives 0. */
    *retval = child->p_pid; 

    return 0; 
}