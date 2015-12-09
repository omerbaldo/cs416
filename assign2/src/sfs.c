/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"


///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//


/*-------------------------------------------------------*/

#define BLK_NUM 2048
#define FILENAME_LEN 14
#define INODE_SIZE 128  //bytes
#define INODE_NUM 32
#define INODE_NUM_PER_BLK (BLOCK_SIZE/INODE_SIZE)
#define INODE_BLK_NUM 10
#define FS_MAGIC 0x6789
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

struct superblock {
    u32 s_magic; // magic number used to distinguish our fs
    u32 s_blocks; // total number of blocks
    u32 s_root; // inode number of root directory
    u32 s_ino_start; // first block of inodes
    u32 s_ino_blocks; // number of blocks used to store the inodes
    u32 s_bitmap_start; // first block for allocation bitmap
    u32 s_bitmap_blocks; // number of blocks used to store the bitmap
    u32 s_data_start; // first block for data
    u32 s_data_blocks; // number of blocks
};

struct inode {
    u32 i_mode; // types of file
    u32 i_uid; // user id
    u32 i_gid; // group id
    u32 i_atime; // la  st access time
    u32 i_ctime; // last change time
    u32 i_mtime; // last modify time
    u16 i_links; // links to file
    u32 i_size; // file size by byte
    u32 i_blocks; // blocks number
    u32 i_addresses[INODE_BLK_NUM]; // physical block addresses
    u8 padding[INODE_SIZE-74]; // make the size to be power of 2
};

/*-------------------------------------------------------*/

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");
   
    log_conn(conn);
    log_fuse_context(fuse_get_context());

/*--------------------------------------------------------*/

    char buffer[BLOCK_SIZE];
    int ret;
    struct superblock sb;
    struct inode ino;

    memset(buffer, 0, BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);
    if ((ret=block_read(0, buffer)) <= 0) {
        sb.s_magic = FS_MAGIC;
        sb.s_blocks = BLK_NUM;
        sb.s_root = 0;
        sb.s_ino_start = (u32)(sizeof(struct superblock) / BLOCK_SIZE) + 1;
        sb.s_ino_blocks = INODE_NUM / INODE_NUM_PER_BLK;
        sb.s_bitmap_start = sb.s_ino_start+sb.s_ino_blocks;
        sb.s_bitmap_blocks = 1;
        sb.s_data_start = sb.s_bitmap_start+sb.s_bitmap_blocks;
        sb.s_data_blocks = 0;

        ino.i_links = 1;
        ino.i_mode = S_IFDIR | S_IRWXU | S_IRWXG;
        ino.i_uid = getuid();
        ino.i_gid = getgid();
        ino.i_atime = time(NULL);
        ino.i_ctime = ino.i_atime;
        ino.i_mtime = ino.i_atime;
        ino.i_size = 0;
        ino.i_blocks = 0;

        memset((void *)buffer, 0, BLOCK_SIZE);
        memcpy((void *)buffer, (void *)&sb, sizeof(struct superblock));
        block_write(0, (void *) buffer);
        memset((void *)buffer, 0, BLOCK_SIZE);
        memcpy((void *)buffer, (void *)&ino, sizeof(struct inode));
        block_write(sb.s_ino_start, (void *) buffer);
    }
    else {
        memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
        if (sb.s_magic != FS_MAGIC) {
            //not our file system, overwrite it?
        }
    }
    
/*--------------------------------------------------------*/

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    disk_close();
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);


