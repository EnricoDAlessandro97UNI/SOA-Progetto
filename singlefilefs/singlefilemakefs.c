#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "singlefilefs.h"
#include "../common_header.h"

/*
	This makefs will write the following information onto the disk
	- BLOCK 0, superblock
	- BLOCK 1, inode of the unique file (the inode for root is volatile)
	- BLOCK 2, ..., BLOCK N, metadata + data
*/

int main(int argc, char *argv[]) {

    int i, fd, nbytes, nblocks;
    ssize_t ret;
    unsigned int metadata, next;
    struct onefilefs_sb_info sb;
    struct onefilefs_inode root_inode;
    struct onefilefs_inode file_inode;
    struct onefilefs_dir_record record;
    char *block_padding;
    char file_body[] = "SOA-PROJECT: block-level data management service";
    char end_str = '\0';

    if (argc != 3) {
        printf("Usage: ./singlefilemakefs <device> <num_blocks>\n");
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("Error opening the device\n");
        return -1;
    }

    nblocks = strtol(argv[2], NULL, 10);

    // pack the superblock
    sb.version = 1;
    sb.magic = MAGIC;
    sb.block_size = DEFAULT_BLOCK_SIZE;
    sb.first_valid = (unsigned int) -1;
    sb.last_valid = (unsigned int) -1;

    ret = write(fd, (char *)&sb, sizeof(sb));
	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("Bytes written [%d] are not equal to the default block size\n", (int)ret);
		close(fd);
		return ret;
	}
    printf("\nSuper block written successfully\n\n");

    // write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = SINGLEFILEFS_FILE_INODE_NUMBER;
	file_inode.file_size = nblocks*DEFAULT_BLOCK_SIZE;
	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);

    ret = write(fd, (char *)&file_inode, sizeof(file_inode));
	if (ret != sizeof(root_inode)) {
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}
    printf("File inode written successfully\n");

    // padding for block 1
	nbytes = DEFAULT_BLOCK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);
    memset(block_padding, 0, nbytes);
	ret = write(fd, block_padding, nbytes);
	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		close(fd);
		return -1;
	}
	printf("Padding in the inode block written successfully\n\n");

    // write file datablocks
    for (i = 0; i < nblocks - 2; i++) {
        // metadata
        if (i+1 != nblocks) 
            next = i+3; // punta al successivo escludendo il superblocco e l'inode (ad esempio, 3 punta a 4)
        else
            next = 0;
        metadata = set_invalid((unsigned int)-1);

        ret = write(fd, &metadata, METADATA_SIZE);
        if (ret != METADATA_SIZE) {
			printf("Writing file metadata has failed.\n");
			close(fd);
			return -1;
		}
    
        // data
        nbytes = strlen(file_body);
        printf("file_body='%s' (len=%d+1)\n", file_body, nbytes);
        if (nbytes >= DATA_SIZE) {
            printf("Data dimension not enough to contain text\n");
            close(fd);
            return -1;
        }
        file_body[nbytes] = end_str;

        ret = write(fd, file_body, nbytes+1);
        if (ret != nbytes+1) {
			printf("Writing file datablock has failed\n");
			close(fd);
			return -1;
		}

        // padding
        nbytes = DEFAULT_BLOCK_SIZE - METADATA_SIZE - nbytes - 1;
        block_padding = malloc(nbytes);
        printf("Padding: %d\n", nbytes);
        memset(block_padding, 0, nbytes);
        ret = write(fd, block_padding, nbytes);
        if (ret != nbytes) {
			printf("The padding bytes are not written properly. Retry your mkfs\n");
			close(fd);
			return -1;
		}

        printf("(%d/%d) File datablock has been written successfully\n\n", i+3, nblocks);
    }

    close(fd);

    return 0;
}