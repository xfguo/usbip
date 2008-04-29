/*
 * $Id$
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "utils.h"
#include "usbip_export.h"
#include "usbip_hotplug.h"
#include "logging.h"
#include "hotplug_config.h"

#define _GNU_SOURCE
#include <getopt.h>
#include <glib.h>


/*
 * functions using 'logging.h' facilities log to stdout per default
 */
int logfile_open = 0;

static const struct option longopts[] = {
	{"usbip",	required_argument,	NULL, 'u'},
	{"other",	required_argument,	NULL, 'o'},
	{"list",	no_argument,		NULL, 'l'},
	{"scriptlist",	no_argument,	NULL, 's'},
	{"autobind",	no_argument,	NULL, 'a'},
	{"help",	no_argument,		NULL, 'h'},
	{"export-to",   required_argument,	NULL, 'e'},
	{"unexport",    required_argument,	NULL, 'x'},
	{"busid",	required_argument,	NULL, 'b'},
	{"hotplug",     no_argument,            NULL, 'H'},
	{NULL,		0,			NULL,  0}
};

static const char match_busid_path[] = "/sys/bus/usb/drivers/usbip/match_busid";


static void show_help(void)
{
	printf("Usage: bind-driver [OPTION]\n");
	printf("Change driver binding for USB/IP.\n");
	printf("  --usbip busid        make a device exportable by USB/IP\n");
	printf("  --other busid        use a device by a local driver\n");
	printf("  --list               print usb devices and their drivers\n");
	printf("  --scriptlist         print usb devices and their drivers. handy for scripts\n");
	printf("  --autobind           make all devices exportable by USB/IP\n");
	printf("  --export-to host     export the device to 'host'\n");
	printf("  --unexport host      unexport a device previously exported to 'host'\n");
	printf("  --busid busid        the busid used for --export-to\n");
	printf("  --hotplug            automatically export a new device\n");
	printf("                       NOTE: This option must be used from a\n");
	printf("                             hotplug environment\n");
}