/*---------------------------------------------------------*/

    char buffer[BLOCK_SIZE];
    char *data;
    struct dirent *entry;
    struct superblock sb;
    struct inode root_ino, ino;
    int nentries, i;

     // read superblock and root directory inode
    memset(buffer, 0, BLOCK_SIZE);
    block_read(0, buffer);
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
    memset(buffer, 0, BLOCK_SIZE);
    block_read(sb.s_ino_start, buffer);
    memcpy((void *)&root_ino, (void *)buffer, sizeof(struct inode));

    if (strcmp("/", path) == 0) {
        // statbuf->st_ino = sb.s_root;
        statbuf->st_mode = root_ino.i_mode;
        statbuf->st_uid = root_ino.i_uid;
        statbuf->st_gid = root_ino.i_gid;
        statbuf->st_rdev = 0;
        statbuf->st_atime = root_ino.i_atime;
        statbuf->st_ctime = root_ino.i_ctime;
        statbuf->st_mtime = root_ino.i_mtime;
        statbuf->st_blocks = root_ino.i_blocks;
        statbuf->st_nlink = root_ino.i_links;
        statbuf->st_size = root_ino.i_size;
    }
    else {
        // read all data of root directory
        data = malloc(BLOCK_SIZE * root_ino.i_blocks);

        for (i=0; i != root_ino.i_blocks; ++i) {
            memset(buffer, 0, BLOCK_SIZE);
            block_read(root_ino.i_addresses[i], buffer);
            memcpy((void *)&data[BLOCK_SIZE*i], (void *)buffer, BLOCK_SIZE);
        }

        nentries = root_ino.i_size/sizeof(struct dirent);

        entry = (struct dirent *) data;
        for (i=0; i != nentries; ++i) {
            if (strcmp(&path[1], entry[i].d_name) == 0) {
                //statbuf->st_ino = entry[i].d_ino;

                // get this inode
                memset(buffer, 0, BLOCK_SIZE);
                block_read(sb.s_ino_start+(u32)((entry[i].d_ino)/INODE_NUM_PER_BLK), buffer);
                memcpy((void *)&ino, (void *)&((struct inode *)buffer)[(entry[i].d_ino)%INODE_NUM_PER_BLK], sizeof(struct inode));
                statbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG;
                statbuf->st_uid = ino.i_uid;
                statbuf->st_gid = ino.i_gid;
                statbuf->st_rdev = 0;
                statbuf->st_atime = ino.i_atime;
                statbuf->st_ctime = ino.i_ctime;
                statbuf->st_mtime = ino.i_mtime;
                statbuf->st_blocks = ino.i_blocks;
                statbuf->st_nlink = ino.i_links;
                statbuf->st_size = ino.i_size;
                break;
            }
        }
        if (i == nentries) {
            statbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG;
        }
        free(data);
    }

/*---------------------------------------------------------*/

    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
	    path, mode, fi);


    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);

