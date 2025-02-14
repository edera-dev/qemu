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
#include "migration/qemu-file-types.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/replay.h"
#include "hw/virtio/virtio-xen.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/qemu-print.h"

static void virtio_xen_class_init(ObjectClass *klass, void *data)
{
    // DeviceClass *dc = DEVICE_CLASS(klass);

    // dc->realize = virtio_mmio_realizefn;
    // dc->reset = virtio_mmio_reset;
    // set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    // device_class_set_props(dc, virtio_mmio_properties);
}

static const TypeInfo virtio_xen_info = {
    .name          = TYPE_VIRTIO_XEN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtIOXenProxy),
    .class_init    = virtio_xen_class_init,
};


static const TypeInfo virtio_xen_device_info = {
    .name = TYPE_VIRTIO_XEN_DEVICE,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtioCcwDevice),
    .class_init = virtio_ccw_device_class_init,
    .class_size = sizeof(VirtIOCCWDeviceClass),
    .abstract = true,
};

static void virtio_xen_bus_class_init(ObjectClass *klass, void *data)
{
    qemu_printf("%s\n", __func__);
    // BusClass *bus_class = BUS_CLASS(klass);
    // VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);

    // k->notify = virtio_xen_update_irq;
    // k->save_config = virtio_xen_save_config;
    // k->load_config = virtio_xen_load_config;
    // k->save_extra_state = virtio_xen_save_extra_state;
    // k->load_extra_state = virtio_xen_load_extra_state;
    // k->has_extra_state = virtio_xen_has_extra_state;
    // k->set_guest_notifiers = virtio_xen_set_guest_notifiers;
    // k->ioeventfd_enabled = virtio_xen_ioeventfd_enabled;
    // k->ioeventfd_assign = virtio_xen_ioeventfd_assign;
    // k->pre_plugged = virtio_xen_pre_plugged;
    // k->vmstate_change = virtio_xen_vmstate_change;
    // k->has_variable_vring_alignment = true;
    // bus_class->max_dev = 1;
    // bus_class->get_dev_path = virtio_xen_bus_get_dev_path;
}

static const TypeInfo virtio_xen_bus_info = {
    .name          = TYPE_VIRTIO_XEN_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioBusState),
    .class_init    = virtio_xen_bus_class_init,
};

static void virtio_xen_register_types(void)
{
    type_register_static(&virtio_xen_bus_info);
    type_register_static(&virtio_xen_info);
}

type_init(virtio_xen_register_types)
