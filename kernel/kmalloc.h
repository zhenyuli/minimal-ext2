#ifndef _MALLOC_H
#define _MALLOC_H

#include <stddef.h>
#include <stdint.h>

void    kmalloc(size_t);				
void    krealloc(void *, size_t);
void    kcalloc(size_t, size_t);
void    kfree(void *);

#endif
