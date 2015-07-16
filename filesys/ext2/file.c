#include "filesys/file.h"
#include "filesys/ext2/inode.h"

#include "kernel/kmalloc.h"
#include "devices/block.h"
#include "kernel/synch.h"

#include <stddef.h>
#include <debug.h>

/* Opening and closing files. */
struct file *file_open (struct block *device,struct directory *dir,struct inode *inode){
	struct file * file = NULL;

	ASSERT(device != NULL && inode != NULL);

	file = kmalloc(sizeof(struct file));
	if(file != NULL){
		file->device = device;
		file->dir = dir;
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		lock_init(&file->lock);
	}
	
	return file;
}
struct file *file_reopen (struct file *file){
	return file_open(file->device,file->dir,file->inode);
}
void file_close (struct file *file){
	if(file != NULL){
		file_allow_write(file);
		// TODO should flush any changes.
		kfree(file->dir);
		kfree(file->inode);
		kfree(file);
	}
}
struct inode *file_get_inode (struct file *file){
	return file->inode;
}

/* Reading and writing. */
off_t file_read (struct file *file, void *buffer, off_t size){
	lock_acquire(&file->lock);
	off_t bytes_read = inode_read_at(file->device,file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	lock_release(&file->lock);
	return bytes_read;
}
off_t file_read_at (struct file *file, void *buffer, off_t size, off_t start){
	ASSERT(start >= 0);

	lock_acquire(&file->lock);
	off_t bytes_read = inode_read_at(file->device,file->inode, buffer, size, start);
	file->pos += bytes_read;
	lock_release(&file->lock);
	return bytes_read;
}
off_t file_write (struct file *file, const void *buffer, off_t size){
	lock_acquire(&file->lock);
	off_t bytes_written = inode_write_at(file->device,file->inode,buffer,size,file->pos);
	file->pos+= bytes_written;
	// Update inode in disk
	ext2_write_inode(file->device,file->dir->inode,file->inode);
	lock_release(&file->lock);
	return bytes_written;
}
off_t file_write_at (struct file *file, const void *buffer, off_t size, off_t start){
	ASSERT(start >= 0);

	lock_acquire(&file->lock);
	off_t bytes_written = inode_write_at(file->device,file->inode,buffer,size,start);
	file->pos+= bytes_written;
	// Update inode in disk
	ext2_write_inode(file->device,file->dir->inode,file->inode);
	lock_release(&file->lock);
	return bytes_written;
}

int file_truncate(struct file *file, off_t size){
	int err;
	lock_acquire(&file->lock);
	err = inode_resize(file->inode,size);
	// Update position
	if(err == 0 && file->pos >= size)
		file->pos = size-1;
	// Update inode
	ext2_write_inode(file->device,file->dir->inode,file->inode);
	lock_release(&file->lock);

	return err;
}

/* Preventing writes. */
void file_deny_write (struct file *file){
	//TODO
}
void file_allow_write (struct file *file){
	//TODO
}

/* File position. */
void file_seek (struct file *file, off_t new_pos){
	ASSERT(file != NULL);
	ASSERT(new_pos >= 0);
	lock_acquire(&file->lock);
	file->pos = new_pos;
	lock_release(&file->lock);
}
off_t file_tell (struct file *file){
	ASSERT(file != NULL);
	return file->pos;
}
off_t file_length (struct file *file){
	ASSERT(file != NULL && file->inode != NULL);
	return file->inode->i_size;
}


