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


/* sys_dup2: clone a file descriptor */


/* dup2 clones the file handle oldfd onto the file handle newfd. */
/* If newfd names an already-open file, that file is closed. */
/* Both filehandles must be non-negative, and, if applicable, smaller than the maximum allowed file handle number. */
/* Using dup2 to clone a file handle onto itself has no effect. */


/* dup2 returns newfd. */


int sys_dup2(int oldfd, int newfd, int *retval) {


    /* validate the file descriptor */
    if (oldfd < 0 || newfd < 0 || oldfd >= __OPEN_MAX || newfd >= __OPEN_MAX) {
        return EBADF;
    }


    /* check if the two are the same */
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    /* get the file tables */
    struct file_table *ft = curproc->file_table;


    /* acquire the file table lock */
    lock_acquire(ft->lock);


    struct open_file_handler *old_fh = ft->files[oldfd];
    struct open_file_handler *new_fh = ft->files[newfd];


    if (old_fh == NULL) {
        lock_release(ft->lock);
        return EBADF;
    }


    if (new_fh == old_fh) {
        /* they point to the same open file already, nothing to do */
        lock_release(ft->lock);
        *retval = newfd;
        return 0;
    }


    /* increment the reference count for the old file */
    open_file_incref(old_fh);


    /* if newfd is already open, close it first */
    if (new_fh != NULL) {
        /* decrement the reference count for the new file */
        open_file_decref(new_fh);


        /* clear the slot */
        ft->files[newfd] = NULL;
    }


    /* reassign the pointer in the file table at newfd to point towards the same open entry as oldfd */
    ft->files[newfd] = old_fh;
    lock_release(ft->lock);
    *retval = newfd;
    return 0;
}
