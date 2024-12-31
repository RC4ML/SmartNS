#include "smartns_kernel.h"
#include "../lib/smartns_abi.h"

//  Define the module metadata.

MODULE_AUTHOR("cxz66666");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("A module to simulate SmartNS kernel module");
MODULE_VERSION("0.1");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0) && !defined(HAVE_UNLOCKED_IOCTL)
#define HAVE_UNLOCKED_IOCTL 1
#endif 

//  The device number, automatically set. The message buffer and current message
//  size. The number of device opens and the device class struct pointers.
static int    majorNumber;
static DEFINE_MUTEX(ioMutex);

//  Prototypes for our device functions.
static int     smartns_open(struct inode *, struct file *);
static int     smartns_release(struct inode *, struct file *);
static long smartns_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg);
static long smartns_unlocked_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

struct ib_device *global_device = NULL;

struct ib_client smartns_ib_client;

struct socket *global_tcp_socket = NULL;


struct smartns_qp_handler global_qp_handler;
static DEFINE_MUTEX(qp_mutex);


unsigned int current_mtu;

//  Create the file operations instance for our driver.
static struct file_operations fops =
{
   .open = smartns_open,
#ifdef HAVE_UNLOCKED_IOCTL
    .unlocked_ioctl = smartns_unlocked_ioctl,
#else
    .ioctl = smartns_ioctl,
#endif  
   .release = smartns_release,
};



int smartns_add_device(struct ib_device *dev) {
    struct ib_port_attr port_attr;
    struct PingPongInfo response;
    int response_size;
    DECLARE_WAIT_QUEUE_HEAD(recv_wait);

    pr_info("%s: device show\n", dev->name);

    if (strcmp(dev->name, "mlx5_0") == 0) {
        global_device = dev;
        pr_info("%s: device added\n", dev->name);

        ib_query_port(dev, 1, &port_attr);
        current_mtu = 128 << port_attr.active_mtu;

        pr_info("%-20s : %d\n", "Max Outreads", dev->attrs.max_qp_rd_atom);
        pr_info("%-20s : %d\n", "Max Pkeys", dev->attrs.max_pkeys);
        pr_info("%-20s : %d\n", "Atomic Capacity", dev->attrs.atomic_cap);
        pr_info("%-20s : %d\n", "CUR MTU", 128 << port_attr.active_mtu);
        pr_info("%-20s : %d\n", "CUR SPEED", port_attr.active_speed);

        if (smartns_create_qp_and_send_to_bf(&global_qp_handler)) {
            pr_err("%s: failed to create qp and send to bf\n", MODULE_NAME);
            return -EIO;
        }
        wait_event_timeout(recv_wait, \
            !skb_queue_empty(&global_tcp_socket->sk->sk_receive_queue), \
            5 * HZ);

        if (!skb_queue_empty(&global_tcp_socket->sk->sk_receive_queue)) {
            response_size = tcp_client_receive(global_tcp_socket, (char *)&response, MSG_DONTWAIT);
            if (response_size != sizeof(struct PingPongInfo)) {
                pr_err("%s: recv inlegal bf response %d\n", MODULE_NAME, response_size);
                return -EIO;
            }
            pr_info("%s: TCP received bf response\n", MODULE_NAME);
            smartns_init_qp(&global_qp_handler, &response);
        } else {
            pr_err("%s: failed to receive bf response\n", MODULE_NAME);
            return -EIO;
        }
    }
    return 0;
}

void smartns_remove_device(struct ib_device *dev, void *client_data) {
    if (strcmp(dev->name, "mlx5_0") == 0) {
        global_device = NULL;
        pr_info("%s: device removed\n", dev->name);
    }
}

static int __init mod_init(void) {
    pr_info("%s: module loaded at 0x%p\n", MODULE_NAME, mod_init);

    //  Create a mutex to guard io operations.
    mutex_init(&ioMutex);
    mutex_init(&qp_mutex);

    //  Register the device, allocating a major number.
    majorNumber = register_chrdev(0 /* i.e. allocate a major number for me */, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        pr_alert("%s: failed to register a major number\n", MODULE_NAME);
        return majorNumber;
    }
    pr_info("%s: registered correctly with major number %d\n", MODULE_NAME, majorNumber);

    if (tcp_connect_to_bf()) {
        pr_err("%s: failed to connect to bf\n", MODULE_NAME);
        return -EIO;
    }

    smartns_ib_client.name = "smartns";
    smartns_ib_client.add = smartns_add_device;
    smartns_ib_client.remove = smartns_remove_device;
    ib_register_client(&smartns_ib_client);

    return 0;
}

