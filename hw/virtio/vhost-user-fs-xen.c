/*
 * virtio xen vhost-user-fs implementation
 *
 * Copyright (c) 2025 Edera
 *
 * Author:
 *  Alexander Merritt <alexander@edera.dev>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-fs.h"
#include "hw/virtio/virtio-xen.h"
#include "qom/object.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h" // error_get_*

#define QPRINT qemu_printf("%s\n", __func__)

typedef struct VHostUserFSXen {
    VirtioXenDevice parent_obj;
    VHostUserFS vdev;
} VHostUserFSXen;

#define TYPE_VHOST_USER_FS_XEN "vhost-user-fs-xen"
DECLARE_INSTANCE_CHECKER(VHostUserFSXen, VHOST_USER_FS_XEN,
                         TYPE_VHOST_USER_FS_XEN)

static Property vhost_user_fs_xen_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_fs_xen_realize(VirtioXenDevice *xen_dev, Error **errp)
{
    QPRINT;

    VHostUserFSXen *dev = VHOST_USER_FS_XEN(xen_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    // initialize and plug the device into the specified bus
    qdev_realize(vdev, BUS(&xen_dev->bus), errp);
    if (!qdev_is_realized(vdev)) {
        qemu_printf("%s: vdev not realized\n", __func__);
        if (errp)
            qemu_printf("%s\n", error_get_pretty(*errp));
    }
    qemu_printf("%s exit\n", __func__);
}

static void vhost_user_fs_xen_instance_init(Object *obj)
{
    QPRINT;

    VHostUserFSXen *dev = VHOST_USER_FS_XEN(obj);
    // VirtioXenDevice *xen_dev = VIRTIO_XEN_DEVICE(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_FS);
}

static void vhost_user_fs_xen_class_init(ObjectClass *klass, void *data)
{
    QPRINT;

    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioXenDeviceClass *k = VIRTIO_XEN_DEVICE_CLASS(klass);

    k->realize = vhost_user_fs_xen_realize;
    device_class_set_props(dc, vhost_user_fs_xen_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo vhost_user_fs_xen_info = {
    .name          = TYPE_VHOST_USER_FS_XEN,
    .parent        = TYPE_VIRTIO_XEN_DEVICE,
    .instance_size = sizeof(VHostUserFSXen),
    .instance_init = vhost_user_fs_xen_instance_init,
    .class_init    = vhost_user_fs_xen_class_init,
};

static void vhost_user_fs_xen_register(void)
{
    printf("%s\n", __func__);
    type_register_static(&vhost_user_fs_xen_info);
}

type_init(vhost_user_fs_xen_register)

// NOTE: From LW patches:
// every action in the bus class, there is reference
// to the xen virtio device, as well as a virtio device (base class?)
