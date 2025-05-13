/*
 * Virtio Xen bindings
 *
 * Copyright (c) 2025 Edera
 *
 * Author:
 *  Alexander Merritt <alexander@edera.dev>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-xen.h"
#include "migration/qemu-file-types.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/replay.h"
#include "hw/virtio/virtio-xen.h"
#include "hw/xen/xen-bus.h"
#include "hw/xen/xen_pvdev.h"
#include "hw/xen/xen_backend_ops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "linux/virtio_ids.h"

#define UNUSED __attribute__((__unused__))

static void virtio_xen_device_realize(XenDevice *, Error **);
static void virtio_xen_device_unrealize(XenDevice *);
static bool virtio_xen_event(void *);

/* virtio-xen-device */

static char *xen_device_class_get_name(XenDevice *xendev, Error **errp)
{
    VUF_DBG("");
    // NOTE: convention seems to be the name ends with a numeral
    static uint32_t num = 0;
    return g_strdup_printf("%u", num++);
}

static void xen_device_class_frontend_changed(XenDevice *xd,
                                       enum xenbus_state frontend_state,
                                       Error **errp)
{
    ERRP_GUARD();
    enum xenbus_state backend_state = xen_device_backend_get_state(xd);

    VUF_DBG("frontend_state -> %u %s | be = %u %s", frontend_state, 
            xenbus_strstate(frontend_state), backend_state, xenbus_strstate(backend_state));

    switch (frontend_state) {
    case XenbusStateInitialised:
    case XenbusStateConnected:
        if (backend_state == XenbusStateConnected) {
            break;
        }

        // TODO: perform the connection? do we have anything else to do?

        // xen_block_connect(xd, errp);
        // if (*errp) {
        //     break;
        // }

        xen_device_backend_set_state(xd, XenbusStateConnected);
        break;

    case XenbusStateClosing:
        xen_device_backend_set_state(xd, XenbusStateClosing);
        break;

    case XenbusStateClosed:
    case XenbusStateUnknown:
        // TODO: free the resources now that FE is no longer using them
        // xen_block_disconnect(xd, errp);
        // if (*errp) {
        //     break;
        // }

        xen_device_backend_set_state(xd, XenbusStateClosed);
        break;

    default:
        break;
    }
}

static void virtio_xen_device_class_init(ObjectClass *obj_class, void *data)
{
    VUF_DBG("enter. set up abstract xen device with realize fn");

    DeviceClass *dev_class = DEVICE_CLASS(obj_class);
    XenDeviceClass *xd_class = XEN_DEVICE_CLASS(dev_class);
    // VirtioXenDeviceClass *vxd_class = VIRTIO_XEN_DEVICE_CLASS(obj_class);

    // xd_class->unplug = virtio_ccw_busdev_unplug;
    //dev_class->realize = virtio_xen_busdev_realize; // XXX: override or not??? maybe don't touch DeviceClass!
    // dev_class->unrealize = virtio_ccw_busdev_unrealize;
    // device_class_set_parent_reset(dc, virtio_ccw_reset, &vdc->parent_reset); // legacy API

    // NOTE: xen_bus_backend_create identifies the following keys
    // state, online, frontend, frontend-id, hotplug-status
    // as use for creating a generic XenDevice, and captures any others

    // run script writes state 1 for FE then BE to activate
    // but xen_config_dev_all does this in xen/xen_devconfig.c

    // XXX: maybe I'm only supposed to touch my own subclass?

    // NOTE: If you look at xen_nic.c in fn xen_netdev_class_init
    // you see the methods for XenDeviceClass are initialized there,
    // so probably safe for us to do the same here. Below is following
    // the prior code

    xd_class->get_name = xen_device_class_get_name;
    xd_class->realize = virtio_xen_device_realize; // is the subclass realize method
    xd_class->frontend_changed = xen_device_class_frontend_changed;
    xd_class->unrealize = virtio_xen_device_unrealize;
    set_bit(DEVICE_CATEGORY_STORAGE, dev_class->categories);
    dev_class->user_creatable = true; // XXX: ??????

    // device_class_set_props(dev_class, xen_block_props); // TODO:
    // vxd_class->realize = virtio_xen_device_realize;
    // vxd_class->unrealize = virtio_xen_device_unrealize;
    // vxd_class->parent_reset = virtio_xen_device_parent_reset;
}

static const TypeInfo virtio_xen_device_info = {
    .name = TYPE_VIRTIO_XEN_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(VirtioXenDevice),
    .class_init = virtio_xen_device_class_init,
    .class_size = sizeof(VirtioXenDeviceClass),
    .abstract = true, // FIXME: ????
};

