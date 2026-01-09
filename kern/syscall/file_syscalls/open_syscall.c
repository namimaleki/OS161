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
#include <limits.h>



/* sys_open - our kernel side implementation of the open() sys call */


/* OVERVIEW */
/* this function is called whenever a user programs runs something like: int fd = open("331.txt", O_RDONLY); */
/* The job of sys_open is to first copy the filename from userspace to kernel space. Then it must use vfs to open the file */
/* and get the corresponding vnode(kernel structure that represents the file on a disk). Afterwards we wrap that vnode in our open_file struct.*/
/* Lastly, we put that open_file into the current process's file table, to  the process to refer to it later through the file descriptor (which is what sys_open returns) */


/* Inputs:
            - filename (userptr_t): pointer in user space to a string containing the file name
            - flags (int): The access mode (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT) which controls how the file can be used
            - mode: file permission bits if a new file is created
            - retval : a pointer where we write back the resulting file descriptor. Note that we can't just return the fd directly because the syscall return value (int) is reserved for error codes. */


/* The syscall dispatcher copies will copy file_d (our return value) back to user space if this function returns 0, else it sets errno and returns -1 to the user program */


int sys_open(userptr_t filename, int flags, mode_t mode, int *retval){


    /* 1. Copy file name from user space into kernel butter */
    /* recall that the user provides a pointer to a string that lives in user memory and the kernel cannot safely read from user memory directly, so we must copy it into a local kernel buffer using copyinstr() */
    char kern_file_name[PATH_MAX]; /* Destination kernel buffer */
    int result = copyinstr(filename, kern_file_name, sizeof(kern_file_name), NULL);


    if (result){
        /* an error occured (file not found or permission denied) so just return the error code */
        return result;
    }


    /* 2. Now we ask the VFS to open the file and get the vnode */
    struct vnode *vn;
    result = vfs_open(kern_file_name, flags, mode, &vn);
    if (result){
        return result;
    }


    /* 3. Now we wrap the vnode with our open_file struct  */
    struct open_file_handler *f = create_open_file(vn, flags);
    if (f == NULL){
        /* first make sure we close the vnode in case it was opened then return error */
        vfs_close(vn);
        return ENOMEM;
    }


    /* 4. Now we can insert our open_file instance into the process's file table */
   
    /* first retrieve a pointer to this processes file table */
    struct file_table *ft = curproc->file_table;
    /* then we acquire the file tables lock */
    lock_acquire(ft->lock);


    /* Now we can insert it into the first open slot that isn't pointing to a file */
    int fd;
    for (fd = 0; fd < __OPEN_MAX; fd++){
        if (ft->files[fd] == NULL){
            ft->files[fd] = f;
            break;
        }
    }


    lock_release(ft->lock);


    /* make sure we take care of the case where we couldn’t find an empty slot in the file table */
    /* if this occured we must free the file and return EMFILE which means too many open files */
    if (fd == __OPEN_MAX){
        open_file_destroy(f);
        return EMFILE;
    }

    /* 5. Store the new file descriptor in our return pointer (sys call dispatcher will copy this value back to user space by setting err to retval must add this in syscall.c) and then return 0 to indiccate success */
    *retval = fd;
    return 0;
}


