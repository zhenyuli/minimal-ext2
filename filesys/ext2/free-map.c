#include "filesys/ext2/free-map.h"
#include "filesys/ext2/ext2.h"
#include "filesys/ext2/superblock.h"
#include "filesys/ext2/block_group.h"
#include "devices/block.h"
#include "kernel/synch.h"
#include "kernel/kmalloc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <bitmap.h>
#include <debug.h>

/*
 * CHANGLOG
 * 13 JULY 2015 BY ZHENYU LI
 * Be careful, write the bitmap internal storage to the disk,
 * instead of the bitmap structure itself!
*/

static struct lock freemap_lock;

/* Initialise freemap */
void freemap_init(){
	lock_init(&freemap_lock);
}
/* Get one available block from disk */
uint32_t freemap_get_block(bool zero){
	return freemap_get_blocks(1,zero);
}
/* Get BLOCKS free blocks */
uint32_t freemap_get_blocks(uint32_t blocks,bool zero){
	int i;
	struct block *d = block_get_role(BLOCK_FILESYS);
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc = NULL;
	struct bitmap *block_map = NULL;
	uint32_t block_size = 0,free_blocks = 0, bg_groups = 0, bg_group = 0;
	uint32_t block_id;

	ASSERT(blocks > 0);
	ASSERT(d != NULL);

	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL && meta->bg_desc_tabs != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// Check if there are enough free blocks
	free_blocks = meta->sb->s_free_blocks_count;
	if(free_blocks < blocks) return FREEMAP_GET_ERROR;

	// Get block groups
	bg_groups = meta->sb->s_blocks_count / meta->sb->s_blocks_per_group;

	// Search block groups, start from the second group
	for(i = 1; i < bg_groups; i++){
		bg_desc = &(meta->bg_desc_tabs[i]);
		free_blocks = bg_desc->bg_free_blocks_count;
		if(free_blocks >= blocks) break;
	} 

	// Check if block group is found
	if(i == bg_groups) return FREEMAP_GET_ERROR;
	bg_group = i; // Save block_group

	// Acquire lock
	lock_acquire(&freemap_lock);

	// Block group is found
	block_map = ext2_read_bitmap(d,bg_desc->bg_block_bitmap);
	block_id = bitmap_scan_and_flip (block_map,0,blocks,false);
	if(block_id != BITMAP_ERROR){
		// Important: offset by the block group.
		block_id += bg_group * meta->sb->s_blocks_per_group;
		/* Important: bit 0 of byte 0 represent the first block of the block group.
		 * superblock is the first block of block group 0,
		 * but it is actually located at s_first_data_block.
		 * Hence, one must offset s_first_data_block in block id calculation.
		*/
		block_id += meta->sb->s_first_data_block;
		// Update statistics
		meta->sb->s_free_blocks_count -= blocks;
		bg_desc->bg_free_blocks_count -= blocks;
		// Write bitmap to disk
		ext2_write_block(d,bg_desc->bg_block_bitmap,block_size,bitmap_get_bits(block_map));
		// Write superblock
		ext2_write_superblock(d,meta->sb);
		// Write block group description table
		ext2_write_bg_desc_tables(d,meta->bg_desc_tabs);	
		// Zero newly allotted blocks
		if(zero){
			void *zero_buf = kmalloc(block_size);
			memset(zero_buf,0,block_size);
			for(i = 0; i < blocks; i++)
				ext2_write_block(d,block_id+i,block_size,zero_buf);
			kfree(zero_buf);
		}
	}
	lock_release(&freemap_lock);

	// Check if successful
	if(block_id == BITMAP_ERROR) block_id = FREEMAP_GET_ERROR;

	//free memory, this will also free internal memory
	bitmap_destroy(block_map);

	return block_id;
}
/* Free block pointed by block id */
void freemap_free_block(uint32_t block_id){
	freemap_free_blocks(block_id,1);
}
/* Free multiple blocks starting at block id */
void freemap_free_blocks(uint32_t block_id,uint32_t blocks){
	struct block *d = block_get_role(BLOCK_FILESYS);
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc = NULL;
	struct bitmap *block_map = NULL;
	uint32_t block_size = 0, bg_group = 0;
	uint32_t local_idx;

	ASSERT(block_id > 0 && blocks > 0);

	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL && meta->bg_desc_tabs != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// Re-calibrate block id to data block id
	// For details, see freemap_get_blocks().
	block_id -= meta->sb->s_first_data_block;

	// Get block group
	bg_group = block_id / meta->sb->s_blocks_per_group;
	bg_desc = &(meta->bg_desc_tabs[bg_group]);

	// Get local block index
	local_idx = block_id % meta->sb->s_blocks_per_group;

	// Acqiure lock
	lock_acquire(&freemap_lock);
	// read bitmap
	block_map = ext2_read_bitmap(d,bg_desc->bg_block_bitmap);
	ASSERT(bitmap_all(block_map,local_idx,blocks));

	bitmap_set_multiple(block_map,local_idx,blocks,false);

	// Update statistics
	meta->sb->s_free_blocks_count += blocks;
	bg_desc->bg_free_blocks_count += blocks;
	// Write bitmap to disk
	ext2_write_block(d,bg_desc->bg_block_bitmap,block_size,bitmap_get_bits(block_map));
	// Write superblock
	ext2_write_superblock(d,meta->sb);
	// Write block group description table
	ext2_write_bg_desc_tables(d,meta->bg_desc_tabs);	

	// free lock
	lock_release(&freemap_lock);

	//free memory, this will also free internal memory
	bitmap_destroy(block_map);
}

