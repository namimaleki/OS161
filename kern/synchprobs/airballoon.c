/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <current.h>
#include <kern/errno.h>


/* ====================== DESIGN DOCUMENTATION ============================ */

/* Overview of the problem : we want a concurrent system where:
	1. Marigold unties ropes from ground stakes.
	2. Dandelion unhooks ropes from balloon hooks.
	3. Lord FlowerKillers (multiple of them) swap ropes between stakes.
	4. The Balloon waits until all ropes are severed to announce escape.
	
	Each rope connects one hook to one stake and can be detatched exactly once that is either 
	from the hook or from the stake not from both. 
	
	The data structures we will use: 
		- rope: keep track of rope_id, the stake index (that is the stake the rope is attathced to), severed state, and rope_lk to allow only one worker to access the rope at a time.
		- stake: maps one stake to one rope, protected by stake_lk (only on thread can access a specefic stake at a time)
		- hook: fixed attatchment to the ballon, accessed by dandelion only.
		
	Now that we have our data structures we create lists of them and store them there (arrays: ropes[], hooks[], stakes[])
	
	We also need global synchronization primitives: 
		- counter_lk - protects the global count of ropes_left and escape_cv 
		- print_lk  - ensures atomic output (no interleaving prints)
		- escape_cv - used by Balloon to wait for all ropes to be cut
		- threads_finished - semaphorefor main to wait for all threads
		
	
	Invariants: 
		- Each rope is attatched to exactly one stake and hook 
		- Once severed, rope.severed remains true permanently for that rope and no operations will be performed on that rope (switches)
		- ropes_left equals the number of unserved ropes
		
	
	Thread Exit Conditions
	
		- Marigold & Dandelion: will loop until ropes_left == 0, then exit. The one who makes the last rope cut will signal the Balloon via escape_cv.
		- FlowKillers: Also stop once ropes_left == 0
		- Balloon: waits on escape_cv; when signaled, prints escape message
		- Main: waits on threads_finished untill all threads are done, then calls cleanup */

/* ==============================================================================================================================*/



/* This is th number of concurrent Lord FlowerKiller clones and total ropes in the problem */
#define N_LORD_FLOWERKILLER 8
#define NROPES 16

/* This is a shared global couter amongs all threads which tracks the number of remaining attatched ropes */
static int ropes_left = NROPES;


/* GLOBAL SYNCHRONIZATION PRIMITIVES */

/* Create a semaphore to use by the main thread to wait for all threads (Marigold, dandelion, flowerkillers, baloon) to complete before cleanup */
/* Through this semaphore we can also ensure that the main threads done print statement is after all the other threads */
static struct semaphore *threads_finished;

/* need a lock so we can safely access the ropes_left count which is shared amongs all threads. */	
static struct lock *counter_lk; 

/* condition variable that signals when all ropes have been severed. Used by the balloon thread */
/* The Balloon sleeps on this cv until all ropes have been detatched at which point marigold or dandelion will signal it to wake up */
static struct cv *escape_cv; 

/* need a lock to ensure print statments are printed atomically */
static struct lock *print_lk;



/* DATA STRUCTURES FOR ROPE MAPPINGS */

/* Rope structure: Each rope has its own rope_lk to ensuring that severing and swapping operations are mutually exclusive.*/
struct rope {
	bool severed; /* true if rope has been severed false if still attached*/
	int rope_id; /* assign an id for each rope [0 .. NROPES - 1]*/
	int stake_index; /* index of the stake the rope is currently attatched to */
	int hook_index; /* This will be fixed through out the hole process */
	struct lock *rope_lk; /* Lock for rope to avoid access from more than one character at once */
};

/* Stake structure: multiple threads may access the same stake, so each stake will have its own lock to prevent simultaneaous swaps or detachment*/
struct stake{
	int id; /* stake index [0 .. NROPES - 1]*/
	struct rope *rope; /* pointer to the rope currently attatched to this stake */
	struct lock *stake_lk; /* each stake will have a corresponding lock */
};

