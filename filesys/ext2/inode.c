#include "filesys/ext2/inode.h"
#include "filesys/ext2/ext2.h"
#include "filesys/ext2/superblock.h"
#include "filesys/ext2/block_group.h"
#include "filesys/ext2/free-map.h"
#include "filesys/off_t.h"
#include "devices/block.h"
#include "kernel/kmalloc.h"
#include "kernel/synch.h"

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <bitmap.h>

#define DIRECT_BLOCKS 12

enum RANGE {
	RANGE_OVERLAP = 1,
	RANGE_CONTAINS = 1<<1, // range 1 contains range 2
	RANGE_CONTAINED = 1<<2, // range 2 contains range 1
	RANGE_AHEAD = 1<<3,
	RANGE_BEHIND = 1<<4,
};

static uint32_t inode_get_data_block (struct block *d, struct inode *inode, uint32_t idx);
static uint32_t inode_traverse_linklist(struct block *d, uint32_t block_id, uint32_t idx, uint32_t level);
static int inode_expand_range(uint32_t block_id, uint32_t level, uint32_t start, uint32_t end, uint32_t items_per_block,uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3);
static int inode_shrink_range(uint32_t block_id, uint32_t level, uint32_t start, uint32_t end, uint32_t items_per_block,uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3);
static enum RANGE inode_range_compare(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2);
static uint32_t inode_get_direct_block_idx(uint32_t items_per_block, uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3);
static int inode_get_indirect_blocks(uint32_t direct_blocks, uint32_t items_per_block);

void print_inode(struct inode *ino){
	int i;
	ASSERT(ino != NULL);

	printf("i_mode: 0x%x, i_flags: 0x%x, i_uid: 0x%x, i_size: 0x%x, i_blocks: %d\n",
		ino->i_mode, ino->i_flags, ino->i_uid, ino->i_size, ino->i_blocks);
	for(i = 0; i < 15; i++){
		printf("i_block[%d]: 0x%"PRIx32" ", i, ino->i_block[i]);
	}
	printf("\n");
}

/* Get the ENTRY item of the inode table from BLOCK_GROUP */
struct inode *ext2_get_inode(struct block *b, uint32_t ino_idx){
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc_tabs = NULL;
	uint32_t block_size = 0;
	uint32_t inode_table = 0, inodes_per_group = 0, inodes_per_block = 0;
	uint32_t block_group = 0, block_idx = 0, block_offset = 0;
	struct inode *inode_tab = NULL, *inode = NULL;

	//get meta data
	ASSERT(b != NULL);
	meta = ext2_get_meta(b);
	ASSERT(meta != NULL);
	bg_desc_tabs = meta->bg_desc_tabs;
	ASSERT(bg_desc_tabs != NULL);
	ASSERT(block_group < DIV_ROUND_UP(meta->sb->s_blocks_count,
		meta->sb->s_blocks_per_group));
	inodes_per_group = meta->sb->s_inodes_per_group;
	block_size = ext2_get_block_size(meta->sb);

	// get block group
	ino_idx = ino_idx - 1; // inode index starts from 1 !!!
	block_group = ino_idx / inodes_per_group;
	bg_desc_tabs = &bg_desc_tabs[block_group];

	// get inode table
	inode_table = bg_desc_tabs->bg_inode_table;
	inodes_per_block = block_size/sizeof(struct inode);
	ASSERT((block_size % sizeof(struct inode)) == 0);

	// calculate inode index in local table
	ino_idx -= block_group * inodes_per_group;
	
	// get block location of inode
	block_idx = inode_table + ino_idx/inodes_per_block;
	block_offset = ino_idx % inodes_per_block;

	// read block data
	inode_tab = ext2_read_block(b,block_idx,block_size,NULL);
	inode = kmalloc(sizeof(struct inode));
	memcpy(inode, &inode_tab[block_offset], sizeof(struct inode));
	kfree(inode_tab);

	return inode;
}