/* virtio-xen-bus state */

// NOTE: xen_be_printf -> xen_pv_printf

// NOTE: aka virtio_alloc from old code?
// TODO: rename to xen_virtio_realize ?
static void virtio_xen_device_realize(XenDevice *xd, Error **errp)
{
    ERRP_GUARD();

    // XenDevice.realize invokes us

    VUF_DBG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    VUF_DBG(">> XenDevice name '%s'", xd->name);
    VUF_DBG(">> XenDevice backend_path '%s'", xd->backend_path);
    VUF_DBG(">> XenDevice frontend_path '%s'", xd->frontend_path);
    VUF_DBG(">> XenDevice frontend-id %u", xd->frontend_id);

    // NOTE: FE should be in Initialised state
    // before we attempt to read resources from xenbus

    // ✓ point XenDevice.realize to the subclass realize!
    //      xd_class->realize = virtio_xen_device_class_realize;
    // ✓ cast XenDevice into VirtioXenDevice
    // ✓ in VirtioXenDevice.realize, invoke the vxd_class.realize (vhost user fs realize)

    //XenDeviceClass *xd_class = XEN_DEVICE_GET_CLASS(xd);
    VirtioXenDevice *vxd = VIRTIO_XEN_DEVICE(xd);
    VirtioXenDeviceClass *vxd_class = VIRTIO_XEN_DEVICE_GET_CLASS(vxd);

    // FIXME: see virtio_blk_get_features
    //virtio_add_feature(&vd->host_features, VIRTIO_F_VERSION_1);

    int ret;

    // TODO: Why are we initializing a bus in a device instantiation fn?
    char virtio_bus_name[] = "virtio-bus";
    qbus_init(&vxd->bus, sizeof(vxd->bus), TYPE_VIRTIO_XEN_BUS, DEVICE(xd), virtio_bus_name);

    //enum xenbus_state xb_state;

    // TODO: do xenbus things?

    // NOTE: old code is reading items from xenstore.
    // Perhaps [xl] writes to xenstore before launching QEMU?
    // When you run an HVM, then QEMU must be started by something, which would
    // be the toolstack. The MAC addr is in the domU config file, and
    // the old QEMU code reads this from xenstore. Must be toolstack put it there.

    // TODO: set host_features?

    // NOTE: the below realize invocations invoke our
    // vhost-user-fs-xen realize callback

    // FIXME: continue here?

    // Open a handle to the event channel system
    // cf [virtio_notify_init]
    vxd->evtchn = qemu_xen_evtchn_open();
    if (vxd->evtchn == NULL) {
        error_setg(errp, "error opening handle to Xen event channel");
        return;
    }
    fcntl(qemu_xen_evtchn_fd(vxd->evtchn), F_SETFD, FD_CLOEXEC);

    // Open a handle to the grant table system
    vxd->gnttab = qemu_xen_gnttab_open(); // XXX: still needed? VirtioDevice has its own
    if (vxd->gnttab == NULL) {
        error_setg(errp, "error opening handle to Xen grant table");
        return;
    }

    // TODO: continue with [virtio_connect]

    // XXX: don't we need to wait until FE is state=3?
    // I see some race conditions reading from XS with zero values.

    // TODO: Read the guest resources from xenstore

    if (0 == strncmp(xd->name, "virtio-fs", 9)) {
        // TODO: confirm this?
    }

    // XXX: read stuff from xenstore? map in resources?
    // see virtio_alloc in the older code
    // see xen_virtio_blk_init too

    // Wait for frontend to export its resources to xenstore.
    // FIXME: why do we have to do this manually here?
    int state;
    do {
        ret = xenstore_read_int(xd->frontend_path, "state", &state);
        if (ret == -1) {
            error_setg(errp, "error reading fe state");
            return;
        }
        VUF_DBG("wait... fe state = %d", state);
    } while (state != XenbusStateInitialised);

    int port;

    //
    // NOTE: Shared page for the configuration
    //

    uint64_t gntref;
    ret = xenstore_read_uint64(xd->frontend_path, "conf-gntref",
                               &gntref);
    if (ret == -1) {
        error_setg(errp, "error reading fe/conf-gntref from xs");
        return;
    }
    if (gntref > UINT_MAX) {
        error_setg(errp, "bad fe/conf-gntref (too big): %lu", gntref);
        return;
    }
    vxd->conf_gntref = gntref;
    VUF_DBG("conf-gntref %u", vxd->conf_gntref);

    vxd->conf_page = xen_device_map_grant_refs(xd, &vxd->conf_gntref,
                                               1u, PROT_READ|PROT_WRITE, errp);
    if (vxd->conf_page == NULL) {
        error_setg(errp, "error mapping gntref");
        return;
    }
    VUF_DBG("conf page = %p", vxd->conf_page);

    //
    // NOTE: Event channel for configuration updates
    //

    vxd->conf_remote = -1;

    ret = xenstore_read_int(xd->frontend_path, "conf-evtchn", &port);
    if (ret == -1 || port < 0) {
        error_setg(errp, "bad fe/conf-evtchn");
        return;
    }
    vxd->conf_remote = port;
    VUF_DBG("vxd->conf_remote %u", vxd->conf_remote);

    vxd->conf = xen_device_bind_event_channel(xd, vxd->conf_remote,
                                              virtio_xen_event,
                                              vxd, errp);
    if (vxd->conf == NULL)
        goto out_unbind_conf;
    VUF_DBG("bind config evtchn ok");

    //
    // NOTE: Event channel for the virtqueues
    //

    vxd->notify_remote = -1;
    vxd->notify_local = -1;

    ret = xenstore_read_int(xd->frontend_path, "notify-evtchn", &port);
    if (ret == -1 || port < 0) {
        error_setg(errp, "bad fe/notify-evtchn");
        return;
    }
    vxd->notify_remote = port;
    VUF_DBG("vxd->notify_remote %u", vxd->notify_remote);

    // The old implementation [xen_be_bind_evtchn] uses code
    // from commit d94f94862015 in 2009, which seems to use internal
    // XenDevice state to handle an event, calling
    // [xen_be_evtchn_event], then a likely user-defined callback.
    // This seems to be part of XenDevOps.event... hooked to
    // [virtio_event].
    // The new API requires these arguments explicitly. Which is the
    // "port" here? The one allocated to us after binding to the
    // remote?

    // FIXME: this should bind to the virtqueue handler, not the config handler!
    // vxd->notify = xen_device_bind_event_channel(xd, vxd->notify_remote,
    //                                             virtio_xen_event,
    //                                             vxd, errp);
    // if (vxd->notify == NULL)
    //     goto out_unbind_notify;
    // VUF_DBG("bind notify evtchn ok");

    if (vxd_class->realize)
        vxd_class->realize(vxd, errp);

    return;

out_unbind_conf:
    xen_device_unbind_event_channel(xd, vxd->conf, &error_warn);
// out_unbind_notify:
//     xen_device_unbind_event_channel(xd, vxd->notify, &error_warn);
}

