/*
 * $Id$
 *
 * Copyright (C) 2007 by Robert Leibl <robert.leibl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either 
 * version 2 of the License, or (at your option) any later 
 * version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#ifndef _USBIP_EXPORT_H_
#define _USBIP_EXPORT_H_

#include <stub_driver.h>
#include <glib.h>

#include "usbip_network.h"

/*
 * XXX This is defined in usbaid.c
 * Use a common header!
 */
#define USBAID_PORT_STRING "6000"


int export_busid_to_host(char *host, char *busid);
int unexport_busid_from_host(char *host, char *busid);

int send_request_export(int sockfd, struct usb_device *udev);
int send_request_unexport(int sockfd, struct usb_device *udev);

struct usbip_exported_device *busid_to_edev(char *busid);



#endif /* _USBIP_EXPORT_H_ */
