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

#ifndef HW_VIRTIO_XEN_H
#define HW_VIRTIO_XEN_H

#include "hw/sysbus.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/xen/xen-bus.h"

#define VUF_DBG(fmt, ...) \
    do { \
        qemu_printf("%s: " fmt "\n", __func__, ## __VA_ARGS__); \
    } while (0)

#define NOT_IMPL VUF_DBG("not implemented")

#define VUF_QERR(errp, fmt, ...) \
    do { \
        error_report(fmt ": %s" "\n", ## __VA_ARGS__, \
                     error_get_pretty(errp)); \
    } while (0)


/* virtio-xen-bus */

typedef struct VirtioBusState VirtioXenBusState;
typedef struct VirtioBusClass VirtioXenBusClass;

#define TYPE_VIRTIO_XEN_BUS "virtio-xen-bus"
DECLARE_OBJ_CHECKERS(VirtioXenBusState, VirtioXenBusClass,
                     VIRTIO_XEN_BUS, TYPE_VIRTIO_XEN_BUS)

/* virtio-xen */

#define TYPE_VIRTIO_XEN_DEVICE "virtio-xen-device"
OBJECT_DECLARE_TYPE(VirtioXenDevice, VirtioXenDeviceClass, VIRTIO_XEN_DEVICE)

struct VirtioXenDeviceClass {
    XenDeviceClass parent_class;
    void (*realize)(VirtioXenDevice *dev, Error **errp);
    void (*unrealize)(VirtioXenDevice *dev);
    void (*parent_reset)(DeviceState *dev);
};

struct VirtioXenDevice {
    XenDevice parent_obj;
    VirtioBusState bus;

    VirtIODevice *vio_dev;
    uint32_t host_features;

    // TODO: Pull in items shared by guest via xenbus:
    // void *page;
    // int conf_page_ref;
    // XenEvtchn notify_evtchndev;
    // int notify_local_port;
    // int notify_remote_port;
};

#endif
