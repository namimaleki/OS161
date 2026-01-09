#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <copyinout.h>
#include <synch.h>
#include <file_table.h>
#include <open_file_handler.h>
#include <uio.h>
#include <uio_helper.h>
#include <syscall.h>
#include <kern/fcntl.h>
#include <limits.h>

/* sys_read - kernel side implementation of the read() sys call */


/* based on the os161 manual the call is made by user program through: ssize_t read(int fd, void *buf, size_t buflen); */
/* This call reads up to 'buflen' bytes from the file associated with file descriptor 'fd', starting at the file's current */
/* seek position, and stores the data into the user's memory buffer at 'buf'. Afterwards it advances the file's current offset */
/* by the number of bytes read.*/
/* Input:
            - fd : file descriptor identifying an open file
            - buf : pointer (in user space) to the buffer to read data into
            - buflen: number of bytes to read
            - retval: pointer where the kernel writes the number of bytes read (this is what is returrned to the user and contains number of bytes successfuly read)
*/


/* The count of bytes read is returned. This count should be positive. A return value of 0 should be construed as signifying end-of-file. */
/* On error, read returns -1 and sets errno to a suitable error code for the error condition encountered. */


int sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *retval){
    /* 1. Validate the file descriptor */
    if (fd >= __OPEN_MAX || fd < 0) {
        return EBADF;
    }


    /* get the current process's file table */
    struct file_table *ft = curproc->file_table;
   
    /* acquire the file table lock before accessing */
    lock_acquire(ft->lock);


    /* get the open file handler for the given fd */
    struct open_file_handler *file = ft->files[fd];


    if (file == NULL) {
        /* release the lock and return bad file descriptor error */
        lock_release(ft->lock);
        return EBADF;
    }
   
    /* check flags */
    /* O_WRONLY if its write only you return error */
    if ((file->flags & O_ACCMODE) == (O_WRONLY)) {
        lock_release(ft->lock);
        return EBADF;
    }


    /* increment the reference count for the file */
    open_file_incref(file);


    /* release the file table lock as we have the open file handler now */
    lock_release(ft->lock);


    /* get the file lock */
    lock_acquire(file->lock);


    /* iovec describes the user buffer that we'll be writing to */
    struct iovec iov;


    /* uio serves as a wrapper/descriptor for the whole transfer process */
    struct uio u;


    /* use the helper function */
    uio_init(&u, &iov, buf, buflen, file->offset, UIO_READ);


    /* perform the read operation */
   
    /* VOP_READ reads data from the file to the uio, updating its resid and offset */
    int result = VOP_READ(file->file_vn, &u);


    if (result) {
        /* error during read */
        lock_release(file->lock);
        open_file_decref(file);
        return result;
    }


    /* calculate how many bytes were read and update retval accordingly */
    ssize_t bytes_read = (ssize_t) (buflen - u.uio_resid);
    *retval = bytes_read;


    /* update the file offset */
    file->offset = u.uio_offset;
   
    /* release the file lock */
    lock_release(file->lock);


    /* decrement the reference count for the file */
    open_file_decref(file);


    return 0;
}