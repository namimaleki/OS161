#include <types.h>
#include <lib.h>            
#include <copyinout.h>      
#include <addrspace.h>      
#include <vfs.h>            
#include <kern/fcntl.h>     
#include <kern/errno.h>    
#include <proc.h>          
#include <current.h>       
#include <kern/limits.h>    
#include <syscall.h>       

/* This implements the execv system call. */

/* execv replaces the current proccess image with a new process image */
/* the new programs entry point is determined b*/

int sys_execv(userptr_t path, userptr_t argv) {
    /* implementation of execv system call */

    if (path == NULL) {
        return EFAULT;
    }

    if (argv == NULL) {
        return EFAULT;
    }
    
    /* copy in the path from user space */
    char kpath[__PATH_MAX];
    memset(kpath, 0, sizeof(kpath)); /* zero the buffer */
    int result = copyinstr((const_userptr_t)path, kpath, __PATH_MAX, NULL);

    if (result) {
        return result;
    }

    if (kpath[0] == '\0') {
        return EINVAL;
    }

    /* we allocate a single buffer */
    char *karg = kmalloc(__ARG_MAX);
    if (karg == NULL) {
        return ENOMEM;
    }

  
    /* copy in the argv from user space */
    int argc = 0;
    /* keep track of the total number of bytes used so we can stay under the max*/
    size_t total_bytes = 0;

    /* reuse one scratch buffer for both passes to avoid repeated __ARG_MAX allocations */

    while (1) {
        userptr_t current_argument;

        /* copy in the current argument, incrementing based on the number of arguments so far */
        result = copyin((userptr_t)((uintptr_t)argv + (size_t)argc * sizeof(userptr_t)), &current_argument, sizeof(userptr_t));
        if (result) {
            kfree(karg);
            return result;
        }

        if (current_argument == NULL) {
            break;
        }

        /* measure length byte-by-byte from user space (including the null) */
        size_t arglen = 0;
        char ch;
        do {
            result = copyin((userptr_t)((uintptr_t)current_argument + arglen), &ch, 1);
            if (result) {
                kfree(karg);
                return result;
            }
            arglen++;
            if (arglen > __ARG_MAX) { /* exceeded per-arg bound before copying */
                kfree(karg);
                return E2BIG;
            }
        } while (ch != '\0');

        total_bytes += arglen; /* includes the null byte */

        /* check if the total bytes exceed the max (include space for argv pointers) */
        if (total_bytes + (size_t)((argc + 1) * sizeof(userptr_t)) > __ARG_MAX) {
            kfree(karg);
            return E2BIG;
        }

        argc++;
    }


    /* copy the arguments into the kernel */
    char **kargv = kmalloc((argc + 1) * sizeof(char *));
    if (kargv == NULL) {
        kfree(karg);
        return ENOMEM;
    }

    /* one packed buffer for all strings */
    char *kargs_block = kmalloc(total_bytes > 0 ? total_bytes : 1);
    if (kargs_block == NULL) {
        kfree(kargv);
        kfree(karg);
        return ENOMEM;
    }

    size_t offset = 0;

    for (int i = 0; i < argc; i++) {
        userptr_t current_argument;

        /* copy in the current argument pointer */
        result = copyin((userptr_t)((uintptr_t)argv + (size_t)i * sizeof(userptr_t)),
                        &current_argument, sizeof(userptr_t));
        if (result) {
            kfree(kargs_block);
            kfree(kargv);
            kfree(karg);
            return result;
        }

        /* copy in the actual argument string (second pass) */
        result = copyinstr((const_userptr_t)current_argument, karg, __ARG_MAX, NULL);
        if (result) {
            if (result == ENAMETOOLONG) {
                result = E2BIG;
            }
            kfree(kargs_block);
            kfree(kargv);
            kfree(karg);
            return result;
        }

        size_t arg_length = strlen(karg) + 1;
        if (arg_length > __ARG_MAX) {
            kfree(kargs_block);
            kfree(kargv);
            kfree(karg);
            return E2BIG;
        }

        memcpy(kargs_block + offset, karg, arg_length);
        kargv[i] = kargs_block + offset;
        offset += arg_length;
    }
    kargv[argc] = NULL;

    /* done with the scratch buffer */
    kfree(karg);

    /* now, we proceed similarly to runprogram, opening the file*/
    /* the main difference here being that we destroy our current one*/

    struct vnode *v;
    result = vfs_open(kpath, O_RDONLY, 0, &v);
    if (result) {
        kfree(kargs_block);
        kfree(kargv);
        return result;
    }

    /* create a new address space */
    struct addrspace *as;
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        kfree(kargs_block);
        kfree(kargv);
        return ENOMEM;
    }

    /* switch to the new address space */
    struct addrspace *old_as = proc_setas(as);
    as_activate();

    /* load the executable */
    vaddr_t entrypoint;
    result = load_elf(v, &entrypoint);
    if (result) {
        /* return to the old address space */
        proc_setas(old_as);
        as_activate();

        as_destroy(as);
        vfs_close(v);
        kfree(kargs_block);
        kfree(kargv);
        return result;
    }

    /* done with the file now */
    vfs_close(v);

    /* create the user stack in the new address space */
    vaddr_t stackptr;
    result = as_define_stack(as, &stackptr);
    if (result) {
        proc_setas(old_as);
        as_activate();

        as_destroy(as);
        kfree(kargs_block);
        kfree(kargv);
        return result;
    }

    /* copy the arguments onto the user stack */
    vaddr_t *arg_pointers = kmalloc((argc + 1) * sizeof(vaddr_t));
    if (arg_pointers == NULL) {
        proc_setas(old_as);
        as_activate();

        as_destroy(as);
        kfree(kargs_block);
        kfree(kargv);
        return ENOMEM;
    }

    for (int i = argc - 1; i >= 0; i--) {
        size_t arg_length = strlen(kargv[i]) + 1;
        stackptr -= arg_length;
        result = copyout(kargv[i], (userptr_t)stackptr, arg_length);
        if (result) {
            proc_setas(old_as);
            as_activate();

            as_destroy(as);
            kfree(kargs_block);
            kfree(kargv);
            kfree(arg_pointers);
            return result;
        }
        arg_pointers[i] = stackptr;
    }

    /* align the stack pointer to a multiple of 8 */
    stackptr &= ~7;

    arg_pointers[argc] = 0; // null terminate the argv array
    
    /* now copy out the argv array itself */
    for (int i = argc; i >= 0; i--) {
        stackptr -= sizeof(vaddr_t);
        result = copyout(&arg_pointers[i], (userptr_t)stackptr, sizeof(vaddr_t));
        if (result) {
            proc_setas(old_as);
            as_activate();

            as_destroy(as);
            kfree(kargs_block);
            kfree(kargv);
            kfree(arg_pointers);
            return result;
        }
    }

    userptr_t argv_userptr = (userptr_t)stackptr;

    /* free the kernel argument copies */
    kfree(kargs_block);
    kfree(kargv);
    kfree(arg_pointers);

    /* destroy the old address space */
    as_destroy(old_as);

    /* finally enter user mode */
    enter_new_process(argc, argv_userptr, NULL, stackptr, entrypoint);
}
