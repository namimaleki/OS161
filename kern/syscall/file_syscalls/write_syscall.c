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
#include <kern/stat.h>
#include <limits.h>


/* sys_write: writes data to a file */


/* Overview (from os161 man): from user program: ssize_t write (int fd, const void *buf, size_t nbytes); this function writes up to buflen bytes*/
/* to the file specified by fd, at the location in the file specified by the current seek position of the file, taking the data from the space pointed*/
/* to by buf. File must be open for writing. The current seek position of the file is advanced by the number of bytes written.*/

/* Input:
            - fd: file descriptor number of the open file
            - buf: pointer (in uder space) to data to write
            - nbytes: size of bytes to write
            - retval: pointer to whre the kernal stores bytes actually written
*/

/* function returns 0 on success (with *retval = number of bytes written) or an error*/

int sys_write(int fd, userptr_t buf, size_t nbytes, ssize_t *retval){

         /* 1. Validate the file descriptor */
        if (fd < 0 || fd > __OPEN_MAX){
                return EBADF;
        }

        struct file_table *ft = curproc->file_table;

        /* get the file table lock */
        lock_acquire(ft->lock);

        struct open_file_handler *f = ft->files[fd];
       
        if(f == NULL){
                lock_release(ft->lock);
                return EBADF;
        }

        /* 2. Then check the if the right permission bits are set (apply the mask and if the file is in read only then return error)*/
        if ((f->flags & O_ACCMODE) == O_RDONLY){
                lock_release(ft->lock);
                return EBADF;
        }


        /* increment the reference count for the file */
        open_file_incref(f);


        /* release the file table lock as we have the open file handler now */
        lock_release(ft->lock);


        /* 3. acquire the lock to ensure atomicity */
        lock_acquire(f->lock);


        /* 4. Prepare uio/iovec for user data. This describes the user buffer (source of data) and the file offset*/
        struct iovec iov;
        struct uio u;
        uio_init(&u, &iov, buf, nbytes, f->offset, UIO_WRITE);
       
        /* check whether the append flag is set */
        if (f->flags & O_APPEND){
            /* we need to start writing at the end of the file */
            /* we get the file size using VOP_STAT */
            struct stat st;
            int stat_result = VOP_STAT(f->file_vn, &st);
            if (stat_result){
                lock_release(f->lock);
                open_file_decref(f);
                return stat_result; /* return corresponding error */
            }
            /* set the uio offset to the end of the file */
            u.uio_offset = st.st_size;
        }


        /* 5. perform the write using VOP_WRITE which handles the low level write operation into the vnode */
        int result = VOP_WRITE(f->file_vn, &u);

        if (result){
            lock_release(f->lock);
            open_file_decref(f);
            return result; /* return corresponding error */
        }
       
        /* 6. Now that we have performed the write we need to first update our new offset and then calc the #bytes written*/
        f->offset = u.uio_offset;
        ssize_t bytes_written = (ssize_t) (nbytes - u.uio_resid);

        lock_release(f->lock);

        /* 7. decrement the reference count for the file */
        open_file_decref(f);


        *retval = bytes_written;
        return 0;     
}