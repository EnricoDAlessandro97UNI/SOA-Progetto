#ifndef _COMMON_HEADER_H
#define _COMMON_HEADER_H

#define DEFAULT_BLOCK_SIZE 4096
#define METADATA_SIZE 4
#define DATA_SIZE (DEFAULT_BLOCK_SIZE - METADATA_SIZE)

#define NBLOCKS 6                                           // change here the number of the blocks (superblock and inode are included)
#define IMAGE_PATH "/home/enrico/Desktop/SOA-project/image" // change this line with your image file path

#define VALID_MASK 0x80000000       // 0x80000000 -> 1000 0000 ... 0000
#define INVALID_MASK (~VALID_MASK)  // 0x7FFFFFFF -> 0111 1111 ... 1111
#define set_valid(n) ((unsigned int)(n) | VALID_MASK)
#define set_invalid(n) ((unsigned int)(n) & INVALID_MASK)
#define get_validity(n) ((unsigned int)(n) >> 31)
#define get_block_num(n) ((unsigned int)(n) & INVALID_MASK)
#define blk_offset(i) (i+2)

#endif