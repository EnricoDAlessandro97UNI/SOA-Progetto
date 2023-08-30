#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "singlefilefs.h"
#include "../common_header.h"
#include "../utils_header.h"

int onefilefs_open(struct inode *, struct file *);
int onefilefs_release(struct inode *, struct file *);
ssize_t onefilefs_read(struct file *, char *, size_t, loff_t *);

struct dentry *onefilefs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    struct onefilefs_inode *FS_specific_inode;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    struct inode *the_inode = NULL;

    printk("%s: running the lookup inode-function for name %s",MODNAME,child_dentry->d_name.name);

    if (!strcmp(child_dentry->d_name.name, UNIQUE_FILE_NAME)) {
	
		// get a locked inode from the cache 
        the_inode = iget_locked(sb, 1);
        if (!the_inode)
       		 return ERR_PTR(-ENOMEM);

		// already cached inode - simply return successfully
		if (!(the_inode->i_state & I_NEW)) {
			return child_dentry;
		}

		// this work is done if the inode was not already cached
		inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG );
		the_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

		the_inode->i_fop = &onefilefs_file_operations;
		the_inode->i_op = &onefilefs_inode_ops;

		// just one link for this file
		set_nlink(the_inode,1);

		// now we retrieve the file size via the FS specific inode, putting it into the generic inode
		bh = (struct buffer_head *)sb_bread(sb, SINGLEFILEFS_INODES_BLOCK_NUMBER );
		if (!bh) {
			iput(the_inode);
			return ERR_PTR(-EIO);
		}

		FS_specific_inode = (struct onefilefs_inode*)bh->b_data;
		the_inode->i_size = FS_specific_inode->file_size;
		brelse(bh);

		d_add(child_dentry, the_inode);
		dget(child_dentry);

		// unlock the inode to make it usable 
		unlock_new_inode(the_inode);

		return child_dentry;
    }

    return NULL;
}

// Open operation
int onefilefs_open(struct inode *inode, struct file *file) {
	
	printk(KERN_INFO "%s: il thread %d sta tentando di aprire file\n", MODNAME, current->pid);
	
	// controlla se il filesystem è montato
	if (bdev_md.bdev == NULL) {
		printk("%s: nessun device montato\n", MODNAME);
		return -ENODEV;
	}

	// nega aperture del file in scrittura (filesystem in sola lettura)
	if (file->f_mode & FMODE_WRITE) {
		printk("%s: apertura in modalità scrittura non consentita\n", MODNAME);
		return -EROFS;
	}

	return 0;
}

// Read operation
ssize_t onefilefs_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {

	int ret, index, length, copied;

	char *klvl_buf;
	char newline_str = '\n';
	char end_str = '\0';

	unsigned int block_to_read;
	unsigned long my_epoch;

	struct buffer_head *bh = NULL;
	struct block_device *bdev_temp;
	struct bdev_layout *bdev_blk;
	struct Node *p;
	
	if (*pos != 0) return 0;
	copied = 0;

	printk("%s: operazione read chiamata\n", MODNAME);

	// segnala la presenza di un reader sulla variabile bdev
	__sync_fetch_and_add(&(bdev_md.usage),1);
	bdev_temp = bdev_md.bdev;
	if (bdev_temp == NULL) {
		printk("%s: nessun device montato\n", MODNAME);
		__sync_fetch_and_sub(&(bdev_md.usage),1);
		wake_up_interruptible(&unmount_wq);
		return -ENODEV;
	}

	// segnala la presenza del reader
	my_epoch = __sync_fetch_and_add(&(rcu.epoch),1);

	// controlla se la lista dei messaggi validi è vuota
	p = head;
	if (p == NULL) {
		ret = 0;
		goto read_exit;
	}

	// leggi i blocchi validi
	while (p != NULL) {

		block_to_read = blk_offset(p->block_num);
		printk(KERN_INFO "%s: read sta leggendo il blocco %u\n", MODNAME, p->block_num);

		// prendi i dati in cache
		bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, block_to_read);
		if (!bh) {
			ret = -EIO;
			goto read_exit;
		}
		if (bh->b_data != NULL) {
			bdev_blk = (struct bdev_layout *) bh->b_data;
        	length = strlen(bdev_blk->data);
			printk(KERN_INFO "%s: read - blocco %u, %s, len=%d\n", MODNAME, p->block_num, bdev_blk->data, length);

			klvl_buf = kmalloc(length + 1, GFP_KERNEL);
			if (!klvl_buf) {
				printk("%s: errore kmalloc, impossibilità di allocare memoria\n", MODNAME);
				ret = -1;
				goto read_exit;
			}

			memcpy(klvl_buf, bdev_blk->data, length);
			klvl_buf[length] = newline_str;
			ret = copy_to_user(buf + copied, klvl_buf, length + 1);
			copied = copied + length + 1 - ret; 
			printk(KERN_INFO "%s: copiati %d bytes\n", MODNAME, copied);
			kfree(klvl_buf);
		}

		brelse(bh);
		p = p->next;
	}	

	ret = copy_to_user(buf + copied, &end_str, 1);
	copied = copied + 1 - ret;
	ret = count;

read_exit:
	index = (my_epoch & MASK) ? 1 : 0;           
	__sync_fetch_and_add(&(rcu.standing[index]),1);
	wake_up_interruptible(&readers_wq);
	__sync_fetch_and_sub(&(bdev_md.usage),1);
	wake_up_interruptible(&unmount_wq);

	*pos = *pos + copied;
	return ret;
}

// Close operation
int onefilefs_release(struct inode *inode, struct file *file) {
	
	printk(KERN_INFO "%s: il thread %d sta tentando di chiudere il file\n", MODNAME, current->pid);
	
	// controlla se il filesystem è montato
	if(bdev_md.bdev == NULL){
		printk("%s: nessun device montato\n", MODNAME);
		return -ENODEV;
	}

	return 0;
}

const struct inode_operations onefilefs_inode_ops = {
    .lookup = onefilefs_lookup,
};

const struct file_operations onefilefs_file_operations = {
  .read = onefilefs_read,
  .open = onefilefs_open,
  .release = onefilefs_release,
};