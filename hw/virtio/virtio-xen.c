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
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/qemu-print.h"

static void virtio_xen_bus_new(VirtioBusState *, size_t, VirtioXenDevice *);
static void virtio_xen_device_realize(VirtioXenDevice *, Error **);

/* virtio-xen */

static void virtio_xen_busdev_realize(DeviceState *dev, Error **errp)
{
    QPRINT; // TODO:
    VirtioXenDevice *_dev = (VirtioXenDevice *)dev;

    virtio_xen_bus_new(&_dev->bus, sizeof(_dev->bus), _dev);
    virtio_xen_device_realize(_dev, errp);
}

static void virtio_xen_device_class_init(ObjectClass *klass, void *data)
{
    QPRINT;

    DeviceClass *dc = DEVICE_CLASS(klass);
    //XenDeviceClass *k = XEN_DEVICE_CLASS(dc);
    //VirtioXenDeviceClass *vdc = VIRTIO_XEN_DEVICE_CLASS(klass);

    // k->unplug = virtio_ccw_busdev_unplug;
    dc->realize = virtio_xen_busdev_realize;
    // dc->unrealize = virtio_ccw_busdev_unrealize;
    // device_class_set_parent_reset(dc, virtio_ccw_reset, &vdc->parent_reset);
}

static const TypeInfo virtio_xen_device_info = {
    .name = TYPE_VIRTIO_XEN_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(VirtioXenDevice),
    .class_init = virtio_xen_device_class_init,
    .class_size = sizeof(VirtioXenDeviceClass),
    .abstract = true,
};

/* virtio-xen-bus */

static void virtio_xen_device_realize(VirtioXenDevice *dev, Error **errp)
{
    QPRINT; // FIXME: do it

    VirtioXenDeviceClass *k = VIRTIO_XEN_DEVICE_GET_CLASS(dev);
    XenDevice *xen_dev = XEN_DEVICE(dev);
    XenDeviceClass *xk = XEN_DEVICE_GET_CLASS(xen_dev);
    Error *err = NULL;

    // TODO: do xenbus things?

    // NOTE: the below realize invocations invoke our
    // vhost-user-fs-xen realize callback

    if (k->realize) {
        qemu_printf("%s: -> VirtioXenDeviceClass::realize()\n", __func__);
        k->realize(dev, &err);
        if (err) {
            goto out_err;
        }
    }

    if (xk->realize) {
        qemu_printf("%s: -> XenDeviceClass::realize()\n", __func__);
        xk->realize(xen_dev, &err);
        if (err) {
            goto out_err;
        }
    }
    return;

out_err:
    // TODO: bla bla bla
}

static void virtio_xen_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtioXenDevice *dev)
{
    QPRINT;
    DeviceState *qdev = DEVICE(dev);
    char virtio_bus_name[] = "virtio-bus";

    qbus_init(bus, bus_size, TYPE_VIRTIO_XEN_BUS, qdev, virtio_bus_name);
}


static void virtio_xen_notify(DeviceState *d, uint16_t vector)
{
    QPRINT;

    // xc_evtchn_notify(xv_dev->notify_evtchndev, xv_dev->notify_local_port);
}

static int virtio_xen_set_guest_notifiers(DeviceState *d, int nvqs,
                                          bool assigned)
{
    QPRINT; // TODO:

    return -EFAULT;
}


static void virtio_xen_bus_class_init(ObjectClass *klass, void *data)
{
    QPRINT;

    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    BusClass *bus_class = BUS_CLASS(klass);

    bus_class->max_dev = 1;

    k->notify = virtio_xen_notify;
    // k->save_config = virtio_xen_save_config;
    // k->load_config = virtio_xen_load_config;
    // k->save_queue = virtio_pci_save_queue;
    // k->load_queue = virtio_pci_load_queue;

    // k->save_extra_state = virtio_xen_save_extra_state;
    // k->load_extra_state = virtio_xen_load_extra_state;
    // k->has_extra_state = virtio_xen_has_extra_state;
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

