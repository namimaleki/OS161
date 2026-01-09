#include <types.h>
#include <kern/wait.h>      
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <thread.h>
#include <syscall.h>
#include <lib.h>
void sys__exit(int code) {
    struct proc *p = curproc;

    KASSERT(p != NULL); 

    /* mark exit status and wake any waiters */
    lock_acquire(p->p_waitlock);
    p->p_exitcode = _MKWAIT_EXIT(code & 0xff); /* pack as an exit status */
    p->p_exited = true;
    cv_broadcast(p->p_waitcv, p->p_waitlock);
    lock_release(p->p_waitlock);

    /* detach this thread from its process before exiting */
    thread_exit();

    /* not reached */
    panic("sys__exit: thread_exit returned");
}