/* Hook strucutre: Each hook is fixed to the ballon and has a corresponding rope. Since they are only accessed by dandelion no lock is needed*/
struct hook{
	int id; /* hook index */
	struct rope *rope; /* pointer to the rope attatched to this hook */
};


/* Arrays to hold all the ropes, stakes, and hooks */
static struct rope *ropes[NROPES];
static struct stake *stakes[NROPES];
static struct hook *hooks[NROPES];


/* INITIALIZATION & CLEANUP */

/* init_setup(): Called at the start of the program. Function allocates and initializes all ropes, stakes, and hooks, and creates the necessary synchronization primitives.*/
/* As states in the problem at the beginning there will be a 1:1 correspondence between balloon hooks and ground stakes */
static void init_setup(void){
	/* must reset rope counter */
	ropes_left = NROPES;

	/* Allocate and initialize each rope, stake and hook */
	for (int i = 0; i < NROPES; i++){

		/* Rope set up */
		ropes[i] = kmalloc(sizeof(struct rope)); 
		ropes[i]->severed = false;
		ropes[i]->rope_id = i; 
		ropes[i]->stake_index = i; 
		ropes[i]->hook_index = i; 
		ropes[i]->rope_lk = lock_create("rope_lk");

		/* stakes setup */
		stakes[i] = kmalloc(sizeof(struct stake)); 
		stakes[i]->id = i; 
		stakes[i]->rope = ropes[i]; 
		stakes[i]->stake_lk = lock_create("stake_lk"); 

		/* hooks setup */
		hooks[i] = kmalloc (sizeof(struct hook));
		hooks[i]->id = i; 
		hooks[i]->rope = ropes[i];
	}

	/* create the global synchronization primitives */
	counter_lk = lock_create("counter_lk"); 
	escape_cv = cv_create("escape_cv");
	print_lk = lock_create("print_lk"); 
	threads_finished = sem_create("threads_finished", 0); 
}


/* cleanup_setup(): Frees all dynamically allocated resources at the end of the program. Prevents memory leaks. */
static void cleanup_setup(void){	
	for (int i = 0; i < NROPES; i++){
		/* destroy all the rocks associated with the ropes first */
		lock_destroy(ropes[i]->rope_lk);
		/* once the lock is destroyed we can free the memory for the corresponding rope  */
		kfree(ropes[i]);

		/* destroy locks associated with stakes then free the memory for the stakes */
		lock_destroy(stakes[i]->stake_lk); 
		kfree(stakes[i]);

		/* hooks got no locks so just free it */
		kfree(hooks[i]); 
	}

	/* now we destroy the global synchronization primitives */
	lock_destroy(counter_lk);
	cv_destroy(escape_cv); 
	lock_destroy(print_lk); 
	sem_destroy(threads_finished);
}




/* dandelion: Unhooks ropes from ballon */
/* Rope can be severed by exactly one thread, once severed no other thread can perform operations on it.*/
static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	kprintf("Dandelion thread starting\n");
	lock_release(print_lock);


	/* The Dandelion thread runs until all ropes are detatched. Each iteration represents one attempt to randomly pick a hook, check its rope and (if attached) sever it*/
	while(1){

		/* first we check if all ropes have been severed */
		lock_acquire(counter_lk); 
		if (ropes_left == 0){
			/* since the signal has already been sent by whoever cut the last rope there's nothing to wake */
			/* balloon is already awake or will soon be so we just release the counter lock and exit */
			lock_release(counter_lk); 
			break;
		}
		lock_release(counter_lk); 
		
		/* choose randome hook */
		int hook_index = random() % NROPES;
		struct rope *r = hooks[hook_index]->rope;
		
		/* lock the rope before accessing its state */
		lock_acquire(r->rope_lk); 

		/* if the rope has already been detatched we just continue looking for other roops */
		if(r->severed){
			lock_release(r->rope_lk);
			thread_yield(); /* yield to allow other threads to run */
			continue;
		}

		/* Grab the print lock so we can print that the rope has been severed */
		lock_acquire(print_lk);
		kprintf("Dandelion severed rope %d\n", r->rope_id);
		lock_release(print_lk); 

		/* mark the rope as detatched */
		r->severed = true;

		

		/* now we update the rope counter */
		lock_acquire(counter_lk);
		ropes_left--; 
		if (ropes_left == 0){
			/* signal balloon with escape_cv if all ropes have been detatched */
			cv_signal(escape_cv, counter_lk); 
		}
		lock_release(counter_lk);
		
		/* work is done onto the next */
		lock_release(r->rope_lk);
		
		/* this is to allow threads to interleave */
		thread_yield();
	}
	
	kprintf("Dandelion thread done\n");
	/* Signal main thread that this thread is finished */
	V(threads_finished);
}

