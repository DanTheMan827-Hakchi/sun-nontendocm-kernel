#include "nand_blk.h"
#include "nand_dev.h"

/*****************************************************************************/

#define REMAIN_SPACE 0
#define PART_FREE 0x55
#define PART_DUMMY 0xff
#define PART_READONLY 0x85
#define PART_WRITEONLY 0x86
#define PART_NO_ACCESS 0x87

#define TIMEOUT                 300         // ms
#define NAND_CACHE_RW

static unsigned int dragonboard_test_flag = 0;

//#define NAND_IO_RESPONSE_TEST

#pragma pack(push,1)

struct burn_param_b
{
    void*buffer;
    uint32_t length;
};

struct burn_param_t
{
    void*buffer;
    uint32_t length;
    uint32_t offset;
    union
    {
        uint32_t flags;
        struct
        {
            uint32_t raw:2;
            uint32_t getoob:1;
            uint32_t unused:29;
        } in;
        struct
        {
            uint32_t unused;
        } out;
    };
    struct burn_param_b oob;
    uint32_t badblocks;
};

struct hakchi_nandinfo
{
    char str[8];
    __u32 size;
    __u32 page_size;
    __u32 pages_per_block;
    __u32 block_count;
};

#pragma pack(pop)

/*****************************************************************************/

extern int NAND_BurnBoot0(uint length, void *buf);
extern int NAND_BurnBoot1(uint length, void *buf);

extern struct _nand_info* p_nand_info;

//for Int
//extern void NAND_ClearRbInt(void);
//extern void NAND_ClearDMAInt(void);
//extern void NAND_Interrupt(__u32 nand_index);

extern  int add_nand(struct nand_blk_ops *tr, struct _nand_phy_partition* phy_partition);
extern  int add_nand_for_dragonboard_test(struct nand_blk_ops *tr);
extern  int remove_nand(struct nand_blk_ops *tr);
extern  int nand_flush(struct nand_blk_dev *dev);
extern struct _nand_phy_partition* get_head_phy_partition_from_nand_info(struct _nand_info*nand_info);
extern struct _nand_phy_partition* get_next_phy_partition(struct _nand_phy_partition* phy_partition);

/*****************************************************************************/

DEFINE_SEMAPHORE(nand_mutex);

unsigned char volatile IS_IDLE = 1;
static int nand_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg);
long max_r_io_response = 1;
long max_w_io_response = 1;

int debug_data = 0;

struct timeval tpstart,tpend;
long timeuse;


