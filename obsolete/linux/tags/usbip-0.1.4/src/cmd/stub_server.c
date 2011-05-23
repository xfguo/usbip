/*
 * Copyright (C) 2005-2006 Takahiro Hirofuchi
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <unistd.h>
#include <netdb.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_LIBWRAP
#include <tcpd.h>
#endif

#define _GNU_SOURCE
#include <getopt.h>
#include <signal.h>

#include "usbip.h"
#include "usbip_network.h"

static const char version[] = PACKAGE_STRING
	" ($Id: stub_server.c 97 2006-03-31 16:08:40Z taka-hir $)";

static struct usbip_stub_driver *StubDriver;

static int send_reply_devlist(struct usbip_stub_driver *driver, int sockfd)
{
	int ret;
	struct usbip_exported_device *edev;
	struct op_devlist_reply reply;


	reply.ndev = 0;

	/* how many devices are exported ? */
	dlist_for_each_data(driver->edev_list, edev, struct usbip_exported_device) {
		reply.ndev += 1;
	}

	dbg("%d devices are exported", reply.ndev);

	ret = usbip_send_op_common(sockfd, OP_REP_DEVLIST,  ST_OK);
	if(ret < 0) {
		err("send op_common");
		return ret;
	}

	PACK_OP_DEVLIST_REPLY(1, &reply);

	ret = usbip_send(sockfd, (void *) &reply, sizeof(reply));
	if(ret < 0) {
		err("send op_devlist_reply");
		return ret;
	}

	dlist_for_each_data(driver->edev_list, edev, struct usbip_exported_device) {
		struct usb_device pdu_udev;

		dump_usb_device(&edev->udev);
		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		pack_usb_device(1, &pdu_udev);

		ret = usbip_send(sockfd, (void *) &pdu_udev, sizeof(pdu_udev));
		if(ret < 0) {
			err("send pdu_udev");
			return ret;
		}

		for(int i=0; i < edev->udev.bNumInterfaces; i++) {
			struct usb_interface pdu_uinf;

			dump_usb_interface(&edev->uinf[i]);
			memcpy(&pdu_uinf, &edev->uinf[i], sizeof(pdu_uinf));
			pack_usb_interface(1, &pdu_uinf);

			ret = usbip_send(sockfd, (void *) &pdu_uinf, sizeof(pdu_uinf));
			if(ret < 0) {
				err("send pdu_uinf");
				return ret;
			}
		}
	}

	return 0;
}


static int recv_request_devlist(int sockfd)
{
	int ret;
	struct op_devlist_request req;

	bzero(&req, sizeof(req));

	ret = usbip_recv(sockfd, (void *) &req, sizeof(req));
	if(ret < 0) {
		err("recv devlist request");
		return -1;
	}

	ret = send_reply_devlist(StubDriver, sockfd);
	if(ret < 0) {
		err("send devlist reply");
		return -1;
	}

	return 0;
}


static int recv_request_import(int sockfd)
{
	int ret;
	struct op_import_request req;
	struct op_common reply;
	struct usbip_exported_device *edev;
	int found = 0;
	int error = 0;

	bzero(&req, sizeof(req));
	bzero(&reply, sizeof(reply));

	ret = usbip_recv(sockfd, (void *) &req, sizeof(req));
	if(ret < 0) {
		err("recv import request");
		return -1;
	}

	PACK_OP_IMPORT_REQUEST(0, &req);

	dlist_for_each_data(StubDriver->edev_list, edev, struct usbip_exported_device) {
		if(!strncmp(req.busid, edev->udev.busid, SYSFS_BUS_ID_SIZE)) {
			dbg("found requested device %s", req.busid);
			found = 1;
			break;
		}
	}

	if(found) {
		/* export_device needs a TCP/IP socket descriptor */
		ret = usbip_stub_export_device(StubDriver, edev, sockfd);
		if(ret < 0) 
			error = 1;
	} else {
		info("not found requested device %s", req.busid);
		error = 1;
	}


	ret = usbip_send_op_common(sockfd, OP_REP_IMPORT, (!error ? ST_OK : ST_NA));
	if(ret < 0) {
		err("send import reply");
		return -1;
	}

	if(!error) {
		struct usb_device pdu_udev;

		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		pack_usb_device(1, &pdu_udev);

		ret = usbip_send(sockfd, (void *) &pdu_udev, sizeof(pdu_udev));
		if(ret < 0) {
			err("send devinfo");
			return -1;
		}
	}

	return 0;
}



