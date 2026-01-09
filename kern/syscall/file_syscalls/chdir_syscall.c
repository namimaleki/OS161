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

/* sys_chdir: changes the current directory */
/* The current directory of the current process is set to the directory named by pathname. */
int sys_chdir(userptr_t pathname) {


    /* we copy the pathname from user space through a buffer */
    char pathbuf[OPEN_MAX];
    int result = copyinstr(pathname, pathbuf, OPEN_MAX, NULL);
    if (result) {
        return result; /* return error from copyinstr */
    }


    /* now that the string is in kernel space, we can use it */
    return vfs_chdir(pathbuf);
}