/* Write ENTRY item of the inode table from BLOCK_GROUP */
void ext2_write_inode(struct block *b, uint32_t ino_idx, struct inode *inode){
	struct ext2_meta_data *meta = NULL;
	struct bg_desc_table *bg_desc_tabs = NULL;
	uint32_t block_size = 0;
	uint32_t inode_table = 0, inodes_per_group = 0, inodes_per_block = 0;
	uint32_t block_group = 0, block_idx = 0, block_offset = 0;
	struct inode *inode_tab = NULL;

	//get meta data
	ASSERT(b != NULL);
	meta = ext2_get_meta(b);
	ASSERT(meta != NULL);
	bg_desc_tabs = meta->bg_desc_tabs;
	ASSERT(bg_desc_tabs != NULL);
	ASSERT(block_group < DIV_ROUND_UP(meta->sb->s_blocks_count,
		meta->sb->s_blocks_per_group));
	inodes_per_group = meta->sb->s_inodes_per_group;
	block_size = ext2_get_block_size(meta->sb);

	// get block group
	ino_idx = ino_idx - 1; // inode index starts from 1 !!!
	block_group = ino_idx / inodes_per_group;
	bg_desc_tabs = &bg_desc_tabs[block_group];

	// get inode table
	inode_table = bg_desc_tabs->bg_inode_table;
	inodes_per_block = block_size/sizeof(struct inode);
	ASSERT((block_size % sizeof(struct inode)) == 0);

	// calculate inode index in local table
	ino_idx -= block_group * inodes_per_group;
	
	// get block location of inode
	block_idx = inode_table + ino_idx/inodes_per_block;
	block_offset = ino_idx % inodes_per_block;

	// read block data
	inode_tab = ext2_read_block(b,block_idx,block_size,NULL);
	// modify corresponding entry
	memcpy(&inode_tab[block_offset], inode, sizeof(struct inode));
	// write to disk
	ext2_write_block(b,block_idx,block_size,inode_tab);

	// release memory
	kfree(inode_tab);
}
/* inode read from given position */
off_t inode_read_at(struct block *d, struct inode *inode, void *buffer_, off_t size, off_t offset){
	struct ext2_meta_data *meta;
	uint32_t block_size,block_id,block_idx,block_ofs;
	uint8_t *buffer = buffer_, *bounce = NULL;
	off_t bytes_read = 0;

	ASSERT(d != NULL && inode != NULL);

	// get device meta data
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// read from disk
	while(size > 0){
		// get data block id
		block_idx = offset / block_size; // the nth data block
		block_ofs = offset % block_size; // byte offset with data block
		block_id = inode_get_data_block(d,inode,block_idx); // data block id in fs
		ASSERT(block_id != UINT32_MAX);

		// Calculate bytes to copy
		// off_t is signed.
		off_t inode_left = inode->i_size - offset;
		off_t block_left = block_size - block_ofs;
		off_t min_left = inode_left < block_left ? inode_left : block_left;
		off_t chunk_size = size < min_left ? size : min_left;

		// no bytes to be read
		if(chunk_size <= 0) break;

		// whole block data
		if(block_ofs == 0 && chunk_size == block_size)
			ext2_read_block(d,block_id,block_size,buffer+bytes_read);
		else{
			if(bounce == NULL){
				bounce = kmalloc(block_size);
				if(bounce == NULL) break;
			}
			ext2_read_block(d,block_id,block_size,bounce);
			memcpy(buffer+bytes_read,bounce+block_ofs,chunk_size);
		}

		// advance.
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}

	if(bounce != NULL) kfree(bounce);
	return bytes_read;
}

/* inode read from given position */
off_t inode_write_at(struct block *d, struct inode *inode, const void *buffer_, off_t size, off_t offset){
	struct ext2_meta_data *meta;
	uint32_t block_size,block_id,block_idx,block_ofs;
	const uint8_t *buffer = buffer_;
	uint8_t *bounce = NULL;
	off_t bytes_written = 0, err;

	ASSERT(d != NULL && inode != NULL);

	// Resize inode
	err = inode_resize(inode,offset + size);
	if(err < 0) {
		printf("inode_write_at: resize failed.\n");
		return 0;
	}

	// get device meta data
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// read from disk
	while(size > 0){
		// get data block id
		block_idx = offset / block_size; // the nth data block
		block_ofs = offset % block_size; // byte offset with data block
		block_id = inode_get_data_block(d,inode,block_idx); // data block id in fs
		ASSERT(block_id != UINT32_MAX);

		// Calculate bytes to write
		// off_t is signed.
		off_t inode_left = inode->i_size - offset;
		off_t block_left = block_size - block_ofs;
		off_t min_left = inode_left < block_left ? inode_left : block_left;
		off_t chunk_size = size < min_left ? size : min_left;

		// no bytes to be read
		if(chunk_size <= 0) break;

		// whole block data
		if(block_ofs == 0 && chunk_size == block_size)
			ext2_write_block(d,block_id,block_size,buffer+bytes_written);
		else{
			if(bounce == NULL){
				bounce = kmalloc(block_size);
				if(bounce == NULL) break;
			}
			// First read the block from disk
			ext2_read_block(d,block_id,block_size,bounce);
			// Modify data read
			memcpy(bounce+block_ofs,buffer+bytes_written,chunk_size);
			// Write to disk
			ext2_write_block(d,block_id,block_size,bounce);
		}

		// advance.
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}

	if(bounce != NULL) kfree(bounce);
	return bytes_written;
}

