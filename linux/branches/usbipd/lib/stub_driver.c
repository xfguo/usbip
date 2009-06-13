/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include "usbip.h"

static const char *usbip_stub_driver_name = "usbip";


struct usbip_stub_driver *stub_driver;

/* only the first interface value is true! */
static int32_t read_attr_usbip_status(struct usb_device *udev)
{
	char attrpath[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	int value = 0;
	int  ret;

	snprintf(attrpath, SYSFS_PATH_MAX, "%s/%s:%d.%d/usbip_status",
			udev->path, udev->busid,
			udev->bConfigurationValue,
			0);

	attr = sysfs_open_attribute(attrpath);
	if (!attr) {
		err("open %s", attrpath);
		return -1;
	}

	ret = sysfs_read_attribute(attr);
	if (ret) {
		err("read %s", attrpath);
		sysfs_close_attribute(attr);
		return -1;
	}

	value = atoi(attr->value);

	sysfs_close_attribute(attr);

	return value;
}


static void usbip_exported_device_delete(void *dev)
{
	struct usbip_exported_device *edev =
		(struct usbip_exported_device *) dev;

	sysfs_close_device(edev->sudev);
	free(dev);
}

#if 0
do_init_dev(int fd)
{
	int ret;
	struct usbdevfs_urb urb, *r_urb;
	unsigned char setup1[8]={0,9,1,0,0,0,0,0};
	unsigned char setup2[8]={1,0xb,0,0,0,0,0,0};
	memset(&urb, 0, sizeof(urb));
	urb.type=USBDEVFS_URB_TYPE_CONTROL;
	urb.buffer=setup1;
	urb.buffer_length=8;
	ret=ioctl(fd, USBDEVFS_SUBMITURB, &urb);
	dbg("ret %d\n",ret);
	ret=ioctl(fd, USBDEVFS_REAPURB, &r_urb);
	dbg("ret reap %d\n", ret);
	if(r_urb!=&urb){
		dbg("faint a faint\n");
	}
	memset(&urb, 0, sizeof(urb));
	urb.type=USBDEVFS_URB_TYPE_CONTROL;
	urb.buffer=setup2;
	urb.buffer_length=8;
	ret=ioctl(fd, USBDEVFS_SUBMITURB, &urb);
	dbg("ret %d\n",ret);
	ret=ioctl(fd, USBDEVFS_REAPURB, &r_urb);
	dbg("ret reap %d\n", ret);
	if(r_urb!=&urb){
		dbg("faint a faint\n");
	}
}
#endif

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

	edev->processing_urbs = dlist_new(sizeof(AsyncURB));
	if(!edev->processing_urbs) {
		err("dlist_new_with_delete processing_urbs");
		return NULL;
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


static int check_new(struct dlist *dlist, struct sysfs_device *target)
{
	struct sysfs_device *dev;

	dlist_for_each_data(dlist, dev, struct sysfs_device) {
		if (!strncmp(dev->bus_id, target->bus_id, SYSFS_BUS_ID_SIZE))
			/* found. not new */
			return 0;
	}

	return 1;
}

static void delete_nothing(void *dev)
{
	/* do not delete anything. but, its container will be deleted. */
}

int export_device(char *busid)
{
	struct sysfs_device	*sudev;  /* sysfs_device of usb_device */
	struct usbip_exported_device *edev;

	sudev=sysfs_open_device("usb", busid);
	if(!sudev){
		err("can't export devce busid %s", busid);
		return -1;
	}
	edev = usbip_exported_device_new(sudev->path);
	if (!edev) {
		err("usbip_exported_device new");
		return -1;
	}
	dlist_unshift(stub_driver->edev_list, (void *) edev);
	stub_driver->ndevs++;
	printf("%d\n", stub_driver->ndevs);
	return 0;
}

static int refresh_exported_devices(void)
{
	struct sysfs_device	*suinf;  /* sysfs_device of usb_interface */
	struct dlist		*suinf_list;

	struct sysfs_device	*sudev;  /* sysfs_device of usb_device */
	struct dlist		*sudev_list;

#if 0
	sudev_list = dlist_new_with_delete(sizeof(struct sysfs_device), delete_nothing);

	suinf_list = sysfs_get_driver_devices(stub_driver->sysfs_driver);
	if (!suinf_list) {
		printf("Bind usbip.ko to a usb device to be exportable!\n");
		goto bye;
	}

	/* collect unique USB devices (not interfaces) */
	dlist_for_each_data(suinf_list, suinf, struct sysfs_device) {

		/* get usb device of this usb interface */
		sudev = sysfs_get_device_parent(suinf);
		if (!sudev) {
			err("get parent dev of %s", suinf->name);
			continue;
		}

		if (check_new(sudev_list, sudev)) {
			dlist_unshift(sudev_list, sudev);
		}
	}

	dlist_for_each_data(sudev_list, sudev, struct sysfs_device) {
		struct usbip_exported_device *edev;

		edev = usbip_exported_device_new(sudev->path);
		if (!edev) {
			err("usbip_exported_device new");
			continue;
		}

		dlist_unshift(stub_driver->edev_list, (void *) edev);
		stub_driver->ndevs++;
	}


	dlist_destroy(sudev_list);

bye:
#endif
	return 0;
}

int usbip_stub_refresh_device_list(void)
{
	int ret;
	return 0;
#if 0
	if (stub_driver->edev_list)
		dlist_destroy(stub_driver->edev_list);

	stub_driver->ndevs = 0;

	stub_driver->edev_list = dlist_new_with_delete(sizeof(struct usbip_exported_device),
			usbip_exported_device_delete);
	if (!stub_driver->edev_list) {
		err("alloc dlist");
		return -1;
	}

	ret = refresh_exported_devices();
	if (ret < 0)
		return ret;

	return 0;
#endif
}

int usbip_stub_driver_open(void)
{
	int ret;


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

int usbip_stub_export_device(struct usbip_exported_device *edev, int sockfd)
{
	int ret;
	if (edev->status != SDEV_ST_AVAILABLE) {
		info("device not available, %s", edev->udev.busid);
		switch( edev->status ) {
			case SDEV_ST_ERROR:
				info("     status SDEV_ST_ERROR");
				break;
			case SDEV_ST_USED:
				info("     status SDEV_ST_USED");
				break;
			default:
				info("     status unknown: 0x%x", edev->status);
		}
		return -1;
	}
	return 0;
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