/* Marigold : unties ropes form the ground stakes */
static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(print_lock);
	kprintf("Marigold thread starting\n");
	lock_release(print_lock);

	while(1){

		/* check if all ropes have been severed first*/
		lock_acquire(counter_lk); 
		if (ropes_left == 0){
			lock_release(counter_lk); 
			break; 
		}
		lock_release(counter_lk);

		/* Choose a random stake and acquire its lock */
		int idx = random() % NROPES;
		struct stake *s = stakes[idx]; 
		lock_acquire(s->stake_lk); 

		/* Access the rope attatched to this stake */
		struct rope *r = s->rope; 

		/* lock the rope to avoid dandelion or flowerkiller from accessing it*/
		lock_acquire(r->rope_lk); 
		
		/* we skip it if the rope has already been detached */
		if (r->severed){
			lock_release(r->rope_lk); 
			lock_release(s->stake_lk); 
			thread_yield();
			continue; 
		}

		/* Grab the print lock so we can print that the rope has been severed */
		lock_acquire(print_lk);
		kprintf("Marigold severed rope %d from stake %d\n", r->rope_id, s->id);
		lock_release(print_lk); 

		/* mark as severe */
		r->severed = true; 

	
		/* now we update the rope counter */
		lock_acquire(counter_lk);
		ropes_left--; 
		if (ropes_left == 0){
			cv_signal(escape_cv, counter_lk); 
		}
		lock_release(counter_lk);

		/* Release locks in reverse acquisition order (rope_lk -> stake_lk) this order avoids deadlocks */
		/* because stake_lk protects stake ownership adn rope_lk protects rope state.*/
		lock_release(r->rope_lk); 
		lock_release(s->stake_lk);
		thread_yield();
	}

	kprintf("Marigold thread done\n");
	/* Again we need to notify the main thread that this thread is done working*/
	V(threads_finished);
}


