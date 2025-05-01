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

#define UNUSED __attribute__((__unused__))

static void virtio_xen_device_realize(XenDevice *, Error **);
static void virtio_xen_device_unrealize(XenDevice *);

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

    //Error *err = NULL;
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
    vxd->gnttab = qemu_xen_gnttab_open();
    if (vxd->gnttab == NULL) {
        error_setg(errp, "error opening handle to Xen grant table");
        return;
    }

    // TODO: continue with [virtio_connect]

    // XXX: don't we need to wait until FE is state=3?
    // I see some race conditions reading from XS with zero values.

    // TODO: Read the guest resources from xenstore

    if (0 == strncmp(xd->name, "virtio-fs", 9)) {
        // TODO:
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

    vxd->conf_page = qemu_xen_gnttab_map_refs(vxd->gnttab,
                                              1u, xd->frontend_id,
                                              &vxd->conf_gntref,
                                              PROT_READ|PROT_WRITE);
    if (vxd->conf_page == NULL) {
        error_setg_errno(errp, errno, "error mapping gntref");
        return;
    }
    VUF_DBG("conf page = %p", vxd->conf_page);
    //
    // NOTE: Event channel for configuration updates
    //

    // ret = xenstore_read_uint64(xd->frontend_path, "conf-evtchn",
    //                            &vx->conf_evtchn);
    // if (ret == -1) {
    //     error_setg(errp, "bad conf-evtchn from frontend");
    //     return;
    // }
    // VUF_DBG("conf-evtchn %lu", vx->conf_evtchn);

    //
    // NOTE: Event channel for the virtqueues
    //

    vxd->notify_remote = -1;
    vxd->notify_local = -1;

    ret = xenstore_read_int(xd->frontend_path, "notify-evtchn", &port);
    if (ret == -1 || port < 0) {
        error_setg(errp, "bad notify-evtchn from frontend");
        return;
    }
    vxd->notify_remote = port;
    VUF_DBG("vxd->notify_remote %u", vxd->notify_remote);

    // bind both end points to the same event channel
    vxd->notify_local = qemu_xen_evtchn_bind_interdomain(vxd->evtchn,
                                                         xd->frontend_id,
                                                         vxd->notify_remote);
    if (vxd->notify_local == -1) {
        if (errp)
            error_setg_errno(errp, errno, "error binding notify evtchn");
        goto out_unbind;
    }
    VUF_DBG("bind notify_remote ok");

    // TODO: continue here

    if (vxd_class->realize)
        vxd_class->realize(vxd, errp);

    return;

out_unbind:
    qemu_xen_evtchn_unbind(vxd->evtchn, vxd->notify_local);
}

static void virtio_xen_device_unrealize(XenDevice *xd)
{
    VUF_DBG("");

    VirtioXenDevice *vxd = VIRTIO_XEN_DEVICE(xd);
    int ret;

    if (vxd->notify_local != -1)
        qemu_xen_evtchn_unbind(vxd->evtchn, vxd->notify_local);
    vxd->notify_local = -1;

    // TODO: unbind notify_local

    if (vxd->conf_page != NULL) {
        ret = qemu_xen_gnttab_unmap(vxd->gnttab, vxd->conf_page,
                                    &vxd->conf_gntref, 1u);
        if (ret < 0)
            qemu_printf("%s: error unmapping conf_page: %d", __func__, ret);
    }
}

/* virtio-xen-bus class */

// NOTE: follow from virtio-ccw for structure

static void virtio_xen_notify(DeviceState *d, uint16_t vector)
{
    VUF_DBG("bus -> ");
    // XenVirtioDev *xv_dev = opaque;
    // xc_evtchn_notify(xv_dev->notify_evtchndev, xv_dev->notify_local_port);
}

static void virtio_xen_save_config(DeviceState *d, QEMUFile *f)
{
    NOT_IMPL;
    VUF_DBG("bus -> ");
    // VirtioCcwDevice *dev = VIRTIO_XEN_DEVICE(d);
    // TODO: vmstate_save_state(f, &vmstate_virtio_xen_dev, dev, NULL);
}

static int virtio_xen_load_config(DeviceState *d, QEMUFile *f)
{
    VUF_DBG("bus -> ");
    // VirtioCcwDevice *dev = VIRTIO_XEN_DEVICE(d);
    // TODO: return vmstate_load_state(f, &vmstate_virtio_ccw_dev, dev, 1);
    return 0;
}

static void virtio_xen_save_queue(DeviceState *d, int n, QEMUFile *f)
{
    VUF_DBG("bus -> ");
}

static int virtio_xen_load_queue(DeviceState *d, int n, QEMUFile *f)
{
    VUF_DBG("bus -> ");
    return 0;
}

static int virtio_xen_set_guest_notifiers(DeviceState *d, int nvqs,
                                          bool assigned)
{
    VUF_DBG("bus -> ");
    return -EFAULT;
}

static void virtio_xen_bus_class_init(ObjectClass *klass, void *data)
{
    qemu_printf("%s: bus -> set up method callbacks\n", __func__);

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