/*---------------------------------------------------------*/

    char buffer[BLOCK_SIZE];
    char *data, *inodes_data;
    struct dirent *entry;
    struct superblock sb;
    struct inode root_ino, *inodes_table;
    int nentries, i, j, k, m, off, block_index;
    u8 *byte;

    memset(buffer, 0, BLOCK_SIZE);
    block_read(0, buffer);
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
    memset(buffer, 0, BLOCK_SIZE);
    block_read(sb.s_ino_start, buffer);
    memcpy((void *)&root_ino, (void *)buffer, sizeof(struct inode));

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root_ino.i_blocks);
    for (i=0; i != root_ino.i_blocks; ++i) {
        memset(buffer, 0, BLOCK_SIZE);
        block_read(root_ino.i_addresses[i], buffer);
        memcpy((void *)&data[BLOCK_SIZE*i], (void *)buffer, BLOCK_SIZE);
    }

    // find this file in current directory
    nentries = root_ino.i_size/sizeof(struct dirent);
    entry = (struct dirent *) data;
    for (i=0; i != nentries; ++i) {
        if (strcmp(&path[1], entry[i].d_name) == 0) {
            break;
        }
    }

    // this file doesn't exist, create
    if (i == nentries) {
        // read all inodes
        inodes_data = malloc(BLOCK_SIZE * sb.s_ino_blocks);
        for (j=0; j != sb.s_ino_blocks; ++j) {
            memset(buffer, 0, BLOCK_SIZE);
            block_read(sb.s_ino_start+j, buffer);
            memcpy((void *)&inodes_data[BLOCK_SIZE*j], (void *)buffer, BLOCK_SIZE);
        }
        // find first free inode
        inodes_table = (struct inode *)inodes_data;
        for (j=1; j != INODE_NUM; ++j) {
            if(inodes_table[j].i_links == 0) {
                break;
            }
        }
        // if there is no more inode
        if (j == INODE_NUM) {
            retstat = -1;
        }
        else {
            // create this inode
            inodes_table[j].i_links = 1;
            inodes_table[j].i_mode = S_IFREG | S_IRWXU | S_IRWXG;
            inodes_table[j].i_uid = getuid();
            inodes_table[j].i_gid = getgid();
            inodes_table[j].i_atime = time(NULL);
            inodes_table[j].i_ctime = inodes_table[j].i_atime;
            inodes_table[j].i_mtime = inodes_table[j].i_atime;
            inodes_table[j].i_size = 0;
            inodes_table[j].i_blocks = 0;
            block_write(sb.s_ino_start+(u32)(j/INODE_NUM_PER_BLK), &inodes_data[BLOCK_SIZE*(u32)(j/INODE_NUM_PER_BLK)]);
            // make a new entry
            entry = (struct dirent *) malloc(sizeof(struct dirent));
            entry->d_ino = sb.s_root+j;
            if (strlen(&path[1]) > 256) retstat = -1;
            else memcpy(entry->d_name, &path[1], sizeof(path)-1);
            // need a new block
            if (root_ino.i_size + sizeof(struct dirent) > BLOCK_SIZE*root_ino.i_blocks) {
                // find first free data block
                memset(buffer, 0, BLOCK_SIZE);
                block_read(sb.s_bitmap_start, buffer);
                block_index = sb.s_data_start;
                for (k=0; k!=BLOCK_SIZE; ++k) {
                    byte = (u8 *) &buffer[k];
                    for (m=0; m!=8;++m) {
                        if ( ((*byte >> m) & 1) == 0 ) {
                            *byte |= 1 << m;
                            break;
                        }
                        block_index++;
                    }
                    if (m != 8) break;
                }
                root_ino.i_addresses[root_ino.i_blocks] = block_index;
                block_write(sb.s_bitmap_start, buffer);
            }

            // write this entry
            off = root_ino.i_blocks*BLOCK_SIZE-root_ino.i_size;
            if (off != 0) {
                memcpy(&data[root_ino.i_size], entry, off);
                memcpy(&buffer, &data[BLOCK_SIZE*(root_ino.i_blocks-1)], BLOCK_SIZE);
                block_write(root_ino.i_addresses[root_ino.i_blocks-1], buffer);
            }
            if (off < sizeof(struct dirent)) {
                memset(buffer, 0, BLOCK_SIZE);
                memcpy((void *)buffer, &((u8*)entry)[off], sizeof(struct dirent)-off);
                block_write(root_ino.i_addresses[root_ino.i_blocks], buffer);
            }
            root_ino.i_blocks++;
            sb.s_ino_blocks++;
            root_ino.i_size += sizeof(struct dirent);

            //write root_ino and superblock back
            memset(buffer, 0, BLOCK_SIZE);
            block_read(sb.s_ino_start, buffer);
            memcpy((void *)buffer, (void *)&root_ino, sizeof(struct inode));
            block_write(sb.s_ino_start, buffer);
            memset(buffer, 0, BLOCK_SIZE);
            memcpy((void *)buffer, (void *)&sb, sizeof(struct superblock));
            block_write(0, buffer);

            free(entry);
        }

        free(inodes_data);
    }

    free(data);
/*---------------------------------------------------------*/
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

