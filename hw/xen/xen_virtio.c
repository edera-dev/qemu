/*
 *  xen paravirt virtio backend
 *
 *  Wei Liu  <liuw@liuw.name>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>

#include <xs.h>
#include <xenctrl.h>
#include <xen/io/xenbus.h>

#include "hw.h"
#include "qemu-error.h"
#include "net.h"
#include "net/util.h"
#include "virtio.h"
#include "virtio-blk.h"
#include "virtio-net.h"
#include "blockdev.h"
#include "net.h"
#include "xen_backend.h"

/* From Linux's linux/virtio_xenbus.h */

/* Following macros define commands used by front end and back end.
 * These are also offsets for specific options in config space. */

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_XENBUS_HOST_FEATURES        0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_XENBUS_GUEST_FEATURES       4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_XENBUS_QUEUE_PFN            8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_XENBUS_QUEUE_NUM            12

/* A 16-bit r/w queue selector */
#define VIRTIO_XENBUS_QUEUE_SEL            14

/* A 16-bit r/w queue notifier */
#define VIRTIO_XENBUS_QUEUE_NOTIFY         16

/* An 8-bit device status register.  */
#define VIRTIO_XENBUS_STATUS               18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_XENBUS_ISR                  19

/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_XENBUS_ISR_CONFIG           0x2

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_XENBUS_CONFIG(dev)          20

/* Virtio Xenbus ABI version, this must match exactly */
#define VIRTIO_XENBUS_ABI_VERSION          0

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_XENBUS_QUEUE_ADDR_SHIFT     12

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize. */
#define VIRTIO_XENBUS_VRING_ALIGN          4096

/* Avoid collision in bootindex, which is useless in PV case */
#define XEN_VIRTIO_NET_REGION              100
#define XEN_VIRTIO_BLK_REGION              200

struct virtio_config_page {
    unsigned char  config[256];            /* config space */
    int write;                             /* is write? */
    int size;                              /* operand size */
    int offset;                 /* offset in config space */
    int be_active;              /* Huh... */
};

struct XenVirtioDev {
    struct XenDevice  xendev;  /* must be first */
    VirtIODevice     *vdev;
    uint32_t          host_features;

    /* For virtio blk */
    BlockConf         block;
    char             *params;
    char             *mode;
    char             *type;
    char             *dev;
    char             *devtype;
    const char       *fileproto;
    const char       *filename;

    /* For virtio net */
    NICConf           nic;
    virtio_net_conf   net;

    /* For configuration */
    void             *page;
    int               conf_page_ref;

    /* Used to notify guest's virtqueue(s) */
    XenEvtchn         notify_evtchndev;
    int               notify_local_port;
    int               notify_remote_port;
};
typedef struct XenVirtioDev XenVirtioDev;

/* ----------------------------------------------- */
static int virtio_notify_init(XenVirtioDev *xvdev)
{
    /* This pair is used by notification code */
    xvdev->notify_evtchndev = xen_xc_evtchn_open(NULL, 0);
    if (xvdev->notify_evtchndev == XC_HANDLER_INITIAL_VALUE) {
        xen_be_printf(NULL, 0, "can't open evtchn device\n");
        return -1;
    }
    fcntl(xc_evtchn_fd(xvdev->notify_evtchndev), F_SETFD, FD_CLOEXEC);
    xvdev->notify_local_port  = -1;
    xvdev->notify_remote_port = -1;

    return 0;
}

static int virtio_bind_notify_evtchn(XenVirtioDev *xvdev)
{
    if (xvdev->notify_local_port != -1) {
        return -1;
    }

    /* Take extra care of our notification channel */
    xvdev->notify_local_port = xc_evtchn_bind_interdomain
        (xvdev->notify_evtchndev, xvdev->xendev.dom,
         xvdev->notify_remote_port);
    if (xvdev->notify_local_port == -1) {
        xen_be_printf(&xvdev->xendev, 0,
                      "xc_evtchn_bind_interdomain failed\n");
        return -1;
    }
    xen_be_printf(&xvdev->xendev, 2, "bind notify evtchn port %d\n",
                  xvdev->notify_local_port);
    /* qemu_set_fd_handler(xc_evtchn_fd(xvdev->notify_evtchndev), */
    /*                     virtio_handle_incorrect_event, NULL, xvdev); */
    return 0;
}

