#include "filesys/ext2/block_group.h"
#include <stdio.h>
#include <debug.h>

void print_bg_desc_table(struct bg_desc_table *tab){
	ASSERT(tab != NULL);

	printf("inode_table : %d,",tab->bg_inode_table);
	printf("free blocks: %d, free inodes: %d, used directorys: %d\n",
		tab->bg_free_blocks_count, tab->bg_free_inodes_count,
		tab->bg_used_dirs_count);
}