/*-------------------------------------------------------*/

    char buffer[BLOCK_SIZE];
    char *data, *inodes_data;
    struct dirent *entry;
    struct superblock sb;
    struct inode root_ino, ino;
    int nentries, i, j, num, off;

    // read superblock and root directory inode
    memset(buffer, 0, BLOCK_SIZE);
    block_read(0, buffer);
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
    memset(buffer, 0, BLOCK_SIZE);
    block_read(sb.s_ino_start, buffer);
    memcpy((void *)&root_ino, (void *)buffer, sizeof(struct inode));

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root_ino.i_blocks);

    for (i=0; i != root_ino.i_blocks; ++i) {
        memset(buffer, 0, BLOCK_SIZE);
        block_read(root_ino.i_addresses[i], buffer);
        memcpy((void *)&data[BLOCK_SIZE*i], (void *)buffer, BLOCK_SIZE);
    }

    // find the corresponding inode
    nentries = root_ino.i_size/sizeof(struct dirent);
    entry = (struct dirent *) data;
    for (i=0; i != nentries; ++i) {
        if (strcmp(&path[1], entry[i].d_name) == 0) {
            break;
        }
    }

    // if this file exists, read it.
    if (i != nentries) {
        // read the data from the file
        memset(buffer, 0, BLOCK_SIZE);
        block_read(sb.s_ino_start+(u32)(entry[i].d_ino/INODE_NUM_PER_BLK), buffer);
        memcpy((void *)&ino, (void *)&((struct inode *)buffer)[entry[i].d_ino%INODE_NUM_PER_BLK], sizeof(struct inode));

        // size <= file actual size
        if (size <= ino.i_size) {
            num = (u32)(size/BLOCK_SIZE);
            for (j=0; j!= num; ++j) {
                block_read(ino.i_addresses[j], buffer);
                memcpy((void *)&buf[BLOCK_SIZE*j], buffer, BLOCK_SIZE);
            }
            retstat = size;
        }
        else {
            off = ino.i_size % BLOCK_SIZE;
            num = ino.i_size / BLOCK_SIZE;
            for (j=0; j!=num; ++j) {
                block_read(ino.i_addresses[j], buffer);
                memcpy((void *)&buf[BLOCK_SIZE*j], buffer, BLOCK_SIZE);
            }
            if (off != 0) {
                block_read(ino.i_addresses[j], buffer);
                memcpy((void *)&buf[BLOCK_SIZE*j], buffer, off);
            }
            retstat = ino.i_size;
        }
    }

    free(data);
/*-------------------------------------------------------*/
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);

/*-------------------------------------------------------*/

    char buffer[BLOCK_SIZE];
    char *data, *inodes_data;
    struct dirent *entry;
    struct superblock sb;
    struct inode root_ino, ino;
    int nentries, i, j, k, m, num, off, block_index;
    u8 *byte;
    // read superblock and root directory inode
    memset(buffer, 0, BLOCK_SIZE);
    block_read(0, buffer);
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
    memset(buffer, 0, BLOCK_SIZE);
    block_read(sb.s_ino_start, buffer);
    memcpy((void *)&root_ino, (void *)buffer, sizeof(struct inode));

    // read all data of root directory
    data = malloc(BLOCK_SIZE * root_ino.i_blocks);

    for (i=0; i != root_ino.i_blocks; ++i) {
        memset(buffer, 0, BLOCK_SIZE);
        block_read(root_ino.i_addresses[i], buffer);
        memcpy((void *)&data[BLOCK_SIZE*i], (void *)buffer, BLOCK_SIZE);
    }

    // find the corresponding inode
    nentries = root_ino.i_size/sizeof(struct dirent);
    entry = (struct dirent *) data;
    for (i=0; i != nentries; ++i) {
        if (strcmp(&path[1], entry[i].d_name) == 0) {
            break;
        }
    }

    // if this file exists, write it.
    if (i != nentries) {
        // get the ino structure
        memset(buffer, 0, BLOCK_SIZE);
        block_read(sb.s_ino_start+(u32)((entry[i].d_ino)/INODE_NUM_PER_BLK), buffer);
        memcpy((void *)&ino, (void *)&((struct inode *)buffer)[entry[i].d_ino%INODE_NUM_PER_BLK], sizeof(struct inode));
        // fill the last using blocks
        off = ino.i_blocks*BLOCK_SIZE-ino.i_size;
        if (off != 0) {
            memcpy(&buffer, &data[BLOCK_SIZE*(ino.i_blocks-1)], BLOCK_SIZE);
            memcpy(&buffer[BLOCK_SIZE-off], buf, off);
            block_write(ino.i_addresses[ino.i_blocks-1], buffer);
        }

        // how many new blocks we need
        num = (u32) ((size-off) / BLOCK_SIZE);
        num += ((size-off)%BLOCK_SIZE==0 ? 0 : 1);
        // create num new blocks
        for (j=0; j!=num; ++j) {
            // find a free data block
            memset(buffer, 0, BLOCK_SIZE);
            block_read(sb.s_bitmap_start, buffer);
            block_index = sb.s_data_start;
            for (k=0; k!=BLOCK_SIZE; ++k) {
                byte = (u8 *) &buffer[k];
                for (m=0; m!=8;++m) {
                    if ( ((*byte >> m) & 1) == 0 ) {
                        *byte |= 1 << m;
                        break;
                    }
                    block_index++;
                }
                if (m != 8) break;
            }
            ino.i_addresses[ino.i_blocks] = block_index;
            block_write(sb.s_bitmap_start, buffer);

            // write a block of data
            if (j != num-1) {
                block_write(ino.i_addresses[ino.i_blocks], &buf[off]);
                off += BLOCK_SIZE;
            }
            else {
                memset(buffer, 0, BLOCK_SIZE);
                memcpy(buffer, &buf[off], size-off);
                block_write(ino.i_addresses[ino.i_blocks], buffer);
                off = size;
            }

            ino.i_blocks++;
            sb.s_ino_blocks++;
        }

        ino.i_size += size;

        // update ino and superblock

        memset(buffer, 0, BLOCK_SIZE);
        block_read(sb.s_ino_start+(u32)(entry[i].d_ino/INODE_NUM_PER_BLK), buffer);
        memcpy((void *)&((struct inode *)buffer)[entry[i].d_ino%INODE_NUM_PER_BLK], (void *)&ino, sizeof(struct inode));
        block_write(sb.s_ino_start+(u32)(entry[i].d_ino/INODE_NUM_PER_BLK), buffer);
        memset(buffer, 0, BLOCK_SIZE);
        memcpy((void *)buffer, (void *)&sb, sizeof(struct superblock));
        block_write(0, buffer);
    }
    free(data);