static int modify_match_busid(char *busid, int add)
{
	int fd;
	int ret;
	char buff[BUS_ID_SIZE + 4];

	/* BUS_IS_SIZE includes NULL termination? */
	if (strnlen(busid, BUS_ID_SIZE) > BUS_ID_SIZE - 1) {
		g_warning("too long busid");
		return -1;
	}

	fd = open(match_busid_path, O_WRONLY);
	if (fd < 0)
		return -1;

	if (add)
		snprintf(buff, BUS_ID_SIZE + 4, "add %s", busid);
	else
		snprintf(buff, BUS_ID_SIZE + 4, "del %s", busid);

	g_debug("write \"%s\" to %s", buff, match_busid_path);

	ret = write(fd, buff, sizeof(buff));
	if (ret < 0) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static const char unbind_path_format[] = "/sys/bus/usb/devices/%s/driver/unbind";

/* buggy driver may cause dead lock */
static int unbind_interface_busid(char *busid)
{
	char unbind_path[PATH_MAX];
	int fd;
	int ret;

	snprintf(unbind_path, sizeof(unbind_path), unbind_path_format, busid);

	fd = open(unbind_path, O_WRONLY);
	if (fd < 0) {
		g_warning("opening unbind_path failed: %d", fd);
		return -1;
	}

	ret = write(fd, busid, strnlen(busid, BUS_ID_SIZE));
	if (ret < 0) {
		g_warning("write to unbind_path failed: %d", ret);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int unbind_interface(char *busid, int configvalue, int interface)
{
	char inf_busid[BUS_ID_SIZE];
	g_debug("unbinding interface");

	snprintf(inf_busid, BUS_ID_SIZE, "%s:%d.%d", busid, configvalue, interface);

	return unbind_interface_busid(inf_busid);
}


static const char bind_path_format[] = "/sys/bus/usb/drivers/%s/bind";

static int bind_interface_busid(char *busid, char *driver)
{
	char bind_path[PATH_MAX];
	int fd;
	int ret;

	snprintf(bind_path, sizeof(bind_path), bind_path_format, driver);

	fd = open(bind_path, O_WRONLY);
	if (fd < 0)
		return -1;

	ret = write(fd, busid, strnlen(busid, BUS_ID_SIZE));
	if (ret < 0) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int bind_interface(char *busid, int configvalue, int interface, char *driver)
{
	char inf_busid[BUS_ID_SIZE];

	snprintf(inf_busid, BUS_ID_SIZE, "%s:%d.%d", busid, configvalue, interface);

	return bind_interface_busid(inf_busid, driver);
}

static int unbind(char *busid)
{
	int configvalue = 0;
	int ninterface = 0;
	int devclass = 0;
	int i;
	int failed = 0;

	configvalue = read_bConfigurationValue(busid);
	ninterface  = read_bNumInterfaces(busid);
	devclass  = read_bDeviceClass(busid);

	if (configvalue < 0 || ninterface < 0 || devclass < 0) {
		g_warning("read config and ninf value, removed?");
		return -1;
	}

	if (devclass == 0x09) {
		g_message("skip unbinding of hub");
		return -1;
	}

 	for (i = 0; i < ninterface; i++) {
 		char driver[PATH_MAX];
		int ret;

		bzero(&driver, sizeof(driver));

 		getdriver(busid, configvalue, i, driver, PATH_MAX-1);

		g_debug(" %s:%d.%d	-> %s ", busid, configvalue, i, driver);

		if (!strncmp("none", driver, PATH_MAX))
			continue; /* unbound interface */

#if 0
		if (!strncmp("usbip", driver, PATH_MAX))
			continue; /* already bound to usbip */
#endif

		/* unbinding */
		ret = unbind_interface(busid, configvalue, i);
		if (ret < 0) {
			g_warning("unbind driver at %s:%d.%d failed",
					busid, configvalue, i);
			failed = 1;
		}
	}

	if (failed)
		return -1;
	else
		return 0;
}

/* call at unbound state */
static int bind_to_usbip(char *busid)
{
	int configvalue = 0;
	int ninterface = 0;
	int i;
	int failed = 0;

	configvalue = read_bConfigurationValue(busid);
	ninterface  = read_bNumInterfaces(busid);

	if (configvalue < 0 || ninterface < 0) {
		g_warning("read config and ninf value, removed?");
		return -1;
	}

 	for (i = 0; i < ninterface; i++) {
		int ret;

		ret = bind_interface(busid, configvalue, i, "usbip");
		if (ret < 0) {
			g_warning("bind usbip at %s:%d.%d, failed",
					busid, configvalue, i);
			failed = 1;
			/* need to contine binding at other interfaces */
		}
	}

	if (failed)
		return -1;
	else
		return 0;
}


static int use_device_by_usbip(char *busid)
{
	int ret;

	ret = unbind(busid);
	if (ret < 0) {
		g_warning("unbind drivers of %s, failed", busid);
		return -1;
	}

	ret = modify_match_busid(busid, 1);
	if (ret < 0) {
		g_warning("add %s to match_busid, failed", busid);
		return -1;
	}

	ret = bind_to_usbip(busid);
	if (ret < 0) {
		g_warning("bind usbip to %s, failed", busid);
		modify_match_busid(busid, 0);
		return -1;
	}

	g_message("bind %s to usbip, complete!", busid);

	return 0;
}



static int use_device_by_other(char *busid)
{
	int ret;
	int config;

	/* read and write the same config value to kick probing */
	config = read_bConfigurationValue(busid);
	if (config < 0) {
		g_warning("read bConfigurationValue of %s, failed", busid);
		return -1;
	}

	ret = modify_match_busid(busid, 0);
	if (ret < 0) {
		g_warning("del %s to match_busid, failed", busid);
		return -1;
	}

	ret = write_bConfigurationValue(busid, config);
	if (ret < 0) {
		g_warning("read bConfigurationValue of %s, failed", busid);
		return -1;
	}

	g_message("bind %s to other drivers than usbip, complete!", busid);

	return 0;
}


#include <sys/types.h>
#include <regex.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>



static int is_usb_device(char *busid)
{
	int ret;

	regex_t regex;
	regmatch_t pmatch[1];

	ret = regcomp(&regex, "^[0-9]+-[0-9]+(\\.[0-9]+)*$", REG_NOSUB|REG_EXTENDED);
	if (ret < 0)
		g_error("regcomp: %s\n", strerror(errno));

	ret = regexec(&regex, busid, 0, pmatch, 0);
	if (ret)
		return 0;	/* not matched */

	return 1;
}


#include <dirent.h>
static int show_devices(void)
{
	DIR *dir;

	dir = opendir("/sys/bus/usb/devices/");
	if (!dir)
		g_error("opendir: %s", strerror(errno));

	printf("List USB devices\n");
	for (;;) {
		struct dirent *dirent;
		char *busid;

		dirent = readdir(dir);
		if (!dirent)
			break;

		busid = dirent->d_name;

		if (is_usb_device(busid)) {
			char name[100] = {'\0'};
			char driver[100] =  {'\0'};
			int conf, ninf = 0;
			int i;

			conf = read_bConfigurationValue(busid);
			ninf = read_bNumInterfaces(busid);

			getdevicename(busid, name, sizeof(name));

			printf(" - busid %s (%s)\n", busid, name);

			for (i = 0; i < ninf; i++) {
				getdriver(busid, conf, i, driver, sizeof(driver));
				printf("         %s:%d.%d -> %s\n", busid, conf, i, driver);
			}
			printf("\n");
		}
	}

	closedir(dir);

	return 0;
}

static int show_devices_script(void)
{
	DIR *dir;

	dir = opendir("/sys/bus/usb/devices/");
	if (!dir)
		g_error("opendir: %s", strerror(errno));

	for (;;) {
		struct dirent *dirent;
		char *busid;

		dirent = readdir(dir);
		if (!dirent)
			break;

		busid = dirent->d_name;

		if (is_usb_device(busid)) {
			char name[100] = {'\0'};
			char driver[100] =  {'\0'};
			int conf, ninf = 0;
			int i;
			int islocal = 0;

			conf = read_bConfigurationValue(busid);
			ninf = read_bNumInterfaces(busid);

			getdevicename(busid, name, sizeof(name));

			for (i = 0; i < ninf; i++) {
				getdriver(busid, conf, i, driver, sizeof(driver));
				
				if (strncmp(driver, "usbhid", 6) == 0 || strncmp(driver, "usb-storage", 11) == 0) {
					islocal = 1;
					break;
				}
			}
			
			if (islocal == 0)
				printf("%s|%s|%s\n", busid, driver, name);
		}
	}

	closedir(dir);

	return 0;
}

static int export_to(char *host, char *busid) {

	int ret;

	if( host == NULL ) {
		printf( "no host given\n\n");
		show_help();
		return -1;
	}
	if( busid == NULL ) {
		/* XXX print device list and ask for busnumber, if none is
		 * given */
		printf( "no busid given, use --busid switch\n\n");
		show_help();
		return -1;
	}


	ret = use_device_by_usbip(busid);
	if( ret != 0 ) {
		printf( "could not bind driver to usbip\n");
		return -1;
	}

	printf( "DEBUG: exporting device '%s' to '%s'\n", busid, host );
	ret = export_busid_to_host(host, busid); /* usbip_export.[ch] */
	if( ret != 0 ) {
		printf( "could not export device to host\n" );
		printf( "   host: %s, device: %s\n", host, busid );
		use_device_by_other(busid);
		return -1;
	}

	return 0;
}

static int unexport_from(char *host, char *busid) {

	int ret;

	if( host == NULL ) {
		logerr( "no host given\n\n");
		show_help();
		return -1;
	}
	if( busid == NULL ) {
		/* XXX print device list and ask for busnumber, if none is
		 * given */
		logerr( "no busid given, use --busid switch\n\n");
		show_help();
		return -1;
	}
	logmsg("unexport_from: host: '%s', busid: '%s'", host, busid);


	printf( "DEBUG: unexporting device '%s' from '%s'\n", busid, host );
	ret = unexport_busid_from_host(host, busid); /* usbip_export.[ch] */
	if( ret != 0 ) {
		logerr( "could not unexport device from host\n" );
		logerr( "   host: %s, device: %s\n", host, busid );
	}

	ret = use_device_by_other(busid);
	if( ret != 0 ) {
		logerr( "could not unbind device from usbip\n");
		return -1;
	}

	return 0;
}

static int hotplug_remove_device(void) {

	int ret;
	char serv[256];
	char *busid;

	logmsg("bind-driver.c:: removing device" );

	busid = get_busid_from_env();
	if( busid == NULL ) {
		logmsg("error retrieving busid");
		return -1;
	}
	logmsg("busid = '%s'", busid);

	ret = get_tmp_file(serv, busid);
	if(ret) {
		logerr("could not get server from /tmp file" );
		return -1;
	}
	logmsg("export server is '%s'", serv);

	/* XXX
	 * we don't do anything here. The server will know by itself, that the
	 * device is no longer available, as the connection will break down,
	 * and it will (afai guess) receive the 'UNAVAILABLE' signal. If we
	 * try to use unexport_from at this point, the driver might already be
	 * unavailable, which will result in a segmentation fault!
	 */
	/*ret = unexport_from(serv, busid);
	if(ret) {
		logerr("exporting of device failed");
		return -1;
	}*/
	
	remove_tmp_file(serv, busid);

	logmsg("device removed");

	return 0;
}

static int hotplug_add_device(void) {

	int ret;
	char serv[256];
	char *busid = NULL;

	logmsg("bind-driver.c:: adding device" );

	ret = read_config();
	if(ret) {
		if(ret == 1) {
			logmsg("usbip turned off in config file");
			return 0;
		}else {
			logerr("error reading config file");
			return -1;
		}
	}

	ret = get_export_server(serv, 256);/*XXX*/
	if(ret) {
		logerr("could not get export server");
		return -1;
	}
	logmsg("export server is '%s'", serv);

	ret = export_device_q();
	switch (ret) {
		case EXPORT:
			logmsg(" -- exporting device --" );
			break;
		case NO_EXPORT:
			logmsg(" -- not exporting device --");
			return 0;
			break;
		default:
			logwarn("export_device_q returned with error: %d",ret);
			return -1;
	}

	busid = get_busid_from_env();
	if( busid == NULL ) {
		logerr("error retrieving busid");
		return -1;
	}
	logmsg("busid: '%s'", busid);

	logmsg("exporting device '%s' to server '%s'", busid, serv);
	ret = export_to(serv, busid);
	if(ret) {
		logerr("exporting of device failed");
		logerr("see syslog for details");
		return -1;
	}
	logmsg("device exported");

	
	ret = create_tmp_file(serv, busid);
	if(ret) {
		logwarn("failed to create /tmp file");
	}

	return 0;
}

static int hotplug(void) {

	int ret;
	char *action;


	ret = open_log();
	if( ret < 0 ) {
		logwarn("could not open logfile");
	}
	logmsg(" ------------ hotplug() called ---------------" );


	ret = check_environment_sane();
	if( ret ) {
		logerr("environment is not sane");
		return -1;
	}

	action = getenv("ACTION");
	if( action == NULL ) { /* after check_environment that cannot happen */
		logerr("ACTION is not set.");
		return -1;
	}

	if( ! strncmp(action, "add", 3 )) {
		hotplug_add_device();
	}
	else if( ! strncmp(action, "remove", 6)) {
		hotplug_remove_device();
	}
	else {
		logerr("illegal ACTION value: %s", action);
		return -1;
	}
	
	close_log();

	return 0;
}

static int auto_bind(void)
{
	DIR *dir;

	dir = opendir("/sys/bus/usb/devices/");
	if (!dir)
		g_error("opendir: %s", strerror(errno));

	for (;;) {
		struct dirent *dirent;
		char *busid;

		dirent = readdir(dir);
		if (!dirent)
			break;

		busid = dirent->d_name;

		if (is_usb_device(busid)) {
			char name[100] = {'\0'};
			char driver[100] =  {'\0'};
			int conf, ninf = 0;
			int i;
			int islocal = 0;

			conf = read_bConfigurationValue(busid);
			ninf = read_bNumInterfaces(busid);

			getdevicename(busid, name, sizeof(name));

			for (i = 0; i < ninf; i++) {
				getdriver(busid, conf, i, driver, sizeof(driver));
				
				if (strncmp(driver, "usbhid", 6) == 0 || strncmp(driver, "usb-storage", 11) == 0) {
					islocal = 1;
					break;
				}
			}
			
			if (islocal == 0)
				use_device_by_usbip(busid);
		}
	}

	closedir(dir);

	return 0;
}

int main(int argc, char **argv)
{
	char *busid = NULL;
	char *remote_host = NULL;

	enum {
		cmd_unknown = 0,
		cmd_use_by_usbip,
		cmd_use_by_other,
		cmd_list,
		cmd_scriptlist,
		cmd_autobind,
		cmd_export_to,
		cmd_unexport,
		cmd_help,
		cmd_hotplug
	} cmd = cmd_unknown;

	if (geteuid() != 0)
		g_warning("running non-root?");

	for (;;) {
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "u:o:hlsae:x:b:H", longopts, &index);
		if (c == -1)
			break;

		switch (c) {
			case 'u':
				cmd = cmd_use_by_usbip;
				busid = optarg;
				break;
			case 'o' :
				cmd = cmd_use_by_other;
				busid = optarg;
				break;
			case 'l' :
				cmd = cmd_list;
				break;
			case 's' :
				cmd = cmd_scriptlist;
				break;
			case 'a' :
				cmd = cmd_autobind;
				break;
			case 'b':
				busid = optarg;
				break;
			case 'e':
				cmd = cmd_export_to;
				remote_host = optarg;
				break;
			case 'x':
				cmd = cmd_unexport;
				remote_host = optarg;
				break;
			case 'H':
				cmd = cmd_hotplug;
				break;
			case 'h': /* fallthrough */
			case '?':
				cmd = cmd_help;
				break;
			default:
				g_error("getopt");
		}

		//if (cmd)
		//	break;
	}

	switch (cmd) {
		case cmd_use_by_usbip:
			use_device_by_usbip(busid);
			break;
		case cmd_use_by_other:
			use_device_by_other(busid);
			break;
		case cmd_list:
			show_devices();
			break;
		case cmd_scriptlist:
			show_devices_script();
			break;
		case cmd_autobind:
			auto_bind();
			break;
		case cmd_export_to:
			export_to(remote_host, busid);
			break;
		case cmd_unexport:
			unexport_from(remote_host, busid);
			break;
		case cmd_hotplug:
			hotplug();
			break;
		case cmd_help: /* fallthrough */
		case cmd_unknown:
			show_help();
			break;
		default:
			g_error("NOT REACHED");
	}

	return 0;
}
