
#include "utils.h"

#define _GNU_SOURCE
#include <getopt.h>

static const struct option longopts[] = {
	{"usbip",	required_argument,	NULL, 'u'},
	{"other",	required_argument,	NULL, 'o'},
	{"list",	no_argument,		NULL, 'l'},
	{"help",	no_argument,		NULL, 'h'},
	{NULL,		0,			NULL,  0}
};

static const char match_busid_path[] = "/sys/bus/usb/drivers/usbip/match_busid";

static int modify_match_busid(char *busid, int add)
{
	int fd;
	int ret;
	char buff[BUS_ID_SIZE + 4];

	/* BUS_IS_SIZE includes NULL termination? */
	if (strnlen(busid, BUS_ID_SIZE) > BUS_ID_SIZE - 1) {
		g_message("too long busid");
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

static int unbind_interface(char *busid, int configvalue, int interface)
{
	char inf_busid[BUS_ID_SIZE];

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
		g_debug("read config and ninf value, removed?");
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
			g_message("unbind driver at %s:%d.%d failed",
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
		g_debug("read config and ninf value, removed?");
		return -1;
	}
 
 	for (i = 0; i < ninterface; i++) {
		int ret;

		ret = bind_interface(busid, configvalue, i, "usbip");
		if (ret < 0) {
			g_message("bind usbip at %s:%d.%d, failed",
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
		g_debug("unbind drivers of %s, failed", busid);
		return -1;
	}

	ret = modify_match_busid(busid, 1);
	if (ret < 0) {
		g_message("add %s to match_busid, failed", busid);
		return -1;
	}

	ret = bind_to_usbip(busid);
	if (ret < 0) {
		g_message("bind usbip to %s, failed", busid);
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
		g_message("read bConfigurationValue of %s, failed", busid);
		return -1;
	}

	ret = modify_match_busid(busid, 0);
	if (ret < 0) {
		g_debug("del %s to match_busid, failed", busid);
		return -1;
	}

	ret = write_bConfigurationValue(busid, config);
	if (ret < 0) {
		g_message("read bConfigurationValue of %s, failed", busid);
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

static void show_help(void)
{
	printf("Usage: bind-driver [OPTION]\n");
	printf("Change driver binding for USB/IP.\n");
	printf("  --usbip busid        make a device exportable by USB/IP\n");
	printf("  --other busid        use a device by a local driver\n");
	printf("  --list               print usb devices and their drivers\n");
}

int main(int argc, char **argv)
{
	char *busid = NULL; 

	enum {
		cmd_unknown = 0,
		cmd_use_by_usbip,
		cmd_use_by_other,
		cmd_list,
		cmd_help
	} cmd = cmd_unknown;

	for(;;) {
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "u:o:hl", longopts, &index);
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
			case 'h':
			case '?':
				cmd = cmd_help;
				break;
			default:
				g_error("getopt");
		}

		if (cmd)
			break;
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
		case cmd_help:
		case cmd_unknown:
			show_help();
			break;
		default:
			g_error("NOT REACHED");
	}

	return 0;
}
