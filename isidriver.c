#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/of_dma.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched/task.h>
#include <linux/wait.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/random.h>

MODULE_LICENSE("GPL");


#define DMA_MAPPING (1 << 7)

/* Innovative Magic Number */
#define II_IOCTL_MAGIC_NUM  'm'

#define  IOCTL_ALLOC_DMA_MEMORY    _IOWR(II_IOCTL_MAGIC_NUM, 10,dma_memory_handle_t*)
#define  IOCTL_FREE_DMA_MEMORY     _IOR (II_IOCTL_MAGIC_NUM, 11,dma_memory_handle_t*)
#define  IOCTL_START_DMA           _IO  (II_IOCTL_MAGIC_NUM, 12)
#define  IOCTL_PRINT_RX_BUFF       _IO  (II_IOCTL_MAGIC_NUM, 13)

static DECLARE_WAIT_QUEUE_HEAD(thread_wait);

static unsigned int test_buf_size = 8192;

static LIST_HEAD(MCDMA_channels);

#define DRIVER_NAME 		       "ISI_MCDMA"
#define NUMBER_OF_CHANNELS_PER_DIR     16
#define CHANNEL_COUNT                  NUMBER_OF_CHANNELS_PER_DIR * 2      // RX 16 AND TX 16
#define TX_CHANNEL		       0
#define RX_CHANNEL		       1
#define ERROR 			       -1
#define NOT_LAST_CHANNEL 	       0
#define LAST_CHANNEL 		       1
#define NUMBER_OF_CHANNELS             16

#define XILINX_BD_CNT	11
#define MAX_DMA_HANDLES XILINX_BD_CNT


typedef struct
{
    unsigned int *  uaddr;
    unsigned int *  kaddr;
    unsigned long   paddr;
    unsigned int    size;
    unsigned int    index;
    unsigned        mapped;
    dma_addr_t      handle;
} dma_memory_handle_t;


/* The following data structure represents a single channel of DMA, transmit or receive in the case
 * when using AXI DMA.  It contains all the data to be maintained for the channel.
 */
struct ISI_MCDMA_channel {
    struct device *char_device_p;			/* character device support */
    struct device *dma_device_p;
    dev_t dev_node;
    struct cdev cdev;
    struct class *class_p;

    struct dma_chan *channel_p;				/* dma support */
    struct completion cmp;
    u32 direction;					/* DMA_MEM_TO_DEV or DMA_DEV_TO_MEM */
    struct MCDMA_chan *test_chan;
    struct scatterlist sglist[XILINX_BD_CNT];
    dma_memory_handle_t dmas[XILINX_BD_CNT];
};

/* Allocate the channels for this example statically rather than dynamically for simplicity.
 */
static struct ISI_MCDMA_channel channels[CHANNEL_COUNT];


struct MCDMA_slave_thread {
    struct list_head node;
    struct task_struct *task;
    struct dma_chan *dmachan;
    struct ISI_MCDMA_channel *_pchannel_p;
    enum dma_transaction_type type;
    bool done;
};

struct MCDMA_chan {
    struct list_head node;
    struct dma_chan *chan;
    struct list_head threads;
};


static void MCDMA_slave_callback(void *completion)
{
    complete(completion);

    pr_warn("DMA Callback....");
}

/* Function for slave transfers
 * Each thread requires 2 channels, one for transmit, and one for receive
 */