static void virtio_unbind_notify_evtchn(XenVirtioDev *xvdev)
{
    if (xvdev->notify_local_port == -1) {
        return;
    }
    xc_evtchn_unbind(xvdev->notify_evtchndev, xvdev->notify_local_port);
    xen_be_printf(&xvdev->xendev, 2, "unbind evtchn port %d\n",
                  xvdev->notify_local_port);
    xvdev->notify_local_port = -1;
}


/* ----------------------------------------------- */

static void virtio_xenbus_notify(void *opaque, uint16_t vector)
{
    XenVirtioDev *xv_dev = opaque;

    xc_evtchn_notify(xv_dev->notify_evtchndev, xv_dev->notify_local_port);
}

static unsigned virtio_xenbus_get_features(void *opaque)
{
    XenVirtioDev *xv_dev = opaque;

    return xv_dev->host_features;
}

static void virtio_xenbus_guest_notifier_read(void *opaque)
{
    VirtQueue *vq = opaque;

    /* XXX if we are here, we must have some event, right? */
    virtio_irq(vq);
}

static int virtio_xenbus_set_guest_notifier(void *opaque, int n, bool assign)
{
    XenVirtioDev *xv_dev = opaque;
    VirtIODevice *vdev = xv_dev->vdev;
    VirtQueue *vq = virtio_get_queue(vdev, n);

    if (assign) {
        qemu_set_fd_handler(xc_evtchn_fd(xv_dev->notify_evtchndev),
                            virtio_xenbus_guest_notifier_read, NULL, vq);
    } else {
        qemu_set_fd_handler(xc_evtchn_fd(xv_dev->notify_evtchndev),
                            NULL, NULL, NULL);
    }

    return 0;
}

static int virtio_xenbus_set_guest_notifiers(void *opaque, bool assign)
{
    XenVirtioDev *xv_dev = opaque;
    VirtIODevice *vdev = xv_dev->vdev;
    int r, n;

    for (n = 0; n < VIRTIO_XENBUS_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_xenbus_set_guest_notifier(opaque, n, assign);
        if (r < 0) {
            goto assign_error;
        }
    }

    return 0;

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
    while (--n >= 0) {
        virtio_xenbus_set_guest_notifier(opaque, n, !assign);
    }
    return r;
}

static int virtio_xenbus_set_host_notifier(void *opaque, int n, bool assign)
{
    /* We use evtchn to deliver event, which has already been set by
     * setting guest notifiers */

    return 0;
}

static const VirtIOBindings virtio_xenbus_bindings = {
    .notify = virtio_xenbus_notify,
    .get_features = virtio_xenbus_get_features,
    .set_host_notifier = virtio_xenbus_set_host_notifier,
    .set_guest_notifiers = virtio_xenbus_set_guest_notifiers,
};

static void virtio_init_xenbus(XenVirtioDev *xv_dev, VirtIODevice *vdev)
{
    xv_dev->vdev = vdev;

    virtio_bind_device(vdev, &virtio_xenbus_bindings, xv_dev);
    xv_dev->host_features |= 0x1 << VIRTIO_F_NOTIFY_ON_EMPTY;
    xv_dev->host_features |= 0x1 << VIRTIO_F_BAD_FEATURE;
    xv_dev->host_features = vdev->get_features(vdev, xv_dev->host_features);
}

static VirtIODevice *xen_virtio_net_init(XenVirtioDev *xv_dev)
{
    DeviceState ds;
    char *mac;
    VirtIODevice *vdev;

    /* Enable as many features as possible.
     * Disable MRG_RXBUF and CTRL_* features. */
    xv_dev->host_features |= (1 << VIRTIO_NET_F_CSUM);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GUEST_CSUM);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_MAC);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GSO);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GUEST_TSO4);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GUEST_TSO6);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GUEST_ECN);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_GUEST_UFO);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_HOST_TSO4);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_HOST_TSO6);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_HOST_ECN);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_HOST_UFO);
    xv_dev->host_features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    xv_dev->host_features |= (1 << VIRTIO_NET_F_STATUS);
    xv_dev->host_features &= ~(1 << VIRTIO_NET_F_CTRL_VQ);
    xv_dev->host_features &= ~(1 << VIRTIO_NET_F_CTRL_RX);
    xv_dev->host_features &= ~(1 << VIRTIO_NET_F_CTRL_VLAN);
    xv_dev->host_features &= ~(1 << VIRTIO_NET_F_CTRL_RX_EXTRA);

    /* prepare DeviceState */
    memset(&ds, 0, sizeof(ds));

    ds.info = qemu_mallocz(sizeof(DeviceInfo));
    ds.info->name = "xen";

    /* prepare NICConf */
    mac = xenstore_read_be_str(&xv_dev->xendev, "mac");

    if (mac == NULL) {
        fprintf(stderr, "unable to set mac\n");
        exit(-1);
    }

    if (net_parse_macaddr(xv_dev->nic.macaddr.a, mac) < 0) {
        fprintf(stderr, "unable to parse mac\n");
        exit(-1);
    }

    xv_dev->nic.vlan = qemu_find_vlan(xv_dev->xendev.dev, 1);
    xv_dev->nic.peer = NULL;

    /* prepare virtio_net_conf, zero-out */
    memset(&xv_dev->net, 0, sizeof(xv_dev->net));

    xv_dev->nic.bootindex = xv_dev->xendev.dev + XEN_VIRTIO_NET_REGION;

    vdev = virtio_net_init(&ds, &xv_dev->nic, &xv_dev->net);

    qemu_free(ds.info);

    return vdev;
}