static void virtio_xen_device_unrealize(XenDevice *xd)
{
    VirtioXenDevice *vxd = VIRTIO_XEN_DEVICE(xd);

    VUF_DBG("");

    if (vxd->notify != NULL)
        xen_device_unbind_event_channel(xd, vxd->notify, &error_warn);
    vxd->notify = NULL;

    if (vxd->conf_page != NULL)
        xen_device_unmap_grant_refs(xd, vxd->conf_page, &vxd->conf_gntref,
                                    1u, &error_warn);
    vxd->conf_page = NULL;
}

// @pre [vxd->conf_page.size] is 1,2,4,8
static bool virtio_event_read(VirtioXenDevice *vxd)
{
    VirtIODevice *vd = vxd->vd;
    struct VirtioConfigPage *conf = vxd->conf_page;
    uint64_t val = 0;
    int size, offset;

    // FIXME: Check offset does not exceed config buffer
    offset = conf->offset;
    size = conf->size;

    //VUF_DBG("offset %d size %d", offset, size);

    if (offset < VIRTIO_XENBUS_CONFIG_OFF) {
        switch (offset) {
        case VIRTIO_XENBUS_HOST_FEATURES:
            val = vd->host_features;
            VUF_DBG("host_features %#lx", val);
            break;
        case VIRTIO_XENBUS_GUEST_FEATURES:
            val = vd->guest_features;
            VUF_DBG("guest_features %#lx", val);
            break;
        case VIRTIO_XENBUS_QUEUE_PFN:
            val = virtio_queue_get_addr(vd, vd->queue_sel)
                >> VIRTIO_XENBUS_QUEUE_ADDR_SHIFT;
            VUF_DBG("queue_pfn %#lx", val);
            break;
        case VIRTIO_XENBUS_QUEUE_NUM:
            val = virtio_queue_get_num(vd, vd->queue_sel);
            VUF_DBG("queue_num %#lx", val);
            break;
        case VIRTIO_XENBUS_QUEUE_SEL:
            val = vd->queue_sel;
            VUF_DBG("queue_sel %#lx", val);
            break;
        case VIRTIO_XENBUS_STATUS:
            val = vd->status;
            VUF_DBG("status %#lx", val);
            break;
        case VIRTIO_XENBUS_ISR:
            val = vd->isr;
            vd->isr = 0;
            VUF_DBG("isr %#lx", val);
            break;
        default:
            error_report("%s: unexpected offset 0x%x value 0x%lx",
                         __func__, offset, val);
            break;
        }
    } else {
        uint32_t off = offset - VIRTIO_XENBUS_CONFIG_OFF;
        switch (size) {
        case 1:
            val = virtio_config_readb(vd, off);
            break;
        case 2:
            val = virtio_config_readw(vd, off);
            break;
        case 4:
            val = virtio_config_readl(vd, off);
            break;
        case 8:
            // FIXME:
            // "2.5.1 Driver Requirements: Device Configuration Space
            // Drivers MUST NOT assume reads from fields greater than
            // 32 bits wide are atomic, nor are reads from
            // multiple fields"
            val = (uint64_t)virtio_config_readl(vd, off) << 32;
            val |= virtio_config_readl(vd, off + 4);
            break;
        }
    }

    //VUF_DBG("val %lu %#lx", val, val);

    // config page in guest is only a shadow
    switch (size) {
    case 1:
        *((uint8_t *)&conf->config[offset]) = (uint8_t)val;
        break;
    case 2:
        *((uint16_t *)&conf->config[offset]) = (uint16_t)val;
        break;
    case 4:
        *((uint32_t *)&conf->config[offset]) = (uint32_t)val;
        break;
    case 8:
        *((uint64_t *)&conf->config[offset]) = (uint64_t)val;
        break;
    }

    return true;
}

