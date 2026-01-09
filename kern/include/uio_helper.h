#ifndef _UIO_HELPER_H_
#define _UIO_HELPER_H_

#include <types.h>
#include <uio.h>

/* helper for setting up a uio/iovec for read and write operations*/
void uio_init(struct uio *u, struct iovec *iov,
    userptr_t buf, size_t len,
    off_t offset, enum uio_rw rw_type);

#endif