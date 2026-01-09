#include <types.h>
#include <kern/wait.h>      
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <thread.h>
#include <copyinout.h>
#include <syscall.h>

/* Waits for a specific child process to terminate, retrieves its exit status and then cleans up the childs resources */
int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval){
    
    /* optinos is not supported so we will just make sure its set to 0 */
    if (options != 0){
        return EINVAL;
    }

    /* get a pointer to the child process */
    struct proc *child = proc_get(pid); 
    if (child == NULL){
        return ESRCH; 
    }

    /* confirm that the cur proc (calling process) is in fact the parent of the process we're waiting for */
    if (child->p_parent != curproc->p_pid){
        return ECHILD; 
    }

    /* acquire the childs wait lock before waiting on exit status. _exit() uses the same lock so this prevents any cases that could lead to incorrect behaviour */
    lock_acquire(child->p_waitlock);

    /* We wait until the child has called _exit(). cv_wait will realese the lock while sleeping and then reacquire before returning */
    while (!child->p_exited){
        cv_wait(child->p_waitcv, child->p_waitlock); 
    }

    /* child has exited */
    int exitcode = child->p_exitcode; 
    lock_release(child->p_waitlock); 

    /* copy the exit code to user space. If status is null we skip this step */
    if (status != NULL){
        int err = copyout(&exitcode, status, sizeof(int));
        if (err){
            return err; 
        }
    }

    /* child process ahs now been fullly reaped so we call destoroy */
    proc_destroy(child);

    *retval = pid; /* retval is the pid of the child */
    return 0; 
}