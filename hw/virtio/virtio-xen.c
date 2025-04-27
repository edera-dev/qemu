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
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"

#define UNUSED __attribute__((__unused__))

static void virtio_xen_bus_new(VirtioBusState *, size_t, VirtioXenDevice *);
static void virtio_xen_device_realize(VirtioXenDevice *, Error **);

/* virtio-xen-device */

// FIXME: this needs to be called?
static void UNUSED virtio_xen_busdev_realize(DeviceState *dev, Error **errp)
{
    VUF_DBG("enter");
    XenDevice *xd = (XenDevice *)dev;
    VirtioXenDevice *vxd = (VirtioXenDevice *)xd;

    virtio_xen_bus_new(&vxd->bus, sizeof(vxd->bus), vxd);
    virtio_xen_device_realize(vxd, errp);
}

static char *xen_device_class_get_name(XenDevice *xendev, Error **errp)
{
    VUF_DBG("");
    // NOTE: convention seems to be the name ends with a numeral
    static uint32_t num = 0;
    return g_strdup_printf("%u", num++);
}

static void xen_device_class_realize(XenDevice *xd, Error **errp)
{
    // NOTE: qemu seems to get to this fn but does not
    // invoke my virtio_xen class methods

    VUF_DBG("");

    // FIXME: init the bus
    DeviceState *qdev = DEVICE(xd); // TODO: what can you pass here?
    VirtioXenDevice *vxd = (VirtioXenDevice *)xd;
    char virtio_bus_name[] = "virtio-bus";
    qbus_init(&vxd->bus, sizeof(vxd->bus), TYPE_VIRTIO_XEN_BUS, qdev, virtio_bus_name);
    VUF_DBG("virtio-bus registered");

    // FIXME: invoke the subclass realize
    VirtioXenDeviceClass *vxd_class = VIRTIO_XEN_DEVICE_GET_CLASS(vxd);
    if (vxd_class->realize)
        vxd_class->realize(vxd, errp);
}

static void xen_device_class_frontend_changed(XenDevice *xendev,
                                       enum xenbus_state frontend_state,
                                       Error **errp)
{
    VUF_DBG("frontend_state -> %u", frontend_state);
}

static void virtio_xen_device_class_init(ObjectClass *obj_class, void *data)
{
    VUF_DBG("enter. set up abstract xen device with realize fn");

    DeviceClass *dev_class = DEVICE_CLASS(obj_class);
    XenDeviceClass *xd_class = XEN_DEVICE_CLASS(dev_class);
    VirtioXenDeviceClass *vxd_class = VIRTIO_XEN_DEVICE_CLASS(obj_class);

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
    xd_class->realize = xen_device_class_realize; // lots of XS writes
    xd_class->frontend_changed = xen_device_class_frontend_changed;
    // xd_class->unrealize = xen_block_unrealize; // TODO:
    set_bit(DEVICE_CATEGORY_STORAGE, dev_class->categories);
    dev_class->user_creatable = true; // XXX: ??????

    // device_class_set_props(dev_class, xen_block_props); // TODO:
    vxd_class->realize = virtio_xen_device_realize;

    // XXX: set up vxd_class at all??? when would this get created???
    // link them together?
}

static const TypeInfo virtio_xen_device_info = {
    .name = TYPE_VIRTIO_XEN_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(VirtioXenDevice),
    .class_init = virtio_xen_device_class_init,
    .class_size = sizeof(VirtioXenDeviceClass),
    .abstract = true, // ????
};

/* virtio-xen-bus state */

// NOTE: xen_be_printf -> xen_pv_printf

