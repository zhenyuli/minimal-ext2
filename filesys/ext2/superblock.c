#include "filesys/ext2/superblock.h"
#include <stdio.h>
#include <debug.h>

void ext2_print_superblock(struct superblock *sb){
	ASSERT(sb != NULL);

	printf("Creator OS : 0x%x, Magic : 0x%x\n",sb->s_creator_os,sb->s_magic);
	printf("block size : %d\n",(1024 << sb->s_log_block_size));
	printf("inodes count : %d, blocks count : %d\n",sb->s_inodes_count,sb->s_blocks_count);
	printf("free inodes count : %d, free blocks count : %d\n",
		sb->s_free_inodes_count, sb->s_free_blocks_count);
	printf("first data block: %d, blocks per group: %d\n",
		sb->s_first_data_block, sb->s_blocks_per_group);
}

