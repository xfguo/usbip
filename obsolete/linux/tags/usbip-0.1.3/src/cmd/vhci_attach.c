/*
 * Copyright (C) 2005-2006 Takahiro Hirofuchi
 */

#include "usbip.h"
#include "usbip_network.h"
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static const char version[] = "$Id: vhci_attach.c 97 2006-03-31 16:08:40Z taka-hir $";


/* /sys/devices/platform/vhci_hcd/usb6/6-1/6-1:1.1  -> 1 */
static int get_interface_number(struct usbip_vhci_driver *driver, char *path)
{
	char *c;

	c = strstr(path, driver->hc_device->bus_id);
	if(!c) 
		return -1;	/* hc exist? */
	c++;
	/* -> usb6/6-1/6-1:1.1 */

	c = strchr(c, '/');
	if(!c)
		return -1;	/* hc exist? */
	c++;
	/* -> 6-1/6-1:1.1 */

	c = strchr(c, '/');
	if(!c) 
		return -1;	/* no interface path */
	c++;
	/* -> 6-1:1.1 */

	c = strchr(c, ':');
	if(!c)
		return -1;	/* no configuration? */
	c++;
	/* -> 1.1 */

	c = strchr(c, '.');
	if(!c)
		return -1;	/* no interface? */
	c++;
	/* -> 1 */


	return atoi(c);
}


static struct sysfs_device *open_usb_interface(struct usb_device *udev, int i)
{
	struct sysfs_device *suinf;
	char busid[SYSFS_BUS_ID_SIZE];

	snprintf(busid, SYSFS_BUS_ID_SIZE, "%s:%d.%d",
			udev->busid, udev->bConfigurationValue, i);

	suinf = sysfs_open_device("usb", busid);
	if(!suinf)
		err("sysfs_open_device %s", busid);

	return suinf;
}


