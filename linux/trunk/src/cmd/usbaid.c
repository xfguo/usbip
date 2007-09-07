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

#include "usbaid.h"

/*
 * displays a help message
 */
static void print_help(void) {
	printf( "usage: usbaid [OPTIONS]\n\n" );
	printf( "  options:\n" );
	printf( "     -f | --foreground\n" );
	printf( "            run in foreground\n" );
	printf( "     -d | --debug\n" );
	printf( "            enable debugging output\n" );
	printf( "     -h | --help\n" );
	printf( "            show this help message\n" );
	printf( "\n" );
}


/*
 * signal handler
 *
 * We stop at any of the four signals mentioned.
 *
 * @sig	
 * 	the signal sent to the process
 */
static void signal_handler(int sig) {
	strsignal(sig);

	switch(sig) {
		case SIGHUP: /* falltrough */
		case SIGTERM: /* falltrough */
		case SIGINT: /* falltrough */
		case SIGQUIT: /* falltrough */
			keep_running = 0;
			break;
		default:
			break;
	}
}

/*
 * Registers the function 'signal_handler()'
 * for the following signals:
 * 	SIGTERM
 * 	SIGINT
 * 	SIGHUP
 *	SIGQUIT
 */
static void init_signal_handler(void) {
	struct sigaction sigact;

	bzero(&sigact, sizeof(sigact));
	sigact.sa_handler = signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigaction( SIGTERM, &sigact, NULL );
	sigaction( SIGINT, &sigact, NULL );
	sigaction( SIGHUP, &sigact, NULL );
	sigaction( SIGQUIT, &sigact, NULL );
}

/*
 * prints the (numeric) host, and the (numeric) port/service for a given list
 * of 'struct addrinfo's
 *
 * this function is used for debugging
 */
static void print_addrinfo(struct addrinfo *ai) {

	int n;
	char host[NI_MAXHOST];
	char port[NI_MAXSERV];

	for (n=1; ai && n < MAX_SOCKETS; ai=ai->ai_next, n++) {
		getnameinfo( ai->ai_addr, ai->ai_addrlen,
				host, sizeof(host),
				port, sizeof(port),
				NI_NUMERICHOST | NI_NUMERICSERV );
		info("host: %s, service (port): %s", host, port );
	}
}

/*
 * networking
 */

/*
 * Retrieves the address structure of the given host
 */
static struct addrinfo *init_addrinfo(char *host, int ai_family) {

	int ret;
	struct addrinfo hints;
	struct addrinfo *ai_head;

	bzero(&hints, sizeof(hints));

	hints.ai_family   = ai_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;

	ret = getaddrinfo(host, USBAID_PORT_STRING, &hints, &ai_head);
	if(ret) {
		err("could not getaddrinfo");
		return NULL;
	}
	
	if( usbip_use_debug ) {
		info("local addresses:");
		print_addrinfo(ai_head);
	}

	return ai_head;
}

/*
 * Initializes a single network socket with the information from 'ai' and
 * write the corresponding file descriptor to 'sock'
 *
 * A socket is created, bound to the given address, and set to listen state.
 *
 * @sock	the file descriptor of the newly created socket will be
 * 		written to this int.
 * @ai		The address_info structure for the new socket
 *
 * returns:
 * 	 1 success
 * 	 0 failure. On failure the 'sock' variable will be set to -1
 */
static int init_single_socket(int *sock, struct addrinfo *ai) {

	int ret;
	int s;
	int val = 1;

	/* socket */
	s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if( s < 0 ) {
		*sock = s;
		err("error creating socket");
		return 0;
	}

	if( s > FD_SETSIZE ) {
		close(s);
		err("FD_SETSIZE overrun");
		*sock = -1;
		return 0;
	}

	
	/* set socket options */
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));


	/* bind */
	ret = bind( s, ai->ai_addr, ai->ai_addrlen);
	if( ret < 0 ) {
		err("could not bind socket: %s", strerror(errno));
		close(s);
		*sock = -1;
		return 0;
	}


	/* listen */
	ret = listen(s, SOMAXCONN);
	if( ret < 0 ) {
		err("could not set listen mode for socket");
		close(s);
		*sock = -1;
		return 0;
	}

	if( s > max_fd ) 
		max_fd = s;

	*sock = s;

	dbg("single socket initialized");
	return 1;

}