/* Lord FlowerKiller threads: continously swap ropes between stakes.*/
static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(print_lock);
	kprintf("Lord FlowerKiller thread starting\n");
	lock_release(print_lock);

	while (1) {
		
		/* First check if all ropes are severed cuz if so then there is no work to be done */
		lock_acquire(counter_lk);
		if (ropes_left == 0){
			lock_release(counter_lk);
			break; 
		}
		lock_release(counter_lk); 

		/* Need to pick 2 randome distinct stakes */
		int stake1 = random() % NROPES;
		int stake2 = random() % NROPES;
		if (stake1 == stake2){
			thread_yield();
			continue; 
		}
		
		/* Lock in increasing index order to avoid deadlocks. So then everythread will try to grab the stake with the lower index */
		/* before the one with the higher index this prevents two flowrkiller threads form deadlocking one another*/
		int low = (stake1 < stake2) ? stake1 : stake2;
		int high = (stake1 > stake2) ? stake1 : stake2;

		struct stake *s = stakes[low];
		struct stake *s1 = stakes[high];

		
		lock_acquire(s->stake_lk); 
		lock_acquire(s1->stake_lk); 

		/* Get the ropes attatched to each stake */
		struct rope *r = s->rope;
		struct rope *r1 = s1->rope;

		/* Lock the ropes */
		lock_acquire(r->rope_lk);
		lock_acquire(r1->rope_lk);

		/* If either of the ropes are already severed we skip it */
		if(r->severed || r1->severed){
			lock_release(r->rope_lk); 
			lock_release(r1->rope_lk); 

			lock_release(s->stake_lk);
			lock_release(s1->stake_lk); 

			thread_yield();
			continue; 
		}

		/* Save the old indexes for printing */
		int old_low_idx = r->stake_index; 
		int old_high_idx = r1->stake_index;

		/* Swap */
		r->stake_index = s1->id; 
		r1->stake_index = s->id;

		s1->rope = r;
		s->rope = r1; 


		/* Print the swap msg */
		lock_acquire(print_lk); 
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", r->rope_id, old_low_idx, s1->id);
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", r1->rope_id, old_high_idx, s->id);
		lock_release(print_lk);

		/* Release the locks in reverese order */
		lock_release(r1->rope_lk);
		lock_release(r->rope_lk);
		lock_release(s1->stake_lk);
		lock_release(s->stake_lk); 

		thread_yield();

	}

	kprintf("Lord FlowerKiller thread done\n");
	/* notify main thread of completion */
	V(threads_finished);
}


/* Balloon thread: waits for escape condition. Remains idle untill all ropes have been severed */
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(print_lock);
	kprintf("Balloon thread starting\n");
	lock_release(print_lock);

	/* Wait until all ropes have been severed */
	lock_acquire(counter_lk);

	while(ropes_left > 0){
		/* Sleep until signaled by dandelion or marigold*/
		cv_wait(escape_cv, counter_lk); 
	}
	lock_release(counter_lk); 

	/* Announce escape */
	lock_acquire(print_lk);
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	lock_release(print_lk);

	kprintf("Balloon thread done\n");
	V(threads_finished);
}


// airballoon(): this is the entry point (main thread)
int
airballoon(int nargs, char **args)
{

	/* reset rope count */
	ropes_left = NROPES;

	/* set up rope data structures */
	for (int i = 0; i < NROPES; i++) {
		ropes[i].is_tied = true;
		ropes[i].rope_lock = lock_create("rope_lock");
		if (ropes[i].rope_lock == NULL) {
			panic("airballoon: lock_create failed\n");
		}
		ropes_hooks[i] = i;
		ropes_stakes[i] = i;
	}

	/* set up synchronization primitives */
	print_lock = lock_create("print_lock");
	if (print_lock == NULL) {
		panic("airballoon: lock_create failed\n");
	}

	rope_count_lock = lock_create("rope_count_lock");
	if (rope_count_lock == NULL) {
		panic("airballoon: lock_create failed\n");
	}

	for (int i = 0; i < NROPES; i++) {
		stakes_locks[i] = lock_create("stake_lock");
		if (stakes_locks[i] == NULL) {
			panic("airballoon: lock_create failed\n");
		}
	}

	rope_count_cv = cv_create("rope_count_cv");
	if (rope_count_cv == NULL) {
		panic("airballoon: cv_create failed\n");
	}

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	init_setup(); /* Initialize everything */

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;
	
	
	/* wait for all threads to finish (11 threads total)*/
	for (i = 0; i < N_LORD_FLOWERKILLER + 3; i++){
		/* we initialized the semaphore with 0 so it starts at count 0. Hence any time P(threads_finished) is called */
		/* it will block until another thread performs V(threads_finished), so after 11 V() calls we will have gone through this loop*/
		P(threads_finished);
	}

	kprintf("Main thread done\n");
	goto done;

panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	/* clean up everythign after threads complete */
	cleanup_setup();
	return 0;
}
