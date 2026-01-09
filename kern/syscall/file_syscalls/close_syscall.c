#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <vfs.h>
#include <vnode.h>
#include <current.h>
#include <proc.h>
#include <synch.h>
#include <copyinout.h>
#include <file_table.h>
#include <open_file_handler.h>
#include <syscall.h>
#include <limits.h>


/* sys_close: close a file descriptor */
int sys_close(int fd){
   

    /* first validate the given fd */
    if (fd >= __OPEN_MAX || fd < 0){
        /* return bad file descriptor error */
        return EBADF;
    }


    /* retrieve a pointer to this processes file table so that we can close the specified entry */
    struct file_table *ft = curproc->file_table;

    /* acquire file table lock before accessing */
    lock_acquire(ft->lock);


    /* get the pointer for the file */
    struct open_file_handler *f = ft->files[fd];
    if (f == NULL) {
        /* if it's already null then we release lock and return the bad file descriptor error */
        lock_release(ft->lock);
        return EBADF;
    }


    /* clear the slot and release the file references */
    ft->files[fd] = NULL;
    lock_release(ft->lock);


    /* decrement the reference count for the file */
    open_file_decref(f);


    /* return 0 on success */
    return 0;
}