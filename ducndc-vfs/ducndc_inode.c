#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "bitmap.h"
#include "ducndc_fs.h"

static const struct inode_operations simplefs_inode_ops;
static const struct inode_operations symlink_inode_ops;

struct inode *
ducndc_fs_iget(
	struct super_block *sb,
	unsigned long ino
)
{
	struct inode *inode = NULL;
	struct ducndc_fs_inode *cinode = NULL;
	struct ducndc_fs_inode_info *ci = NULL;
	struct ducndc_fs_sb_info *sbi = DUCNDC_FS_SB(sb);
	struct buffer_head *bh = NULL;
	uint32_t inode_block = (ino / DUCNDC_FS_INODES_PER_BLOCK) + 1;
	uint32_t inode_shift = ino % DUCNDC_FS_INODES_PER_BLOCK;
	int ret;

	if (ino >= sbi->nr_inodes) {
		return ERR_PTR(-EINVAL);
	}

	inode = iget_locked(sb, ino);

	if (!inode) {
		return ERR_PTR(-ENOMEM);
	}

	if (!(inode->i_state & I_NEW)) {
		return inode;
	}

	ci = DUCNDC_FS_INODE(inode);
	bh = sb_bread(sb, inode_block);

	if (!bh) {
		ret = -EIO;
		goto failed;
	}

	cinode = (struct ducndc_fs_inode *)bh->b_data;
	cinode += inode_shift;
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &ducndc_fs_inode_ops;
    inode->i_mode = le32_to_cpu(cinode->i_mode);
    i_uid_write(inode, le32_to_cpu(cinode->i_uid));
    i_gid_write(inode, le32_to_cpu(cinode->i_gid));
    inode->i_size = le32_to_cpu(cinode->i_size);

#if DUCNDC_FS_AT_LEAST(6, 6, 0)
    inode_set_ctime(inode, (time64_t) le32_to_cpu(cinode->i_ctime), 0);
#else
    inode->i_ctime.tv_sec = (time64_t) le32_to_cpu(cinode->i_ctime);
    inode->i_ctime.tv_nsec = 0;
#endif

#if DUCNDC_FS_AT_LEAST(6, 7, 0)
    inode_set_atime(inode, (time64_t) le32_to_cpu(cinode->i_atime), 0);
    inode_set_mtime(inode, (time64_t) le32_to_cpu(cinode->i_mtime), 0);
#else
    inode->i_atime.tv_sec = (time64_t) le32_to_cpu(cinode->i_atime);
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_sec = (time64_t) le32_to_cpu(cinode->i_mtime);
    inode->i_mtime.tv_nsec = 0;
#endif    

    inode->i_blocks = le32_to_cpu(cinode->i_blocks);
    set_nlink(inode, le32_to_cpu(cinode->i_nlink));

    if (S_ISDIR(inode->i_mode)) {
        ci->ei_block = le32_to_cpu(cinode->ei_block);
        inode->i_fop = &simplefs_dir_ops;
    } else if (S_ISREG(inode->i_mode)) {
        ci->ei_block = le32_to_cpu(cinode->ei_block);
        inode->i_fop = &simplefs_file_ops;
        inode->i_mapping->a_ops = &simplefs_aops;
    } else if (S_ISLNK(inode->i_mode)) {
        strncpy(ci->i_data, cinode->i_data, sizeof(ci->i_data));
        inode->i_link = ci->i_data;
        inode->i_op = &symlink_inode_ops;
    }

    brelse(bh);

    /* Unlock the inode to make it usable */
    unlock_new_inode(inode);

    return inode;

failed:
    brelse(bh);
    iget_failed(inode);
    return ERR_PTR(ret);
}

