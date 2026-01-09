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

/* sys___get_cwd: get the name of the current working directory */


/* The name of the current directory is computed and stored in buf, an area of size buflen. */
/* The length of data actually stored, which must be non-negative is returned. */
int sys___get_cwd(userptr_t buf, size_t buflen, int *retval) {
   
    /* we need to wrap the given memory in an uio so we can make a call to vfs_getcwd */
    struct iovec iov;
    struct uio u;
    uio_init(&u, &iov, buf, buflen, 0, UIO_READ);
    int result = vfs_getcwd(&u);


    if (result) {
        return result; /* return error from vfs_getcwd */
    }


    /* calculate how much data was actually stored */
    *retval = buflen - u.uio_resid;
    
    return 0;
}
