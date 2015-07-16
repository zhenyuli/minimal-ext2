#ifndef EXT2_BLOCKGROUP_H
#define EXT2_BLOCKGROUP_H

#include <stdint.h>

#define EXT2_BG_OFFSET_BLOCK 1 //offset in units of blocks
#define EXT2_BG_SIZE_BLOCK 1 //size in units of blocks
// block group descriptor table
struct bg_desc_table {
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint8_t bg_reserved[12];
} __attribute__((packed));

void print_bg_desc_table(struct bg_desc_table *tab);
#endif
