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


/* Innovative Magic Number */
#define II_IOCTL_MAGIC_NUM  'm'

#define  IOCTL_ALLOC_DMA_MEMORY    _IOWR(II_IOCTL_MAGIC_NUM, 10,dma_memory_handle_t*)
#define  IOCTL_FREE_DMA_MEMORY     _IOR (II_IOCTL_MAGIC_NUM, 11,dma_memory_handle_t*)
#define  IOCTL_Start_DMA           _IO  (II_IOCTL_MAGIC_NUM, 12)

static DECLARE_WAIT_QUEUE_HEAD(thread_wait);

static unsigned int test_buf_size = 8192;
module_param(test_buf_size, uint, 0444);
MODULE_PARM_DESC(test_buf_size, "Size of the memcpy test buffer");

static LIST_HEAD(dmatest_channels);

#define DRIVER_NAME 		"ISI_MCDMA"
#define CHANNEL_COUNT 		2
#define TX_CHANNEL		0
#define RX_CHANNEL		1
#define ERROR 			-1
#define NOT_LAST_CHANNEL 	0
#define LAST_CHANNEL 		1

#define XILINX_DMATEST_BD_CNT	11
#define MAX_DMA_HANDLES XILINX_DMATEST_BD_CNT


typedef struct
{
    unsigned int *  uaddr;
    unsigned int *  kaddr;
    unsigned int    paddr;
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
    struct scatterlist sglist;
    struct dmatest_chan *test_chan;
    dma_memory_handle_t dmas[XILINX_DMATEST_BD_CNT];
};

/* Allocate the channels for this example statically rather than dynamically for simplicity.
 */
static struct ISI_MCDMA_channel channels[CHANNEL_COUNT];


struct dmatest_slave_thread {
    struct list_head node;
    struct task_struct *task;
    struct dma_chan *dmachan;
    struct ISI_MCDMA_channel *_pchannel_p;
    enum dma_transaction_type type;
    u8 **buffer;
    bool done;
};

struct dmatest_chan {
    struct list_head node;
    struct dma_chan *chan;
    struct list_head threads;
};


static void dmatest_slave_callback(void *completion)
{
    complete(completion);

    pr_warn("Rx Callback....");
}

/* Function for slave transfers
 * Each thread requires 2 channels, one for transmit, and one for receive
 */
static int dmatest_slave_func(void *data)
{
    int testcounter=0;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct dmatest_slave_thread	*thread = data;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct ISI_MCDMA_channel *_pchannel_p = thread->_pchannel_p;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct dma_chan *chan;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    const char *thread_name;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    dma_cookie_t _cookie;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    enum dma_status status;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    enum dma_ctrl_flags flags;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    int ret;
    int bd_cnt = XILINX_DMATEST_BD_CNT;
    int i;


    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    thread_name = current->comm;
    ret = -ENOMEM;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    /* Ensure that all previous reads are complete */
    smp_rmb();

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    chan = thread->dmachan;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    thread->buffer = kcalloc(bd_cnt + 1, sizeof(u8 *), GFP_KERNEL);
    if (!thread->buffer)
        goto err_buf;
    for (i = 0; i < bd_cnt; i++) {
        thread->buffer[i] = kmalloc(test_buf_size, GFP_KERNEL);
        if (!thread->buffer[i])
            goto err_bufs;
    }
    thread->buffer[i] = NULL;

    set_user_nice(current, 10);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct dma_device *dma_dev = chan->device;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct dma_async_tx_descriptor *desc = NULL;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    dma_addr_t dma_addrs[XILINX_DMATEST_BD_CNT];

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    struct completion _cmp;
    unsigned long _tmo = msecs_to_jiffies(300000); /* RX takes longer */
    u8 align = 0;
    struct scatterlist _sg[XILINX_DMATEST_BD_CNT];

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;


    if (dma_dev->copy_align > align)
        align = dma_dev->copy_align;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    if (1 << align > test_buf_size) {
        pr_err("%u-byte buffer too small for %d-byte alignment\n",
               test_buf_size, 1 << align);
        return ERROR;
    }

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    for (i = 0; i < bd_cnt; i++) {
        dma_addrs[i] = dma_map_single(dma_dev->dev,  thread->buffer[i],   test_buf_size, DMA_FROM_DEVICE);
    }

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    sg_init_table(_sg, bd_cnt);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    for (i = 0; i < bd_cnt; i++) {
        sg_dma_address(&_sg[i]) = dma_addrs[i];
        sg_dma_len(&_sg[i]) = test_buf_size;
    }

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    desc = dma_dev->device_prep_slave_sg(chan, _sg, bd_cnt, _pchannel_p->direction, flags, NULL);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    if (!desc) {
        for (i = 0; i < bd_cnt; i++)
            dma_unmap_single(dma_dev->dev, dma_addrs[i],
                             test_buf_size,
                             DMA_FROM_DEVICE);
        pr_warn("%s: prep error with _off=0x%x ",
                thread_name, test_buf_size);
        msleep(100);
    }

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    init_completion(&_cmp);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    desc->callback = dmatest_slave_callback;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    desc->callback_param = &_cmp;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    _cookie = desc->tx_submit(desc);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;


    if (dma_submit_error(_cookie)) {
        pr_warn("%s: submit error %d with _off=0x%x ",
                thread_name, _cookie,  test_buf_size);
        msleep(100);
    }

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    dma_async_issue_pending(chan);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    _tmo = wait_for_completion_timeout(&_cmp, _tmo);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    status = dma_async_is_tx_complete(chan, _cookie, NULL, NULL);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

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

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;


    //---------------------------------------
    int idx;
    u8 *buf;
    u8 **buff = thread->buffer;
    //temp
    for (; (buf = *buff); buff++) {
        pr_warn("__________________New Buf____________________ \n");
        for(idx=0;idx<100;idx++){
            pr_warn("buf[%d]: [%u] ", idx, buf[idx]);
        }
    }
    //---------------------------------------


    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    /* Unmap by myself */
    for (i = 0; i < bd_cnt; i++)
        dma_unmap_single(dma_dev->dev, dma_addrs[i],
                         test_buf_size, DMA_FROM_DEVICE);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    ret = 0;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    thread->done = true;

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    wake_up(&thread_wait);

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

err_bufs:
    for (i = 0; thread->buffer[i]; i++)
        kfree(thread->buffer[i]);
err_buf:
    kfree(thread->buffer);
    return ret;
}


