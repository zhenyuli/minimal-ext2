#ifndef EXT2_INODE_H
#define EXT2_INODE_H

#include <stdint.h>
#include "devices/block.h"
#include "filesys/off_t.h"

// inode table structure
struct inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks; //important: this is sector count, not fs block count!
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t i_osd2[12];
} __attribute__((packed));

// Reserved Inodes in Inode Table
#define EXT2_BAD_INO			1
#define EXT2_ROOT_INO			2
#define EXT2_ACL_IDX_INO		3
#define EXT2_ACL_DATA_INO		4
#define EXT2_BOOT_LOADER_INO	5
#define EXT2_UNDEL_DIR_INO		6

// definitions for i_mode
#define EXT2_S_IFSOCK	0xc000		//socket
#define EXT2_S_IFLNK	0xa000		//symbolic link
#define EXT2_S_IFREG	0x8000		//regular file
#define EXT2_S_IFBLK	0x6000		//block device
#define EXT2_S_IFDIR	0x4000		//directory
#define EXT2_S_IFCHR	0x2000		//character device
#define EXT2_S_IFIFO	0x1000		//fifo
// process execution user/group override
#define EXT2_S_ISUID	0x0800		//set process user id
#define EXT2_S_ISGID	0x0400		//set process group id
#define EXT2_S_ISVTX	0x0200		//sticky bit
// access rights
#define EXT2_S_IRUSR	0x0100		//user read
#define EXT2_S_IWUSR	0x0080		//user write
#define EXT2_S_IXUSR	0x0040		//user execute
#define EXT2_S_IRGRP	0x0020		//group read
#define EXT2_S_IWGRP	0x0010		//group write
#define EXT2_S_IXGRP	0x0008		//group execute
#define EXT2_S_IROTH	0x0004		//others read
#define EXT2_S_IWOTH	0x0002		//others write
#define EXT2_S_IXOTH	0x0001		//others execute

// define default file permission
#define EXT2_DEFAULT_PERMISSION (EXT2_S_IRUSR|EXT2_S_IWUSR|EXT2_S_IRGRP|EXT2_S_IWGRP|EXT2_S_IROTH)
struct inode *ext2_get_inode(struct block *b, uint32_t ino_idx);
void ext2_write_inode(struct block *b, uint32_t ino_idx, struct inode *inode);
void *inode_get_block_data(struct block *d, struct inode *inode, uint32_t idx);
off_t inode_read_at(struct block *d, struct inode *inode, void *buffer_, off_t size, off_t offset);
off_t inode_write_at(struct block *d, struct inode *inode, const void *buffer_, off_t size, off_t offset);
int inode_resize(struct inode *inode, uint32_t bytes);
void print_inode(struct inode *ino);
#endif