static int recv_pdu(int sockfd)
{
	int ret;
	uint16_t code = OP_UNSPEC;


	ret = usbip_recv_op_common(sockfd, &code);
	if(ret < 0) {
		err("recv op_common, %d", ret);
		return ret;
	}


	ret = usbip_stub_refresh_device_list(StubDriver);
	if(ret < 0) 
		return -1;

	switch(code) {
		case OP_REQ_DEVLIST:
			ret = recv_request_devlist(sockfd);
			break;

		case OP_REQ_IMPORT:
			ret = recv_request_import(sockfd);
			break;

		case OP_REQ_DEVINFO:
		case OP_REQ_CRYPKEY:

		default:
			err("unknown op_code, %d", code);
			ret = -1;
	}


	return ret;
}




static void log_addrinfo(struct addrinfo *ai)
{
	int ret;
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];

	ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, hbuf, sizeof(hbuf),
			sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	if(ret)
		err("getnameinfo, %s", gai_strerror(ret));

	dbg("listen at [%s]:%s", hbuf, sbuf);
}

static struct addrinfo *my_getaddrinfo(char *host, int ai_family)
{
	int ret;
	struct addrinfo hints, *ai_head;

	bzero(&hints, sizeof(hints));

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;

	ret = getaddrinfo(host, USBIP_PORT_STRING, &hints, &ai_head);
	if (ret){
		err("%s: %s", USBIP_PORT_STRING, gai_strerror(ret) );
		return NULL;
	}

	return ai_head;
}

