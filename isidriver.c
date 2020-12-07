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
#include <linux/interrupt.h>
#include <linux/of_irq.h>

MODULE_LICENSE("GPL");


#define DMA_MAPPING (1 << 7)

/* Innovative Magic Number */
#define ISI_IOCTL_MAGIC_NUM  'm'

#define ISI_IOCTL_ALLOC_DMA_MEMORY    _IOWR(ISI_IOCTL_MAGIC_NUM, 10,dma_memory_handle_t*)
#define ISI_IOCTL_FREE_DMA_MEMORY     _IOR (ISI_IOCTL_MAGIC_NUM, 11,dma_memory_handle_t*)
#define ISI_IOCTL_START_DMA           _IO  (ISI_IOCTL_MAGIC_NUM, 12)
#define ISI_IOCTL_WAIT_INTERRUPT      _IO  (ISI_IOCTL_MAGIC_NUM, 13)
#define ISI_IOCTL_INTERRUPT_KICK      _IO  (ISI_IOCTL_MAGIC_NUM, 14)



//static DECLARE_WAIT_QUEUE_HEAD(thread_wait);

static unsigned int test_buf_size = 8192;

static LIST_HEAD(MCDMA_channels);

#define DRIVER_NAME 		       "ISI_MCDMA"
#define NUMBER_OF_CHANNELS_PER_DIR     16
#define ERROR 			       -1

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

// Wait Queue
wait_queue_head_t _wait_queue;
int IRQ_Kick_Flag;


/* The following data structure represents a single channel of DMA, transmit or receive in the case
 * when using AXI DMA.  It contains all the data to be maintained for the channel.
 */
struct ISI_MCDMA_channel {
    struct device *char_device_p;			/* character device support */
    struct device *dma_device_p;
    dev_t dev_node;
    struct cdev cdev;
    struct class *class_p;
    struct dma_chan *channel_p;
    struct dma_async_tx_descriptor *desc;			/* dma support */
    struct completion cmp;
    dma_cookie_t _cookie;
    enum dma_ctrl_flags flags;
    struct scatterlist sglist[XILINX_BD_CNT];
    dma_memory_handle_t dmas[XILINX_BD_CNT];
    wait_queue_head_t *WaitQueue;
    int _IRQ;
    int _ID;
    u32 direction;					/* DMA_MEM_TO_DEV or DMA_DEV_TO_MEM */
};

struct ISI_Device_Context {
    /* Allocate the channels for this example statically rather than dynamically for simplicity.
 */
    struct ISI_MCDMA_channel Rx_Channels[NUMBER_OF_CHANNELS_PER_DIR];
    struct ISI_MCDMA_channel Tx_Channels[NUMBER_OF_CHANNELS_PER_DIR];

};
struct ISI_Device_Context *isi_device_context;






static void MCDMA_slave_callback(void *completion)
{
    complete(completion);

    pr_warn("DMA Callback....");
}

static int MCDMA_Start_Channel_Transfer(struct ISI_MCDMA_channel *pchannel_p)
{
    struct dma_chan *chan;
    int ret;
    int bd_cnt = XILINX_BD_CNT;
    int i;
    u8 align = 0;
    struct dma_device *dma_dev;

    ret = -ENOMEM;
    /* Ensure that all previous reads are complete */
    smp_rmb();
    chan = pchannel_p->channel_p;
    dma_dev = chan->device;

    set_user_nice(current, 10);
    pchannel_p->flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

    if (dma_dev->copy_align > align)
        align = dma_dev->copy_align;

    if (1 << align > test_buf_size) {
        pr_err("%u-byte buffer too small for %d-byte alignmen_pchannel_pt\n",
               test_buf_size, 1 << align);
        return ERROR;
    }

    sg_init_table(pchannel_p->sglist, bd_cnt);

    for (i = 0; i < bd_cnt; i++) {
        sg_dma_address(&pchannel_p->sglist[i]) =  pchannel_p->dmas[i].handle;
        sg_dma_len(&pchannel_p->sglist[i]) = test_buf_size;
    }

    pchannel_p->desc = dma_dev->device_prep_slave_sg(chan, pchannel_p->sglist, bd_cnt, pchannel_p->direction, pchannel_p->flags, NULL);

    if (!pchannel_p->desc) {
        for (i = 0; i < bd_cnt; i++)
            pr_warn("[ISI]: prep error with _off=0x%x ",
                    test_buf_size);
        msleep(100);
    }
    init_completion(&pchannel_p->cmp);
    pchannel_p->desc->callback = MCDMA_slave_callback;
    pchannel_p->desc->callback_param = &pchannel_p->cmp;
    pchannel_p->_cookie = pchannel_p->desc->tx_submit(pchannel_p->desc);

    if (dma_submit_error(pchannel_p->_cookie)) {
        pr_warn("[ISI]: submit error %d with _off=0x%x ",
                pchannel_p->_cookie,  test_buf_size);
        msleep(100);
    }
    dma_async_issue_pending(chan);

    pr_warn("[ISI]: DMA issued for channel[%d] ",
            pchannel_p->_ID);

    return 0;
}

