/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <file_table.h> 
#include <open_file_handler.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/errno.h>




/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/* ADDED FOR A5 */
static struct proc *pid_table[__PID_MAX]; /* PID table that keeps track of all the live processes (maps PIDs to the proc struct) */
static struct lock *pid_lk; /* lock for the pid table */

/* Initialize the global PID system. This is called once in proc_bootstrap() */
void pid_bootstrap(void){
	pid_lk = lock_create("pid_lk"); 
	for (int i = __PID_MIN; i < __PID_MAX; i++){
		pid_table[i] = NULL; 
	}
}

/* This function uses the PID table to find a free PID and asssigns it to the given process and returns the PID. If there are no free PIDS it returns ENPROC */
pid_t proc_allocpid(struct proc *p){
	/* First we acquire the lock for the pid table */
	lock_acquire(pid_lk);
	for (pid_t i = __PID_MIN; i < __PID_MAX; i++){
		/* Check if the entry is free */
		if (pid_table[i] == NULL){
			pid_table[i] = p; /* if the slot is free then we assign the given proc to it */
			lock_release(pid_lk);
			return i; 
		}
	}
	lock_release(pid_lk);
	return ENPROC; 
}

/* Function returns a pointer to the proc struct of the corresponding PID. */
struct proc *proc_get(pid_t pid){
	struct proc *p = NULL;
	lock_acquire(pid_lk);
	if (pid >= __PID_MIN && pid < __PID_MAX){
		p = pid_table[pid];
	}
	lock_release(pid_lk);
	return p;
}

/* Helper function that releases a PID (frees the corresponding slot in the pid table) once a process has been terminated and reaped by its parent */
void proc_freepid(pid_t pid){
	lock_acquire(pid_lk);
	pid_table[pid] = NULL; 
	lock_release(pid_lk);
}

/*
 * Create a proc structure.
 */
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* ADDED FOR A5 */

	/* Initialize new pid managmenet fields */

	/* The kernel process should not recieve a user PID (pid 1 is reserved for kernel proc) */
	if (strcmp(name, "[kernel]") != 0){
		proc->p_pid = proc_allocpid(proc);
	}
	else {
		proc->p_pid = 1;
	}
	proc->p_parent = -1; /* this will be set by fork set to -1 for now*/
	proc->p_exitcode = 0; 
	proc->p_exited = false; 
	proc->p_waitlock = lock_create("proc_waitlock");
	proc->p_waitcv = cv_create("proc_waitcv");

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	if (proc->file_table != NULL) {
		destroy_file_table(proc->file_table);
		proc->file_table = NULL;
	}

	/* ADDED FOR A5 */
	if (proc->p_waitcv != NULL){
		cv_destroy(proc->p_waitcv);
	}
	if (proc->p_waitlock != NULL){
		lock_destroy(proc->p_waitlock);
	}
	
	proc_freepid(proc->p_pid);

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}

	/* Initialize pid managmenet system for user processes. */
	pid_bootstrap();
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;
	struct vnode *v0, *v1, *v2;
	int result;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */
	newproc->p_addrspace = NULL;

	/* Copy current working directory */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	/* ===== ADDED A4 CODE ===== */
	/* Create a new file table for this process */
	newproc->file_table = create_file_table();
	if (newproc->file_table == NULL) {
		proc_destroy(newproc);
		return NULL;
	}

	/* we now need to add the first 3 file descriptors which every proceess expects to already exist (stdin, stdout, stderr)*/

	/* fd 0 is stdin. This sets up the new process so it can receive input from the terminal*/
	/* first duplicate the console device name into kernel mem*/
	char *path_copy = kstrdup("con:");
	if (path_copy == NULL) {
		proc_destroy(newproc);
		return NULL;
	}

	/*open the console in rd only mode*/
	result = vfs_open(path_copy, O_RDONLY, 0, &v0);

	/* we free the string since we're done using it */
	kfree(path_copy); 
	if (result) {
		proc_destroy(newproc);
		return NULL;
	}

	/* wrap the vnode in an open_file struct and store it in fd0 */
	newproc->file_table->files[0] = create_open_file(v0, O_RDONLY);
	if (newproc->file_table->files[0] == NULL) {
		vfs_close(v0);
		proc_destroy(newproc);
		return NULL;
	}

	/* fd 1 is stdout so this also goes to the console but for writing only. Allows the process to print to the terminal using printf(), write(), etc. */
	path_copy = kstrdup("con:");
	if (path_copy == NULL) {
		proc_destroy(newproc);
		return NULL;
	}
	result = vfs_open(path_copy, O_WRONLY, 0, &v1);
	kfree(path_copy);
	if (result) {
		proc_destroy(newproc);
		return NULL;
	}
	newproc->file_table->files[1] = create_open_file(v1, O_WRONLY);
	if (newproc->file_table->files[1] == NULL) {
		vfs_close(v1);
		proc_destroy(newproc);
		return NULL;
	}

	/* fd 2 is stderr behaves like stdout but is kept separate so err messegases don't interfere with regular outputs */
	path_copy = kstrdup("con:");
	if (path_copy == NULL) {
		proc_destroy(newproc);
		return NULL;
	}
	result = vfs_open(path_copy, O_WRONLY, 0, &v2);
	kfree(path_copy);
	if (result) {
		proc_destroy(newproc);
		return NULL;
	}
	newproc->file_table->files[2] = create_open_file(v2, O_WRONLY);
	if (newproc->file_table->files[2] == NULL) {
		vfs_close(v2);
		proc_destroy(newproc);
		return NULL;
	}

	/* Now this process has a valid ft and can perform standard I/O operatiosn */

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
