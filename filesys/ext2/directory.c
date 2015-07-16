#include "filesys/ext2/directory.h"
#include "filesys/ext2/inode.h"
#include "filesys/ext2/ext2.h"
#include "devices/block.h"
#include "kernel/kmalloc.h"

#include <stdio.h>
#include <string.h>
#include <debug.h>

static struct directory *dir_get_root(struct block *d);
static struct directory *dir_lookup_current(struct directory *cur, const char *file_name);

struct directory *dir_lookup(struct block *d, const char *path){
	char *path_copy, *token, *save_ptr;
	struct directory *cur,*next,*last,*file_ptr;
	struct directory *found = NULL;

	ASSERT(path != NULL);

	// copy path
	path_copy = kmalloc(strlen(path)+1);
	memcpy(path_copy,path,strlen(path)+1);
	
	// start from root
	last = NULL;
	cur = dir_get_root(d);
	ASSERT(cur != NULL);

	// search path
	for(token = strtok_r(path_copy,"/",&save_ptr); token != NULL;
		token = strtok_r(NULL,"/",&save_ptr)){
		// check if current directory is the same as the last
		if(cur == last) {
			file_ptr = NULL;
			break;
		}

		// look up current directory
		file_ptr = dir_lookup_current(cur,token);
		last = cur; //save current directory

		// if file is directory
		if(file_ptr != NULL && file_ptr->inode != 0 && file_ptr->file_type == EXT2_FT_DIR){
			// get file inode, it is temporary
			struct inode *file_ino = ext2_get_inode(d,file_ptr->inode);
			if(file_ino == NULL || (file_ino->i_mode & EXT2_S_IFDIR) == 0){
				file_ptr = NULL;
				if(file_ino != NULL) kfree(file_ino);
				break;
			}

			// read directory file
			next = kmalloc(file_ino->i_size);
			if(next == NULL) {
				file_ptr = NULL;
				kfree(file_ino);
				break;
			}
			inode_read_at(d,file_ino,next,file_ino->i_size,0);		

			// free temporary memory and switch directory
			kfree(file_ino);
			kfree(cur);
			cur = next;
		}
	}

	// file found
	if(file_ptr != NULL && file_ptr->inode != 0){
		found = kmalloc(sizeof(struct directory));
		memcpy(found,file_ptr,sizeof(struct directory));
	}
	
	kfree(cur);
	kfree(path_copy);
	return found;
}

static struct directory *dir_lookup_current(struct directory *cur, const char *file_name){
	struct directory *next = cur;

	ASSERT(cur != NULL);
	
	while(next != NULL && next->inode != 0){
		if(memcmp (next->name,file_name,next->name_len) == 0) return next;
		next = dir_get_next(next);
	}
	return NULL;
}

static struct directory *dir_get_root(struct block *d){
	struct inode *root_ino;
	struct directory *root;

	ASSERT(d != NULL);

	// get root inode
	root_ino = ext2_get_inode(d,EXT2_ROOT_INO);

	// allocate memory
	root = kmalloc(root_ino->i_size);
	if(root == NULL) return NULL;

	// read entire file
	inode_read_at(d,root_ino,root,root_ino->i_size,0);

	return root;
}

struct directory *dir_get_next(struct directory *dir){
	struct directory *next;

	ASSERT(dir != NULL);
	
	next = (struct directory*)((uint8_t*)dir + dir->rec_len);

	return next;
}

/* Print directory structure */
void print_directory(struct directory *dir){
	ASSERT(dir != NULL);
	dir->name[dir->name_len] = '\0';
	printf("Inode: %u, record len: %d, name len: %d, type: 0x%x,name: %s\n",
		dir->inode, dir->rec_len, dir->name_len, dir->file_type, dir->name);
}