static void __exit mod_exit(void) {
    pr_info("%s: unloading...\n", MODULE_NAME);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    ib_unregister_client(&smartns_ib_client);

    mutex_destroy(&ioMutex);
    mutex_destroy(&qp_mutex);
    pr_info("%s: device unregistered\n", MODULE_NAME);
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int smartns_open(struct inode *inodep, struct file *filep) {
    smartns_info_t *info = kzalloc(sizeof(smartns_info_t), GFP_KERNEL);
    //  Try and lock the mutex.
    mutex_lock(&ioMutex);
    if (!info) {
        pr_err("%s: failed to allocate memory for smartns_info_t\n", MODULE_NAME);
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&info->list);
    mutex_init(&info->lock);
    info->pid = current->pid;
    info->tgid = current->tgid;
    filep->private_data = info;
    pr_info("%s: tgid %d pid %d has been opened\n", MODULE_NAME, info->tgid, info->pid);
    mutex_unlock(&ioMutex);
    return 0;
}


/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int smartns_release(struct inode *inodep, struct file *filep) {
    smartns_info_t *info = filep->private_data;
    pid_t pid = info->pid, tgid = info->tgid;

    mutex_lock(&ioMutex);

    if (!info) {
        pr_err("%s: filep->private_data is NULL\n", MODULE_NAME);
        return -EIO;
    }
    if (current->tgid != tgid) {
        pr_err("%s: tgid %d pid %d is not allowed to close this file\n", MODULE_NAME, current->tgid, current->pid);
        return -EPERM;
    }

    kfree(info);
    filep->private_data = NULL;

    smartns_free_qp(&global_qp_handler);

    if (global_tcp_socket) {
        sock_release(global_tcp_socket);
        global_tcp_socket = NULL;
    }

    pr_info("%s: tgid %d pid %d successfully closed\n", MODULE_NAME, tgid, pid);

    mutex_unlock(&ioMutex);
    return 0;
}

static long smartns_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg) {
    int ret = 0;
    size_t send_offset;
    int ne, index;
    unsigned int size;
    smartns_info_t *info = filep->private_data;
    void __user *param = (void __user *)arg;
    struct SMARTNS_KERNEL_COMMON_PARAMS *common_params;
    size_t begin_tsc, cur_tsc;

    if (_IOC_TYPE(cmd) != SMARTNS_IOCTL) {
        pr_err("%s: invalid ioctl type\n", MODULE_NAME);
        return -EINVAL;
    }

    if (!info) {
        pr_err("%s: filep->private_data is NULL\n", MODULE_NAME);
        return -EIO;
    }

    if (current->tgid != info->tgid) {
        pr_err("%s: tgid %d pid %d is not allowed to ioctl this file\n", MODULE_NAME, current->tgid, current->pid);
        return -EACCES;
    }

    size = _IOC_SIZE(cmd);

    mutex_lock(&qp_mutex);

    send_offset = offset_handler_offset(&global_qp_handler.send_offset_handler) + global_qp_handler.local_buf;
    if (copy_from_user((void *)send_offset, param, size)) {
        pr_err("%s: failed to copy from user\n", MODULE_NAME);
        ret = -EFAULT;
    }
    common_params = (struct SMARTNS_KERNEL_COMMON_PARAMS *)send_offset;
    common_params->pid = current->pid;
    common_params->tgid = current->tgid;
    common_params->cmd = cmd;

    global_qp_handler.send_sge_list[0].addr = global_qp_handler.local_dma_buf + offset_handler_offset(&global_qp_handler.send_offset_handler);
    global_qp_handler.send_sge_list[0].length = size;
    global_qp_handler.send_wr[0].wr_id = offset_handler_offset(&global_qp_handler.send_offset_handler);
    global_qp_handler.send_wr[0].next = NULL;
    if (ib_post_send(global_qp_handler.qp, global_qp_handler.send_wr, NULL)) {
        pr_err("%s: failed to post send\n", MODULE_NAME);
        return -EFAULT;
    }

    ne = ib_poll_cq(global_qp_handler.send_cq, SMARTNS_CQ_POLL_BATCH, global_qp_handler.send_wc);
    index = 0;
    for (;index < ne;index++) {
        if (global_qp_handler.send_wc[index].status != IB_WC_SUCCESS) {
            pr_err("%s: failed to send data\n", MODULE_NAME);
            return -EFAULT;
        }
    }

    offset_handler_step(&global_qp_handler.send_offset_handler);

    begin_tsc = get_cycles();
    while (1) {
        cur_tsc = get_cycles();
        if (cur_tsc - begin_tsc > 2 * 1000000000) {
            pr_err("%s: failed to recv data\n", MODULE_NAME);
            return -EFAULT;
        }
        ne = ib_poll_cq(global_qp_handler.recv_cq, SMARTNS_CQ_POLL_BATCH, global_qp_handler.recv_wc);
        if (ne > 0) {
            if (ne != 1) {
                pr_err("%s: inlegal recv wc num %d\n", MODULE_NAME, ne);
                return -EFAULT;
            }
            if (global_qp_handler.recv_wc[0].status != IB_WC_SUCCESS || global_qp_handler.recv_wc[0].byte_len != size) {
                pr_err("%s: failed to recv data\n", MODULE_NAME);
                return -EFAULT;
            }
            if (copy_to_user(param, (void *)(global_qp_handler.local_buf + global_qp_handler.recv_wc[0].wr_id), size)) {
                pr_err("%s: failed to copy to user\n", MODULE_NAME);
                return -EFAULT;
            }

            global_qp_handler.recv_sge_list[0].addr = global_qp_handler.local_dma_buf + offset_handler_offset(&global_qp_handler.recv_offset_handler);
            global_qp_handler.recv_sge_list[0].length = SMARTNS_MSG_SIZE;
            global_qp_handler.recv_wr[0].wr_id = offset_handler_offset(&global_qp_handler.recv_offset_handler);
            global_qp_handler.recv_wr[0].next = NULL;
            if (ib_post_recv(global_qp_handler.qp, global_qp_handler.recv_wr, NULL)) {
                pr_err("%s: failed to post recv\n", MODULE_NAME);
                return -EFAULT;
            }
            offset_handler_step(&global_qp_handler.recv_offset_handler);

            break;
        }
    }

    mutex_unlock(&qp_mutex);
    return ret;
}

#ifdef HAVE_UNLOCKED_IOCTL
static long smartns_unlocked_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    return smartns_ioctl(0, filep, cmd, arg);
}
#endif  

module_init(mod_init);
module_exit(mod_exit);