static int dmatest_add_slave_threads(struct ISI_MCDMA_channel *pchannel_p)
{
    int testcounter=0;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;
    struct dmatest_slave_thread *thread;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    struct dma_chan *chan = pchannel_p->channel_p;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    int ret;

    thread = kzalloc(sizeof(struct dmatest_slave_thread), GFP_KERNEL);

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    if (!thread) {
        pr_warn("dmatest: No memory for slave thread %s\n",
                dma_chan_name(chan));
    }

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    //fixme    if(pchannel_p->direction == DMA_MEM_TO_DEV)
    //        thread->tx_chan = chan;
    //    else


    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    thread->dmachan = chan;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    thread->type = (enum dma_transaction_type)DMA_SLAVE;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    thread->_pchannel_p = pchannel_p;

    pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;

    /* Ensure that all previous writes are complete */
    smp_wmb();

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    thread->task = kthread_run(dmatest_slave_func, thread, "%s",
                               dma_chan_name(chan));

    pr_warn("dmatest_slave_func[%d]\n", testcounter); testcounter+=1;

    ret = PTR_ERR(thread->task);
    if (IS_ERR(thread->task)) {
        pr_warn("dmatest: Failed to run thread %s\n",
                dma_chan_name(chan));
        int testcounter=0;

        pr_warn("dmatest_add_slave_threads[%d]\n", testcounter); testcounter+=1;
        kfree(thread);
        return ret;
    }

    return 1;
}

static bool is_threaded_test_run(struct dmatest_chan *dtc)
{
    int testcounter=0;

    pr_warn("is_threaded_test_run[%d]\n", testcounter); testcounter+=1;

    struct dmatest_slave_thread *thread;

    pr_warn("is_threaded_test_run[%d]\n", testcounter); testcounter+=1;

    int ret = false;

    list_for_each_entry(thread, &dtc->threads, node) {
        if (!thread->done)
            ret = true;
    }

    pr_warn("is_threaded_test_run[%d]\n", testcounter); testcounter+=1;

    return ret;
}