static VirtIODevice *xen_virtio_blk_init(XenVirtioDev *xv_dev)
{
    DeviceState ds;
    VirtIODevice *vdev;
    char *h = NULL;
    int qflags;

    memset(&ds, 0, sizeof(ds));

    xv_dev->params = xenstore_read_be_str(&xv_dev->xendev, "params");
    if (xv_dev->params != NULL) {
        h = strchr(xv_dev->params, ':');
    } else {
        fprintf(stderr, "unable to get params from xenstore\n");
        exit(-1);
    }
    if (h != NULL) {
        xv_dev->fileproto = xv_dev->params;
        xv_dev->filename  = h+1;
        *h = 0;
    } else {
        xv_dev->fileproto = "<unset>";
        xv_dev->filename  = xv_dev->params;
    }

    if (!strcmp("aio", xv_dev->fileproto)) {
        xv_dev->fileproto = "raw";
    }

    xv_dev->mode = xenstore_read_be_str(&xv_dev->xendev, "mode");
    xv_dev->type = xenstore_read_be_str(&xv_dev->xendev, "type");
    xv_dev->dev = xenstore_read_be_str(&xv_dev->xendev, "dev");
    xv_dev->devtype = xenstore_read_be_str(&xv_dev->xendev, "device-type");

    if (xv_dev->params == NULL ||
        xv_dev->mode == NULL   ||
        xv_dev->type == NULL   ||
        xv_dev->dev == NULL) {
        goto out_error;
    }

    /* read-only ? */
    if (strcmp(xv_dev->mode, "w") == 0) {
        qflags = BDRV_O_RDWR;
    } else {
        qflags = 0;
    }

    /* setup via xenbus -> create new block driver instance */
    xen_be_printf(&xv_dev->xendev, 2, "create new bdrv (xenbus setup)\n");
    xv_dev->block.bs = bdrv_new(xv_dev->dev);
    if (xv_dev->block.bs) {
        if (bdrv_open(xv_dev->block.bs, xv_dev->filename, qflags,
                      bdrv_find_whitelisted_format(xv_dev->fileproto)) != 0) {
            bdrv_delete(xv_dev->block.bs);
            xv_dev->block.bs = NULL;
        }
    }
    if (!xv_dev->block.bs) {
        goto out_error;
    }


    /* copied from virtio-pci blk init macro */
    xv_dev->block.logical_block_size = 512;
    xv_dev->block.physical_block_size = 512;
    xv_dev->block.min_io_size = 0;
    xv_dev->block.opt_io_size = 0;
    xv_dev->block.discard_granularity = 0;

    xv_dev->block.bootindex = xv_dev->xendev.dev + XEN_VIRTIO_BLK_REGION;

    vdev = virtio_blk_init(&ds, &xv_dev->block);
    return vdev;

out_error:
    qemu_free(xv_dev->params);
    xv_dev->params = NULL;
    qemu_free(xv_dev->mode);
    xv_dev->mode = NULL;
    qemu_free(xv_dev->type);
    xv_dev->type = NULL;
    qemu_free(xv_dev->dev);
    xv_dev->dev = NULL;
    qemu_free(xv_dev->devtype);
    xv_dev->devtype = NULL;
    return NULL;
}

static void virtio_alloc(struct XenDevice *xendev)
{
    XenVirtioDev *xv_dev = DO_UPCAST(XenVirtioDev, xendev, xendev);
    VirtIODevice *vdev = NULL;

    if (!strncmp(xendev->type, "virtio-net", sizeof("virtio-net"))) {
        vdev = xen_virtio_net_init(xv_dev);
    } else if (!strncmp(xendev->type, "virtio-blk", sizeof("virtio-blk"))) {
        vdev = xen_virtio_blk_init(xv_dev);
    } else if (!strncmp(xendev->type, "virtio-dummy", sizeof("virtio-dummy"))) {
        exit(-1);
    }

    if (vdev) {
        virtio_init_xenbus(xv_dev, vdev);
    } else {
        fprintf(stderr, "Unable to create vdev\n");
        exit(-1);
    }
}

