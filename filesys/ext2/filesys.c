#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/block.h"
#include "filesys/ext2/ext2.h"
#include "filesys/ext2/inode.h"
#include "filesys/ext2/free-map.h"
#include "kernel/kmalloc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>

struct block *fs_device;

static void filesys_split_path(const char *path, char **parent, char **name);

void filesys_init (bool format){
	fs_device = block_get_role(BLOCK_FILESYS);
	if(fs_device == NULL)
		PANIC("No file system device found, can't initialize file system.");

	if(is_ext2(fs_device) == false)
		PANIC("Device file system not recognised!");
	else {
		ext2_init();
		ext2_register(fs_device);
		printf("File system type: ext2.\n");
	}
}

struct file *filesys_open (const char *name){
	struct block *block = NULL;
	struct directory *file_desc = NULL;
	struct inode *file_ino = NULL;
	struct file *file = NULL;

	ASSERT(name != NULL);

	// look up device
	block = block_get_role (BLOCK_FILESYS);

	// look up file
	if(block != NULL)
		file_desc = dir_lookup(block,name);

	// look up inode
	if(file_desc != NULL)
		file_ino = ext2_get_inode(block,file_desc->inode);

	// open file
	if(file_ino != NULL)
		file = file_open(block,file_desc,file_ino);

	return file;
}

void filesys_done (void){
	// TODO 
	// Flush any caches

	// free memory
	ext2_free();
}

bool filesys_create (const char *path, off_t initial_size, enum FILE_TYPE type, uint32_t permission){
	struct block *d = NULL;
	struct ext2_meta_data *meta = NULL;
	char *parent = NULL, *name = NULL;
	struct directory *parent_dir = NULL;
	struct file *parent_file = NULL;
	struct directory *directory_data = NULL;
	struct directory *last_entry = NULL;
	uint32_t file_size = 0, bytes_read = 0;
	uint32_t inode_num;
	struct inode inode;
	int err, i;
	bool success = false;

	ASSERT(path != NULL && initial_size >= 0);

	// Get device
	d = block_get_role (BLOCK_FILESYS);
	ASSERT(d != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);

	// Check if file already exists!
	directory_data = dir_lookup(d,path);
	if(directory_data != NULL){ // file exists
		printf("File %s exists!.\n",path);
		goto cleanup;
	}

	// Split path
	filesys_split_path(path, &parent, &name);
	ASSERT(parent != NULL && name != NULL);

	// Get parent directory
	parent_dir = dir_lookup(d,parent);
	if(parent_dir == NULL){
		printf("Directory %s does not exist.\n",parent);
		goto cleanup;
	}
	// Check file type
	if(parent_dir -> file_type != EXT2_FT_DIR){
		printf("Path %s is not a directory.\n",parent);
		goto cleanup;
	}

	// Get directory file
	parent_file = filesys_open(parent);
	if(parent_file == NULL) goto cleanup;

	// Get parent directory data
	file_size = parent_file->inode->i_size;
	directory_data = kmalloc(file_size);
	bytes_read = file_read(parent_file,directory_data,file_size);
	if(bytes_read != file_size) goto cleanup;
	
	// Get last entry
	last_entry = directory_data;
	do {
		last_entry = dir_get_next(last_entry);
	} while (last_entry != NULL 
		&& last_entry->inode != 0 
		&& (uint8_t*)last_entry+last_entry->rec_len < (uint8_t*)directory_data+file_size);

	// check if pointer passed file limit
	if(last_entry == NULL || (uint8_t*)last_entry >= (uint8_t*)directory_data+file_size)
		goto cleanup;

	// Get free inode
	inode_num = freemap_get_inode();
	if(inode_num == FREEMAP_GET_ERROR) goto cleanup;

	// Recalibrate record length
	if(last_entry->inode != 0){
		last_entry->rec_len = sizeof(struct directory) - (UINT8_MAX - last_entry->name_len);
		// Align 4 bytes
		i = last_entry->rec_len % 4;
		if(i > 0) last_entry->rec_len += (4-i);
	}

	// Goto new last entry
	last_entry = dir_get_next(last_entry);
	if(last_entry == NULL || (uint8_t*)last_entry >= (uint8_t*)directory_data+file_size)
		goto cleanup;
	
	// Create inode
	memset(&inode,0,sizeof(struct inode));
	inode.i_mode = EXT2_S_IFREG | permission;
	inode.i_links_count = 1;
	err = inode_resize(&inode,initial_size);
	if(err < 0) goto cleanup;
	// Write inode to disk
	ext2_write_inode(d,inode_num,&inode);
	
	// Create file entry
	memset(last_entry,0,sizeof(struct directory));
	last_entry->inode = inode_num;
	last_entry->name_len = strlen(name);
	if(last_entry->name_len > UINT8_MAX) last_entry->name_len = UINT8_MAX;
	memcpy(last_entry->name,name, last_entry->name_len);
	switch(type){
		case FILESYS_REGULAR: 
			last_entry -> file_type = EXT2_FT_REG_FILE;
			break;
		case FILESYS_DIRECTORY: 
			last_entry -> file_type = EXT2_FT_DIR;
			break;
		default:
			last_entry -> file_type = EXT2_FT_UNKNOWN;
			break;
	};
	last_entry->rec_len = ext2_get_block_size(meta->sb) - 
		(uint32_t)((uintptr_t)last_entry - (uintptr_t) directory_data);

	// Write directory file
	file_write_at(parent_file,directory_data,file_size,0);
	
	// file created successfully.
	success = true;

cleanup:
	// close file
	if(parent_file != NULL) file_close(parent_file);
	// release memory
	if(parent != NULL) kfree(parent);
	if(name != NULL) kfree(name);
	if(parent_dir != NULL) kfree(parent_dir);
	if(directory_data != NULL) kfree(directory_data);
	
	return success;
}