/* Get free inode */
uint32_t freemap_get_inode(){
	int i;
	struct block *d = block_get_role(BLOCK_FILESYS);
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc = NULL;
	struct bitmap *inode_map = NULL;
	uint32_t block_size = 0,free_inodes = 0, bg_groups = 0, bg_group = 0;
	uint32_t inode_id;

	ASSERT(d != NULL);

	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL && meta->bg_desc_tabs != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// Check if there are enough free inodes
	free_inodes = meta->sb->s_free_inodes_count;
	if(free_inodes < 1) return FREEMAP_GET_ERROR;

	// Get block groups
	bg_groups = meta->sb->s_inodes_count / meta->sb->s_inodes_per_group;

	// Search block groups, start from the second group
	for(i = 1; i < bg_groups; i++){
		bg_desc = &(meta->bg_desc_tabs[i]);
		free_inodes = bg_desc->bg_free_inodes_count;
		if(free_inodes > 0) break;
	} 

	// Check if block group is found
	if(i == bg_groups) return FREEMAP_GET_ERROR;
	bg_group = i; // Save block_group

	// Acquire lock
	lock_acquire(&freemap_lock);

	// Block group is found
	inode_map = ext2_read_bitmap(d,bg_desc->bg_inode_bitmap);
	inode_id = bitmap_scan_and_flip (inode_map,0,1,false);
	if(inode_id != BITMAP_ERROR){
		// Important: offset by the block group.
		inode_id += bg_group * meta->sb->s_inodes_per_group;
		// Inode Number starts from 1
		inode_id ++;
		// Update statistics
		meta->sb->s_free_inodes_count --;
		bg_desc->bg_free_inodes_count --;
		// Write bitmap to disk
		ext2_write_block(d,bg_desc->bg_inode_bitmap,block_size,bitmap_get_bits(inode_map));
		// Write superblock
		ext2_write_superblock(d,meta->sb);
		// Write block group description table
		ext2_write_bg_desc_tables(d,meta->bg_desc_tabs);	
	}
	lock_release(&freemap_lock);

	// Check if successful
	if(inode_id == BITMAP_ERROR) inode_id = FREEMAP_GET_ERROR;

	//free memory, this will also free internal memory
	bitmap_destroy(inode_map);

	return inode_id;
}

/* Free inode */
void freemap_free_inode(uint32_t inode){
	struct block *d = block_get_role(BLOCK_FILESYS);
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc = NULL;
	struct bitmap *inode_map = NULL;
	uint32_t block_size = 0, bg_group = 0;
	uint32_t local_idx;

	ASSERT(d != NULL);

	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL && meta->bg_desc_tabs != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// Get block group, note: inodes start from 1.
	bg_group = (inode - 1) / meta->sb->s_inodes_per_group;
	bg_desc = &(meta->bg_desc_tabs[bg_group]);

	// Calculate local index, note: inodes start from 1.
	local_idx = (inode - 1) % meta->sb->s_inodes_per_group;

	// Acquire lock
	lock_acquire(&freemap_lock);

	// Block group is found
	inode_map = ext2_read_bitmap(d,bg_desc->bg_inode_bitmap);
	ASSERT(bitmap_all(inode_map,local_idx,1));

	// Set bit to false
	bitmap_set_multiple(inode_map,local_idx,1,false);

	// Update statistics
	meta->sb->s_free_inodes_count ++;
	bg_desc->bg_free_inodes_count ++;
	// Write bitmap to disk
	ext2_write_block(d,bg_desc->bg_inode_bitmap,block_size,bitmap_get_bits(inode_map));
	// Write superblock
	ext2_write_superblock(d,meta->sb);
	// Write block group description table
	ext2_write_bg_desc_tables(d,meta->bg_desc_tabs);	

	// release lock
	lock_release(&freemap_lock);

	//free memory, this will also free internal memory
	bitmap_destroy(inode_map);
}
