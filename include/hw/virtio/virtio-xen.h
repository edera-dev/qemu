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

/* From Linux frontend */

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
#define VIRTIO_XENBUS_CONFIG_OFF           20

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

// NOTE: must match frontend definition
struct VirtioConfigPage {
    uint8_t config[256];
    uint32_t write;
    uint32_t size;
    uint32_t offset;
    uint32_t be_active;
};

struct VirtioXenDevice {
    XenDevice parent_obj;
    VirtioBusState bus;

    // subclass of TYPE_VIRTIO_XEN_DEVICE must set this
    VirtIODevice *vd;

    uint32_t host_features;

    xenevtchn_handle *evtchn; // TODO: remove, not needed
    xengnttab_handle *gnttab; // TODO: remove, not needed

    // virtqueue
    evtchn_port_t notify_local;
    evtchn_port_t notify_remote;

    // configuration page
    uint32_t conf_gntref;
    evtchn_port_t conf_remote;
    struct VirtioConfigPage *conf_page;

    // NOTE: I think the modern API uses this new type
    XenEventChannel *notify;
    XenEventChannel *conf;
};

#endif