///* print flags by name */
//const char *rq_flag_bit_names[] = {
//  "REQ_WRITE",                 /* not set, read. set, write */
//  "REQ_FAILFAST_DEV",          /* no driver retries of device errors */
//  "REQ_FAILFAST_TRANSPORT",    /* no driver retries of transport errors */
//  "REQ_FAILFAST_DRIVER",       /* no driver retries of driver errors */
//  "REQ_SYNC",                  /* request is sync (sync write or read) */
//  "REQ_META",                  /* metadata io request */
//  "REQ_DISCARD",               /* request to discard sectors */
//  "REQ_NOIDLE",                /* don't anticipate more IO after this one */
//  "REQ_RAHEAD",                /* read ahead, can fail anytime */ /* bio only flags */
//  "REQ_THROTTLED",             /* This bio has already been subjected to * throttling rules. Don't do it again. */
//  "REQ_SORTED",                /* elevator knows about this request */
//  "REQ_SOFTBARRIER",           /* may not be passed by ioscheduler */
//  "REQ_FUA",                   /* forced unit access */
//  "REQ_NOMERGE",               /* don't touch this for merging */
//  "REQ_STARTED",               /* drive already may have started this one */
//  "REQ_DONTPREP",              /* don't call prep for this one */
//  "REQ_QUEUED",                /* uses queueing */
//  "REQ_ELVPRIV",               /* elevator private data attached */
//  "REQ_FAILED",                /* set if the request failed */
//  "REQ_QUIET",                 /* don't worry about errors */
//  "REQ_PREEMPT",               /* set for "ide_preempt" requests */
//  "REQ_ALLOCED",               /* request came from our alloc pool */
//  "REQ_COPY_USER",             /* contains copies of user pages */
//  "REQ_FLUSH",                 /* request for cache flush */
//  "REQ_FLUSH_SEQ",             /* request for flush sequence */
//  "REQ_IO_STAT",               /* account I/O stat */
//  "REQ_MIXED_MERGE",           /* merge of different types, fail separately */
//  "REQ_SECURE",                /* secure discard (used with __REQ_DISCARD) */
//  "REQ_NR_BITS",              /* stops here */
//};
//void print_rq_flags(int flags)
//{
//  int i;
//  uint32_t j;
//  j = 1;
//  printk("rq:");
//  for (i = 0; i < 32; i++) {
//      if (flags & j)
//          printk("%s ", rq_flag_bit_names[i]);
//      j = j << 1;
//  }
//  printk("\n");
//}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
void start_time(int data)
{
    if(debug_data != data)
        return;

    do_gettimeofday(&tpstart);
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int end_time(int data,int time,int par)
{
    if(debug_data != data)
        return -1;

    do_gettimeofday(&tpend);
    timeuse = 1000*(tpend.tv_sec-tpstart.tv_sec)*1000+(tpend.tv_usec-tpstart.tv_usec);
    if(timeuse > time)
    {
        printk("N%ld %d\n", timeuse, par);
        return 1;
    }
    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int do_blktrans_request(struct nand_blk_ops *tr,struct nand_blk_dev *dev,struct request *req)
{
    int ret = 0;
    unsigned int block, nsect;
    char *buf;

    struct _nand_dev* nand_dev;
    nand_dev = (struct _nand_dev*)dev->priv;

    block = blk_rq_pos(req) << 9 >> tr->blkshift;
    nsect = blk_rq_cur_bytes(req) >> tr->blkshift;

    buf = req->buffer;

    if (req->cmd_type != REQ_TYPE_FS)
    {
        nand_dbg_err(KERN_NOTICE "not type fs\n");
        return -EIO;
    }

    if (blk_rq_pos(req) + blk_rq_cur_sectors(req) > get_capacity(req->rq_disk))
    {
        nand_dbg_err(KERN_NOTICE "over capacity\n");
        return -EIO;
    }

    if (req->cmd_flags & REQ_DISCARD)
    {
        goto request_exit;
    }

    switch(rq_data_dir(req)) {
    case READ:

            nand_dev->read_data(nand_dev,block,nsect,buf);

            rq_flush_dcache_pages(req);
            ret = 0;
            goto request_exit;

        case WRITE:

            rq_flush_dcache_pages(req);


            nand_dev->write_data(nand_dev,block,nsect,buf);

            ret = 0;

            goto request_exit;
        default:
            nand_dbg_err(KERN_NOTICE "Unknown request %u\n", rq_data_dir(req));
            ret = -EIO;
            goto request_exit;
    }

request_exit:

    return ret;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int mtd_blktrans_thread(void *arg)
{
    struct nand_blk_ops *tr = arg;
    struct request_queue *rq = tr->rq;
    struct request *req = NULL;
    struct nand_blk_dev *dev;
    int background_done = 0;

    spin_lock_irq(rq->queue_lock);

    while (!kthread_should_stop()) {
        int res;

        tr->bg_stop = false;
        if (!req && !(req = blk_fetch_request(rq))) {
//          if (tr->background && !background_done) {
//              spin_unlock_irq(rq->queue_lock);
//              mutex_lock(&dev->lock);
//              tr->background(dev);
//              mutex_unlock(&dev->lock);
//              spin_lock_irq(rq->queue_lock);
//              /*
//               * Do background processing just once per idle
//               * period.
//               */
//              background_done = !tr->bg_stop;
//              continue;
//          }

            set_current_state(TASK_INTERRUPTIBLE);

            if (kthread_should_stop())
                set_current_state(TASK_RUNNING);

            spin_unlock_irq(rq->queue_lock);
            tr->rq_null++;
            schedule();
            spin_lock_irq(rq->queue_lock);
            continue;
        }
        dev = req->rq_disk->private_data;

        spin_unlock_irq(rq->queue_lock);
        tr->rq_null = 0;
        mutex_lock(&dev->lock);
        res = do_blktrans_request(tr, dev, req);
        mutex_unlock(&dev->lock);

        spin_lock_irq(rq->queue_lock);

        if (!__blk_end_request_cur(req, res))
            req = NULL;

        background_done = 0;
    }

    if (req)
        __blk_end_request_all(req, -EIO);

    spin_unlock_irq(rq->queue_lock);

    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static void mtd_blktrans_request(struct request_queue *rq)
{
    struct nand_blk_ops *nandr;
    struct request *req = NULL;

    nandr = rq->queuedata;

    if (!nandr)
        while ((req = blk_fetch_request(rq)) != NULL)
            __blk_end_request_all(req, -ENODEV);
    else {
        nandr->bg_stop = true;
        wake_up_process(nandr->thread);
    }
}

static void null_for_dragonboard(struct request_queue *rq)
{
    return ;
}



/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int nand_open(struct block_device *bdev, fmode_t mode)
{
    struct nand_blk_dev *dev;
    struct nand_blk_ops *nandr;
    int ret = -ENODEV;

    dev = bdev->bd_disk->private_data;
    nandr = dev->nandr;

    if (!try_module_get(nandr->owner))
        goto out;

    ret = 0;
    if (nandr->open && (ret = nandr->open(dev))) {
        out:
        module_put(nandr->owner);
    }
    return ret;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int nand_release(struct gendisk *disk, fmode_t mode)
{
    struct nand_blk_dev *dev;
    struct nand_blk_ops *nandr;

    int ret = 0;

    dev = disk->private_data;
    nandr = dev->nandr;
    if (nandr->release)
        ret = nandr->release(dev);

    if (!ret) {
        module_put(nandr->owner);
    }

    return ret;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
#define DISABLE_WRITE           _IO('V',0)
#define ENABLE_WRITE            _IO('V',1)
#define DISABLE_READ            _IO('V',2)
#define ENABLE_READ             _IO('V',3)
#define DRAGON_BOARD_TEST       _IO('V',55)

#define OOB_BUF_SIZE 64

enum
{
    hakchi_test=_IO('v',121),
    BLKBURNBOOT0=_IO('v',122),
    BLKBURNBOOT1=_IO('v',123),
    phy_read=_IO('v',124),
    phy_write=_IO('v',125),
    read_boot0=_IO('v',126),
};

extern int NAND_PhysicLock(void);
extern int NAND_PhysicUnLock(void);

extern __u32 NAND_GetPageSize(void);
extern __u32 NAND_GetPageCntPerBlk(void);
extern __u32 NAND_GetBlkCntPerChip(void);
extern __u32 NAND_GetChipCnt(void);
extern __s32 NAND_GetBlkCntOfDie(void);

extern int NAND_ReadBoot0(unsigned int length,void*buf);

int mark_bad_block( uint chip_num, uint blk_num)
{
    boot_flash_info_t info;
    struct boot_physical_param para;
    unsigned char oob_buf[OOB_BUF_SIZE];
    unsigned char* page_buf;
    int page_index[4];
    uint page_with_bad_block, page_per_block;
    uint i;
    int mark_err_flag = -1;
    if( NAND_GetFlashInfo( &info ))
    {
        nand_dbg_err("get flash info failed.\n");
        return -1;
    }
    //cal nand parameters
    //page_buf = (unsigned char*)(MARK_BAD_BLK_BUF_ADR);
    page_buf = (unsigned char*)kmalloc(32 * 1024, GFP_KERNEL);
    if(!page_buf)
    {
        nand_dbg_err("malloc memory for page buf fail\n");
        return -1;
    }
    memset(page_buf,0xff,32*1024);
    page_with_bad_block = info.pagewithbadflag;
    page_per_block = info.blocksize/info.pagesize;
    //read the first, second, last, last-1 page for check bad blocks
    page_index[0] = 0;
    page_index[1] = 0xEE;
    page_index[2] = 0xEE;
    page_index[3] = 0xEE;
    switch(page_with_bad_block & 0x03)
    {
    case 0x00:
        //the bad block flag is in the first page, same as the logical information, just read 1 page is ok
        break;
    case 0x01:
        //the bad block flag is in the first page or the second page, need read the first page and the second page
        page_index[1] = 1;
        break;
    case 0x02:
        //the bad block flag is in the last page, need read the first page and the last page
        page_index[1] = page_per_block - 1;
        break;
    case 0x03:
        //the bad block flag is in the last 2 page, so, need read the first page, the last page and the last-1 page
        page_index[1] = page_per_block - 1;
        page_index[2] = page_per_block - 2;
        break;
    }
    for(i =0; i<4; i++)
    {
        oob_buf[0] = 0x0;
        oob_buf[1] = 0x1;
        oob_buf[2] = 0x2;
        oob_buf[3] = 0x3;
        oob_buf[4] = 0x89;
        oob_buf[5] = 0xab;
        oob_buf[6] = 0xcd;
        oob_buf[7] = 0xef;
        para.chip = chip_num;
        para.block = blk_num;
        para.page = page_index[i];
        para.mainbuf = page_buf;
        para.oobbuf = oob_buf;
        if(para.page == 0xEE)
            continue;
        PHY_SimpleWrite( &para );
        PHY_SimpleRead( &para );
        if(oob_buf[0] !=0xff)
            mark_err_flag = 0;
    }
    kfree(page_buf);
    return mark_err_flag;
}

static uint NAND_UbootSimpleRead(uint start, uint blocks, void* buffer, struct burn_param_t*burn_param)
{
    __u32 i, k, block, badblock;
    __u32 page_size, pages_per_block, block_size, block_count;
    unsigned char oob_buf[OOB_BUF_SIZE];
    struct boot_physical_param para;

    const int raw=burn_param->in.raw;
    burn_param->flags=0;

    page_size = NAND_GetPageSize();
    pages_per_block = NAND_GetPageCntPerBlk();
    block_count = NAND_GetBlkCntPerChip();
    block_size = page_size * pages_per_block;

    block = 0;
    for (i = start; i < block_count && block < blocks; i++)
    {
        badblock=0;
        for (k = 0; k < pages_per_block; k++)
        {
            para.chip  = 0;
            para.block = i;
            para.page  = k;
            para.mainbuf = (void *) ((__u32)buffer + block * block_size + k * page_size);
            para.oobbuf = oob_buf;
            memset(oob_buf,0xff,OOB_BUF_SIZE);

            if(PHY_SimpleRead(&para)<0)
            {
                nand_dbg_err("Warning. Fail in read page %x in block %x.\n", k, i);
                memset(para.mainbuf,'X',page_size);
                badblock=1;
            }
            if(oob_buf[0]!=0xff)
            {
                badblock=1;
            }
            if(badblock)
            {
                //0 - logical, 1 - break, 2 - read all pages, 3 - unused
                if(raw<2)
                    break;
            }
        }

        if(badblock)
        {
            ++burn_param->badblocks;
            if(raw==0)//retry to read data block on next NAND block
                --block;
        }

        ++block;
    }

    return blocks;
}

static uint NAND_UbootSimpleWrite(uint start, uint blocks, void *buffer, struct burn_param_t*burn_param)
{
    __u32 i, k, block, badblock;
    __u32 page_size, pages_per_block, block_size, block_count;
    unsigned char oob_buf[OOB_BUF_SIZE];
    struct boot_physical_param para;

    const int raw=burn_param->in.raw;
    burn_param->flags=0;

    page_size = NAND_GetPageSize();
    pages_per_block = NAND_GetPageCntPerBlk();
    block_count = NAND_GetBlkCntPerChip();
    block_size = page_size * pages_per_block;

    block = 0;
    for (i = start; i < block_count && block < blocks; i++)
    {
        para.chip  = 0;
        para.block = i;
        if (PHY_SimpleErase(&para) < 0)
        {
            nand_dbg_err("Fail in erasing block %x.\n", i);
            mark_bad_block(para.chip, para.block);
            ++burn_param->badblocks;
            if(raw>0)
              ++block;
            continue;
        }

        badblock=0;
        for (k = 0; k < pages_per_block; k++)
        {
            para.chip  = 0;
            para.block = i;
            para.page  = k;
            para.mainbuf = (void *) ((__u32)buffer + block * block_size + k * page_size);
            para.oobbuf = oob_buf;
            memset(oob_buf,0xff,OOB_BUF_SIZE);

            if (PHY_SimpleWrite(&para) < 0)
            {
                nand_dbg_err("Warning. Fail in write page %x in block %x.\n", k, i);
                badblock=1;
                //0 - logical, 1 - break, 2 - write all pages, 3 - unused
                if(raw<2)
                    break;
            }
        }

        if(badblock)
        {
            mark_bad_block(para.chip, para.block);
            ++burn_param->badblocks;
            if(raw==0)//retry to write data block on next NAND block
                --block;
        }

        ++block;
    }

    return blocks;
}

static int NAND_ioctlRW(unsigned int cmd, struct burn_param_t*burn_param)
{
    void*buffer;
    struct hakchi_nandinfo htn;
    __u32 block_size;

    burn_param->badblocks=0;

    if(burn_param->length==0)
    {
        buffer=0;
    }
    else
    {
        buffer=(void*)kmalloc(burn_param->length,GFP_KERNEL);
        if(buffer==NULL)
        {
            nand_dbg_err("no memory!\n");
            return -1;
        }
    }

    NAND_PhysicLock();
    PHY_WaitAllRbReady();

    htn.page_size=NAND_GetPageSize();
    htn.pages_per_block=NAND_GetPageCntPerBlk();
    block_size=htn.page_size*htn.pages_per_block;

    switch(cmd)
    {
    case hakchi_test:
        burn_param->flags=0;
        htn.block_count=NAND_GetBlkCntPerChip();
        memcpy(htn.str,"hakchi",7);
        htn.str[7]=0;
        htn.size=sizeof(htn);
        if(copy_to_user(burn_param->buffer,&htn,sizeof(htn)))
        {
            nand_dbg_err("copy_to_user error!\n");
            goto error;
        }
        break;

    case phy_read:
        if((burn_param->length%block_size)||(burn_param->offset%block_size))
        {
            nand_dbg_err("phy_read: requested %x at %x, block_size %x\n",burn_param->length,burn_param->offset,block_size);
            goto error;
        }
        memset(buffer,'X',burn_param->length);
        if(NAND_UbootSimpleRead(burn_param->offset/block_size,burn_param->length/block_size,buffer,burn_param)!=(burn_param->length/block_size))
        {
            nand_dbg_err("phy_read: requested %x at %x, cannot read\n",burn_param->length,burn_param->offset);
            goto error;
        }
        if(copy_to_user(burn_param->buffer,buffer,burn_param->length))
        {
            nand_dbg_err("copy_to_user error!\n");
            goto error;
        }
        break;

    case phy_write:
        if(copy_from_user(buffer,(const void*)burn_param->buffer,burn_param->length))
        {
            nand_dbg_err("copy_from_user error!\n");
            goto error;
        }
        if((burn_param->length%block_size)||(burn_param->offset%block_size))
        {
            nand_dbg_err("phy_write: requested %x at %x, block_size %x\n",burn_param->length,burn_param->offset,block_size);
            goto error;
        }
        if(NAND_UbootSimpleWrite(burn_param->offset/block_size,burn_param->length/block_size,buffer,burn_param)!=(burn_param->length/block_size))
        {
            nand_dbg_err("phy_write: requested %x at %x, cannot write\n",burn_param->length,burn_param->offset);
            goto error;
        }
        break;

    case read_boot0:
        memset(buffer,'X',burn_param->length);
        if(NAND_ReadBoot0(burn_param->length,buffer))
        {
            nand_dbg_err("NAND_ReadBoot0 error!\n");
            goto error;
        }
        if(copy_to_user(burn_param->buffer,buffer,burn_param->length))
        {
            nand_dbg_err("copy_to_user error!\n");
            goto error;
        }
        break;
    }

    kfree(buffer);
    PHY_WaitAllRbReady();
    NAND_PhysicUnLock();
    return 0;

error:
    kfree(buffer);
    PHY_WaitAllRbReady();
    NAND_PhysicUnLock();
    return -1;
}

static int nand_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    struct nand_blk_dev *dev = bdev->bd_disk->private_data;
    struct nand_blk_ops *nandr = dev->nandr;
    struct burn_param_t burn_param;
    int ret=-EFAULT;

    switch (cmd) {
    case BLKFLSBUF:
        nand_dbg_err("BLKFLSBUF called!\n");
        if (nandr->flush)
            return nandr->flush(dev);
        // The core code did the work, we had nothing to do.
        return 0;

    case HDIO_GETGEO:
        if (nandr->getgeo) {
            struct hd_geometry g;
            int ret;

            memset(&g, 0, sizeof(g));
            ret = nandr->getgeo(dev, &g);
            if (ret)
                return ret;
            nand_dbg_err("HDIO_GETGEO called!\n");
            g.start = get_start_sect(bdev);
            if (copy_to_user((void __user *)arg, &g, sizeof(g)))
                return -EFAULT;

            return 0;
        }
        return 0;
    case ENABLE_WRITE:
        nand_dbg_err("enable write!\n");
        dev->disable_access = 0;
        dev->readonly = 0;
        set_disk_ro(dev->disk, 0);
        return 0;

    case DISABLE_WRITE:
        nand_dbg_err("disable write!\n");
        dev->readonly = 1;
        set_disk_ro(dev->disk, 1);
        return 0;

    case ENABLE_READ:
        nand_dbg_err("enable read!\n");
        dev->disable_access = 0;
        dev->writeonly = 0;
        return 0;

    case DISABLE_READ:
        nand_dbg_err("disable read!\n");
        dev->writeonly = 1;
        return 0;

    case BLKBURNBOOT0:
        if (copy_from_user(&burn_param, (const void*)arg, sizeof (struct burn_param_t)))
                return -EFAULT;

        down(&(nandr->nand_ops_mutex));
        {
            IS_IDLE = 0;
            ret = NAND_BurnBoot0(burn_param.length, burn_param.buffer);
            up(&(nandr->nand_ops_mutex));
            IS_IDLE = 1;
        }
        return ret;

    case BLKBURNBOOT1:
        if (copy_from_user(&burn_param, (const void*)arg, sizeof (struct burn_param_t)))
                return -EFAULT;

        down(&(nandr->nand_ops_mutex));
        {
            IS_IDLE = 0;
            ret = NAND_BurnBoot1(burn_param.length, burn_param.buffer);
            up(&(nandr->nand_ops_mutex));
            IS_IDLE = 1;
        }
        return ret;

    case hakchi_test:
    case phy_read:
    case phy_write:
    case read_boot0:
        if (copy_from_user(&burn_param, (const void*)arg, sizeof (struct burn_param_t)))
                return -EFAULT;

        down(&(nandr->nand_ops_mutex));
        {
            IS_IDLE = 0;
            ret = NAND_ioctlRW(cmd, &burn_param);
            up(&(nandr->nand_ops_mutex));
            IS_IDLE = 1;
        }
        copy_to_user((void*)arg, &burn_param, sizeof (struct burn_param_t));
        return ret;

    case DRAGON_BOARD_TEST:

        down(&(nandr->nand_ops_mutex));
        {
            IS_IDLE = 0;
            ret = NAND_DragonboardTest();
            up(&(nandr->nand_ops_mutex));
            IS_IDLE = 1;
        }
        return ret;

    default:
        return -ENOTTY;
    }

    return ret;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/

struct block_device_operations nand_blktrans_ops = {
    .owner      = THIS_MODULE,
    .open       = nand_open,
    .release    = nand_release,
    .ioctl      = nand_ioctl,
};


/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int nand_blk_open(struct nand_blk_dev *dev)
{
    //nand_dbg_err("nand_blk_open!\n");
    //mutex_lock(&dev->lock);
    //nand_dbg_err("nand_open ok!\n");

    //kref_get(&dev->ref);

    //mutex_unlock(&dev->lock);
    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int nand_blk_release(struct nand_blk_dev *dev)
{
    int error = 0;
    struct _nand_dev *nand_dev = (struct _nand_dev *)dev->priv;
    if(dragonboard_test_flag == 0)
    {
    //nand_dbg_err("nand_blk_release!\n");

    //error = nand_dev->flush_sector_write_cache(nand_dev,0);
        error = nand_dev->flush_write_cache(nand_dev,0xffff);

        //mutex_lock(&dev->lock);
        //kref_put(&dev->ref, del_nand_blktrans_dev);
        //mutex_unlock(&dev->lock);

        return error;
    }
    else
        return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int del_nand_blktrans_dev(struct nand_blk_dev *dev)
{
//    if (!down_trylock(&nand_mutex)) {
//        up(&nand_mutex);
//        BUG();
//    }
//    blk_cleanup_queue(dev->rq);
//    kthread_stop(dev->thread);
    list_del(&dev->list);
    dev->disk->queue = NULL;
    del_gendisk(dev->disk);
    put_disk(dev->disk);

    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
static int nand_getgeo(struct nand_blk_dev *dev,  struct hd_geometry *geo)
{
    geo->heads = dev->heads;
    geo->sectors = dev->sectors;
    geo->cylinders = dev->cylinders;

    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
struct nand_blk_ops mytr = {
    .name               =  "nand",
    .major              = 93,
    .minorbits          = 3,
    .blksize            = 512,
    .blkshift           = 9,
    .open               = nand_blk_open,
    .release            = nand_blk_release,
    .getgeo             = nand_getgeo,
    .add_dev            = add_nand,
    .add_dev_test     = add_nand_for_dragonboard_test,
    .remove_dev         = remove_nand,
    .flush              = nand_flush,
    .owner              = THIS_MODULE,
};

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
void set_part_mod(char *name,int cmd)
{
    struct file *filp = NULL;
    filp = filp_open(name, O_RDWR, 0);
    filp->f_dentry->d_inode->i_bdev->bd_disk->fops->ioctl(filp->f_dentry->d_inode->i_bdev, 0, cmd, 0);
    filp_close(filp, current->files);
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int add_nand_blktrans_dev(struct nand_blk_dev *dev)
{
    struct nand_blk_ops *tr = dev->nandr;
    struct list_head *this;
    struct gendisk *gd;
    unsigned long temp;
    int ret = -ENOMEM;

    int last_devnum = -1;

    dev->cylinders = 1024;
    dev->heads = 16;

    temp = dev->cylinders * dev->heads;
    dev->sectors = ( dev->size) / temp;
    if ((dev->size) % temp) {
        dev->sectors++;
        temp = dev->cylinders * dev->sectors;
        dev->heads = (dev->size)  / temp;

        if ((dev->size)   % temp) {
            dev->heads++;
            temp = dev->heads * dev->sectors;
            dev->cylinders = (dev->size)  / temp;
        }
    }

    if (!down_trylock(&nand_mutex)) {
        up(&nand_mutex);
        BUG();
    }

    list_for_each(this, &tr->devs) {
        struct nand_blk_dev *tmpdev = list_entry(this, struct nand_blk_dev, list);
        if (dev->devnum == -1) {
            /* Use first free number */
            if (tmpdev->devnum != last_devnum+1) {
                /* Found a free devnum. Plug it in here */
                dev->devnum = last_devnum+1;
                list_add_tail(&dev->list, &tmpdev->list);
                goto added;
            }
        } else if (tmpdev->devnum == dev->devnum) {
            /* Required number taken */
            nand_dbg_err("\nerror00\n");
            return -EBUSY;
        } else if (tmpdev->devnum > dev->devnum) {
            /* Required number was free */
            list_add_tail(&dev->list, &tmpdev->list);
            goto added;
        }
        last_devnum = tmpdev->devnum;
    }
    if (dev->devnum == -1)
        dev->devnum = last_devnum+1;

    if ((dev->devnum <<tr->minorbits) > 256) {
        nand_dbg_err("\nerror00000\n");
        return -EBUSY;
    }

    //init_MUTEX(&dev->sem);
    list_add_tail(&dev->list, &tr->devs);

added:
    gd = alloc_disk(1 << tr->minorbits);
    if (!gd) {
        list_del(&dev->list);
        goto error2;
    }

    gd->major = tr->major;
    gd->first_minor = (dev->devnum) << tr->minorbits;
    gd->fops = &nand_blktrans_ops;

    if(dev->devnum>0)
        snprintf(gd->disk_name, sizeof(gd->disk_name),"%s%c", tr->name, (tr->minorbits?'a':'0') + dev->devnum-1);
    else
        snprintf(gd->disk_name, sizeof(gd->disk_name),"%s", tr->name);
    /* 2.5 has capacity in units of 512 bytes while still
       having BLOCK_SIZE_BITS set to 10. Just to keep us amused. */
    set_capacity(gd, dev->size);

    gd->private_data = dev;
    dev->disk = gd;
    gd->queue = tr->rq;

//    /*set rw partition*/
//    if(part->type == PART_NO_ACCESS)
//        dev->disable_access = 1;
//
//    if(part->type == PART_READONLY)
//        dev->readonly = 1;
//
//    if(part->type == PART_WRITEONLY)
//        dev->writeonly = 1;

//    if (dev->readonly)
//        set_disk_ro(gd, 1);

    dev->disable_access = 0;
    dev->readonly = 0;
    dev->writeonly = 0;

    mutex_init(&dev->lock);
    //kref_init(&dev->ref);

    // Create the request queue
//    spin_lock_init(&dev->queue_lock);

    add_disk(gd);

    return 0;

error2:
    nand_dbg_err("\nerror2\n");
    list_del(&dev->list);
    return ret;
}

int add_nand_blktrans_dev_for_dragonboard(struct nand_blk_dev *dev)
{
    struct nand_blk_ops *tr = dev->nandr;
    struct gendisk *gd;
    int ret = -ENOMEM;

    gd = alloc_disk(1);
    if (!gd) {
        list_del(&dev->list);
        goto error2;
    }

    gd->major = tr->major;
    gd->first_minor = 0;
    gd->fops = &nand_blktrans_ops;

    snprintf(gd->disk_name, sizeof(gd->disk_name),
         "%s%c", tr->name, (1?'a':'0') + dev->devnum);
    set_capacity(gd, 512);

    gd->private_data = dev;
    dev->disk = gd;
    gd->queue = tr->rq;

    dev->disable_access = 0;
    dev->readonly = 0;
    dev->writeonly = 0;

    mutex_init(&dev->lock);

    // Create the request queue
    spin_lock_init(&tr->queue_lock);
    tr->rq = blk_init_queue(null_for_dragonboard, &tr->queue_lock);
    if (!tr->rq){
       goto error3;
    }
    tr->rq->queuedata = dev;
    blk_queue_logical_block_size(tr->rq, tr->blksize);

    gd->queue = tr->rq;
    add_disk(gd);

    return 0;

error3:
    nand_dbg_err("\nerror3\n");
    put_disk(dev->disk);
error2:
    nand_dbg_err("\nerror2\n");
    list_del(&dev->list);
    return ret;
}


/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int nand_blk_register(struct nand_blk_ops *tr)
{
    int ret;
    struct _nand_phy_partition* phy_partition;

    down(&nand_mutex);

    ret = register_blkdev(tr->major, tr->name);
    if(ret){
        nand_dbg_err("\nfaild to register blk device\n");
        up(&nand_mutex);
        return -1;
    }

    spin_lock_init(&tr->queue_lock);
    init_completion(&tr->thread_exit);
    init_waitqueue_head(&tr->thread_wq);
    sema_init(&tr->nand_ops_mutex, 1);


    tr->rq = blk_init_queue(mtd_blktrans_request, &tr->queue_lock);
    if (!tr->rq) {
        unregister_blkdev(tr->major, tr->name);
        up(&nand_mutex);
        return  -1;
    }

    tr->rq->queuedata = tr;
    blk_queue_logical_block_size(tr->rq, tr->blksize);
    blk_queue_max_hw_sectors(tr->rq,128);

    tr->thread = kthread_run(mtd_blktrans_thread, tr, "%s", tr->name);
    if (IS_ERR(tr->thread)) {
        ret = PTR_ERR(tr->thread);
        blk_cleanup_queue(tr->rq);
        unregister_blkdev(tr->major, tr->name);
        up(&nand_mutex);
        return ret;
    }

    //devfs_mk_dir(nandr->name);
    INIT_LIST_HEAD(&tr->devs);
    tr->nftl_blk_head.nftl_blk_next = NULL;
    tr->nand_dev_head.nand_dev_next = NULL;

    phy_partition = get_head_phy_partition_from_nand_info(p_nand_info);

    while(phy_partition != NULL)
    {
        tr->add_dev(tr, phy_partition);
        phy_partition = get_next_phy_partition(phy_partition);
    }

    up(&nand_mutex);

    return 0;
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
void nand_blk_unregister(struct nand_blk_ops *tr)
{

    down(&nand_mutex);
    /* Clean up the kernel thread */
    tr->quit = 1;
    wake_up(&tr->thread_wq);
    wait_for_completion(&tr->thread_exit);

    /* Remove it from the list of active majors */
    tr->remove_dev(tr);

    unregister_blkdev(tr->major, tr->name);

    //devfs_remove(nandr->name);
    blk_cleanup_queue(tr->rq);

    up(&nand_mutex);

    if (!list_empty(&tr->devs))
        BUG();
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
//int cal_partoff_within_disk(char *name,struct inode *i)
//{
//    struct gendisk *gd = i->i_bdev->bd_disk;
//    int current_minor = MINOR(i->i_bdev->bd_dev)  ;
//    int index = current_minor & ((1<<mytr.minorbits) - 1) ;
//    if(!index)
//        return 0;
//    return ( gd->part_tbl->part[ index - 1]->start_sect);
//}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int  init_blklayer(void)
{
    script_item_u   good_block_ratio_flag;
    script_item_value_type_e  type;

    type = script_get_item("nand0_para", "good_block_ratio", &good_block_ratio_flag);

    if(SCIRPT_ITEM_VALUE_TYPE_INT != type)
        nand_dbg_err("nand type err!\n");
    else
    {

    }

    return nand_blk_register(&mytr);
}

int init_blklayer_for_dragonboard(void)
{
    int ret;
    struct nand_blk_ops *tr;

    tr =  &mytr;

    dragonboard_test_flag = 1;

    down(&nand_mutex);

    ret = register_blkdev(tr->major, tr->name);
    if(ret){
        nand_dbg_err("\nfaild to register blk device\n");
        up(&nand_mutex);
        return -1;
    }

    init_completion(&tr->thread_exit);
    init_waitqueue_head(&tr->thread_wq);
    sema_init(&tr->nand_ops_mutex, 1);

    //devfs_mk_dir(nandr->name);
    INIT_LIST_HEAD(&tr->devs);
    tr->nftl_blk_head.nftl_blk_next = NULL;
    tr->nand_dev_head.nand_dev_next = NULL;

    tr->add_dev_test(tr);

    up(&nand_mutex);

    return 0;

}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
void   exit_blklayer(void)
{
    nand_blk_unregister(&mytr);
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
int __init nand_drv_init(void)
{
    //printk("[NAND]2016-1-29 18:36\n");
    return init_blklayer();
}

/*****************************************************************************
*Name         :
*Description  :
*Parameter    :
*Return       :
*Note         :
*****************************************************************************/
void __exit nand_drv_exit(void)
{
    exit_blklayer();

}

//module_init(nand_drv_init);
//module_exit(nand_drv_exit);
MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("nand flash groups");
MODULE_DESCRIPTION ("Generic NAND flash driver code");