static int dmatest_add_slave_channels(struct ISI_MCDMA_channel *pchannel_p)
{
    unsigned int thread_count = 0;

    int testcounter=0;

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    pchannel_p->test_chan = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    if (!pchannel_p->test_chan) {
        pr_warn("dmatest: No memory for tx %s\n",
                dma_chan_name(pchannel_p->channel_p));
        return -ENOMEM;
    }

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;


    pchannel_p->test_chan->chan = pchannel_p->channel_p;

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    INIT_LIST_HEAD(&pchannel_p->test_chan->threads);

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    dmatest_add_slave_threads(pchannel_p);

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    thread_count += 1;

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    pr_info("dmatest: Started %u threads using %s \n",
            thread_count, dma_chan_name(pchannel_p->channel_p));

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    list_add_tail(&pchannel_p->test_chan->node, &dmatest_channels);

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

    wait_event(thread_wait, !is_threaded_test_run(pchannel_p->test_chan));

    pr_warn("dmatest_add_slave_channels[%d]\n", testcounter); testcounter+=1;

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
        printk(KERN_INFO "ii : cannot find a free dma slot.\n");
        return -EACCES;
    }

    if (copy_from_user(&minfo, uaddr, sizeof(dma_memory_handle_t))) {
        printk(KERN_INFO "ii : EACCES \n");
        return -EACCES;
    }

    pchannel_p->dmas[idx].size   = minfo.size;
    pchannel_p->dmas[idx].kaddr  = minfo.kaddr = dma_alloc_coherent(pchannel_p->dma_device_p,
                                                                 pchannel_p->dmas[idx].size,
                                                                 &pchannel_p->dmas[idx].handle,
                                                                 GFP_KERNEL);

    if (pchannel_p->dmas[idx].kaddr == NULL) {
        printk(KERN_INFO "ii : Dma memory allocation failed!\n");
        pchannel_p->dmas[idx].size = 0;
        pchannel_p->dmas[idx].kaddr = NULL;
        return -1;
    } else {
        pchannel_p->dmas[idx].paddr = minfo.paddr = virt_to_phys(pchannel_p->dmas[idx].kaddr);
    }

    minfo.index = idx; /* Pass the index to user so he can use it to map */

    printk(KERN_INFO "ii : Alloc size %d at index %d phaddr 0x%08x \n", minfo.size, idx, pchannel_p->dmas[idx].paddr);

    if (copy_to_user(uaddr, &minfo, sizeof(dma_memory_handle_t))) {
        printk(KERN_INFO "ii : EACCES \n");
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
        printk(KERN_INFO "ii : EACCES \n");
        return -EACCES;
    }

    if ((idx = find_dma_handle(pchannel_p, minfo.kaddr)) == -1) {
        printk(KERN_INFO "ii : requested dma memory could not be found. \n");
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
    //    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;

    return 0;
}

static ssize_t ISI_c_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset)
{

    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);
    return 0;
}

static ssize_t ISI_c_write (struct file *pfile, const char __user *buffer, size_t length, loff_t *offset)
{

    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);
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
    case IOCTL_Start_DMA: {

        err = dmatest_add_slave_channels(pchannel_p);
        if (err) {
            pr_err("ISI_MCDMA: Unable to add channels\n");
            return err;
        }
        return 1;
    }
    default: {
        printk(KERN_INFO "ii : deafult \n");
    }
    }

    return 0;
}

static int ISI_c_mmap(struct file *file_p, struct vm_area_struct *vma)
{

    //    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)file_p->private_data;

    //    return dma_mmap_coherent(pchannel_p->dma_device_p, vma,
    //                             pchannel_p->interface_p, pchannel_p->interface_phys_addr,
    //                             vma->vm_end - vma->vm_start);

    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);

    return 0;
}

static int ISI_c_close(struct inode *pinode, struct file *pfile)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;
    struct dma_device *dma_device = pchannel_p->channel_p->device;


    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);

    /* Stop all the activity when the channel is closed assuming this
             * may help if the application is aborted without normal closure
             */

    dma_device->device_terminate_all(pchannel_p->channel_p);
    return 0;

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


    //    /* Allocate DMA memory for the proxy channel interface.
    //             */
    //    pchannel_p->interface_p = (struct dma_channel_interface *)
    //            dmam_alloc_coherent(pchannel_p->dma_device_p,
    //                                sizeof(struct dma_channel_interface),
    //                                &pchannel_p->interface_phys_addr, GFP_KERNEL);
    //    printk(KERN_INFO "Allocating uncached memory at virtual address 0x%p, physical address 0x%p\n",
    //           pchannel_p->interface_p, (void *)pchannel_p->interface_phys_addr);

    //    if (!pchannel_p->interface_p) {
    //        dev_err(pchannel_p->dma_device_p, "DMA allocation error\n");
    //        return ERROR;
    //    }

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
    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);
    printk(KERN_INFO "ISI MCDMA module initialized\n");

    /* Create the transmit and receive channels.
             */
    rc = create_channel(pdev, &channels[TX_CHANNEL], "mcdma_tx", DMA_MEM_TO_DEV);

    if (rc)
        return rc;

    rc = create_channel(pdev, &channels[RX_CHANNEL], "mcdma_rx", DMA_DEV_TO_MEM);
    if (rc)
        return rc;

    return 0;
}

/* Exit the mcdma device driver module.
 */
static int ISI_p_MCDMA_remove(struct platform_device *pdev)
{
    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);

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
    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);

    return platform_driver_register(&ISI_MCDMA_driver);
}

static void __exit ISI_MCDMA_exit(void)
{
    printk(KERN_ALERT "Inside the %s function\n", __FUNCTION__);
    platform_driver_unregister(&ISI_MCDMA_driver);
}

module_init(ISI_MCDMA_init);
module_exit(ISI_MCDMA_exit);

MODULE_AUTHOR("Matthew Mesropian");
MODULE_DESCRIPTION("ISI MCDMA");
MODULE_LICENSE("GPL v2");
