#ifndef SYNCH_H
#define SYNCH_H

#include <list.h>
#include <stdbool.h>

/* Lock. */
struct lock 
  {
	// Your implementation of lock structure.
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

#endif
