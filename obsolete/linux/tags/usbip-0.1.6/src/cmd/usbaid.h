/*
 * usbaid.h
 *
 * Copyright 2007 (C) by Robert Leibl <robert.leibl@gmail.com>
 *
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <getopt.h>

#define _GNU_SOURCE
#include <string.h>

#include "usbip_network.h"
#include "log_util.h"

#define RUN_DIR "/tmp"
#define MAX_SOCKETS 20
#define USBAID_PORT_STRING "6000"

//#define TRACE_DEBUGGING 1


/* main loop control variable */
static int keep_running = 1;

/*
 * the sockets we listen on for incomming connections.
 *
 * max_fd will later be reset to the actual number of file descriptors
 */
int listen_sockets[MAX_SOCKETS];
int max_fd = -1; /* the highest file descriptor (for select()) */

/*
 * used for logging
 */
//extern int usbip_use_syslog = 1;
//extern int usbip_use_stderr = 0;
//extern int usbip_use_debug  = 0;


/*
 * displays a help message
 */
static void print_help(void);

/*
 * signal handler
 */
static void signal_handler(int sig);

/*
 * Registers the function 'signal_handler()'
 * for the following signals:
 * 	SIGTERM
 * 	SIGINT
 * 	SIGHUP
 *	SIGQUIT
 */
static void init_signal_handler(void);

/*
 * prints the (numeric) host, and the (numeric) port/service for a given list
 * of 'struct addrinfo's
 *
 * this function is used for debugging
 */
static void print_addrinfo(struct addrinfo *ai);

/*
 * networking
 */

/*
 * Retrieves the address structure of the given host
 */
static struct addrinfo *init_addrinfo(char *host, int ai_family);

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
static int init_single_socket(int *sock, struct addrinfo *ai);

/*
 * Initializes the listen_sockets array
 */
static int init_sockets(void);

/*
 * Handles an incomming connection.
 *
 * @sockfd	The filedescriptor of the socket, that has a connection
 * 		waiting (i.e. can be read from)
 *
 * returns
 * 		the new socket connected with the client
 */
static int my_accept(int sockfd);

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
static int my_recv( int sock, void *buffer, int bufflen );

/*
 * imports the given device
 */
static int aid_import_device(int sockfd, struct usb_device *udev);

/*
 * detatches the given usb_device from the virtual host
 */
static int aid_unimport_device(struct usb_device *udev);
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
static int recv_export_query( int sock );
static int recv_unexport_query( int sock );

/* 
 * receives the export query, exports the device and sends back a notification
 * to the client
 */
static int handle_export_query(int sock);
static int handle_unexport_query(int sock);

/*
 *
 */
static int my_recv_pdu(int sock);

/*
 * daemonize
 *
 * Detaches this process using fork().
 * The parent will exit, leaving this process as a daemon.
 * The process will then close its IO-Streams and reopen /dev/null for stdin,
 * stderr and stdout.
 * Working directory is RUN_DIR
 */
static void daemonize(void);

/*
 * main loop
 *
 * Waits for any activity on the opened socket (via select()).
 * In case of a connection the connection is my_accept()-ed and the command is
 * received (recv_pdu)
 */
static void main_loop(int nsock);


/*
 * returns the (local) port of given the device 
 *
 * Basically we want to get 'port' here. Since 'port' must be of type uint8_t
 * we cannot use a negative value as error indication (e.g. -1).
 * Therefore we have to use a different value as error indication and pass
 * 'port' as pointer.
 */
static int udev_to_port(struct usb_device *udev, uint8_t *port);

/*
 * command line parameters
 */
static const struct option longopts[] = {
	{"foreground",	no_argument,	NULL, 'f'},
	{"help",	no_argument,	NULL, 'h'},
	{"debug",	no_argument,	NULL, 'd'}
};