/* Get N th data block of inode */
void *inode_get_block_data(struct block *d, struct inode *inode, uint32_t block_idx){
	struct ext2_meta_data *meta;
	uint32_t block_size,block_id;
	void *block_data;

	ASSERT(d != NULL && inode != NULL);

	// get data block id
	block_id = inode_get_data_block(d,inode,block_idx);
	ASSERT(block_id != UINT32_MAX);

	// get device meta data
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);

	// read from disk
	block_data = ext2_read_block(d,block_id,block_size,NULL);

	return block_data;
}

/* Get the actual data block id from idx */
static uint32_t inode_get_data_block (struct block *d, struct inode *inode, uint32_t idx){
	uint32_t block_size, items_per_block, ids_per_level;
	struct ext2_meta_data *meta;

	ASSERT(d != NULL && inode != NULL);

	// get device meta data
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);
	items_per_block = block_size / sizeof(uint32_t);

	// if block is within first 12 blocks
	ids_per_level = DIRECT_BLOCKS;
	if(idx < ids_per_level) return inode->i_block[idx];
	
	// check idx against block link lists
	idx -= ids_per_level;
	ids_per_level = items_per_block; //first level link list
	if(idx < ids_per_level) return inode_traverse_linklist(d,inode->i_block[12],idx,0);

	idx -= ids_per_level;
	ids_per_level *= items_per_block; //second level link list
	if(idx < ids_per_level) return inode_traverse_linklist(d,inode->i_block[13],idx,1);

	idx -= ids_per_level;
	ids_per_level *= items_per_block; //third level link list
	if(idx < ids_per_level) return inode_traverse_linklist(d,inode->i_block[14],idx,2);

	PANIC("Block ID [%u] Excceded EXT2 Limit.",idx);
}

/* Get IDX th entry from block specified by BLOCK ID recursively until LEVEL == 0*/
static uint32_t inode_traverse_linklist(struct block *d, uint32_t block_id, uint32_t idx, uint32_t level){
	int i;
	uint32_t block_size, items_per_block, ids_per_entry, table_idx;
	struct ext2_meta_data *meta;
	uint32_t *array;

	ASSERT(d != NULL);

	// get device meta data
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);
	items_per_block = block_size / sizeof(uint32_t);

	// calculate number of ids per entry
	// and local table index
	ids_per_entry = 1;
	for(i=0;i<level;i++) ids_per_entry *= items_per_block;
	table_idx = idx / ids_per_entry;

	// get local table entry
	array = ext2_read_block(d,block_id,block_size,NULL);
	block_id = array[table_idx];
	kfree(array);

	// if not reach leaf level
	if(level > 0){
		idx = idx - table_idx * ids_per_entry;
		level = level - 1;
		block_id = inode_traverse_linklist(d,block_id,idx,level);
	}

	return block_id;
}