static int MCDMA_slave_func(void *data)
{
    struct MCDMA_slave_thread	*thread = data;
    struct ISI_MCDMA_channel *_pchannel_p = thread->_pchannel_p;
    struct dma_chan *chan;
    const char *thread_name;
    dma_cookie_t _cookie;
    enum dma_status status;
    enum dma_ctrl_flags flags;
    int ret;
    int bd_cnt = XILINX_BD_CNT;
    int i;
    u8 align = 0;
    struct dma_device *dma_dev;
    struct dma_async_tx_descriptor *desc = NULL;
    struct completion _cmp;
    unsigned long _tmo = msecs_to_jiffies(300000); /* RX takes longer */

    //temp
    int idx;
    unsigned int *buf;

    thread_name = current->comm;
    ret = -ENOMEM;
    /* Ensure that all previous reads are complete */
    smp_rmb();
    chan = thread->dmachan;
    dma_dev = chan->device;

    set_user_nice(current, 10);
    flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

    if (dma_dev->copy_align > align)
        align = dma_dev->copy_align;

    if (1 << align > test_buf_size) {
        pr_err("%u-byte buffer too small for %d-byte alignmen_pchannel_pt\n",
               test_buf_size, 1 << align);
        return ERROR;
    }

    sg_init_table(_pchannel_p->sglist, bd_cnt);

    for (i = 0; i < bd_cnt; i++) {
        sg_dma_address(&_pchannel_p->sglist[i]) =  _pchannel_p->dmas[i].handle;
        sg_dma_len(&_pchannel_p->sglist[i]) = test_buf_size;
    }

    desc = dma_dev->device_prep_slave_sg(chan, _pchannel_p->sglist, bd_cnt, _pchannel_p->direction, flags, NULL);

    if (!desc) {
        for (i = 0; i < bd_cnt; i++)
            pr_warn("%s: prep error with _off=0x%x ",
                    thread_name, test_buf_size);
        msleep(100);
    }
    init_completion(&_cmp);
    desc->callback = MCDMA_slave_callback;
    desc->callback_param = &_cmp;
    _cookie = desc->tx_submit(desc);

    if (dma_submit_error(_cookie)) {
        pr_warn("%s: submit error %d with _off=0x%x ",
                thread_name, _cookie,  test_buf_size);
        msleep(100);
    }
    dma_async_issue_pending(chan);

    _tmo = wait_for_completion_timeout(&_cmp, _tmo);

    status = dma_async_is_tx_complete(chan, _cookie, NULL, NULL);


    if (_tmo == 0) {
        pr_warn("%s: test timed out\n",
                thread_name);
    } else if (status != DMA_COMPLETE) {
        pr_warn("%s: got completion callback, ",
                thread_name);
        pr_warn("but status is \'%s\'\n",
                status == DMA_ERROR ? "error" :
                                      "in progress");
    }


    //---------------------------------------
    //temp
    for (i=0;i<bd_cnt;i++) {
        buf = _pchannel_p->dmas[i].kaddr;
        pr_warn("__________________New Buf____________________ \n");
        for(idx=0;idx<20;idx++){
            pr_warn("buf[%d]: [0x%x] ", idx, buf[idx]);
        }
    }
    //---------------------------------------

    ret = 0;
    thread->done = true;
    wake_up(&thread_wait);

    return ret;
}


static int MCDMA_add_slave_threads(struct ISI_MCDMA_channel *pchannel_p)
{
    struct MCDMA_slave_thread *thread;

    struct dma_chan *chan = pchannel_p->channel_p;
    int ret;

    thread = kzalloc(sizeof(struct MCDMA_slave_thread), GFP_KERNEL);
    if (!thread) {
        pr_warn("[ISI] : No memory for slave thread %s\n",
                dma_chan_name(chan));
    }
    //fixme    if(pchannel_p->direction == DMA_MEM_TO_DEV)
    //        thread->tx_chan = chan;
    //    else

    thread->dmachan = chan;
    thread->type = (enum dma_transaction_type)DMA_SLAVE;
    thread->_pchannel_p = pchannel_p;

    /* Ensure that all previous writes are complete */
    smp_wmb();
    thread->task = kthread_run(MCDMA_slave_func, thread, "%s",
                               dma_chan_name(chan));

    ret = PTR_ERR(thread->task);
    if (IS_ERR(thread->task)) {
        pr_warn("[ISI} : Failed to run thread %s\n",
                dma_chan_name(chan));
        kfree(thread);
        return ret;
    }

    return 1;
}

static bool is_threaded_test_run(struct MCDMA_chan *dtc)
{

    struct MCDMA_slave_thread *thread;
    int ret = false;

    list_for_each_entry(thread, &dtc->threads, node) {
        if (!thread->done)
            ret = true;
    }
    return ret;
}
static int Print_Rx_Buff(struct ISI_MCDMA_channel *pchannel_p)
{
    int i,j;
    for (i = 0; i < XILINX_BD_CNT; i++){
        unsigned int *buf = pchannel_p->dmas[i].kaddr;
        pr_warn("_______New Buff (Kernel)________\n");
        for(j=0;j<20;j++){
            pr_warn("Buf[%d] 0x%x ", j, buf[j]);
        }
    }
    return 1;
}

