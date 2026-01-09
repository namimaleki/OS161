#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
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
#include <kern/stat.h>
#include <limits.h>



#define SEEK_SET 0 /* Seek relative to beginning of file */
#define SEEK_CUR 1 /* Seek relative to current position in file */
#define SEEK_END 2 /* Seek relative to end of file */


/* sys_lseek: changes current position in a file */


/* Overview (from os161 man): lseek alters the current seek position of the file handle filehandle, seeking to a new position based on pos and whence. */
/* If whence is: */
/*      SEEK_SET, the new position is pos. */
/*      SEEK_CUR, the new position is the current position plus pos. */
/*      SEEK_END, the new position is the position of end-of-file plus pos. */
/*      anything else, lseek fails. */


/* On success, lseek returns the new position. On error, -1 is returned, and errno is set according to the error encountered. */


int sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
   
    /* check if fd is valid */
    if (fd < 0 || fd >= __OPEN_MAX) {
        return EBADF;
    }


    /* get the file table */
    struct file_table *ft = curproc->file_table;


    /* acquire the file table lock */
    lock_acquire(ft->lock);


    struct open_file_handler *f = ft->files[fd];
   
    if (f == NULL) {
        lock_release(ft->lock);
        return EBADF;
    }
   
    /* increment the reference count for the file */
    open_file_incref(f);


    /* release the file table lock as we have the open file handler now */
    lock_release(ft->lock);


    /* acquire the file lock */
    lock_acquire(f->lock);


    /* check to see if the file is seekable */
    if (!VOP_ISSEEKABLE(f->file_vn)) {
        lock_release(f->lock);
        open_file_decref(f);
        return ESPIPE; /* illegal seek */
    }    


    /* now break into cases depending on the value of whence */

    off_t new_offset;

    switch (whence) {
        case SEEK_SET:
            new_offset = pos;
            break;
        case SEEK_CUR:
            new_offset = f->offset + pos;
            break;
        case SEEK_END: {
            /* use stat to get the file size */
            struct stat st;
            int result = VOP_STAT(f->file_vn, &st);
            if (result) {
                lock_release(f->lock);
                open_file_decref(f);
                return result; /* return error from VOP_STAT */
            }
           
            new_offset = st.st_size + pos;
            break;
        }
        default: {
            /* whence is invalid */
            lock_release(f->lock);
            open_file_decref(f);
            return EINVAL;
        }
         
    }


    /* make sure the seek position isn't negative */
    if (new_offset < 0) {
        lock_release(f->lock);
        open_file_decref(f);
        return EINVAL;
    }


    /* update the file offset */
    f->offset = new_offset;
    *retval = new_offset;
    lock_release(f->lock);


    /* decrement the reference count for the file */
    open_file_decref(f);


    return 0;
}