/*
 * Initializes the listen_sockets array
 */
static int init_sockets(void) {

	struct addrinfo *ai_head;
	struct addrinfo *ai;
	int n;

	ai_head = init_addrinfo(NULL, PF_UNSPEC);
	if(!ai_head) {
		return 0;
	}
		
	/* initialize each socket */
	for( ai = ai_head,  n = 0;
		ai  &&  n < MAX_SOCKETS;
		ai = ai->ai_next ) 
	{
		n += init_single_socket( &listen_sockets[n], ai );
	}

	dbg( "registered %d sockets", n );

	return n;
}

/*
 * Handles an incomming connection.
 *
 * @sockfd	The filedescriptor of the socket, that has a connection
 * 		waiting (i.e. can be read from)
 *
 * returns
 * 		the new socket connected with the client
 */
static int my_accept(int sockfd) {
	
	int newsock, ret;
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	char host[NI_MAXHOST];
	char port[NI_MAXSERV];

	bzero( &ss, sizeof(ss));

	newsock = accept(sockfd, (struct sockaddr *)&ss, &ss_len);
	if( newsock < 0 ) {
		err("error accepting() connection" );
		return -1;
	}
	dbg("accept()-ed new socket");

	ret = getnameinfo((struct sockaddr *)&ss, ss_len,
			host, sizeof(host), port, sizeof(port),
			NI_NUMERICHOST | NI_NUMERICSERV);

	info( "connection from %s, %s", host, port );

	return newsock;
}

/*
 * retrieves data from a socket.
 *
 * @sock	The socket to read from
 * @buffer	The buffer to write the data to
 * @bufflen	The length of 'buffer'
 *
 * returns
 * 		The number of bytes read
 */
static int my_recv( int sock, void *buffer, int bufflen ) {

	ssize_t total = 0;

	do {
		ssize_t nbytes;
		nbytes = recv( sock, buffer, bufflen, MSG_WAITALL );

		if( nbytes <= 0 )
			return -1;

		buffer  += nbytes;
		bufflen -= nbytes;
		total   += nbytes;

	} while( bufflen > 0 );

	dbg("received %d bytes", total);

	return total;
}

/*
 * imports the given device
 */
static int aid_import_device(int sockfd, struct usb_device *udev)
{
	int ret;
	int port;

	ret = usbip_vhci_driver_open();
	if (ret < 0) {
		err("open vhci_driver");
		return -1;
	}
	dbg("import: driver opened");

	port = usbip_vhci_get_free_port();
	if (port < 0) {
		err("no free port");
		usbip_vhci_driver_close();
		return -1;
	}
	dbg("import: found free port");

	ret = usbip_vhci_attach_device(port, sockfd, udev->busnum,
			udev->devnum, udev->speed);
	if (ret < 0) {
		err("import device");
		usbip_vhci_driver_close();
		return -1;
	}
	dbg("import: device attached");

	usbip_vhci_driver_close();

	return port;
}

/*
 * detatches the given usb_device from the virtual host
 *
 * uint8_t port:
 * 	This generates a warning: 'port' may be used uninitialized in this
 * 	function.
 *
 * 	however, port is set in the udev_to_port function. Since port is
 * 	unsigned, it is not possible to use it as error value (e.g. -1).
 * 	Therefore we have to use a different value as 'error indication' and
 * 	set port indirectly trough a pointer.
 */
static int aid_unimport_device(struct usb_device *udev) {

	int ret;
	uint8_t port;

	ret = usbip_vhci_driver_open();
	if (ret < 0) {
		err("open vhci_driver");
		return -1;
	}
	dbg("unimport: driver opened");

	ret = udev_to_port(udev, &port);
	if( ret == -1 ) {
		err( "could not unimport device: no such device" );
		return -1;
	}
	dbg("device to unimport is on port %d", port );

	ret = usbip_vhci_detach_device(port);
	if( ret < 0 ) {
		err( "detaching device failed" );
		return -1;
	}

	usbip_vhci_driver_close();

	info("device on port %d unimported (i.e. detached from this host)", port );
	return 0;
}


