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
#include "qemu/error-report.h" // error_get_*

typedef struct VHostUserFSXen {
    VirtioXenDevice parent_obj;
    VHostUserFS vdev;
} VHostUserFSXen;

//#define TYPE_VHOST_USER_FS_XEN "vhost-user-fs-xen"
#define TYPE_VHOST_USER_FS_XEN "virtio-fs"
DECLARE_INSTANCE_CHECKER(VHostUserFSXen, VHOST_USER_FS_XEN,
                         TYPE_VHOST_USER_FS_XEN)

// QEMU cmdline properties for this device
static Property vhost_user_fs_xen_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_fs_xen_realize(VirtioXenDevice *xen_dev, Error **errp)
{
    ERRP_GUARD();

    VUF_DBG("");

    VHostUserFSXen *dev = VHOST_USER_FS_XEN(xen_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIODevice *vd = VIRTIO_DEVICE(vdev);

    xen_dev->vd = vd; // virtio(-xen) callbacks need this

    // FIXME: Set feature bits? Do we do this here?
    // See hw/virito/vhost-user.h for feature bits
    // If we do not, then why does QEMU complain:
    // warning: vhost-user backend supports VHOST_USER_PROTOCOL_F_CONFIG but QEMU does not.

    virtio_add_feature(&vd->host_features, VIRTIO_F_VERSION_1);
    virtio_add_feature(&vd->host_features, VIRTIO_F_ACCESS_PLATFORM);


    qdev_realize(vdev, BUS(&xen_dev->bus), errp); // -> vuf_device_realize
    if (!qdev_is_realized(vdev) && errp)
        VUF_QERR(*errp, "qdev_realize:");
}

static void vhost_user_fs_xen_instance_init(Object *obj)
{
    VUF_DBG("");

    VHostUserFSXen *dev = VHOST_USER_FS_XEN(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_FS);
}

static void vhost_user_fs_xen_class_init(ObjectClass *klass, void *data)
{
    VUF_DBG("");

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
