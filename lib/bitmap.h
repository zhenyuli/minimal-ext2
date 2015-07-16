#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

/* Bitmap abstract data type. */

/* Creation and destruction. */
struct bitmap *bitmap_create (size_t bit_cnt);
struct bitmap *bitmap_create_in_buf (size_t bit_cnt, void *, size_t byte_cnt);
struct bitmap *bitmap_create_from_buf (void* block, size_t block_size);
size_t bitmap_buf_size (size_t bit_cnt);
void * bitmap_get_bits(struct bitmap *b);
void bitmap_destroy (struct bitmap *);

/* Bitmap size. */
size_t bitmap_size (const struct bitmap *);

/* Setting and testing single bits. */
void bitmap_set (struct bitmap *, size_t idx, bool);

/* Setting and testing multiple bits. */
void bitmap_set_multiple (struct bitmap *, size_t start, size_t cnt, bool);
bool bitmap_all (const struct bitmap *, size_t start, size_t cnt);

/* Finding set or unset bits.
 * Modified by Zhenyu Li on 08 July. Changed from SIZE_MAX TO UINT32_MAX
*/
#define BITMAP_ERROR UINT32_MAX
size_t bitmap_scan (const struct bitmap *, size_t start, size_t cnt, bool);
size_t bitmap_scan_and_flip (struct bitmap *, size_t start, size_t cnt, bool);


#endif /* lib/kernel/bitmap.h */
