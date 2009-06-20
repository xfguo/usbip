/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include "usbip.h"

struct usbip_stub_driver *stub_driver;


static void usbip_exported_device_delete(void *dev)
{
	struct usbip_exported_device *edev =
		(struct usbip_exported_device *) dev;

	sysfs_close_device(edev->sudev);
	free(dev);
}

static int usb_host_claim_interfaces(struct usbip_exported_device *edev)
{
	int interface, ret;
	struct usbdevfs_ioctl ctrl;
	for (interface = 0; interface < edev->udev.bNumInterfaces; interface++) {
		ctrl.ioctl_code = USBDEVFS_DISCONNECT;
		ctrl.ifno = interface;
		ret = ioctl(edev->usbfs_fd, USBDEVFS_IOCTL, &ctrl);
		if (ret < 0 && errno != ENODATA) {
			err("USBDEVFS_DISCONNECT");
	                goto fail;
            	}
    	}

    /* XXX: only grab if all interfaces are free */
	for (interface = 0; interface < edev->udev.bNumInterfaces; interface++) {
		ret = ioctl(edev->usbfs_fd, USBDEVFS_CLAIMINTERFACE, &interface);
		if (ret < 0) {
			if (errno == EBUSY) {
				dbg("device already grabbed\n");
			} else {
				err("husb: failed to claim interface");
			}
			goto fail;
		}
	}
//	do_init_dev(edev->usbfs_fd); //for buggy usb disk
	dbg("husb: %d interfaces claimed\n",
           interface);
	return 0;
fail:
	return -1;
 
}

#define usb_host_device_path "/proc/bus/usb"

static int claim_dev(struct usbip_exported_device *edev)
{
    int fd = -1;
    struct usb_device *dev = &edev->udev;
    char buf[1024];

    dbg("husb: open device %d.%d\n", dev->busnum, dev->devnum);

    snprintf(buf, sizeof(buf), "%s/%03d/%03d", usb_host_device_path,
             dev->busnum, dev->devnum);
    fd = open(buf, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
	err("%s", buf);
        goto fail;
    }
    edev->usbfs_fd = fd;
    edev->client_fd = -1;
    dbg("opened %s\n", buf);
    if (usb_host_claim_interfaces(edev))
        goto fail;
#if 0
    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        err("usb_host_device_open: USBDEVFS_CONNECTINFO");
        goto fail;
    }
    dbg("husb: grabbed usb device %d.%d\n", bus_num, addr);

    ret = usb_linux_update_endp_table(dev);
    if (ret)
        goto fail;
#endif
    return 0;
fail:
    if(fd>=0)
	close(fd);
    return -1;
}

static struct usbip_exported_device *usbip_exported_device_new(char *sdevpath)
{
	struct usbip_exported_device *edev = NULL;

	edev = (struct usbip_exported_device *) calloc(1, sizeof(*edev));
	if (!edev) {
		err("alloc device");
		return NULL;
	}

	edev->sudev = sysfs_open_device_path(sdevpath);
	if (!edev->sudev) {
		err("open %s", sdevpath);
		goto err;
	}

	read_usb_device(edev->sudev, &edev->udev);

	edev->status = SDEV_ST_AVAILABLE;

	/* reallocate buffer to include usb interface data */
	size_t size = sizeof(*edev) + edev->udev.bNumInterfaces * sizeof(struct usb_interface);
	edev = (struct usbip_exported_device *) realloc(edev, size);
	if (!edev) {
		err("alloc device");
		goto err;
	}

	for (int i=0; i < edev->udev.bNumInterfaces; i++)
		read_usb_interface(&edev->udev, i, &edev->uinf[i]);

	if(claim_dev(edev)){
		err("claim device");
		goto err;
	}

	return edev;

err:
	if (edev && edev->processing_urbs)
		dlist_destroy(edev->processing_urbs);
	if (edev && edev->sudev)
		sysfs_close_device(edev->sudev);
	if (edev)
		free(edev);
	return NULL;
}

void unexport_device(struct usbip_exported_device * deleted_edev)
{
	struct usbip_exported_device * edev;
	dlist_for_each_data(stub_driver->edev_list, edev,
			struct usbip_exported_device) {
		if (edev!=deleted_edev)
			continue;
		dlist_delete_before(stub_driver->edev_list);
		dbg("delete edev ok\n");
		stub_driver->ndevs--;
		return;
	}
	err("can't found edev to deleted\n");
}

struct usbip_exported_device * export_device(char *busid)
{
	struct sysfs_device	*sudev;  /* sysfs_device of usb_device */
	struct usbip_exported_device *edev;

	sudev=sysfs_open_device("usb", busid);
	if(!sudev){
		err("can't export devce busid %s", busid);
		return NULL;
	}
	edev = usbip_exported_device_new(sudev->path);
	if (!edev) {
		err("usbip_exported_device new");
		return NULL;
	}
	dbg("export dev edev: %p, usbfs fd: %d\n", edev, edev->usbfs_fd);
	dlist_unshift(stub_driver->edev_list, (void *) edev);
	stub_driver->ndevs++;
	dbg("%d devices exported\n", stub_driver->ndevs);
	return edev;
}

int usbip_stub_driver_open(void)
{
	stub_driver = (struct usbip_stub_driver *) calloc(1, sizeof(*stub_driver));
	if (!stub_driver) {
		err("alloc stub_driver");
		return -1;
	}

	stub_driver->ndevs = 0;

	stub_driver->edev_list = dlist_new_with_delete(sizeof(struct usbip_exported_device),
			usbip_exported_device_delete);
	if (!stub_driver->edev_list) {
		err("alloc dlist");
		goto err;
	}
#if 0
	ret = refresh_exported_devices();
	if (ret < 0)
		goto err;
#endif
	return 0;


err:
	if (stub_driver->edev_list)
		dlist_destroy(stub_driver->edev_list);
	free(stub_driver);

	stub_driver = NULL;
	return -1;
}


void usbip_stub_driver_close(void)
{
	if (!stub_driver)
		return;

	if (stub_driver->edev_list)
		dlist_destroy(stub_driver->edev_list);
	free(stub_driver);

	stub_driver = NULL;
}

struct usbip_exported_device *usbip_stub_get_device(int num)
{
	struct usbip_exported_device *edev;
	struct dlist		*dlist = stub_driver->edev_list;
	int count = 0;

	dlist_for_each_data(dlist, edev, struct usbip_exported_device) {
		if (num == count)
			return edev;
		else
			count++ ;
	}

	return NULL;
}