// @pre [vxd->conf_page.size] is 1,2,4,8
static bool virtio_event_write(VirtioXenDevice *vxd)
{
    VirtIODevice *vd = vxd->vd;
    struct VirtioConfigPage *conf = vxd->conf_page;
    hwaddr ma;
    uint64_t val;
    int size, offset;

    // FIXME: Check offset does not exceed config buffer
    offset = conf->offset;
    size = conf->size;

    switch (size) {
    case 1:
        val = *((uint8_t *)&conf->config[offset]);
        break;
    case 2:
        val = *((uint16_t *)&conf->config[offset]);
        break;
    case 4:
        val = *((uint32_t *)&conf->config[offset]);
        break;
    case 8:
        val = *((uint64_t *)&conf->config[offset]);
        break;
    }

    VUF_DBG("size %d offset %d val %ld %#lx", size, offset, val, val);

    if (offset < VIRTIO_XENBUS_CONFIG_OFF) {
        switch (offset) {
        case VIRTIO_XENBUS_STATUS:
            VUF_DBG("status %#lx", val);
            virtio_set_status(vd, val & 0xFF);
            if (vd->status == 0) {
                virtio_reset(vd); /* XXX reset should clean more? */
            }
            break;
        case VIRTIO_XENBUS_GUEST_FEATURES:
            VUF_DBG("guest_features %#lx", val);
            vd->guest_features = val;
            break;
        case VIRTIO_XENBUS_QUEUE_PFN:
            VUF_DBG("queue_pfn %#lx", val);
             ma = (hwaddr)val << VIRTIO_XENBUS_QUEUE_ADDR_SHIFT;
             if (ma == 0) {
                 virtio_reset(vd);
             } else {
                 virtio_queue_set_addr(vd, vd->queue_sel, ma);
             }
             break;
        case VIRTIO_XENBUS_QUEUE_SEL:
            VUF_DBG("queue_sel %#lx", val);
            if (val < VIRTIO_QUEUE_MAX) {
                vd->queue_sel = val;
            }
            break;
        case VIRTIO_XENBUS_QUEUE_NOTIFY:
            VUF_DBG("queue_notify %#lx", val);
            virtio_queue_notify(vd, val);
            break;
        default:
            error_report("%s: unexpected offset %#x value %#lx",
                         __func__, offset, val);
            break;
        }
    } else {
        uint32_t off = offset - VIRTIO_XENBUS_CONFIG_OFF;
        switch (size) {
        case 1:
            virtio_config_writeb(vd, off, val);
            break;
        case 2:
            virtio_config_writew(vd, off, val);
            break;
        case 4:
            virtio_config_writel(vd, off, val);
            break;
        case 8:
            // FIXME:
            // "2.5.1 Driver Requirements: Device Configuration Space
            // Drivers MUST NOT assume reads from fields greater than
            // 32 bits wide are atomic, nor are reads from
            // multiple fields"
            virtio_config_writel(vd, off, val);
            virtio_config_writel(vd, off + 4, val >> 32);
            break;
        }
    }

    return true;
}

