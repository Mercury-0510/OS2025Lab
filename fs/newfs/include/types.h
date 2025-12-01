#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    112     
#define NFS_INODE_PER_FILE      1
#define NFS_DATA_PER_FILE       16
#define NFS_DEFAULT_PERM        0777

#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_DRIVER()                    (super.driver_fd)
#define NFS_IO_SZ()                     (super.sz_io)
#define NFS_LOGIC_SZ()                  (super.sz_logic)
#define NFS_DISK_SZ()                   (super.sz_disk)
#define NFS_INODE_SZ()                  sizeof(struct newfs_inode)
#define NFS_INODE_D_SZ()                sizeof(struct newfs_inode_d)
#define NFS_DENTRY_SZ()                 sizeof(struct newfs_dentry)
#define NFS_DENTRY_D_SZ()               sizeof(struct newfs_dentry_d)

#define NFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define NFS_INO_OFS(ino)                (super.inode_offset + (ino) * NFS_BLKS_SZ(NFS_INODE_PER_FILE))
#define NFS_DATA_OFS(ino)               (super.data_offset + (ino) * NFS_BLKS_SZ(NFS_DATA_PER_FILE))

#define NFS_BLKS_SZ(blks)               ((blks) * NFS_LOGIC_SZ())
#define NFS_MAGIC_NUM           0x20050510  
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

#define NFS_ERROR_NONE          0
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ASSIGN_NAME(psfs_dentry, _name)\ 
                        memcpy(psfs_dentry->name, _name, strlen(_name))
#define NFS_ERROR_IO            EIO

#define NFS_IS_DIR(pinode)              (pinode->dentry->type == DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->type == REG_FILE)
#define NFS_IS_SYM_LINK(pinode)         (pinode->dentry->type == SYM_LINK)

typedef int boolean;

typedef enum file_type {
    REG_FILE,
    DIR,
    SYM_LINK
} FILE_TYPE;

struct custom_options {
	const char*          device;
    boolean              showhelp;
};

struct newfs_super {
    uint32_t             magic;
    int                  fd;
    /* TODO: Define yourself */
    int                  driver_fd;
    
    int                  sz_io;
    int                  sz_logic;
    int                  sz_disk;
    int                  sz_usage;
    
    int                  max_ino;
    uint8_t*             map_inode;
    uint8_t*             map_data;
    int                  map_inode_blks;
    int                  map_data_blks;
    int                  map_inode_offset;
    int                  map_data_offset;
    
    int                  inode_offset;

    int                  data_offset;

    boolean              is_mounted;

    struct newfs_dentry* root_dentry;
};

struct newfs_inode {
    uint32_t             ino;
    /* TODO: Define yourself */
    int                  size;                          /* 文件已占用空间 */
    char                 target_path[MAX_NAME_LEN];     /* store traget path when it is a symlink */
    int                  dir_cnt;
    struct newfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct newfs_dentry* dentrys;                       /* 所有目录项 */
    uint8_t*             data;   
};

struct newfs_dentry {
    char                 name[MAX_NAME_LEN];
    uint32_t             ino;
    /* TODO: Define yourself */
    struct newfs_dentry* parent;                        /* 父亲Inode的dentry */
    struct newfs_dentry* brother;                       /* 兄弟 */
    struct newfs_inode*  inode;                         /* 指向inode */
    FILE_TYPE            type;
};

static inline struct newfs_dentry* new_dentry(char * name, FILE_TYPE type) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NFS_ASSIGN_NAME(dentry, name);
    dentry->type    = type;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;                                           
}

// 硬盘数据结构

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct newfs_super_d
{
    uint32_t           magic_num;
    uint32_t           sz_usage;
    
    uint32_t           max_ino;
    uint32_t           map_inode_blks;
    uint32_t           map_inode_offset;

    uint32_t           map_data_blks;
    uint32_t           map_data_offset;

    uint32_t           inode_offset;
    uint32_t           data_offset;
};

// 长度128B 每个逻辑块存放8个inode
struct newfs_inode_d
{
    uint32_t           ino;                           /* 在inode位图中的下标 */
    uint32_t           size;                          /* 文件已占用空间 */
    char               target_path[MAX_NAME_LEN];     /* store traget path when it is a symlink */
    uint32_t           dir_cnt;
    FILE_TYPE          type;   
};  

struct newfs_dentry_d
{
    char               name[MAX_NAME_LEN];
    uint32_t           ino;                           /* 指向的ino号 */
    FILE_TYPE          type;
};  

#endif /* _TYPES_H_ */