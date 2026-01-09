#ifndef _OPEN_FILE_HANDLER_H_
#define _OPEN_FILE_HANDLER_H_

#include <types.h>
#include <synch.h>
#include <vnode.h>


/* Struct that represents an open file */
struct open_file_handler {
    off_t offset; /* current position in the file */
    int flags; /* file status flags */
    struct vnode *file_vn; /* pointer to the file's vnode */
    struct lock *lock; /* lock for synchronizing access to this file descriptor */
    int reference_count; /* reference count for this file descriptor */
};

/* constructors and destructors */
struct open_file_handler *create_open_file(struct vnode *vn, int flags);
void open_file_destroy(struct open_file_handler *file);

/* reference managment */
void open_file_incref(struct open_file_handler *file);
void open_file_decref(struct open_file_handler *file);

#endif