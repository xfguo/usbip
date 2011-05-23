/*
 * export_device.h
 *
 * Copyright (C) 2007 by Robert Leibl <robert.leibl@gmail.com>
 *
 */

#ifndef _USBIP_HOTPLUG_H_
#define _USBIP_HOTPLUG_H_

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "logging.h"
#include "hotplug_config.h"

#define SYSFS_ROOT "/sys"

#define MAX_LEN 256
#define MAX_PATH 256

//#define LOG_FILE "/var/log/usbip_export.log"
#define LOG_FILE "/tmp/usbip_export.log"


#define EXPORT      0
#define NO_EXPORT   1
#define NO_DECISION 2


extern struct _config *config;


/* 
 * Tests, if a given device should be exported.
 *
 * checks, if the device given on the
 * environment variable DEVPATH should be exported
 *
 * NOTE: The config must have veen read when calling this function!
 *
 * @return
 * 	0  if the device should be exported
 * 	1  if not
 * 	-1 if there was an error
 */
int export_device_q(void);

/*
 * information of the device currently handled is stored here
 */
struct current_device {
	int vendor;
	int product;
	struct iflist *ifs;
	char *devpath;
	char *busid;
};

/*
 * simple list that stores all interfaces available to the current device.
 *
 * Ok, ok, this may be a little overhead ;)
 */
struct iflist {
	int if_num;
	struct iflist *next;
};

int check_environment_sane(void);

int check_export_class(struct current_device *dev);
int check_export_device(struct current_device *dev);
int fill_current_device(struct current_device *dev);
int scan_interfaces(struct current_device *dev);
int add_if_to_current(struct current_device *dev, int ifnum);
void dump_device_interfaces(struct current_device *dev);
char *get_busid_from_env(void);
void free_ifs(void);



/*
 * functions related to temporary file creation 
 */

#define TMP_FILE_PATTERN "/tmp/usbip_exported_"

int create_tmp_file(char *server, char *busid);
int get_tmp_file(char *server, char *busid);

#endif /* _USBIP_HOTPLUG_H_ */