#define MAXSOCK 20
static int listen_all_addrinfo(struct addrinfo *ai_head, int lsock[], int *maxfd)
{
	struct addrinfo *ai;
	int n = 0;		/* number of sockets */
	*maxfd = -1;		/* maximum descriptor number */

	for(ai = ai_head; ai && n < MAXSOCK; ai = ai->ai_next){
		int ret;

		lsock[n] = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if(lsock[n] < 0)
			continue;

		usbip_set_reuseaddr(lsock[n]);

		if(lsock[n] >= FD_SETSIZE){
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		ret = bind(lsock[n], ai->ai_addr, ai->ai_addrlen);
		if(ret < 0){
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		ret = listen(lsock[n], SOMAXCONN);
		if(ret < 0){
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		log_addrinfo(ai);

		if(lsock[n] > *maxfd)
			*maxfd = lsock[n];

		n++;
	}

	if(n == 0){
		err("no socket to listen to");
		return -1;
	}

	dbg("listen %d address%s", n, (n==1)?"":"es");

	return n;
}

#ifdef HAVE_LIBWRAP
static int tcpd_auth(int csock)
{
	int ret;
	struct request_info request;

	request_init(&request, RQ_DAEMON, "usbipd", RQ_FILE, csock, 0);

	fromhost(&request);

	ret = hosts_access(&request);
	if(!ret)
		return -1;

	return 0;	
}
#endif

static int my_accept(int lsock)
{
	int csock;
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int ret;

	bzero(&ss, sizeof(ss));

	csock = accept(lsock, (struct sockaddr *) &ss, &len);
	if(csock < 0) {
		err("accept");
		return -1;
	}

	ret = getnameinfo((struct sockaddr *)&ss, len,
			host, sizeof(host), port, sizeof(port),
			NI_NUMERICHOST | NI_NUMERICSERV);
	if(ret)
		err("getnameinfo, %s", gai_strerror(ret));

#ifdef HAVE_LIBWRAP
	ret = tcpd_auth(csock);
	if(ret < 0) {
		info("deny %s", host);
	}
#endif

	info("connected from %s:%s", host, port);

	return csock;
}

static int loop_standalone = 1;

static void signal_handler(int i)
{
	dbg("signal catched %d", i);
	loop_standalone = 0;
}

static void daemon_init(void)
{
	pid_t pid;

	usbip_use_stderr = 0;
	usbip_use_syslog = 1;

	pid = fork();
	if(pid < 0) {
		err("fork");
		exit(EXIT_FAILURE);
	} else if(pid > 0)
		/* bye parent */
		exit(EXIT_SUCCESS);

	setsid();

	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if(pid < 0) {
		err("fork");
		exit(EXIT_FAILURE);
	} else if(pid > 0)
		/* bye parent */
		exit(EXIT_SUCCESS);

	chdir("/");
	umask(0);

	for(int i=0; i < 3; i++)
		close(i);

}

static void set_signal(void)
{
	struct sigaction act;

	bzero(&act, sizeof(act));
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
}

static void do_standalone_mode(void)
{
	int ret;
	int lsock[MAXSOCK];
	struct addrinfo *ai_head;
	int n;
	int maxfd;

	if(!usbip_use_debug)
		daemon_init();

	set_signal();

	ai_head = my_getaddrinfo(NULL, PF_UNSPEC);
	if(!ai_head)
		return;

	n = listen_all_addrinfo(ai_head, lsock, &maxfd);
	if(n <= 0) {
		err("no socket to listen to");
		goto err;
	}

	StubDriver = usbip_stub_driver_open();
	if(!StubDriver) 
		goto err_driver_open;

 	ret = names_init(USBIDS_FILE);
 	if(ret) 
 		err("open usb.ids");

	info("usbipd start (%s)", version);

	/* setup fd_set */
	fd_set rfd0;
	FD_ZERO(&rfd0);
	for(int i = 0; i < n; i++)
		FD_SET(lsock[i], &rfd0);

	while(loop_standalone){
		fd_set rfd = rfd0;
		ret = select(maxfd + 1, &rfd, NULL, NULL, NULL);
		if(ret < 0){
			if(!(errno == EINTR))
				err("select");
			break;
		}

		for(int i = 0; i < n; i++){
			if(FD_ISSET(lsock[i], &rfd)){
				int csock;

				csock = my_accept(lsock[i]);
				if(csock < 0)
					continue;

				ret = recv_pdu(csock);
				if(ret < 0)
					err("process recieved pdu");

				close(csock);
			}
		}
	}

err:
	usbip_stub_driver_close(StubDriver);
err_driver_open:
	info("shutdown");
	freeaddrinfo(ai_head);
	names_deinit();
	return;
}


const char help_message[] = "\
Usage: usbipd [options]				\n\
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

static const struct option longopts[] = {
	{"version",	no_argument,	NULL, 'v'},
	{"help",	no_argument,	NULL, 'h'},
	{"debug",	no_argument,	NULL, 'D'},
	{NULL,		0,		NULL,  0}
};

int main(int argc, char *argv[])
{
	enum {
		cmd_standalone_mode = 1,
		cmd_help,
		cmd_version
	} cmd = cmd_standalone_mode;



	usbip_use_stderr = 1;
	usbip_use_syslog = 0;


	while(1){
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "vhD", longopts, &index);

		if(c == -1)
			break;

		switch(c){
			case 'v':
				cmd = cmd_version;
				break;
			case 'h':
				cmd = cmd_help;
				break;
			case 'D':
				usbip_use_debug = 1;
				break;
			case '?':
				break;
			default:
				err("getopt");
		}
	}

	switch(cmd) {
		case cmd_standalone_mode:
			do_standalone_mode();
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

	return 0;
}
