/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef _USBIP_STUB_DRIVER_H
#define _USBIP_STUB_DRIVER_H

#include "usbip.h"

typedef struct _AsyncURB
{
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc isocpd;
    char *data;
    unsigned int seqnum;
    unsigned int sub_seqnum;
    unsigned int data_len;
    unsigned int ret_len;
    unsigned int big_bulk_in;
} AsyncURB;

struct usbip_stub_driver {
	int ndevs;
	struct dlist *edev_list;	/* list of exported device */
};

struct big_in_ep {
	struct dlist * waited_urbs;
	AsyncURB * now_urb;
};

struct usbip_exported_device {
	struct sysfs_device *sudev;
	int32_t status;
	int usbfs_fd;
	int client_fd;
	int usbfs_gio_id;
	int client_gio_id;
	struct big_in_ep * big_in_eps[128];
	struct dlist * processing_urbs;
	struct usb_device    udev;
	struct usb_interface uinf[];
};

extern struct usbip_stub_driver *stub_driver;

int usbip_stub_driver_open(void);
void usbip_stub_driver_close(void);

int usbip_stub_refresh_device_list(void);
int usbip_stub_export_device(struct usbip_exported_device *edev);

struct usbip_exported_device * export_device(char * busid);
void unexport_device(struct usbip_exported_device * deleted_edev);

struct usbip_exported_device *usbip_stub_get_device(int num);

#endif
