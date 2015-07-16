#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
struct block *fs_device;

// FILE TYPE
enum FILE_TYPE{
	FILESYS_REGULAR,
	FILESYS_DIRECTORY,
};

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *path, off_t initial_size, enum FILE_TYPE type, uint32_t permission);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

#endif /* filesys/filesys.h */
