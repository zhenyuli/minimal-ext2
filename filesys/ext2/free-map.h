#ifndef EXT2_FREEMAP_H
#define EXT2_FREEMAP_H

#include <stdint.h>
#include <stdbool.h>

#define FREEMAP_GET_ERROR UINT32_MAX

void freemap_init();
uint32_t freemap_get_block(bool);
uint32_t freemap_get_blocks(uint32_t,bool);
void freemap_free_block(uint32_t);
void freemap_free_blocks(uint32_t,uint32_t);
uint32_t freemap_get_inode(void);
void freemap_free_inode(uint32_t);

#endif
