#ifndef EXT2_DIRECTORY_H
#define EXT2_DIRECTORY_

#include "filesys/ext2/inode.h"
#include <stddef.h>

struct directory {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
	uint8_t name[UINT8_MAX];
} __attribute__((packed));

// definitions for file_type
#define EXT2_FT_UNKNOWN		0	//Unknown File Type
#define EXT2_FT_REG_FILE	1	//Regular file
#define EXT2_FT_DIR			2	//Directory File
#define EXT2_FT_CHRDEV		3	//Character Device
#define EXT2_FT_BLKDEV		4	//Block Device
#define EXT2_FT_FIFO		5	//Buffer File
#define EXT2_FT_SOCK		6	//Socket File
#define EXT2_FT_SYMLINK		7	//Symbolic Link

struct directory *dir_get_next(struct directory *dir);
struct directory *dir_lookup(struct block *d, const char *path);
void print_directory(struct directory *dir);
#endif