bool filesys_remove (const char *path){
	struct block *d = NULL;
	struct ext2_meta_data *meta = NULL;
	char *parent = NULL, *name = NULL;
	struct directory *parent_dir = NULL;
	struct file *parent_file = NULL, *file = NULL;
	struct directory *directory_data = NULL;
	struct directory *file_entry = NULL, *last_entry = NULL;
	uint32_t file_size = 0, bytes_read = 0;
	bool success = false;

	ASSERT(path != NULL && strlen(path) > 0);

	// Get device
	d = block_get_role (BLOCK_FILESYS);
	ASSERT(d != NULL);
	meta = ext2_get_meta(d);
	ASSERT(meta != NULL && meta->sb != NULL);

	// open file
	file = filesys_open(path);
	if(file == NULL){ // file does not exists
		printf("filesys_remove: %s does not exist!.\n",path);
		goto cleanup;
	}
	
	//Check file type
	if(file->dir->file_type != EXT2_FT_REG_FILE){
		printf("filesys_remove %s is not a regular file.\n",path);
		goto cleanup;
	}

	// Split path
	filesys_split_path(path, &parent, &name);
	ASSERT(parent != NULL && name != NULL);

	// Get parent directory
	parent_dir = dir_lookup(d,parent);
	if(parent_dir == NULL){
		printf("Directory %s does not exist.\n",parent);
		goto cleanup;
	}
	// Check parent type
	if(parent_dir -> file_type != EXT2_FT_DIR){
		printf("Path %s is not a directory.\n",parent);
		goto cleanup;
	}

	// Get directory file
	parent_file = filesys_open(parent);
	if(parent_file == NULL) goto cleanup;

	// Get parent directory data
	file_size = parent_file->inode->i_size;
	directory_data = kmalloc(file_size);
	bytes_read = file_read(parent_file,directory_data,file_size);
	if(bytes_read != file_size) goto cleanup;
	
	// Get file directory entry
	file_entry = directory_data;
	while (file_entry != NULL 
		&& memcmp(file_entry->name,name,file_entry->name_len) != 0
		&& (uint8_t*)file_entry+file_entry->rec_len < (uint8_t*)directory_data+file_size){
		last_entry = file_entry;
		file_entry = dir_get_next(file_entry);
	}

	// check if file entry if found.
	if( (uint8_t*)file_entry >= (uint8_t*)directory_data+file_size
		|| memcmp(file_entry->name,name,file_entry->name_len) != 0 )
		goto cleanup;

	// Truncate file to zero length
	// Aka. Free blocks
	file_truncate(file,0);

	// Zero inode
	memset(file->inode,0,sizeof(struct inode));
	ext2_write_inode(d,file->dir->inode,file->inode);

	// Free inode
	freemap_free_inode(file_entry->inode);
	
	// Delete directory record
	// Use last entry record length to skip file entry
	if(last_entry->inode != 0)
		last_entry->rec_len += file_entry->rec_len;

	// Write directory file
	file_write_at(parent_file,directory_data,file_size,0);
	
	// file deleted successfully.
	success = true;

cleanup:
	// close file
	if(file != NULL) file_close(file);
	if(parent_file != NULL) file_close(parent_file);
	// release memory
	if(parent != NULL) kfree(parent);
	if(name != NULL) kfree(name);
	if(parent_dir != NULL) kfree(parent_dir);
	if(directory_data != NULL) kfree(directory_data);
	
	return success;
}

/* Gets the path of parent */
static void filesys_split_path(const char *path, char **parent, char **name){
	char *parent_path = NULL, *file_name = NULL;
	int len, i, j, c, next;

	ASSERT(path != NULL && parent != NULL && name != NULL);

	// Get path length
	len = strlen(path);
	ASSERT(len > 0);

	// Make a copy of the path
	parent_path = kmalloc(len+1);
	if(parent_path == NULL) return;
	file_name = kmalloc(len+1);
	if(file_name == NULL){
		kfree(parent_path);
		return;
	}
	memcpy(parent_path,path,len+1);
	memset(file_name,0,len+1);

	// Remove file name
	for(i = len-1; i >=0 ; i--){
		c = parent_path[i];
		if(c == '/'){
				if(i > 0) parent_path[i] = '\0';
				break; //break loop
		}
		else if (c == '.'){
			if(i > 0){
				// get next character
				next = parent_path[i-1];
				// Check if this is part of file name
				if(next != '/' || next != '.'){
					parent_path[i] = '\0';
					continue;
				}
			}
			// Not part of file name, break loop
			break;
		}
		else parent_path[i] = '\0';
	}

	// Save file name
	// Note: variable i contains the index of file name
	j = 0;
	for( i= i+1; i < len; i++)
		file_name[j++] = path[i];

	// Check path length
	if(strlen(parent_path) == 0){
		// set to current path
		parent_path[0] = '.';
		parent_path[1] = '\0';
	}

	// Return value
	*parent = parent_path;
	*name = file_name;
}