// NOTE: aka virtio_alloc from old code?
static void virtio_xen_device_realize(VirtioXenDevice *vx, Error **errp)
{
    VUF_DBG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");

    // VirtIODevice *vio_dev = NULL;

    VirtioXenDeviceClass *vx_class = VIRTIO_XEN_DEVICE_GET_CLASS(vx);
    XenDevice *xd = XEN_DEVICE(vx);
    XenDeviceClass *xd_class = XEN_DEVICE_GET_CLASS(xd);
    Error *err = NULL;
    int ret;

    //enum xenbus_state xb_state;

    // TODO: do xenbus things?

    // NOTE: old code is reading items from xenstore.
    // Perhaps [xl] writes to xenstore before launching QEMU?
    // When you run an HVM, then QEMU must be started by something, which would
    // be the toolstack. The MAC addr is in the domU config file, and
    // the old QEMU code reads this from xenstore. Must be toolstack put it there.

    // TODO: set host_features?

    // FIXME: who writes these?
    // VUF_DBG("XenDevice name '%s'", xd->name);
    // VUF_DBG("XenDevice backend_path '%s'", xd->backend_path);
    // VUF_DBG("XenDevice frontend_path '%s'", xd->frontend_path);
    // VUF_DBG("XenDevice frontend-id %u", xd->frontend_id);
    // VUF_DBG("xs: frontend-id = %u", xd->frontend_id);
    //xen_device_backend_printf(xd, "frontend-id", "%d", xd->frontend_id);

    // NOTE: the below realize invocations invoke our
    // vhost-user-fs-xen realize callback

    // FIXME: continue here?

    if (0 == strncmp(xd->name, "virtio-fs", 9)) {
        // TODO:
    }

    // XXX: read stuff from xenstore? map in resources?
    // see virtio_alloc in the older code
    // see xen_virtio_blk_init too

    ret = xenstore_read_uint64(xd->frontend_path, "conf-mfn",
                               &vx->conf_mfn);
    if (ret != -1) {
        error_setg(errp, "bad conf-mfn from frontend");
        return;
    }
    VUF_DBG("conf-mfn %lu", vx->conf_evtchn);

    ret = xenstore_read_uint64(xd->frontend_path, "conf-evtchn",
                               &vx->conf_evtchn);
    if (ret != -1) {
        error_setg(errp, "bad conf-evtchn from frontend");
        return;
    }
    VUF_DBG("conf-evtchn %lu", vx->conf_evtchn);

    ret = xenstore_read_uint64(xd->frontend_path, "notify-evtchn",
                               &vx->notify_evtchn);
    if (ret != -1) {
        error_setg(errp, "bad notify-evtchn from frontend");
        return;
    }
    VUF_DBG("notify-evtchn %lu", vx->notify_evtchn);

    //TODO: continue here
    // FIXME: Why are we calling this here explicitly?
    if (xd_class->realize) {
        qemu_printf("%s: -> XenDeviceClass::realize()\n", __func__);
        xd_class->realize(xd, &err);
        if (err) {
            goto out_err;
        }
    } else {
        VUF_DBG("XenDeviceClass has no realize method?");
    }
    if (vx_class->realize) {
        qemu_printf("%s: -> VirtioXenDeviceClass::realize()\n", __func__);
        vx_class->realize(vx, &err); // -> vhost_user_fs_xen_realize
        if (err) {
            goto out_err;
        }
    }

    VUF_DBG(">> XenDevice name '%s'", xd->name);
    VUF_DBG(">> XenDevice backend_path '%s'", xd->backend_path);
    VUF_DBG(">> XenDevice frontend_path '%s'", xd->frontend_path);
    VUF_DBG(">> XenDevice frontend-id %u", xd->frontend_id);

    return;

out_err:
    // TODO: bla bla bla
}

static void virtio_xen_bus_new(VirtioBusState *vbs, size_t bus_size,
                               VirtioXenDevice *vxd)
{
    VUF_DBG("enter. register virtio-bus with qemu");
    DeviceState *qdev = DEVICE(vxd);
    char virtio_bus_name[] = "virtio-bus";

    qbus_init(vbs, bus_size, TYPE_VIRTIO_XEN_BUS, qdev, virtio_bus_name);
}

/* virtio-xen-bus class */

// NOTE: follow from virtio-ccw for structure

static void virtio_xen_notify(DeviceState *d, uint16_t vector)
{
    NOT_IMPL;
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
    VUF_DBG("enter. set up method callbacks");

    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    BusClass *bus_class = BUS_CLASS(klass);

    bus_class->max_dev = 1;

    k->notify = virtio_xen_notify;
    k->save_config = virtio_xen_save_config;
    k->load_config = virtio_xen_load_config;
    k->save_queue = virtio_xen_save_queue;
    k->load_queue = virtio_xen_load_queue;

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

