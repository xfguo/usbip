/*
 * $Id$
 *
 * Copyright (C) 2007 Robert Leibl <robert.leibl@gmail.com>
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

#include "usbip_export.h"

/*
int usbip_use_debug  = 1;
int usbip_use_stderr = 1;
int usbip_use_syslog = 0;
*/


/*
 * exports the device on @busid to @host.
 *
 * few function calls. But error checking makes this function long and ugly.
 */
int export_busid_to_host(char *host, char *busid) {

	int ret;
	int sockfd;
	uint16_t code = OP_REP_EXPORT;
	struct usbip_exported_device *edev;

	/*
	 * open stub driver
	 */
	ret = usbip_stub_driver_open();
	if( ret != 0 ) {
		logerr( "could not open stub_driver");
		return -1;
	}

	/*
	 * get the relevant device
	 */
	edev = busid_to_edev(busid);
	if( edev == NULL ) {
		logerr( "no device found matching busid" );
		goto exit_failure;
	}

	/*
	 * Open connection and tell server* we want to export a device
	 *
	 * * server here means 'remote host', 'OS-Server', ...
	 *   This is the machine thats runs the virtual host controller
	 */
	sockfd = tcp_connect(host, USBAID_PORT_STRING);
	if( sockfd < 0 ) {
		logerr("tcp connection failed");
		goto exit_failure;
	} 
	logdbg("tcp connection established");

	/*
	 * mark device as exported
	 */
	ret = usbip_stub_export_device(edev, sockfd);
	if( ret < 0 ) {
		logerr( "exporting of the device failed" );
		goto exit_failure;
	}
	logdbg("devices marked as exported");


	/*
	 * now, tell server
	 */
	ret = usbip_send_op_common( sockfd, OP_REQ_EXPORT, 0 );
	if( ret < 0 ) {
		logerr( "sending OP_REQ_EXPORT failed" );
		goto exit_failure;
	}
	logdbg("export request (OP_COMMON) sent");

	ret = send_request_export( sockfd, &(edev->udev) );
	if( ret < 0 ) {
		logerr( "sending export request failed" );
		goto exit_failure;
	}
	logdbg("export request (device) sent");
	logdbg("device exported" );

	/*
	 * We do not wait for a status notification. see
	 * usbaid.c::handle_export_query() for details.
	 *
	 * For now, we simply assume that the export was successfull
	 */

	usbip_stub_driver_close();
	return 0;

exit_failure:
	close(sockfd);
	usbip_stub_driver_close();
	return -1;
}

int unexport_busid_from_host(char *host, char *busid) {

	int ret;
	int sockfd;
	uint16_t code = OP_REP_UNEXPORT;
	struct usbip_exported_device *edev;

	/*
	 * open stub driver
	 */
	ret = usbip_stub_driver_open();
	if( ret != 0 ) {
		logerr( "could not open stub_driver");
		return -1;
	}

	/*
	 * get the relevant device
	 */
	edev = busid_to_edev(busid);
	if( edev == NULL ) {
		logerr( "no device found matching busid" );
		goto exit_failure;
	}

	/*
	 * Open connection and tell server we want to unexport a device
	 */
	sockfd = tcp_connect(host, USBAID_PORT_STRING);
	if( sockfd < 0 ) {
		logerr("tcp connection failed");
		goto exit_failure;
	} 
	logdbg("tcp connection established");

	/*
	 * now, tell server
	 */
	ret = usbip_send_op_common( sockfd, OP_REQ_UNEXPORT, 0 );
	if( ret < 0 ) {
		logerr( "sending OP_REQ_UNEXPORT failed" );
		goto exit_failure;
	}
	logdbg("unexport request (OP_COMMON) sent");

	ret = send_request_unexport( sockfd, &(edev->udev) );
	if( ret < 0 ) {
		logerr( "sending unexport request failed" );
		goto exit_failure;
	}
	logdbg("unexport request (device) sent");

	/*
	 * mark device as no longer exported
	 */
	/*XXX There seems to be no action on the 'client' side neccessary.
	 * The server needs to be told, that it should detach the device. 
	 * On the client side, the device is 'unexported' by releasing it from
	 * the usbip driver.
	 */


	/* receive status message from server */
	ret = usbip_recv_op_common(sockfd, &code);
	if( ret < 0 ) {
		logerr( "receiving op_common failed" );
		goto exit_failure;
	}

	usbip_stub_driver_close();
	return 0;

exit_failure:
	usbip_stub_driver_close();
	return -1;
}

/*
 * FIXME this may need a rewrite:
 * 	we do not send a struct op_... but just the usb device
 * 	This may cause errors, if the struct op_export_request is changed
 */
int send_request_export(int sockfd, struct usb_device *udev) {

	int ret;
	struct usb_device pdu_udev;

	memcpy( &pdu_udev, udev, sizeof(pdu_udev) );
	pack_usb_device(1, &pdu_udev);

	logdbg("sending usb device: (-) %u %u %u %u",
			sockfd, pdu_udev.busnum, pdu_udev.devnum, pdu_udev.speed );

	ret = usbip_send(sockfd, (void*)&pdu_udev, sizeof(pdu_udev));
	if( ret < 0 ) {
		logerr( "sending deviceinfo failed" );
		return -1;
	}

	return 0;
}

int send_request_unexport(int sockfd, struct usb_device *udev) {

	int ret;
	struct usb_device pdu_udev;
	
	memcpy( &pdu_udev, udev, sizeof(pdu_udev) );
	pack_usb_device(1, &pdu_udev);

	logdbg("sending usb device: (-) %u %u %u %u",
			sockfd, pdu_udev.busnum, pdu_udev.devnum, pdu_udev.speed );

	ret = usbip_send(sockfd, (void*)&pdu_udev, sizeof(pdu_udev));
	if( ret < 0 ) {
		logerr( "sending deviceinfo failed" );
		return -1;
	}

	return 0;
}

/*
 * Searches the list of currently exported devices for the device with the
 * given busid. 
 *
 * @return
 * 	the corresponding 'struct usbip_exported_device'
 */
struct usbip_exported_device *busid_to_edev(char *busid) {

	struct usbip_exported_device *edev = NULL;

	dlist_for_each_data(stub_driver->edev_list, edev, struct usbip_exported_device) {
		if (!strncmp(busid, edev->udev.busid, SYSFS_BUS_ID_SIZE)) {
			logdbg("found requested device %s", busid);
			//udev = &(edev->udev);
			break;
		}
	}
	printf("device status: %d\n", edev->status);


	return edev;
}