#define MAX_BUFF 100
static int record_connection(char *host, char *port, char *busid, int rhport)
{
	int fd;
	char path[PATH_MAX+1];
	char buff[MAX_BUFF+1];
	int ret;

	mkdir("/tmp/vhci_hcd", 0700);

	snprintf(path, PATH_MAX, "/tmp/vhci_hcd/port%d", rhport);

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	if (fd < 0)
		return -1;

	snprintf(buff, MAX_BUFF, "%s %s %s\n", 
			host, port, busid);

	ret = write(fd, buff, strlen(buff));
	if (ret != (ssize_t) strlen(buff)) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int read_record(int rhport, char *host, char *port, char *busid)
{
	FILE *file;
	char path[PATH_MAX+1];

	snprintf(path, PATH_MAX, "/tmp/vhci_hcd/port%d", rhport);

	file = fopen(path, "r");
	if (!file) {
		err("fopen");
		return -1;
	}

	fscanf(file, "%s %s %s\n", host, port, busid);

	fclose(file);

	return 0;
}


void usbip_vhci_imported_device_dump(struct usbip_vhci_driver *driver, struct usbip_imported_device *idev)
{
	char product_name[100];
	char host[NI_MAXHOST] = "unknown host";
	char serv[NI_MAXSERV] = "unknown port";
	char remote_busid[SYSFS_BUS_ID_SIZE];
	int ret;

	if(idev->status == VDEV_ST_NULL || idev->status == VDEV_ST_NOTASSIGNED) {
		info("Port %02d: <%s>", idev->port, usbip_status_string(idev->status));
		return;
	}

	ret = read_record(idev->port, host, serv, remote_busid);
	if(ret)
		err("red_record");

	info("Port %02d: <%s> at %s", idev->port,
			usbip_status_string(idev->status), usbip_speed_string(idev->udev.speed));

	usbip_product_name(product_name, sizeof(product_name),
			idev->udev.idVendor, idev->udev.idProduct);

	info("       %s",  product_name);

	info("%10s -> usbip://%s:%s/%s  (remote bus/dev %03d/%03d)",
			idev->udev.busid, host, serv, remote_busid,
			idev->busnum, idev->devnum);

	for(int i=0; i < idev->udev.bNumInterfaces; i++) {
		/* show interface information */
		struct sysfs_device *suinf;

		suinf = open_usb_interface(&idev->udev, i);
		if(!suinf)
			continue;
		info("       %6s used by %-17s", suinf->bus_id, suinf->driver_name);
		sysfs_close_device(suinf);

		/* show class device information */
		struct class_device *cdev;

		dlist_for_each_data(idev->cdev_list, cdev, struct class_device) {
			int ifnum = get_interface_number(driver, cdev->devpath);
			if(ifnum == i) {
				info("       %6s         %s", " ", cdev->clspath);
			}
		}
	}
}

/* IPv6 Ready */
int tcp_connect(char *hostname, char *service)
{
	struct addrinfo hints, *res, *res0;
	int sockfd;
	int err;


	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	/* get all possible addresses */
	err = getaddrinfo(hostname, service, &hints, &res0);
	if(err) {
		err("%s %s: %s", hostname, service, gai_strerror(err));
		return -1;
	}

	/* try all the addresses */
	for(res = res0; res; res = res->ai_next) {
		char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

		err = getnameinfo(res->ai_addr, res->ai_addrlen,
				hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
		if(err) {
			err("%s %s: %s", hostname, service, gai_strerror(err));
			continue;
		}

		dbg("trying %s port %s\n", hbuf, sbuf);

		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if(sockfd < 0) {
			err("socket");
			continue;
		}

		err = connect(sockfd, res->ai_addr, res->ai_addrlen);
		if(err < 0) {
			close(sockfd);
			continue;
		}

		/* connected */
		dbg("connected to %s:%s", hbuf, sbuf);
		freeaddrinfo(res0);
		return sockfd;
	}


	dbg("%s:%s, %s", hostname, service, "no destination to connect to");
	freeaddrinfo(res0);

	return -1;
}



static int query_exported_devices(int sockfd)
{
	int ret;
	struct op_devlist_reply rep;
	uint16_t code = OP_REP_DEVLIST;

	bzero(&rep, sizeof(rep));

	ret = usbip_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if(ret < 0) {
		err("send op_common");
		return -1;
	}

	ret = usbip_recv_op_common(sockfd, &code);
	if(ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *) &rep, sizeof(rep));
	if(ret < 0) {
		err("recv op_devlist");
		return -1;
	}

	PACK_OP_DEVLIST_REPLY(0, &rep);
	dbg("exportable %d devices", rep.ndev);

	for(unsigned int i=0; i < rep.ndev; i++) {
		char product_name[100];
		char class_name[100];
		struct usb_device udev;

		bzero(&udev, sizeof(udev));

		ret = usbip_recv(sockfd, (void *) &udev, sizeof(udev));
		if(ret < 0) {
			err("recv usb_device[%d]", i);
			return -1;
		}
		pack_usb_device(0, &udev);

		usbip_product_name(product_name, sizeof(product_name),
				udev.idVendor, udev.idProduct);
		usbip_class_name(class_name, sizeof(class_name), udev.bDeviceClass,
				udev.bDeviceSubClass, udev.bDeviceProtocol);

		info("%8s: %s", udev.busid, product_name);
		info("%8s: %s", " ", udev.path);
		info("%8s: %s", " ", class_name);

		for(int j=0; j < udev.bNumInterfaces; j++) {
			struct usb_interface uinf;

			ret = usbip_recv(sockfd, (void *) &uinf, sizeof(uinf));
			if(ret < 0) {
				err("recv usb_interface[%d]", j);
				return -1;
			}

			pack_usb_interface(0, &uinf);
			usbip_class_name(class_name, sizeof(class_name), uinf.bInterfaceClass,
					uinf.bInterfaceSubClass, uinf.bInterfaceProtocol);

			info("%8s: %2d - %s", " ", j, class_name);
		}

		info(" ");
	}

	return rep.ndev;
}

static int import_device(int sockfd, struct usb_device *udev)
{
	int ret;
	struct usbip_vhci_driver *driver;
	int port;

	driver = usbip_vhci_driver_open();
	if(!driver) {
		err("open vhci_driver");
		return -1;
	}

	port = usbip_vhci_get_free_port(driver);
	if(port < 0) {
		err("no free port");
		usbip_vhci_driver_close(driver);
		return -1;
	}

	ret = usbip_vhci_import_device(driver, port, sockfd, udev);
	if(ret < 0) {
		err("import device");
		usbip_vhci_driver_close(driver);
		return -1;
	}

	usbip_vhci_driver_close(driver);

	return port;
}


static int query_import_device(int sockfd, char *busid)
{
	int ret;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;

	bzero(&request, sizeof(request));
	bzero(&reply, sizeof(reply));


	/* send a request */
	ret = usbip_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if(ret < 0) {
		err("send op_common");
		return -1;
	}

	memcpy(&request.busid, busid, SYSFS_BUS_ID_SIZE);

	PACK_OP_IMPORT_REQUEST(0, &request);

	ret = usbip_send(sockfd, (void *) &request, sizeof(request));
	if(ret < 0) {
		err("send op_import_request");
		return -1;
	}


	/* recieve a reply */
	ret = usbip_recv_op_common(sockfd, &code);
	if(ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *) &reply, sizeof(reply));
	if(ret < 0) {
		err("recv op_import_reply");
		return -1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);


	/* check the reply */
	if(strncmp(reply.udev.busid, busid, SYSFS_BUS_ID_SIZE)) {
		err("recv different busid %s", reply.udev.busid);
		return -1;
	}


	/* import a device */
	return import_device(sockfd, &reply.udev);
}

