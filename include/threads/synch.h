#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};

/*** initialize semaphore to given value ***/
void sema_init (struct semaphore *, unsigned value);
bool sema_lower_func(const struct list_elem *a, const struct list_elem *b, void *aux);
/*** lower value by 1 when requested and acquired semaphore ***/
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
/*** return semaphore and upper value by 1 ***/
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

/*** initialize lock data structure ***/
void lock_init (struct lock *);
/*** request lock ***/
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
/*** return lock ***/
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

/*** initialize condition variable data structure ***/
void cond_init (struct condition *);
/*** wait until signal is noticed by condition variable ***/
void cond_wait (struct condition *, struct lock *);
/*** send signal to highest priority thread waiting in condition variable ***/
void cond_signal (struct condition *, struct lock *);
/*** send signal to all threads waiting in condition variable ***/
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