/*-------------------------------------------------------*/

    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);

    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;


/*---------------------------------------------------------*/
    struct superblock sb;
    struct inode ino;
    struct dirent *entry;
    char buffer[BLOCK_SIZE], *data;
    int nentries, i;
    
    log_msg("\nreaddir begins\n");
    // Read the superblock
    memset(buffer, 0, BLOCK_SIZE);
    block_read(0, buffer);
    memcpy((void *)&sb, (void *)buffer, sizeof(struct superblock));
    
    // Read the first inode, containing root directory
    block_read(sb.s_ino_start, buffer);
    memcpy((void *)&ino, (void *)buffer, sizeof(struct inode));
    
    // Get the dirent
    data = malloc(ino.i_blocks * BLOCK_SIZE);
    memset(data, 0, ino.i_blocks * BLOCK_SIZE);
    for (i = 0; i < ino.i_blocks; i++) {
        block_read(ino.i_addresses[i], buffer);
        memcpy((void *)&data[BLOCK_SIZE*i], (void *)buffer, BLOCK_SIZE);
    }

    nentries = ino.i_size / sizeof(struct dirent);
    entry = (struct dirent *) data;
    
    //1. Find the first directory entry following the given offset (see below).
    // Offset ignored (implementation 1)

    for (i = 0; i < nentries; i++) {
        //2. Optionally, create a struct stat that describes the file as for getattr (but FUSE only looks at st_ino and the file-type bits of st_mode).
        //sfs_getattr(entry_arr[i].d_name, statbuf);
        
        //3. Call the filler function with arguments of buf, the null-terminated filename, the address of your struct stat (or NULL if you have none), and the offset of the next directory entry.
        //4. If filler returns nonzero, or if there are no more files, return 0.
        if (filler(buf, entry[i].d_name, NULL, 0) != 0 || i + 1 == nentries) {
            break;
        }
        //5. Find the next file in the directory.
        //6. Go back to step 2.
    }

    free(data);
/*---------------------------------------------------------*/
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    
    return retstat;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir
};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;
    
    // sanity checking on the command line
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