static void attach_device(char *host, char *busid)
{
	int sockfd;
	int ret;
	int rhport;

	sockfd = tcp_connect(host, USBIP_PORT_STRING);
	if(sockfd < 0) {
		err("tcp connect");
		return;
	}

	rhport = query_import_device(sockfd, busid);
	if(rhport < 0) {
		err("query");
		return;
	}

	close(sockfd);

	ret = record_connection(host, USBIP_PORT_STRING,
			busid, rhport);
	if (ret < 0) 
		err("record connection");

	return;
}

static void detach_port(char *port)
{
	struct usbip_vhci_driver *driver;
	uint8_t portnum;

	for(unsigned int i=0; i < strlen(port); i++)
		if(!isdigit(port[i])) {
			err("invalid port %s", port);
			return;
		}

	/* check max port */

	portnum = atoi(port);

	driver = usbip_vhci_driver_open();
	if(!driver) {
		err("open vhci_driver");
		return;
	}

	usbip_vhci_detach_device(driver, portnum);

	usbip_vhci_driver_close(driver);
}

static void show_exported_devices(char *host)
{
	int ret;
	int sockfd;

	sockfd = tcp_connect(host, USBIP_PORT_STRING);
	if(sockfd < 0) {
		info("- %s failed", host);
		return;
	}

	info("- %s", host);

	ret = query_exported_devices(sockfd);
	if(ret < 0) {
		err("query");
	}

	close(sockfd);
}


const char help_message[] = "\
Usage: usbip [options]				\n\
	-a, --attach [host] [bus_id]		\n\
		Attach a remote USB device.	\n\
						\n\
	-d, --detach [ports]			\n\
		Detach an imported USB device.	\n\
						\n\
	-l, --list [hosts]			\n\
		List exported USB devices.	\n\
						\n\
	-p, --port				\n\
		List virtual USB port status. 	\n\
						\n\
	-D, --debug				\n\
		Print debugging information.	\n\
						\n\
	-v, --version				\n\
		Show version.			\n\
						\n\
	-h, --help 				\n\
		Print this help.		\n";

static void show_help(void)
{
	printf("%s", help_message);
}

static void show_port_status(void) 
{
	struct usbip_imported_device *idev;
	struct usbip_vhci_driver *driver;

	driver = usbip_vhci_driver_open();
	if(!driver) {
		return;
	}

	for(int i = 0; i < driver->nports; i++) {
		idev = &driver->idev[i];

		usbip_vhci_imported_device_dump(driver, idev);
	}

	usbip_vhci_driver_close(driver);
}

#define _GNU_SOURCE
#include <getopt.h>
static const struct option longopts[] = {
	{"attach",	no_argument,	NULL, 'a'},
	{"detach",	no_argument,	NULL, 'd'},
	{"port",	no_argument,	NULL, 'p'},
	{"list",	no_argument,	NULL, 'l'},
	{"version",	no_argument,	NULL, 'v'},
	{"help",	no_argument,	NULL, 'h'},
	{"debug",	no_argument,	NULL, 'D'},
	{"syslog",	no_argument,	NULL, 'S'},
	{NULL,		0,		NULL,  0}
};

int main(int argc, char *argv[])
{
	int ret;

	enum {
		cmd_attach = 1,
		cmd_detach,
		cmd_port,
		cmd_list,
		cmd_help,
		cmd_version
	} cmd = 0;

	usbip_use_stderr = 1;

 	ret = names_init(USBIDS_FILE);
 	if(ret)
 		err("open usb.ids");

	while(1) {
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "adplvhDS", longopts, &index);

		if(c == -1)
			break;

		switch(c) {
			case 'a':
				if(!cmd)
					cmd = cmd_attach;
				else
					cmd = cmd_help;
				break;
			case 'd':
				if(!cmd)
					cmd = cmd_detach;
				else
					cmd = cmd_help;
				break;
			case 'p':
				if(!cmd)
					cmd = cmd_port;
				else
					cmd = cmd_help;
				break;
			case 'l':
				if(!cmd)
					cmd = cmd_list;
				else
					cmd = cmd_help;
				break;
			case 'v':
				if(!cmd)
					cmd = cmd_version;
				else
					cmd = cmd_help;
				break;
			case 'h':
				cmd = cmd_help;
				break;
			case 'D':
				usbip_use_debug = 1;
				break;
			case 'S':
				usbip_use_syslog = 1;
				break;
			case '?':
				break;

			default:
				err("getopt");
		}
	}


	switch(cmd) {
		case cmd_attach:
			if(optind == argc - 2)
				attach_device(argv[optind], argv[optind+1]);
			else
				show_help();
			break;
		case cmd_detach:
			while(optind < argc)
				detach_port(argv[optind++]);
			break;
		case cmd_port:
			show_port_status();
			break;
		case cmd_list:
			while(optind < argc)
				show_exported_devices(argv[optind++]);
			break;
		case cmd_version:
			printf("%s\n", version);
			break;
		case cmd_help:
			show_help();
			break;
		default:
			info("unknown cmd");
			show_help();
	}


	names_deinit();

	return 0;
}