// NOTE: xen_device_poll invokes this handler, but its caller
// xen_device_event does nothing with our bool return value
static bool virtio_xen_event(void *_vxd)
{
    VirtioXenDevice *vxd = _vxd;
    struct VirtioConfigPage *conf = vxd->conf_page;
    uint32_t offset, size, is_write;

    bool ret = false;

    offset = conf->offset;
    size = conf->size;
    is_write = conf->write;

    xen_mb();

    VUF_DBG("event: %s size %d offset %d",
            is_write ? "write" : "read", size, offset);

    if (size == 1 || size == 2 || size == 4 || size == 8)
        ret = is_write ? virtio_event_write(vxd) : virtio_event_read(vxd);
    else
        error_report("%s: bad size %u", __func__, size);

    conf->be_active = 0; // break loop in [__vx_wait] of frontend
    xen_mb();

    return ret;
}

/* virtio-xen-bus class */

// NOTE: follow from virtio-ccw for structure

static void virtio_xen_notify(DeviceState *d, uint16_t vector)
{
    VUF_DBG("virtqueue notification (not implemented)");
    // XenVirtioDev *xv_dev = opaque;
    // xc_evtchn_notify(xv_dev->notify_evtchndev, xv_dev->notify_local_port);
}

static void virtio_xen_save_config(DeviceState *d, QEMUFile *f)
{
    NOT_IMPL;
    // VirtioCcwDevice *dev = VIRTIO_XEN_DEVICE(d);
    // TODO: vmstate_save_state(f, &vmstate_virtio_xen_dev, dev, NULL);
}

static int virtio_xen_load_config(DeviceState *d, QEMUFile *f)
{
    NOT_IMPL;
    // VirtioCcwDevice *dev = VIRTIO_XEN_DEVICE(d);
    // TODO: return vmstate_load_state(f, &vmstate_virtio_ccw_dev, dev, 1);
    return 0;
}

static void virtio_xen_save_queue(DeviceState *d, int n, QEMUFile *f)
{
    NOT_IMPL;
}

static int virtio_xen_load_queue(DeviceState *d, int n, QEMUFile *f)
{
    NOT_IMPL;
    return 0;
}

static int virtio_xen_set_guest_notifiers(DeviceState *d, int nvqs,
                                          bool assigned)
{
    NOT_IMPL;
    return -EFAULT;
}

static void virtio_xen_bus_class_init(ObjectClass *klass, void *data)
{
    qemu_printf("%s: bus -> set up method callbacks\n", __func__);

    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    BusClass *bus_class = BUS_CLASS(klass);

    bus_class->max_dev = 1;

    k->notify = virtio_xen_notify; // FIXME: how different from the notify evtchn?
    k->save_config = virtio_xen_save_config;
    k->load_config = virtio_xen_load_config;
    k->save_queue = virtio_xen_save_queue;
    k->load_queue = virtio_xen_load_queue;

    // TODO: implement more of these
    // ?? k->save_extra_state = virtio_xen_save_extra_state;
    // ?? k->load_extra_state = virtio_xen_load_extra_state;
    // ?? k->has_extra_state = virtio_xen_has_extra_state;
    k->set_guest_notifiers = virtio_xen_set_guest_notifiers;
    // k->ioeventfd_enabled = virtio_xen_ioeventfd_enabled;
    // k->ioeventfd_assign = virtio_xen_ioeventfd_assign;
    // k->pre_plugged = virtio_xen_pre_plugged;
    // k->vmstate_change = virtio_xen_vmstate_change;
    // k->has_variable_vring_alignment = true;
}

static const TypeInfo virtio_xen_bus_info = {
    .name          = TYPE_VIRTIO_XEN_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioXenBusState),
    .class_size    = sizeof(VirtioXenBusClass),
    .class_init    = virtio_xen_bus_class_init,
};

static void virtio_xen_register_types(void)
{
    printf("%s\n", __func__);
    type_register_static(&virtio_xen_bus_info);
    type_register_static(&virtio_xen_device_info);
}

type_init(virtio_xen_register_types)

