#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_

#include <types.h>
#include <synch.h>
#include <open_file_handler.h>
#include <limits.h>




/* File table struct that each process has. Our file table will be represented by an array of file handle structs */
struct file_table{
    /* Figure out what to set the size of the array to */
    struct open_file_handler *files[__OPEN_MAX];
    /* Lock for the file table to prevent concurrent access */
    struct lock *lock;
};

/* Create process’s file table */
struct file_table *create_file_table(void);

/* Function destroys a process's file table */
void destroy_file_table(struct file_table *ft);

/* function for copying file table */
struct file_table *copy_file_table(struct file_table *ft);

#endif