/*
 * Receives the 'struct op_export_request' structure.
 * Calls aid_import_device, to have the sent device imported.
 *
 * @sock	The socket to read from
 *
 * returns
 * 		The return value from aid_import_device()
 *
 */
static int recv_export_query( int sock ) {

	int ret;
	struct op_export_request request;

	bzero( &request, sizeof(request) );

	ret = my_recv( sock, (void *)&request, sizeof(request) );
	if( ret != sizeof(struct op_export_request) )
		return -1;

	dbg("op_export_request received");
	dbg("values: busnum %u, devnum %u, speed %u", request.udev.busnum,
			request.udev.devnum, request.udev.speed);
	PACK_OP_EXPORT_REQUEST(0, &request); /* unpack */
	dbg("export query unpacked: values: %u %u %u", request.udev.busnum,
			request.udev.devnum, request.udev.speed);

	return aid_import_device(sock, &request.udev);
}

/* 
 * receives the export query and exports the device.
 */
static int handle_export_query(int sock) {

	int ret;
	int status = ST_OK;

	ret = recv_export_query(sock); /* the port of the new device is returned */
	if( ret < 0 ) {
		status = ST_NA;
		return -1;
	}

	/*
	 * XXX we do not send a status notificaton as this may break things.
	 * When a device has been imported by the server, the socket is used to
	 * pass device related data. Therefore we cant use it to send status
	 * information.
	 */
	/*
	ret = usbip_send_op_common(sock, OP_REQ_EXPORT, status);
	if( ret < 0 ) {
		err( "could not send status notification" );
		return -1;
	}
	*/

	return 0;
}


static int handle_unexport_query(int sock) {

	int ret;
	int status = ST_OK;

	ret = recv_unexport_query(sock);
	if( ret < 0 )
		status = ST_NA;

	ret = usbip_send_op_common(sock,OP_REP_UNEXPORT, status);
	if( ret < 0 ) {
		err( "could not send status notification" );
		return -1;
	}

	return 0;
}

static int recv_unexport_query(int sock) {

	int ret;
	struct op_unexport_request request;

	bzero( &request, sizeof(request) );

	ret = my_recv( sock, (void *)&request, sizeof(request) );
	if( ret != sizeof(struct op_unexport_request) )
		return -1;

	dbg("op_unexport_request received");
	dbg("values: %u %u %u", request.udev.busnum,
			request.udev.devnum, request.udev.speed);
	PACK_OP_UNEXPORT_REQUEST(0, &request); /* unpack */
	dbg("unexport query unpacked: values: %u %u %u", request.udev.busnum,
			request.udev.devnum, request.udev.speed);

	return aid_unimport_device( &(request.udev) );
}


/*
 *
 */
static int my_recv_pdu(int sock) {

	int ret;
	uint16_t code = OP_UNSPEC;

	ret = usbip_recv_op_common(sock, &code);
	if( ret<0 ) {
		err("rev()-ing op-common failed");
		return ret;
	}

	info("receiving pdu");
	ret = -1;
	switch(code) {
		case OP_REQ_EXPORT:
			info("received export request");
			ret = handle_export_query(sock);
			break;
		case OP_REQ_UNEXPORT:
			info("received unexport request");
			ret = handle_unexport_query(sock);
			break;
		case OP_REQ_IMPORT:
			notice("received import request");
			notice("this does not apply to the server!");
			break;
		case OP_REQ_DEVINFO: /* fallthrough */
		case OP_REQ_CRYPKEY:
			info("not implemented");
			break;
		case OP_REQ_DEVLIST: /* does not apply for server */
			notice("received devlist command");
			notice("this does not apply to the server!");
			break;
		case OP_UNSPEC:	
			notice("received UNSPEC command");
			break;
		default:
			notice("received unknown command %d", code);
			break;
	}

	return ret;
}

