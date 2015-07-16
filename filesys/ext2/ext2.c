#include "filesys/ext2/ext2.h"
#include "filesys/ext2/superblock.h"
#include "filesys/ext2/block_group.h"
#include "filesys/ext2/free-map.h"
#include "devices/block.h"
#include "kernel/kmalloc.h"
#include "kernel/synch.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <bitmap.h>

#define EXT2_MAX_DEVICES 1
static int ext2_devices_count;
static struct ext2_meta_data *ext2_meta[EXT2_MAX_DEVICES];

static uint32_t byte_to_sector(size_t bytes);

// Disk Reads
static struct superblock *ext2_read_superblock(struct block *d);
static struct bg_desc_table *ext2_read_bg_desc_tables(struct block *d);

// Synchronisation Mechanisms
static struct lock register_lock;

static uint32_t byte_to_sector(size_t bytes){
	return (uint32_t)DIV_ROUND_UP(bytes,BLOCK_SECTOR_SIZE);
}

struct ext2_meta_data *ext2_get_meta(struct block *d){
	int i;
	struct ext2_meta_data *meta = NULL, *ptr;

	ASSERT(d != NULL);
	
	for(i = 0; i < ext2_devices_count; i++){
		ptr = ext2_meta[i];
		if(memcmp(ptr->device_name,block_name(d),16) == 0){
			meta = ptr;
			break;
		}
	}
	return meta;
}

uint32_t ext2_get_block_size(struct superblock *sb){
	ASSERT(sb != NULL);
	ASSERT(sb->s_log_block_size >= 0);
	return 1024 << sb->s_log_block_size;
}

/* Reads a block from block device,
 * the block size of a file system may be different from device sector size
*/
void* ext2_read_block(struct block *d, uint32_t block_idx, uint32_t block_size, void *buffer_){
	void *buffer = buffer_;
	int sectors = byte_to_sector(block_size);
	int sector_idx = block_idx * sectors;
	int i;

	ASSERT(d != NULL);
	ASSERT((block_size % BLOCK_SECTOR_SIZE) == 0);
	
	// allocate memory
	if(buffer == NULL) buffer = kmalloc(block_size);
	ASSERT(buffer != NULL);

	// read from disk
	for(i = 0; i < sectors; i++){
		block_read(d,sector_idx+i,(void*)((uint8_t*)buffer+i*BLOCK_SECTOR_SIZE));
	}
	return buffer;
}
void ext2_write_block(struct block *d, uint32_t block_idx, uint32_t block_size, const void *buffer){
	int sectors = byte_to_sector(block_size);
	int sector_idx = block_idx * sectors;
	int i;

	ASSERT(d != NULL);
	ASSERT((block_size % BLOCK_SECTOR_SIZE) == 0);
	
	// write to disk
	for(i = 0; i < sectors; i++){
		block_write(d,sector_idx+i,(void*)((uint8_t*)buffer+i*BLOCK_SECTOR_SIZE));
	}
}

bool is_ext2 (struct block * d){
	struct superblock *sb = ext2_read_superblock(d);
	bool flag = false;
	if(sb->s_magic == EXT2_SUPER_MAGIC) flag = true;
	ext2_print_superblock(sb);
	kfree(sb);
	return flag;
}

/* Initialise ext2 filesystem located in the block device
 * read the super block, block group descriptor table
 * block bitmap, inode bitmap from the disk.
*/
int ext2_init(){
	// Initialise device count and meta data
	ext2_devices_count = 0;
	memset((void*)ext2_meta,0,sizeof(ext2_meta));
	// Initialise locks
	lock_init(&register_lock);
	// Initialise free map
	freemap_init();
	return 0;
}

void ext2_free(){
	struct ext2_meta_data *ptr = NULL;
	int i;
	for(i = 0; i < ext2_devices_count; i++){
		ptr = ext2_meta[i];
		if(ptr == NULL) continue;
		if(ptr->sb != NULL) kfree(ptr->sb);
		if(ptr->bg_desc_tabs != NULL) kfree(ptr->bg_desc_tabs);
	}
}

/* Registers block device */
int ext2_register(struct block *d){
	struct ext2_meta_data *meta = NULL;

	ASSERT(d != NULL);
	ASSERT(ext2_devices_count < EXT2_MAX_DEVICES);

	//Acquire lock
	lock_acquire(&register_lock);

	// Allocate memory
	meta = (struct ext2_meta_data*)kmalloc(sizeof(struct ext2_meta_data));
	ext2_meta[ext2_devices_count] = meta;
	// Set device name and increment device count
	memcpy(meta->device_name,block_name(d),16);
	ext2_devices_count++;

	//Release lock
	lock_release(&register_lock);

	// The following can be done without locking
	// Read superblock
	meta->sb = ext2_read_superblock(d);
	// Read block group descriptor table
	meta->bg_desc_tabs = ext2_read_bg_desc_tables(d);

	//Print message
	printf("Device %s is registered in ext2 filesys.\n",block_name(d));

	return 0;
}

