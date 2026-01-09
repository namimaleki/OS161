
#include <types.h>         
#include <lib.h>          
#include <synch.h>        
#include <open_file_handler.h>  
#include <file_table.h>   
#include <syscall.h>
#include <limits.h>
#include <kern/errno.h>


/* initialize a new file table. This is called whenever a new process is created as each process gets their own file table */
struct file_table *create_file_table(void){

    struct file_table *ft = kmalloc(sizeof(struct file_table));
    if (ft == NULL){
        return NULL;
    }


    ft->lock = lock_create("ft_lk");
    if (ft->lock == NULL){
	  kfree(ft);
      return NULL;
    }


    /* loop through and initialize all the slots (set to NULL at initialization) */
    /* again figure out what upper bound is */
    for (int i = 0; i < __OPEN_MAX; i++){
        ft->files[i] = NULL;
    }
   
         return ft;
}


/* Destroy function for ft */
void destroy_file_table(struct file_table *ft){
    if (ft == NULL) return;

    /* First we loop through the table and free all the open files */
    for (int i = 0; i < __OPEN_MAX; i++){
        if (ft->files[i] != NULL){
            /* first make sure to decrement the reference count of the corresponding file before setting the entry to NULL */
            ft->files[i] = NULL;

            open_file_decref(ft->files[i]);
        }
    }
    /* now that we're done with the table entries we release the lock and deallocate the corresponding memory */
    lock_destroy(ft->lock);
    kfree(ft);
}


/* ADDED FOR A5 */
/* This function is called by fork to copy a parents file table for the child process. Returns pointer to the new file table */
struct file_table *copy_file_table(struct file_table *ft){

    struct file_table *new_ft = create_file_table(); 
    if (new_ft == NULL){
        return NULL; 
    }

    /* Acquire the parents file table lock since we will be accessing its entries and coping the file handles over */
    lock_acquire(ft->lock); 
    for(int i = 0; i < __OPEN_MAX; i++){
        if (ft->files[i] != NULL){
            new_ft->files[i] = ft->files[i]; 
            /* Make sure to increment ref count on the shared file objects */
            open_file_incref(ft->files[i]); 
        }       
    }
    lock_release(ft->lock); 
    return new_ft; 
}