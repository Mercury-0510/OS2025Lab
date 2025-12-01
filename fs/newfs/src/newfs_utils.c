#include "../include/newfs.h"
#include "newfs.h"

extern struct newfs_super      super; 
extern struct custom_options   newfs_options;

/**
 * @brief 挂载newfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data |
 * 
 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                   ret = NFS_ERROR_NONE;
    int                   driver_fd;
    struct newfs_super_d  super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;

    int                   inode_num;
    int                   map_inode_blks;
    int                   data_num;
    int                   map_data_blks;
    int                   inode_blks;
    
    int                   super_blks;
    boolean               is_init = FALSE;

    super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    super.driver_fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &super.sz_io);
    super.sz_logic = super.sz_io * 2;
    
    root_dentry = new_dentry("/", DIR);     /* 根目录项每次挂载时新建 */

    if (newfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&super_d), 
                        sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   
                                                        /* 读取super */
    if (super_d.magic_num != NFS_MAGIC_NUM) {           /* 幻数不正确，初始化 */
                                                        /* 估算各部分大小 */
        // 超级块占LOGIC块大小
        super_blks = NFS_ROUND_UP(sizeof(struct newfs_super_d), NFS_LOGIC_SZ()) / NFS_LOGIC_SZ();

        // inode数目
        inode_num  =  NFS_DISK_SZ() / ((NFS_DATA_PER_FILE + NFS_INODE_PER_FILE) * NFS_LOGIC_SZ());

        // inode位图所占LOGIC块大小
        map_inode_blks = NFS_ROUND_UP(NFS_ROUND_UP(inode_num, UINT8_BITS) / UINT8_BITS, NFS_LOGIC_SZ()) 
                         / NFS_LOGIC_SZ();

        // inode区数据块数目
        inode_blks = NFS_ROUND_UP(inode_num * NFS_INODE_D_SZ(), NFS_LOGIC_SZ());

        // data数据块数目
        data_num   =  NFS_DISK_SZ() /  NFS_LOGIC_SZ();

        // data位图所占LOGIC块大小
        map_data_blks = NFS_ROUND_UP(NFS_ROUND_UP(data_num, UINT8_BITS) / UINT8_BITS, NFS_LOGIC_SZ()) 
                         / NFS_LOGIC_SZ();
        
        /* 布局layout */
        super.max_ino               =    (inode_num - super_blks - map_inode_blks - map_data_blks); 

        super_d.map_inode_offset    =    NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);
        super_d.map_data_offset     =    super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);
        super_d.inode_offset        =    super_d.map_data_offset + NFS_BLKS_SZ(map_data_blks);
        super_d.data_offset         =    super_d.map_inode_offset + NFS_BLKS_SZ(inode_blks);

        super_d.map_inode_blks      =    map_inode_blks;
        super_d.map_data_blks       =    map_data_blks;
        super_d.sz_usage            =    0;

        NFS_DBG("inode map blocks: %d\n", map_inode_blks);
        NFS_DBG("data map blocks: %d\n", map_data_blks);

        is_init = TRUE;
    }

    super.sz_usage   = super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    super.map_inode = (uint8_t *)malloc(SFS_BLKS_SZ(super_d.map_inode_blks));
    super.map_inode_blks = super_d.map_inode_blks;
    super.map_inode_offset = super_d.map_inode_offset;
    super.inode_offset = super_d.inode_offset;
    super.data_offset = super_d.data_offset;

    // newfs_dump_map();

	printf("\n--------------------------------------------------------------------------------\n\n");

    if (newfs_driver_read(super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                        NFS_BLKS_SZ(super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (newfs_driver_read(super_d.map_data_offset, (uint8_t *)(super.map_data), 
                        NFS_BLKS_SZ(super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode              = newfs_read_inode(root_dentry, NFS_ROOT_INO);  /* 读取根目录 */
    root_dentry->inode      = root_inode;
    super.root_dentry       = root_dentry;
    super.is_mounted        = TRUE;

    // newfs_dump_map();
    return ret;
}

/**
 * @brief 读取一个逻辑块1024B = 两个IO块2 * 512B
 * @param offset
 * @param out_content
 * @param size
 * @return int 
 */
int newfs_driver_read(int offset, void *out_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_LOGIC_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_LOGIC_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_LOGIC_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 写入一个逻辑块1024B = 两个IO块2 * 512B
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, void *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_LOGIC_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_LOGIC_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_LOGIC_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                /* 当前ino_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == super.max_ino)
        return -NFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(NFS_INODE_SZ());
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    inode->dir_cnt = 0;
    
    if (NFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(NFS_DATA_PER_FILE));
    }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.target_path, inode->target_path, MAX_NAME_LEN);
    inode_d.type       = inode->dentry->type;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    /* 先写inode本身 */
    if (newfs_driver_write(NFS_INO_OFS(ino), (void *)&inode_d, NFS_INODE_D_SZ()) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }

    /* 再写data */
    if (NFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;
        offset        = NFS_DATA_OFS(ino);
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);
            dentry_d.type = dentry_cursor->type;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d, NFS_DENTRY_D_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;                     
            }
            
            if (dentry_cursor->inode != NULL) {
                newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += NFS_DENTRY_D_SZ();
        }
    }
    else if (NFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        if (newfs_driver_write(NFS_DATA_OFS(ino), inode->data, NFS_BLKS_SZ(NFS_DATA_PER_FILE)) != NFS_ERROR_NONE) {
            NFS_DBG("[%s] io error\n", __func__);
            return -NFS_ERROR_IO;
        }
    }
    return NFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct sfs_inode* 
 */
struct newfs_inode* sfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(NFS_INODE_SZ());
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, NFS_INODE_D_SZ()) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, MAX_NAME_LEN);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (newfs_driver_read(NFS_DATA_OFS(ino) + i * NFS_DENTRY_D_SZ(), (void *)&dentry_d, 
            NFS_DENTRY_D_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
            sub_dentry = new_dentry(dentry_d.name, dentry_d.type);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (NFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(NFS_DATA_PER_FILE));
        if (newfs_driver_read(NFS_DATA_OFS(ino), (uint8_t *)inode->data, 
            NFS_BLKS_SZ(NFS_DATA_PER_FILE)) != NFS_ERROR_NONE) {
            NFS_DBG("[%s] io error\n", __func__);
            return NULL;                    
        }
    }
    return inode;
}

/**
 * @brief 将denry插入到inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}