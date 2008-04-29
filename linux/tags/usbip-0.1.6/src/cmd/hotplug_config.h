/*
 * export_config.h
 *
 * Copyright (C) 2007 by Robert Leibl <robert.leibl@gmail.com>
 *
 */

#ifndef _HOTPLUG_CONFIG_H_
#define _HOTPLUG_CONFIG_H_

#include <stdlib.h>

#include "logging.h"

#define CONF_FILE "/etc/usbip/usbip_export.conf"

#define MAX_LEN 256
/*
 * Device and/or Interface Class codes
 * as found in bDeviceClass or bInterfaceClass
 * and defined by www.usb.org documents
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PHYSICAL		5
#define USB_CLASS_STILL_IMAGE		6
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_CDC_DATA		0x0a
#define USB_CLASS_CSCID			0x0b	/* chip+ smart card */
#define USB_CLASS_CONTENT_SEC		0x0d	/* content security */
#define USB_CLASS_VIDEO			0x0e
#define USB_CLASS_WIRELESS_CONTROLLER	0xe0
#define USB_CLASS_MISC			0xef
#define USB_CLASS_APP_SPEC		0xfe
#define USB_CLASS_VENDOR_SPEC		0xff


/*
 * prints the configuration value of 'server' in 'buffer'.
 *
 * @params:
 * 	buffer  the server address is stored in buffer
 * 	len	the length of buffer
 *
 * @return
 * 	0	success
 * 	-1	failure in retrieving the server address
 *
 */
int get_export_server(char *buffer, int len);

/*
 * reads the config.
 *
 * The config file is defined in CONF_FILE.
 *
 * NOTE:
 * This function must be called before any other functions.
 * 
 * THIS FUNCTION MUST BE CALLED BEVORE _ANY_ USAGE OF THE GLOBAL
 * struct _config *config
 *
 * @return
 * 	0	success
 * 	-1	failure
 *
 */
int read_config(void);

/*
 * prints this config to the logfile
 */
void dump_config(void);







/* **************************************** */
/* internal functions and structs           */
/* **************************************** */

int read_server_from_file(char *buffer, int len);

int read_server_from_program(char *buffer, int len);

enum policies {
	auto_export,
	no_auto_export
};

enum server_opts {
	address,
	file,
	program
};


struct device_entry {
	int vendor;
	int product;
	enum policies policy;

	struct device_entry *next;
};

struct interface_entry {
	int if_num;
	enum policies policy;
	
	struct interface_entry *next;
};

struct _config {
	int enabled;
	int policy;
	int if_conflict;
	int server_fac;
	char server_val[MAX_LEN];

	struct device_entry *devs;
	struct interface_entry *ifs;
};

struct _config *config;



void init_default_config(void);
void free_config(void);

int parse_config(void);

int parse_line(char *line);

int add_config_if(char *line);

int add_config_dev(char *line);

int parse_for_server(char *line);

int parse_for_class(char *line);

int parse_for_device(char *line);

int split_in_two(char *src, char *dest1, char *dest2);

int get_if_number( char *interface );

int conf_add_interface(char *class, enum policies policy);

int conf_add_device( char *device, enum policies policy);

int get_vendorid(char *str);

int get_productid(char *str);

int chomp(char *buffer);

int conf_get_vendorid(char *device);
int conf_get_productid(char *device);



#endif /* _HOTPLUG_CONFIG_H_ */