/* Resize the data block of an inode, to hold at least BYTES bytes*/
int inode_resize(struct inode *inode, uint32_t bytes){
	struct block *d;
	struct ext2_meta_data *meta;
	uint32_t block_size, items_per_block, fs_blocks, old_fs_blocks,block_id;
	int indirect_blocks;
	uint32_t start_idx, end_idx;
	enum RANGE range_comparison;
	int i;

	ASSERT(inode != NULL);

	// get meta data
	d = block_get_role(BLOCK_FILESYS);
	ASSERT(d != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);
	block_size = ext2_get_block_size(meta->sb);
	items_per_block = block_size/sizeof(uint32_t);

	// Calculate number of direct fs block to hold bytes
	fs_blocks = DIV_ROUND_UP(bytes,block_size);
	old_fs_blocks = DIV_ROUND_UP(inode->i_size,block_size);

	/* Calculate indirect blocks
	 * Important: the inode->i_blocks field is the number of DISK SECTORS (512bytes)
	 * to hold all data of inode, includes direct blocks and INDIRECT BLOCKS!!
	*/
	indirect_blocks = inode_get_indirect_blocks(fs_blocks,items_per_block);
	if(indirect_blocks < 0) return -1;

	// If blocks does not expand or shrink
	if(fs_blocks == old_fs_blocks) goto done;

	// If data shrinks
	if(fs_blocks < old_fs_blocks) goto shrink;

	// Expands if blocks > inode->i_blocks
	ASSERT(fs_blocks > old_fs_blocks);

	/* Note: Expansion range is from i_blocks to blocks-1 (old_fs_blocks to fs_blocks-1) inclusive */

	// Check direct blocks
	for (i = old_fs_blocks; i < DIRECT_BLOCKS && i < fs_blocks; i++){
		block_id = inode->i_block[i];
		if(block_id == 0){
			block_id = freemap_get_block(false);
			if(block_id != FREEMAP_GET_ERROR) inode->i_block[i] = block_id;
			else {return -1;}
		}
	}

	// Check singly indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(old_fs_blocks,fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS];
		if(block_id == 0){
			block_id = freemap_get_block(true);
			if(block_id != FREEMAP_GET_ERROR) inode->i_block[DIRECT_BLOCKS] = block_id;
			else {return -1;}
		}
		inode_expand_range(block_id,1,old_fs_blocks,fs_blocks-1,items_per_block,DIRECT_BLOCKS,0,0,0);
	}
	// Level range passed expansion range
	else if ((range_comparison & RANGE_AHEAD) > 0)
		goto done;

	// Check doubly indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+1,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+1,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(old_fs_blocks,fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS+1];
		if(block_id == 0){
			block_id = freemap_get_block(true);
			if(block_id != FREEMAP_GET_ERROR) inode->i_block[DIRECT_BLOCKS+1] = block_id;
			else {return -1;}
		}
		inode_expand_range(block_id,1,old_fs_blocks,fs_blocks-1,items_per_block,DIRECT_BLOCKS+1,0,0,0);
	}
	// Level range passed expansion range
	else if ((range_comparison & RANGE_AHEAD) > 0)
		goto done;

	// Check triply indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+2,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+2,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(old_fs_blocks,fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS+2];
		if(block_id == 0){
			block_id = freemap_get_block(true);
			if(block_id != FREEMAP_GET_ERROR) inode->i_block[DIRECT_BLOCKS+2] = block_id;
			else {return -1;}
		}
		inode_expand_range(block_id,1,old_fs_blocks,fs_blocks-1,items_per_block,DIRECT_BLOCKS+2,0,0,0);
	}

	// Finished. Go to done
	goto done;

shrink: 
	//shrinks if blocks < inode->i_blocks
	ASSERT(fs_blocks < old_fs_blocks);
 
	/* Note: Shrink range is from blocks to i_blocks-1 (fs_blocks to old_fs_blocks-1) inclusive */

	// Check direct blocks
	for (i = fs_blocks; i < DIRECT_BLOCKS && i < old_fs_blocks; i++){
		block_id = inode->i_block[i];
		if(block_id != 0){
			// free block
			freemap_free_block(block_id);
			// set entry to zero
			inode->i_block[i] = 0;
		}
	}

	// Check singly indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(fs_blocks,old_fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS];
		if(block_id != 0){
			// Shrink sub range first
			inode_shrink_range(block_id,1,fs_blocks,old_fs_blocks-1,
				items_per_block,DIRECT_BLOCKS,0,0,0);
			// If shrink range contains all singly indirect blocks
			if(fs_blocks <= start_idx){
				// free block and delete entry
				freemap_free_block(block_id);
				inode->i_block[DIRECT_BLOCKS] = 0;
			}
		}
	}
	// if level range passed shrink range
	else if ((range_comparison & RANGE_AHEAD) > 0)
		goto done;

	// Check doubly indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+1,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+1,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(fs_blocks,old_fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS+1];
		if(block_id != 0){
			inode_shrink_range(block_id,1,fs_blocks,old_fs_blocks-1,
				items_per_block,DIRECT_BLOCKS+1,0,0,0);
			// If shrink range contains all doubly indirect blocks
			if(fs_blocks <= start_idx){
				// free block and delete entry
				freemap_free_block(block_id);
				inode->i_block[DIRECT_BLOCKS+1] = 0;
			}
		}
	}
	// if level range passed shrink range
	else if ((range_comparison & RANGE_AHEAD) > 0)
		goto done;

	// Check triply indirect blocks
	start_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+2,0,0,0);
	end_idx = inode_get_direct_block_idx(items_per_block,DIRECT_BLOCKS+2,
		items_per_block-1,items_per_block-1,items_per_block-1);
	range_comparison = inode_range_compare(fs_blocks,old_fs_blocks-1,start_idx,end_idx);
	if((range_comparison & RANGE_OVERLAP) > 0){
		block_id = inode->i_block[DIRECT_BLOCKS+2];
		if(block_id != 0){
			inode_shrink_range(block_id,1,fs_blocks,old_fs_blocks-1,
				items_per_block,DIRECT_BLOCKS+2,0,0,0);
			// If shrink range contains all triply indirect blocks
			if(fs_blocks <= start_idx){
				// free block and delete entry
				freemap_free_block(block_id);
				inode->i_block[DIRECT_BLOCKS+2] = 0;
			}
		}
	}