static int virtio_init(struct XenDevice *xendev)
{
    /* Nothing to do. */
    return 0;
}

static int virtio_connect(struct XenDevice *xendev)
{
    XenVirtioDev *xvdev = container_of(xendev, XenVirtioDev, xendev);
    if (virtio_notify_init(xvdev) == -1) {
        return -1;
    }

    if (xenstore_read_fe_int(&xvdev->xendev, "page-gref",
                             &xvdev->conf_page_ref) == -1) {
        return -1;
    }

    if (xenstore_read_fe_int(&xvdev->xendev, "event-channel",
                             &xvdev->xendev.remote_port) == -1) {
        return -1;
    }

    if (xenstore_read_fe_int(&xvdev->xendev, "event-channel2",
                             &xvdev->notify_remote_port) == -1) {
        return -1;
    }

    xvdev->page = xc_gnttab_map_grant_ref(xvdev->xendev.gnttabdev,
                                          xvdev->xendev.dom,
                                          xvdev->conf_page_ref,
                                          PROT_READ|PROT_WRITE);
    if (!xvdev->page) {
        return -1;
    }

    /* Bind handlers */
    virtio_bind_notify_evtchn(xvdev);
    xen_be_bind_evtchn(&xvdev->xendev);

    return 0;
}

static void virtio_disconnect(struct XenDevice *xendev)
{
    XenVirtioDev *xvdev = container_of(xendev, XenVirtioDev, xendev);
    xen_be_unbind_evtchn(&xvdev->xendev);
    if (xvdev->page) {
        xc_gnttab_munmap(xvdev->xendev.gnttabdev, xvdev->page, 1);
        xvdev->page = NULL;
    }

    virtio_unbind_notify_evtchn(xvdev);
}

static int virtio_free(struct XenDevice *xendev)
{
    /* Nothing to do */
    return 0;
}

static void virtio_event_write(XenVirtioDev *xvdev)
{
    VirtIODevice *vdev = xvdev->vdev;
    struct virtio_config_page *page = xvdev->page;
    uint32_t config = VIRTIO_XENBUS_CONFIG(vdev);
    uint32_t val;
    target_phys_addr_t ma;
    int size, offset;

    offset = page->offset;
    size = page->size;

    switch (size) {
    case 1:
        val = *((uint8_t *)&page->config[offset]);
        break;
    case 2:
        val = *((uint16_t *)&page->config[offset]);
        break;
    case 4:
        val = *((uint32_t *)&page->config[offset]);
        break;
    default:
        fprintf(stderr, "wrong size in write: %d\n", size);
        exit(-1);
    }

    if (offset < config) {
        switch (offset) {
        case VIRTIO_XENBUS_GUEST_FEATURES:
            if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
                if (vdev->bad_features) {
                    val = xvdev->host_features & vdev->bad_features(vdev);
                } else {
                    val = 0;
                }
            }
            if (vdev->set_features) {
                vdev->set_features(vdev, val);
            }
            vdev->guest_features = val;
            break;
        case VIRTIO_XENBUS_QUEUE_PFN:
            ma = (target_phys_addr_t)val << VIRTIO_XENBUS_QUEUE_ADDR_SHIFT;
            if (ma == 0) {
                virtio_reset(vdev);
            } else {
                virtio_queue_set_addr(vdev, vdev->queue_sel, ma);
            }
            break;
        case VIRTIO_XENBUS_QUEUE_SEL:
            if (val < VIRTIO_XENBUS_QUEUE_MAX) {
                vdev->queue_sel = val;
            }
            break;
        case VIRTIO_XENBUS_QUEUE_NOTIFY:
            virtio_queue_notify(vdev, val);
            break;
        case VIRTIO_XENBUS_STATUS:
            virtio_set_status(vdev, val & 0xFF);
            if (vdev->status == 0) {
                virtio_reset(vdev); /* XXX reset should clean more? */
            }
            break;
        default:
            error_report("%s: unexpected offset 0x%x value 0x%x",
                         __func__, offset, val);
            break;
        }
    } else {
        /* writing per driver config, when offset >= config */
        switch (size) {
        case 1:
            virtio_config_writeb(vdev, offset - config, val);
            break;
        case 2:
            virtio_config_writew(vdev, offset - config, val);
            break;
        case 4:
            virtio_config_writel(vdev, offset - config, val);
            break;
        }
    }
}

