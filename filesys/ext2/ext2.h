#ifndef FILESYS_EXT2_H
#define FILESYS_EXT2_H

#include <stdint.h>
#include <stdbool.h>

#include "devices/block.h"

struct ext2_meta_data {
		char device_name[16];
        struct superblock *sb;
        struct bg_desc_table *bg_desc_tabs;
};

// definitions for s_state field
#define EXT2_VALID_FS 1 //Umounted cleanly
#define EXT2_ERROR_FS 2 //Errors detected

// definitions for s_errors field
#define EXT2_ERRORS_CONTINUE 1 //continue as if nothing happened
#define EXT2_ERRORS_RO		 2 //remount read-only
#define EXT2_ERRORS_PANIC	 3 //cause a kernel panic

// Alloc and Free
bool is_ext2 (struct block * d);
int ext2_init();
int ext2_register(struct block*);
void ext2_free();

// Get Ext2 Data
struct ext2_meta_data *ext2_get_meta(struct block *d);
uint32_t ext2_get_block_size(struct superblock *sb);
void* ext2_read_block(struct block *d, uint32_t block_idx, uint32_t block_size, void *buffer_);
struct bitmap* ext2_read_bitmap(struct block *d, int block_idx);

void ext2_write_block(struct block *d, uint32_t block_idx, uint32_t block_size, const void *buffer);
void ext2_write_superblock(struct block *d, void *sb);
void ext2_write_bg_desc_tables(struct block *d, void *bg_desc_tabs);

#endif
