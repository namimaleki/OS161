#include <types.h>       
#include <uio.h>         
#include <current.h>     
#include <proc.h>       
#include <uio_helper.h>  
#include <syscall.h>


/* uio_init: helper function that prepares an iovec/uio pair for either a read or write sys call*/
void uio_init(struct uio *u, struct iovec *iov, userptr_t buf, size_t len, off_t offset, enum uio_rw rw_type){
    iov->iov_ubase = buf;
    iov->iov_len = len;


    u->uio_iov = iov;
    u->uio_iovcnt = 1;
    u->uio_offset = offset;
    u->uio_resid = len;
    u->uio_segflg = UIO_USERSPACE;
    u->uio_rw = rw_type;
    u->uio_space = curproc->p_addrspace;
}


