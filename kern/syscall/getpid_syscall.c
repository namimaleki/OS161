#include <types.h>          
#include <kern/errno.h>     
#include <proc.h>          
#include <current.h>        
#include <syscall.h>        


/* returns the pid of the calling process */
int sys_getpid(pid_t *retval){
    *retval = curproc->p_pid;
    return 0;
}