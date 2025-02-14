/*
 * virtio xen vhost-user-fs implementation
 *
 * Copyright (c) 2025 Edera
 *
 * Author:
 *  Alexander Merritt <alexander@edera.dev>
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-fs.h"
#include "hw/virtio/virtio-xen.h"
#include "qom/object.h"
#include "qemu/qemu-print.h"

struct VHostUserFSXen {
    VirtIOXenProxy parent_obj;
    VHostUserFS vdev;
};

typedef struct VHostUserFSXen VHostUserFSXen;

#define TYPE_VHOST_USER_FS_XEN "vhost-user-fs-xen"

DECLARE_INSTANCE_CHECKER(VHostUserFSXen, VHOST_USER_FS_XEN,
                         TYPE_VHOST_USER_FS_XEN)

#if 0
static Property vhost_user_fs_xen_properties[] = {
    // TODO: What to put here?
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_fs_xen_realize(VirtIOXenProxy *xen_dev, Error **errp)
{
    //VHostUserFSXen *dev = VHOST_USER_FS_XEN(xen_dev);
    //DeviceState *vdev = DEVICE(&dev->vdev);
    qemu_printf("%s\n", __func__);

    // TODO:

    // qdev_realize(vdev, BUS(&xen_dev->bus), errp);
}
#endif

static void vhost_user_fs_xen_instance_init(Object *obj)
{
    //VHostUserFSCcw *dev = VHOST_USER_FS_CCW(obj);
    //VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);

    //ccw_dev->force_revision_1 = true;
    //virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                //TYPE_VHOST_USER_FS);
}

static void vhost_user_fs_xen_class_init(ObjectClass *klass, void *data)
{
    // TODO:
    qemu_printf("%s\n", __func__);

    //DeviceClass *dc = DEVICE_CLASS(klass);
    //VirtioXenClass *k = VIRTIO_PCI_CLASS(klass);
    // PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    //k->realize = vhost_user_fs_xen_realize;
    //set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    //device_class_set_props(dc, vhost_user_fs_xen_properties);
    // pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    // pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    // pcidev_k->revision = 0x00;
    // pcidev_k->class_id = PCI_CLASS_STORAGE_OTHER;
}

static const TypeInfo vhost_user_fs_xen = {
    .name          = TYPE_VHOST_USER_FS_XEN,
    .parent        = TYPE_VIRTIO_XEN_DEVICE,
    .instance_size = sizeof(VHostUserFSXen),
    .instance_init = vhost_user_fs_xen_instance_init,
    .class_init    = vhost_user_fs_xen_class_init,
};

static void vhost_user_fs_ccw_register(void)
{
    type_register_static(&vhost_user_fs_xen);
}

type_init(vhost_user_fs_ccw_register)
