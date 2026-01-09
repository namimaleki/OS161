/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

/*At most one thread may hold the lock at any given time. We can implement it using a semaphore initialized with count = 1.
 * P() on the emaphore acquires the lock (decrement, blcok if count == 0)
 * V() releases the lock (increment, wake a waiting thread if there are any)
 */



struct lock *
lock_create(const char *name)
{

       


        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }


        /* initialize the semaphore to 1 (indicating that it's free held by noone at initialization)*/
        lock->lk_sem = sem_create(name, 1);
        if (lock->lk_sem == NULL){
                kfree(lock->lk_name);
                kfree(lock); 
                return NULL;
        }

        /* No owner at the initiation stage.*/
        lock->lk_owner = NULL; 




        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);


        /* Make sure the lock doesn't have an ownner */
        KASSERT(lock->lk_owner == NULL);
        
        sem_destroy(lock->lk_sem);
        kfree(lock->lk_name);   
        kfree(lock);
}


/* 
 * lock_acquire will wait (P) on the semaphore to acquire the lock. This may block if another thread is holding it.
 * When woken up, the thread will be the owner of the lock.
 */
void
lock_acquire(struct lock *lock)
{
        /* Decremenet semaphore (done in P()) will block if busy */
        P(lock->lk_sem); 
        lock->lk_owner = curthread; 
}

/*lock_release releases the lock and calls V() on the semaphore to wake up a waiting thread
 */
void
lock_release(struct lock *lock)
{
        
        if (lock->lk_owner == curthread){
                /* Reset lock owner to NULL */
                lock->lk_owner = NULL;
                
                /* Increment semaphore */
                V(lock->lk_sem);
        }
}


/* lock_do_i_hold returns true if the curr thread is the lock owner*/
bool
lock_do_i_hold(struct lock *lock)
{ 
        return (lock->lk_owner == curthread); 

}

////////////////////////////////////////////////////////////
//
// CV

/* Allow threads to wait for some condition to become true. Each cv has its own wait channel 
* and spinlock. Threads must hold an external lock when calling cv_wait, cv_signal, or cv_broadcast which ensures proper
* synchronization and complies with mesa semantics. 
*/




struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }


        cv->cv_wchan = wchan_create(cv->cv_name);
        if(cv->cv_wchan == NULL){
                kfree(cv->cv_name);
                kfree(cv);
                return NULL;
        }

        spinlock_init(&cv->cv_lock);                      




        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        
        spinlock_cleanup(&cv->cv_lock);
        wchan_destroy(cv->cv_wchan);
        





        kfree(cv->cv_name);
        kfree(cv);
}



/*
 * cv_wait blocks the calling thread until the specified condition is signalled. 
 * Should be called by the threads lock on locked, and it will release the lock while it waits for the condition.
 * After the signal is received and thread is awakened, the mutex will be locked for use by the thread.
 */
void
cv_wait(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
       
        /*The current thread must have posetion of the lock*/
        KASSERT(lock_do_i_hold(lock));

        /* Acquire CV spinlock to prevent races between releasing the caller's lock and sleeping */
        spinlock_acquire(&cv->cv_lock);

        /* Release the threads lock */
        lock_release(lock);

        /* Atomically put thread to sleep while holding cv_lock ensuring no wakeups are missed */
        wchan_sleep(cv->cv_wchan, &cv->cv_lock); 

        /* After waking we drop the cv spinlock */
        spinlock_release(&cv->cv_lock);
        
        /* Reacquire aller's lock so condition can be safely re-checked */
        lock_acquire(lock);
}

/*
 * cv_signal is used to signal (or wake up) another thread which is waiting on the condition variable.
 * It should be called after mutex is locked, and must unlock mutext in order for cv_wait() to complete.
 */
void
cv_signal(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL); 
        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_lock);
        wchan_wakeone(cv->cv_wchan, &cv->cv_lock);
        spinlock_release(&cv->cv_lock);

}

/*
 * cv_broadcast should be used instead of cv_signal() if more than one thread is waiting on the condition.
 */
void
cv_broadcast(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL); 
        KASSERT(lock_do_i_hold(lock));

        spinlock_acquire(&cv->cv_lock);        
        wchan_wakeall(cv->cv_wchan, &cv->cv_lock);
        spinlock_release(&cv->cv_lock);


}