/**
 * find_dma_handle - Helper that finds the context index from the given address
 * @inode:		Inode pointernode
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

static int isi_wait_interrupt_ioctl(struct file *pfile, void __user *uaddr)
{
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;


    IRQ_Kick_Flag = 0;
    wait_event_interruptible(*pchannel_p->WaitQueue, IRQ_Kick_Flag==1);
        // if (signal_pending(current))
        //         return -ERESTARTSYS;

        return 0;
}

/* Perform I/O control to start a DMA transfer.
 */
static long ISI_c_ioctl(struct file *pfile, unsigned int cmd , unsigned long arg)
{

    int err;
    struct ISI_MCDMA_channel *pchannel_p = (struct ISI_MCDMA_channel *)pfile->private_data;

    switch (cmd) {
    case ISI_IOCTL_ALLOC_DMA_MEMORY: {
        return isi_allocate_dma_memory_ioctl(pfile, (void __user *)arg);
    }
    case ISI_IOCTL_FREE_DMA_MEMORY: {
        return isi_free_dma_memory_ioctl(pfile, (void __user *)arg);
    }
    case ISI_IOCTL_START_DMA: {

        err = MCDMA_Start_Channel_Transfer(pchannel_p);
        if (err) {
            pr_err("ISI_MCDMA: Unable to start channel\n");
            return err;
        }
        return 1;
    }
    case ISI_IOCTL_WAIT_INTERRUPT: {
        return isi_wait_interrupt_ioctl(pfile, (void __user *)arg);
    }
    case ISI_IOCTL_INTERRUPT_KICK: {
      IRQ_Kick_Flag = 1;
      wake_up_interruptible(pchannel_p->WaitQueue);
      break;
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
static int create_channel(struct platform_device *pdev, struct ISI_MCDMA_channel *pchannel_p, int channel_id, char *name, u32 direction)
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
    pchannel_p->_ID = channel_id;
    pchannel_p->WaitQueue = &_wait_queue;

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

/**
 * xilinx_mcdma_irq_handler - MCDMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the Xilinx MCDMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t irq_handler(int irq, void *data)
{
    struct ISI_MCDMA_channel *channel = (struct ISI_MCDMA_channel *)data;
    enum dma_status status;

    pr_warn("______________________INTERRUPT[%d]______________________", irq);
    printk(KERN_INFO "irq[%d] with channel[%d]\n", channel->_IRQ, channel->_ID);

    status = dma_async_is_tx_complete(channel->channel_p, channel->_cookie, NULL, NULL);

    if (status != DMA_COMPLETE) {
        pr_warn("______DMA NOT COMPLETE_____");
    }
    else
      {
        pr_warn("______DMA COMPLETED_____");
        IRQ_Kick_Flag = 1;
        wake_up_interruptible(channel->WaitQueue);
      }

    return IRQ_HANDLED;
}

/*
 */
static int Init_ISI_IRQ(struct device_node *child, struct ISI_MCDMA_channel *channel, char * irq_name)
{
    int rc;

    channel->_IRQ = irq_of_parse_and_map(child, channel->_ID);

    printk(KERN_INFO "irq[%d] init with channel[%d]\n", channel->_IRQ, channel->_ID);

    rc = request_irq(channel->_IRQ, irq_handler,
                     IRQF_SHARED, irq_name, channel);
    if (rc) {
        printk(KERN_INFO "unable to request IRQ %d\n", channel->_IRQ);
        return rc;
    }
    return rc;
}

/* Prob the mcdma platform driver
 */
static int ISI_p_MCDMA_probe(struct platform_device *pdev)
{
    struct device_node *child;
    int rc,i;

    isi_device_context = kzalloc(sizeof(struct ISI_Device_Context), GFP_KERNEL);

    //Init the IRQ wait queue
    init_waitqueue_head(&_wait_queue);

    /* Initialize the channels */
    for_each_child_of_node(pdev->dev.of_node, child) {
        if(of_device_is_compatible(child, "xlnx,axi-dma-mm2s-channel")){
            for(i=0;i<NUMBER_OF_CHANNELS_PER_DIR;i++){
                char tx_cdevice_name[32] = "mcdma_tx_";
                char tx_irqdevice_name[32] = "MCDMA_IRQ_TX_";
                char dummy[32] = "";
                char * name1;
                char * name2;
                sprintf(dummy, "%d", i);
                name1 = strcat(tx_cdevice_name, dummy);
                pr_warn("[Creating Tx Channel [%d] With Name [%s]", i, name1);
                rc = create_channel(pdev, &isi_device_context->Tx_Channels[i], i, name1, DMA_MEM_TO_DEV);
                if (rc)
                {
                    printk(KERN_INFO "[ISI] unable to create char device for TX[%d]\n", i);
                    return rc;
                }
              // Only IRQ for channel zero will be handled because the other channels are linked with this irq
              if(i==0) {
                name2 = strcat(tx_irqdevice_name, dummy);
                rc = Init_ISI_IRQ(child, &isi_device_context->Tx_Channels[i], name2);
                if (rc)
                {
                    printk(KERN_INFO "[ISI] unable to request IRQ for TX[%d]\n", i);
                    return rc;
                }
              }
            }
        }
        else {
            for(i=0;i<NUMBER_OF_CHANNELS_PER_DIR;i++){
                char rx_cdevice_name[32] = "mcdma_rx_";
                char rx_irqdevice_name[32] = "MCDMA_IRQ_RX_";
                char dummy[32] = "";
                char * name1;
                char * name2;
                sprintf(dummy, "%d", i);
                name1 = strcat(rx_cdevice_name, dummy);
                pr_warn("[Creating Rx Channel [%d] With Name [%s]", i, name1);
                rc = create_channel(pdev, &isi_device_context->Rx_Channels[i], i, name1, DMA_DEV_TO_MEM);
                if (rc)
                {
                    printk(KERN_INFO "[ISI] unable to create char device for RX[%d]\n", i);
                    return rc;
                }
                // Only IRQ for channel zero will be handled because the other channels are linked with this irq
                if(i==0) {
                name2 = strcat(rx_irqdevice_name, dummy);
                rc = Init_ISI_IRQ(child,& isi_device_context->Rx_Channels[i], name2);
                if (rc){
                    printk(KERN_INFO "[ISI] unable to request IRQ for RX\n");
                    return rc;
                }
              }
            }
        }
    }

    printk(KERN_INFO "ISI MCDMA module initialized\n");
    return 0;
}


/* Exit the mcdma device driver module.
 */
static int ISI_p_MCDMA_remove(struct platform_device *pdev)
{
    int i;

    for(i=0;i<NUMBER_OF_CHANNELS_PER_DIR;i++){
        if (isi_device_context->Tx_Channels[i]._IRQ > 0)
            free_irq(isi_device_context->Tx_Channels[i]._IRQ, &isi_device_context->Tx_Channels[i]);
        if (isi_device_context->Rx_Channels[i]._IRQ > 0)
            free_irq(isi_device_context->Rx_Channels[i]._IRQ, &isi_device_context->Rx_Channels[i]);
    }

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