/*
 * daemonize
 *
 * Detaches this process using fork().
 * The parent will exit, leaving this process as a daemon.
 * The process will then close its IO-Streams and reopen /dev/null for stdin,
 * stderr and stdout.
 * Working directory is RUN_DIR
 */
static void daemonize(void) {

	int i;

	if( getpid() == 1 ) return; /* already a daemon */

	i = fork();

	/* error */
	if( i < 0 ) {
		printf( "usbaid: error forking\n" );
		exit(1);
	}

	/* parent */
	if( i > 0 ) {
		exit(0);
	}

	/* child */
	setsid();
	for (i=getdtablesize();i>=0;--i) 
		close(i); /* close all descriptors */
	i=open("/dev/null",O_RDWR); dup(i); dup(i); /* handle standart I/O */

	umask(027); /* set newly created file permissions */
	chdir(RUN_DIR);

}

/*
 * main loop
 *
 * Waits for any activity on the opened socket (via select()).
 * In case of a connection the connection is my_accept()-ed and the command is
 * received (recv_pdu)
 */

static void main_loop(int nsock) {

	int i, ret;

	fd_set fds;
	FD_ZERO(&fds);

	/* XXX remove this after testing */
	info("you should see 4 testing messages now:");
	info(" (msg 1)  testing info()");
	notice(" (msg 2)  testing notice()");
	err(" (msg 3)  testing err()");
	dbg(" (msg 4)  testing dbg()");


	for(i=0; i<nsock;i++)
		FD_SET(listen_sockets[i], &fds);

	dbg("starting main loop");
	while(keep_running) {

		info( "\n\nwaiting for connections" );
		info("-----------------------------------------------------------");

		ret = select(max_fd+1, &fds, NULL, NULL, NULL);
		if( ret < 0 ) {
			if( errno != EINTR )
				err( "select returned funny value" );

			info( "bye..." );
			break;
		}

		for( i=0; i<nsock; i++ ) {
			if( FD_ISSET(listen_sockets[i], &fds)) {
				int sock;

				sock = my_accept(listen_sockets[i]);
				if( sock < 0 ) {
					err("accept()-ing failed");
					continue;
				}

				dbg("accepted new socket: %d", sock);

				ret = my_recv_pdu(sock);
				if( ret < 0 ) {
					err("could not recv_pdu()");
					continue;
				}

				close(sock);
			}
		}
	}
}

/*
 * vhci_driver must be open when calling this function!
 */
static int udev_to_port(struct usb_device *udev, uint8_t *port) {

	int i;
	int nports;
	int busnum, devnum;
	struct usbip_imported_device *idev;

	busnum = udev->busnum;
	devnum = udev->devnum;
	nports = vhci_driver->nports;

	dbg("searching device: host has %d ports", nports );
	for( i=0; i<nports; i++ ) {

		idev = &(vhci_driver->idev[i]);
		dbg("looking at port %d", i );

		if( busnum != idev->busnum )
			continue;

		if( devnum != idev->devnum )
			continue;

		*port = idev->port;
		info("found requested device on port %d", *port );
		return 0;
	}

	return -1;
}


/*
 * main
 */
int main(int argc, char **argv) {


	int nsock;
	int run_in_background = 0;

	/*
	 * parse command line parameters
	 */
	for(;;) {
		int c;
		int index;
		
		c = getopt_long(argc, argv, "fhd", longopts, &index);
		if( c == -1 )
			break;

		switch(c) {
			case 'f':
				run_in_background = 1;/* set to 'true' */
				usbip_use_stderr  = 1;/* switch on */
				usbip_use_syslog  = 0;/* switch off */
				printf( "running in foreground\n" );
				break;
			case 'h': /*fallthrough*/
			case '?':
				print_help();
				break;
			case 'd':
				usbip_use_debug = 1;
				printf( "debugging turned on\n" );
				break;
			default:
				printf( "unknown command line option '%c'",c);
				break;
		}
	}


	init_signal_handler();

	printf( "usbaid starting\n" );
	if( run_in_background == 0 )
		daemonize();

	nsock = init_sockets();
	if( nsock == 0 ) {
		err( "could not initialize any sockets");
		return -1;
	}

	main_loop(nsock);


	return 0;
}