static void virtio_event_read(XenVirtioDev *xvdev)
{
    VirtIODevice *vdev = xvdev->vdev;
    struct virtio_config_page *page = xvdev->page;
    uint32_t config = VIRTIO_XENBUS_CONFIG(vdev);
    uint32_t val;
    int size, offset;

    offset = page->offset;
    size = page->size;

    if (offset < config) {
        switch (offset) {
        case VIRTIO_XENBUS_HOST_FEATURES:
            val = xvdev->host_features;
            break;
        case VIRTIO_XENBUS_GUEST_FEATURES:
            val = vdev->guest_features;
            break;
        case VIRTIO_XENBUS_QUEUE_PFN:
            val = virtio_queue_get_addr(vdev, vdev->queue_sel)
                >> VIRTIO_XENBUS_QUEUE_ADDR_SHIFT;
            break;
        case VIRTIO_XENBUS_QUEUE_NUM:
            val = virtio_queue_get_num(vdev, vdev->queue_sel);
            break;
        case VIRTIO_XENBUS_QUEUE_SEL:
            val = vdev->queue_sel;
            break;
        case VIRTIO_XENBUS_STATUS:
            val = vdev->status;
            break;
        case VIRTIO_XENBUS_ISR:
            val = vdev->isr;
            vdev->isr = 0;
            break;
        default:
            error_report("%s: unexpected offset 0x%x value 0x%x",
                         __func__, offset, val);
            break;
        }
    } else {
        /* reading per driver config, when offset >= config */
        switch (size) {
        case 1:
            val = virtio_config_readb(vdev, offset - config);
            break;
        case 2:
            val = virtio_config_readw(vdev, offset - config);
            break;
        case 4:
            val = virtio_config_readl(vdev, offset - config);
            break;
        }
    }

    /* Remember, the config page in guest is only a shadow */
    switch (size) {
    case 1:
        *((uint8_t *)&page->config[offset]) = (uint8_t)val;
        break;
    case 2:
        *((uint16_t *)&page->config[offset]) = (uint16_t)val;
        break;
    case 4:
        *((uint32_t *)&page->config[offset]) = (uint32_t)val;
        break;
    default:
        fprintf(stderr, "wrong size in read: %d\n", size);
        exit(-1);
    }
}

static void virtio_event(struct XenDevice *xendev)
{
    XenVirtioDev *xvdev = container_of(xendev, XenVirtioDev, xendev);
    struct virtio_config_page *page = xvdev->page;
    int offset;
    int size;
    int is_write;
    uint32_t val;

    /* The frontend is poll-waiting, process request directly rather
     * than using a bh, so that we can return to frontend ASAP. */

    offset   = page->offset;
    size     = page->size;
    is_write = page->write;

    xen_mb();

    if (size != 1 && size != 2 && size != 4) {
        fprintf(stderr, "wrong size %d\n", size);
        goto out;
    }

    /* pick the value, only useful for write */
    switch (size) {
    case 1:
        val = *((uint8_t *)&page->config[offset]);
        break;
    case 2:
        val = *((uint16_t *)&page->config[offset]);
        break;
    case 4:
        val = *((uint32_t *)&page->config[offset]);
        break;
    }

    if (is_write) {
        virtio_event_write(xvdev);
    } else {
        virtio_event_read(xvdev);
    }

out:
    /* WRITE in X86 is atomic. Don't forget proper mb() */
    page->be_active = 0;
    xen_mb();
}

/* XXX reconnect? we don't support hotplug now...*/
static void virtio_frontend_changed(struct XenDevice *xendev, const char *node)
{
    if (xendev->fe_state == XenbusStateInitialising &&
        xendev->be_state == XenbusStateClosed) {
        xen_be_printf(xendev, 0, "reconnect\n");
        xen_be_set_state(xendev, XenbusStateConnected);
    }
}

struct XenDevOps xen_virtio_ops = {
    .size             = sizeof(XenVirtioDev),
    .flags            = DEVOPS_FLAG_NEED_GNTDEV,
    .alloc            = virtio_alloc,
    .init             = virtio_init,
    .initialise       = virtio_connect,
    .disconnect       = virtio_disconnect,
    .event            = virtio_event,
    .free             = virtio_free,
    .frontend_changed = virtio_frontend_changed,
};