done:
	// Set new size
	inode->i_size = bytes;
	// Important: i_blocks are number of 512 byte sectors, not fs blocks !!
	// Note: do this after the expansion or truncation.
	inode->i_blocks = (fs_blocks+indirect_blocks)*(2<<meta->sb->s_log_block_size);

	return 0;
}
static int inode_expand_range(uint32_t block_id, uint32_t level, uint32_t start, uint32_t end, uint32_t items_per_block,uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3){
	struct block *d = block_get_role(BLOCK_FILESYS);
	uint32_t item_start, item_end;
	uint32_t *level_data;
	uint32_t block_id2;
	int i, ret = 0;
	enum RANGE range_comparison;

	ASSERT(d != NULL);
	ASSERT(level > 0);

	level_data = ext2_read_block(d,block_id,items_per_block*sizeof(uint32_t),NULL);
	for(i = 0; i < items_per_block; i++){
		// Get Block range the item represents
		if(level == 1){
			item_start = inode_get_direct_block_idx(items_per_block,l0,i,0,0);
			item_end = inode_get_direct_block_idx(items_per_block,l0,i,items_per_block-1,items_per_block-1);
		}
		else if (level == 2){
			item_start = inode_get_direct_block_idx(items_per_block,l0,l1,i,0);
			item_end = inode_get_direct_block_idx(items_per_block,l0,l1,i,items_per_block-1);
		}
		else {
			item_start = inode_get_direct_block_idx(items_per_block,l0,l1,l2,i);
			item_end = item_start;
		}
		// Compare range
		range_comparison = inode_range_compare(item_start,item_end,start,end);
		if((range_comparison & RANGE_OVERLAP) > 0){ // In Range
			block_id2 = level_data[i];
			// Allocate block in disk if not exist.
			if(block_id2 == 0){
				if(item_start == item_end) block_id2 = freemap_get_block(false);
				else block_id2 = freemap_get_block(true);	
#ifdef FILESYS_EXT2_DEBUG
				printf(" New block id: 0x%x for %d/%d/%d/%d:%d\n",
					block_id2,l0,l1,l2,l3,i);
#endif
			}

			if(block_id2 != FREEMAP_GET_ERROR) {level_data[i] = block_id2;}
			else {ret = -1; break;}

			/* If it is a leaf node */
			if(item_start == item_end) continue;
			/* If not leaf node*/
			if(level == 1)
				inode_expand_range(block_id2,level+1,start,end,items_per_block,l0,i,0,0);
			else if(level == 2)
				inode_expand_range(block_id2,level+1,start,end,items_per_block,l0,l1,i,0);
			else
				PANIC("Inode Fill Range Reach Unexpected Level.\n");
		}
		else if((range_comparison & RANGE_AHEAD) > 0){ //Ahead of start
			continue;
		}
		else{ // Passed End
			break;
		}
	}
	ext2_write_block(d,block_id,items_per_block*sizeof(uint32_t),level_data);
	kfree(level_data);

	return ret;
}
static int inode_shrink_range(uint32_t block_id, uint32_t level, uint32_t start, uint32_t end, uint32_t items_per_block,uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3){
	struct block *d = block_get_role(BLOCK_FILESYS);
	uint32_t item_start, item_end;
	uint32_t *level_data;
	uint32_t block_id2;
	int i, ret = 0;
	enum RANGE range_comparison;

	ASSERT(d != NULL);
	ASSERT(level > 0);

	level_data = ext2_read_block(d,block_id,items_per_block*sizeof(uint32_t),NULL);
	for(i = 0; i < items_per_block; i++){
		// Get Block range the item represents
		if(level == 1){
			item_start = inode_get_direct_block_idx(items_per_block,l0,i,0,0);
			item_end = inode_get_direct_block_idx(items_per_block,l0,i,items_per_block-1,items_per_block-1);
		}
		else if (level == 2){
			item_start = inode_get_direct_block_idx(items_per_block,l0,l1,i,0);
			item_end = inode_get_direct_block_idx(items_per_block,l0,l1,i,items_per_block-1);
		}
		else {
			item_start = inode_get_direct_block_idx(items_per_block,l0,l1,l2,i);
			item_end = item_start;
		}
		// Compare range
		range_comparison = inode_range_compare(item_start,item_end,start,end);
		if((range_comparison & RANGE_OVERLAP) > 0){ // In Range
			block_id2 = level_data[i];

			// If the block is cleared already.
			if(block_id2 == 0) continue;

			/* If it is a leaf node */
			if(item_start == item_end) {
				// free block and set entry to zero
				freemap_free_block(block_id2);
				level_data[i] = 0;
				continue;
			}

			/* If not a leaf node*/

			// free sub level
			if(level == 1)
				inode_shrink_range(block_id2,level+1,start,end,items_per_block,l0,i,0,0);
			else if(level == 2)
				inode_shrink_range(block_id2,level+1,start,end,items_per_block,l0,l1,i,0);
			else
				PANIC("Inode Shrink Range Reach Unexpected Level.\n");

			// if shrink range contains entire level
			if(start <= item_start){
				// free block and set entry to zero
				freemap_free_block(block_id2);
				level_data[i] = 0;
			}
		}
		else if((range_comparison & RANGE_AHEAD) > 0){ //Item Ahead of start
			continue;
		}
		else{ // Item Passed End
			break;
		}
	}
	ext2_write_block(d,block_id,items_per_block*sizeof(uint32_t),level_data);
	kfree(level_data);

	return ret;
}
/* Check if two ranges overlap, start and end are inclusive*/
static enum RANGE inode_range_compare(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2){
	if(end1 < start2) return RANGE_AHEAD; // range 1 ahead of range 2
	else if (end2 < start1) return RANGE_BEHIND; // range 1 behind range 2