static int MCDMA_add_slave_channels(struct ISI_MCDMA_channel *pchannel_p)
{
    unsigned int thread_count = 0;

    pchannel_p->test_chan = kmalloc(sizeof(struct MCDMA_chan), GFP_KERNEL);

    if (!pchannel_p->test_chan) {
        pr_warn("[ISI] : No memory for tx %s\n",
                dma_chan_name(pchannel_p->channel_p));
        return -ENOMEM;
    }

    pchannel_p->test_chan->chan = pchannel_p->channel_p;
    INIT_LIST_HEAD(&pchannel_p->test_chan->threads);

    MCDMA_add_slave_threads(pchannel_p);
    thread_count += 1;
    pr_info("[ISI] : Started %u threads using %s \n",
            thread_count, dma_chan_name(pchannel_p->channel_p));
    list_add_tail(&pchannel_p->test_chan->node, &MCDMA_channels);
    wait_event(thread_wait, !is_threaded_test_run(pchannel_p->test_chan));

    return 0;
}

/**
 * find_dma_handle - Helper that finds the context index from the given address
 * @inode:		Inode pointer
 * @filp:		File pointer
 * returns:		-1 (error), or [0..MAX_DMA_HANDLES-1]
 */
static int find_dma_handle(struct ISI_MCDMA_channel *pchannel_p, unsigned int *kaddr)
{
    int i;
    for (i = 0; i < MAX_DMA_HANDLES; i++)
        if (pchannel_p->dmas[i].kaddr == kaddr)
            break;

    return (i == MAX_DMA_HANDLES) ? -1 : i;
}

/**
 * ii_allocate_dma_memory_ioctl - IOCTl operation for DMA memory alloc
 * @filp:		File pointer
 * @uaddr:		User address pointer
 * returns:		-1 fail, 0 success
 */
static int isi_allocate_dma_memory_ioctl(struct file *pfile, void __user *uaddr)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;
    dma_memory_handle_t  minfo;
    int idx = -1;

    if ((idx = find_dma_handle(pchannel_p, NULL)) == -1) {
        printk(KERN_INFO "[ISI] : cannot find a free dma slot.\n");
        return -EACCES;
    }

    if (copy_from_user(&minfo, uaddr, sizeof(dma_memory_handle_t))) {
        printk(KERN_INFO "[ISI] : CANNOT COPY FROM USER POINTER. \n");
        return -EACCES;
    }

    pchannel_p->dmas[idx].size   = minfo.size;
    pchannel_p->dmas[idx].kaddr  = minfo.kaddr = dma_alloc_coherent(pchannel_p->dma_device_p,
                                                                    pchannel_p->dmas[idx].size,
                                                                    &pchannel_p->dmas[idx].handle,
                                                                    GFP_KERNEL);

    if (pchannel_p->dmas[idx].kaddr == NULL) {
        printk(KERN_INFO "[ISI] : Dma memory allocation failed!\n");
        pchannel_p->dmas[idx].size = 0;
        pchannel_p->dmas[idx].kaddr = NULL;
        return -1;
    } else {
        pchannel_p->dmas[idx].paddr = minfo.paddr = virt_to_phys(pchannel_p->dmas[idx].kaddr);
    }

    minfo.index = idx; /* Pass the index to user so he can use it to map */

    printk(KERN_INFO "[ISI] : Alloc size %d at index %d kaddr 0x%p phaddr 0x%lx \n", minfo.size,
           idx, pchannel_p->dmas[idx].kaddr, pchannel_p->dmas[idx].paddr);

    if (copy_to_user(uaddr, &minfo, sizeof(dma_memory_handle_t))) {
        printk(KERN_INFO "[ISI] : CANNOT COPY TO THE USER POINTER. \n");
        return -EFAULT;
    }

    return 0;
}

/**
 * ii_free_dma_memory_ioctl - IOCTl operation for DMA memory free
 * @filp:		File pointer
 * @uaddr:		User address pointer
 * returns:		-1 fail, 0 success
 */
static int isi_free_dma_memory_ioctl(struct file *pfile, void __user *uaddr)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;
    dma_memory_handle_t minfo;
    int idx = -1;

    if (copy_from_user(&minfo, uaddr, sizeof(dma_memory_handle_t))) {
        printk(KERN_INFO "[ISI] : EACCES \n");
        return -EACCES;
    }

    if ((idx = find_dma_handle(pchannel_p, minfo.kaddr)) == -1) {
        printk(KERN_INFO "[ISI] : requested dma memory could not be found. \n");
        return -EFAULT;
    }

    if (pchannel_p->dmas[idx].size) {
        dma_free_coherent(pchannel_p->dma_device_p,
                          pchannel_p->dmas[idx].size,
                          pchannel_p->dmas[idx].kaddr,
                          pchannel_p->dmas[idx].handle);

        pchannel_p->dmas[idx].size  = pchannel_p->dmas[idx].handle = 0;
        pchannel_p->dmas[idx].kaddr = NULL;
    }

    return 0;
}