static struct superblock *ext2_read_superblock(struct block *d){
	int i;
	uint32_t sectors = byte_to_sector(EXT2_SUPER_SIZE);
	uint32_t sector_idx = EXT2_SUPER_OFFSET/BLOCK_SECTOR_SIZE;
	struct superblock *sb;

	ASSERT(d != NULL);

	//allocate memroy
	sb = kmalloc(sectors * BLOCK_SECTOR_SIZE);
	if(sb == NULL) return NULL;

	// read from disk
	for(i = 0 ; i < sectors; i++)
		block_read(d,sector_idx+i,(void*)((uint8_t*)sb+i*BLOCK_SECTOR_SIZE));

	return sb;
}
void ext2_write_superblock(struct block *d, void *sb){
	int i;
	uint32_t sectors = byte_to_sector(EXT2_SUPER_SIZE);
	uint32_t sector_idx = EXT2_SUPER_OFFSET/BLOCK_SECTOR_SIZE;

	ASSERT(d != NULL && sb != NULL);

	for(i = 0 ; i < sectors; i++)
		block_write(d,sector_idx+i,(void*)((uint8_t*)sb+i*BLOCK_SECTOR_SIZE));
}


static struct bg_desc_table *ext2_read_bg_desc_tables(struct block *d){
	struct ext2_meta_data *meta = NULL;
	struct superblock *sb = NULL;
	struct bg_desc_table *bg_desc_tables = NULL;
	uint32_t block_size = 0;
	uint32_t block_groups = 0;
	uint32_t bg_desc_tabs_size = 0;
	uint32_t blocks_to_read = 0;
	int i;


	// get meta data
	ASSERT(d != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL);
	sb = meta->sb;
	ASSERT(sb != NULL);
	block_size = ext2_get_block_size(sb);

	// calculate the number of block groups
	block_groups = DIV_ROUND_UP(meta->sb->s_blocks_count,meta->sb->s_blocks_per_group);
	bg_desc_tabs_size = block_groups * sizeof(struct bg_desc_table);
	blocks_to_read = DIV_ROUND_UP(bg_desc_tabs_size,block_size);

	// Allocating memory
	bg_desc_tables = kmalloc(blocks_to_read*block_size);
	// read bg_desc_tables
	if(block_size > 1024){
		for(i = 0; i < blocks_to_read; i++)
			ext2_read_block(d,1+i,block_size,(void*)((uint8_t*)bg_desc_tables+i*block_size));
	}
	else{
		for(i = 0; i < blocks_to_read; i++)
			ext2_read_block(d,2+i,block_size,(void*)((uint8_t*)bg_desc_tables+i*block_size));
	}

	return bg_desc_tables;
}
void ext2_write_bg_desc_tables(struct block *d, void *bg_desc_tabs){
	struct ext2_meta_data *meta = NULL;
	uint32_t block_size = 0;
	uint32_t block_groups = 0;
	uint32_t bg_desc_tabs_size = 0;
	uint32_t blocks_to_write = 0;
	int i;

	// get meta data
	ASSERT(d != NULL && bg_desc_tabs != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// calculate the number of block groups
	block_groups = DIV_ROUND_UP(meta->sb->s_blocks_count,meta->sb->s_blocks_per_group);
	bg_desc_tabs_size = block_groups * sizeof(struct bg_desc_table);
	blocks_to_write = DIV_ROUND_UP(bg_desc_tabs_size,block_size);

	// read bg_desc_tables
	if(block_size > 1024){
		for(i = 0; i < blocks_to_write; i++)
			ext2_write_block(d,1+i,block_size,(void*)((uint8_t*)bg_desc_tabs+i*block_size));
	}
	else{
		for(i = 0; i < blocks_to_write; i++)
			ext2_write_block(d,2+i,block_size,(void*)((uint8_t*)bg_desc_tabs+i*block_size));
	}
}


// Reads the bitmap located at the OFFSET_BLOCKS blocks of the device.
struct bitmap* ext2_read_bitmap(struct block *d, int block_idx){
	struct ext2_meta_data *meta = NULL;
	struct superblock *sb = NULL;
	uint32_t block_size = 0;
	void *bm_buffer = NULL;
	struct bitmap *bm = NULL;

	// get meta data
	ASSERT(d != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL);
	sb = meta->sb;
	ASSERT(sb != NULL);
	block_size = ext2_get_block_size(sb);

	// read bitmap from disk
	bm_buffer = ext2_read_block(d,block_idx,block_size,NULL);
	ASSERT(bm_buffer != NULL);
	// initialise bitmap
	bm = bitmap_create_from_buf(bm_buffer,block_size);

	return bm;
}
