
#include <types.h>         
#include <lib.h>           
#include <synch.h>         
#include <vfs.h>          
#include <vnode.h>         
#include <open_file_handler.h>  
#include <syscall.h>

/* Helper functions for the file handle of each processor */
/* Create a new open file struct */
/* This function would be called after a thread calls vfs_open() and it succeeds */
struct open_file_handler *create_open_file(struct vnode *vn, int flags){


    /* Allocate memory for the struct */
    struct open_file_handler *file = kmalloc(sizeof(struct open_file_handler));
    if (file == NULL){
        return NULL;
    }

    /* offset is set to 0 at initialization */
    file->offset = 0;
    file->flags = flags;
    file->file_vn = vn;
    file->reference_count = 1;
    file->lock = lock_create("file_lock");
    if (file->lock == NULL){
        kfree(file);
        return NULL;
    }
    return file;


}


/* Destroys an open_file: this function is called when ref count reaches 0 */
void open_file_destroy(struct open_file_handler *file){
   
    /* first we close the vnode */
    vfs_close(file->file_vn);
   
    /* Then we free the file lock */
    lock_destroy(file->lock);


    /* Lastly, free the associated memory of the file */
    kfree(file);
}


/* Increment ref count of file handle */
void open_file_incref(struct open_file_handler *file){
    lock_acquire(file->lock);
    file->reference_count++;
    lock_release(file->lock);
}


/* Decrement ref count of file handle (if a reference count of a file reaches zero then we destroy the file) */
void open_file_decref(struct open_file_handler *file){
    if (file == NULL) return; 
    
    lock_acquire(file->lock);
    file->reference_count--;
    lock_release(file->lock);

    /* once the referecne count reaches 0 we destroy the open file */
    if (file->reference_count == 0){
        open_file_destroy(file);
    }   

}