static int ISI_c_open (struct inode *pinode, struct file *pfile)
{
    pfile->private_data = container_of(pinode->i_cdev, struct ISI_MCDMA_channel, cdev);
    return 0;
}

static ssize_t ISI_c_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset)
{
    return 0;
}

static ssize_t ISI_c_write (struct file *pfile, const char __user *buffer, size_t length, loff_t *offset)
{
    return length;
}

/* Perform I/O control to start a DMA transfer.
 */
static long ISI_c_ioctl(struct file *pfile, unsigned int cmd , unsigned long arg)
{

    int err;
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;

    switch (cmd) {
    case IOCTL_ALLOC_DMA_MEMORY: {
        return isi_allocate_dma_memory_ioctl(pfile, (void __user *)arg);
    }
    case IOCTL_FREE_DMA_MEMORY: {
        return isi_free_dma_memory_ioctl(pfile, (void __user *)arg);
    }
    case IOCTL_START_DMA: {

        err = MCDMA_add_slave_channels(pchannel_p);
        if (err) {
            pr_err("ISI_MCDMA: Unable to add channels\n");
            return err;
        }
        return 1;
    }
    case IOCTL_PRINT_RX_BUFF: {
        Print_Rx_Buff(pchannel_p);
        return 1;
    }
    default: {
        printk(KERN_INFO "[ISI] : deafult \n");
    }
    }

    return 0;
}

static int ISI_c_mmap(struct file *pfile, struct vm_area_struct *vma)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;

    unsigned int idx = (vma->vm_pgoff & ~DMA_MAPPING) & 0xff; /* We are using the offset for index! */
    printk(KERN_ALERT "MMAP DMA #%u length %lu \n", idx, vma->vm_end - vma->vm_start);

    if ((vma->vm_end - vma->vm_start) > pchannel_p->dmas[idx].size) {
        printk(KERN_ALERT "MMAP FAILED, SIZE MISMATCH. \n");
        return -EINVAL;
    }

    vma->vm_pgoff = 0; //clear
    vma->vm_flags    |= VM_IO;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    return dma_mmap_coherent(pchannel_p->dma_device_p, vma, pchannel_p->dmas[idx].kaddr, pchannel_p->dmas[idx].handle, pchannel_p->dmas[idx].size);
}

static int ISI_c_close(struct inode *pinode, struct file *pfile)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;
    struct dma_device *dma_device = pchannel_p->channel_p->device;

    /* Stop all the activity when the channel is closed assuming this
             * may help if the application is aborted without normal closure
             */

    dma_device->device_terminate_all(pchannel_p->channel_p);
    return 0;
}

struct file_operations ISI_c_file_operations = {
    .owner          = THIS_MODULE,
    .open           = ISI_c_open,
    .read           = ISI_c_read,
    .write          = ISI_c_write,
    .unlocked_ioctl = ISI_c_ioctl,
    .mmap	    = ISI_c_mmap,
    .release        = ISI_c_close,

};

/* Initialize the driver to be a character device such that is responds to
 * file operations.
 */
static int cdevice_init(struct ISI_MCDMA_channel *pchannel_p, char *name)
{
    int rc;
    char device_name[32] = "ISI_MCDMA";
    static struct class *local_class_p = NULL;

    /* Allocate a character device from the kernel for this driver.
         */
    rc = alloc_chrdev_region(&pchannel_p->dev_node, 0, 1, "ISI_MCDMA");

    if (rc) {
        dev_err(pchannel_p->dma_device_p, "unable to get a char device number\n");
        return rc;
    }

    /* Initialize the device data structure before registering the character
         * device with the kernel.
         */
    cdev_init(&pchannel_p->cdev, &ISI_c_file_operations);
    pchannel_p->cdev.owner = THIS_MODULE;
    rc = cdev_add(&pchannel_p->cdev, pchannel_p->dev_node, 1);

    if (rc) {
        dev_err(pchannel_p->dma_device_p, "unable to add char device\n");
        goto init_error1;
    }

    /* Only one class in sysfs is to be created for multiple channels,
         * create the device in sysfs which will allow the device node
         * in /dev to be created
         */
    if (!local_class_p) {
        local_class_p = class_create(THIS_MODULE, DRIVER_NAME);

        if (IS_ERR(pchannel_p->dma_device_p->class)) {
            dev_err(pchannel_p->dma_device_p, "unable to create class\n");
            rc = ERROR;
            goto init_error2;
        }
    }
    pchannel_p->class_p = local_class_p;

    /* Create the device node in /dev so the device is accessible
         * as a character device
         */
    strcat(device_name, name);
    pchannel_p->char_device_p = device_create(pchannel_p->class_p, NULL,
                                              pchannel_p->dev_node, NULL, name);

    if (IS_ERR(pchannel_p->char_device_p)) {
        dev_err(pchannel_p->dma_device_p, "unable to create the device\n");
        goto init_error3;
    }

    return 0;

init_error3:
    class_destroy(pchannel_p->class_p);

init_error2:
    cdev_del(&pchannel_p->cdev);

init_error1:
    unregister_chrdev_region(pchannel_p->dev_node, 1);
    return rc;
}