	// Overlap
	enum RANGE range = RANGE_OVERLAP;
	// range 1 contains entire range 2
	if(start1 <= start2 && end1 >= end2) range = range | RANGE_CONTAINS;
	// range 2 contains entire range 1
	if(start2 <= start1 && end2 >= end1) range = range | RANGE_CONTAINED;

	return range;
}
/* Returns the linear direct block index from indirect index */
static uint32_t inode_get_direct_block_idx(uint32_t items_per_block, uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3){
	// direct blocks
	if(l0 < DIRECT_BLOCKS) return l0;
	// single indirect blocks
	if(l0 == DIRECT_BLOCKS) 
		return DIRECT_BLOCKS + l1;
	// doubly indirect blocks
	if(l0 == DIRECT_BLOCKS+1)
		return DIRECT_BLOCKS + items_per_block + l1 * items_per_block + l2;
	// triply indirect blocks
	if(l0 == DIRECT_BLOCKS+2)
		return DIRECT_BLOCKS + items_per_block + items_per_block*items_per_block
			+ l1*items_per_block*items_per_block + l2*items_per_block + l3;

	// Error
	return UINT32_MAX;
}
/* Returns the number of indirect blocks required for DIRECT_BLOCKS */
static int inode_get_indirect_blocks(uint32_t direct_blocks, uint32_t items_per_block){
	int blocks = direct_blocks;
	int indirect_blocks = 0;

	ASSERT(items_per_block > 0);
	ASSERT(direct_blocks <= DIRECT_BLOCKS+items_per_block
		+items_per_block*items_per_block
		+items_per_block*items_per_block*items_per_block);

	// top level
	blocks -= DIRECT_BLOCKS;
	if(blocks < 1) return indirect_blocks;

	// singly indirect level
	indirect_blocks++;
	blocks -= items_per_block;
	if(blocks < 1) return indirect_blocks;

	// doubly indirect level
	indirect_blocks++;
	if(blocks <= items_per_block*items_per_block){
		indirect_blocks += DIV_ROUND_UP(blocks , items_per_block);
		return indirect_blocks;
	}
	else{
		indirect_blocks += items_per_block;
		blocks -= items_per_block*items_per_block;
	}
	
	// triply indirect level
	indirect_blocks++;
	indirect_blocks += DIV_ROUND_UP(blocks, items_per_block*items_per_block);
	indirect_blocks += DIV_ROUND_UP(blocks, items_per_block);

	return indirect_blocks;
}