/* Create a DMA channel by getting a DMA channel from the DMA Engine and then setting
 * up the channel as a character device to allow user space control.
 */
static int create_channel(struct platform_device *pdev, struct ISI_MCDMA_channel *pchannel_p, char *name, u32 direction)
{
    int rc;

    /* Request the DMA channel from the DMA engine and then use the device from
         * the channel for the MCDMA channel also.
         */
    pchannel_p->channel_p = dma_request_chan(&pdev->dev, name);
    if (IS_ERR(pchannel_p->channel_p)) {
        rc = PTR_ERR(pchannel_p->channel_p);
        if (rc != -EPROBE_DEFER)
            pr_err("ISI_MCDMA: No channel with that name.\n");
        dev_err(pchannel_p->dma_device_p, "DMA channel request error"
                                          "n");
        goto free_channel;
    }

    pchannel_p->dma_device_p = &pdev->dev;

    /* Initialize the character device for the dma proxy channel
         */
    rc = cdevice_init(pchannel_p, name);
    if (rc)
        return rc;

    pchannel_p->direction = direction;

    return 0;
free_channel:
    dma_release_channel(pchannel_p->channel_p);

    return rc;

}


/* Prob the mcdma platform driver
 */
static int ISI_p_MCDMA_probe(struct platform_device *pdev)
{
    int rc;

    /* Create the transmit and receive channels.
             */
    int i;
    for(i=0;i<NUMBER_OF_CHANNELS_PER_DIR;i++){
        char tx_device_name[32] = "mcdma_tx_";
        char dummy[32] = "";
        char * name;
        sprintf(dummy, "%d", i);
        name = strcat(tx_device_name, dummy);
        pr_warn("[Creating Tx Channel [%d] With Name [%s]", i, name);
        rc = create_channel(pdev, &channels[i], name, DMA_MEM_TO_DEV);
        if (rc)
            return rc;
    }


    for(i=0;i<NUMBER_OF_CHANNELS_PER_DIR;i++){
        char rx_device_name[32] = "mcdma_rx_";
        char dummy[32] = "";
        char * name;
        sprintf(dummy, "%d", i);
        name = strcat(rx_device_name, dummy);
        pr_warn("[Creating Rx Channel [%d] With Name [%s]", i, name);
        rc = create_channel(pdev, &channels[i], name, DMA_DEV_TO_MEM);
        if (rc)
            return rc;
    }

    printk(KERN_INFO "ISI MCDMA module initialized\n");
    return 0;
}

/* Exit the mcdma device driver module.
 */
static int ISI_p_MCDMA_remove(struct platform_device *pdev)
{
    return 0;
}

/*
 */
static const struct of_device_id ISI_MCDMA_of_ids[] = {
{ .compatible = "xlnx,isi_mcdma",},
{}
};

/*  mcdma device pldriver module.
 */
static struct platform_driver ISI_MCDMA_driver = {
    .driver = {
        .name = "isi_mcdma_driver",
        .owner = THIS_MODULE,
        .of_match_table = ISI_MCDMA_of_ids,
    },
    .probe = ISI_p_MCDMA_probe,
    .remove = ISI_p_MCDMA_remove,
};


static int __init ISI_MCDMA_init(void)
{
    return platform_driver_register(&ISI_MCDMA_driver);
}

static void __exit ISI_MCDMA_exit(void)
{
    platform_driver_unregister(&ISI_MCDMA_driver);
}

module_init(ISI_MCDMA_init);
module_exit(ISI_MCDMA_exit);

MODULE_AUTHOR("Matthew Mesropian");
MODULE_DESCRIPTION("ISI MCDMA");
MODULE_LICENSE("GPL v2");
