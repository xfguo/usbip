/*
 * $Id$
 *
 * Copyright (C) 2007 Robert Leibl <robert.leibl@gmail.com>
 */

#include "usbip_hotplug.h"


struct current_device current_dev = {
	.vendor  = 0,
	.product = 0,
	.ifs     = NULL,
	.devpath = NULL,
	.busid   = NULL,
};

void free_ifs() {

}

int check_environment_sane() {

	char *env_p;

	env_p = getenv("PRODUCT");
	if( !env_p ) {
		logwarn("PRODUCT is not set");
		return -1;
	}

	env_p = getenv("DEVPATH");
	if( !env_p ) {
		logwarn("DEVPATH is not set");
		return -1;
	}

	env_p = getenv("ACTION");
	if( !env_p ) {
		logwarn("ACTION is not set");
		return -1;
	}

	/*
	env_p = getenv("TYPE");
	if( !env_p ) {
		logwarn("TYPE is not set");
		return -1;
	}
	*/

	/*
	env_p = getenv("INTERFACE");
	if( !env_p ) {
		logwarn("INTERFACE is not set");
		return -1;
	}
	*/

	return 0;
}


/*
 * return:
 * 	0  export device
 * 	1  do not export device
 * 	-1 error
 */
int export_device_q() {

	int ret;


	ret = fill_current_device(&current_dev);
	if( ret < 0 ) {
		return -1;
	}
	dump_device_interfaces(&current_dev);

	/* decision based on device */
	logmsg("checking export for 'device'" );
	ret = check_export_device(&current_dev);
	switch (ret) {
		case EXPORT:
			ret = EXPORT;
			logmsg("decision based on 'device': export");
			goto exit_success;
			break;
		case NO_EXPORT:
			ret = NO_EXPORT;
			logmsg("decision based on 'device': no export");
			goto exit_success;
			break;
		case NO_DECISION:
			/* do nothing */
			logmsg("no decision reached for 'device'");
			break;
		default:
			break;
	}


	/* decision based on class */
	logmsg("checking export for 'class'" );
	ret = check_export_class(&current_dev);
	switch (ret) {
		case EXPORT:
			ret = EXPORT;
			logmsg("decision based on 'class': export");
			goto exit_success;
			break;
		case NO_EXPORT:
			ret = NO_EXPORT;
			logmsg("decision based on 'class': no export");
			goto exit_success;
			break;
		case NO_DECISION:
			/* do nothing */
			logmsg("no decision reached for 'class'");
			break;
		default:
			break;
	}
	
	//logmsg("STOPPING FOR DEBUGGING");
	//return -1;
	/* decision based on policy */
	logmsg("decision based on global policy");

	if( config->policy == auto_export )
		ret = EXPORT;
	else
		ret = NO_EXPORT;

exit_success:
	switch (ret) {
		case EXPORT:
			logmsg(" << This is my device >>" );
			break;
		case NO_EXPORT:
			logmsg(" << Not claiming device >>" );
			break;
		default:
			logerr("strange return value: %d", ret );
	}
	return ret;
}


void dump_device_interfaces(struct current_device *dev) {

	struct iflist *list;

	list = dev->ifs;

	logmsg("interfaces for current device:");
	while( list != NULL ) {
		logmsg("  %d", list->if_num );
		list = list->next;
	}
	logmsg( "" );
}




int get_vendorid(char *str) {

	char vendor[5];
	int val;
	int i;

	for(i=0;i<5;i++)
		vendor[i] = '\0';

	for(i=0; i < 5; i++) {
		if( str[i] == '\0' ) {
			logwarn("invalid PRODUCT string");
			return -1;
		}
		if( str[i] == '/' )
			break;

		vendor[i] = str[i];
	}
	vendor[4] = '\0';

	val = (int)strtol(vendor, NULL, 16);

	printf( "vendor:str=%s, hex=%x\n", vendor, val);
	return val;
}

int get_productid( char *str ) {

	char product[5];
	int val;
	int i;
	char *pos;

	for(i=0;i<5;i++)
		product[i] = '\0';

	pos = index(str, '/');
	if(pos == NULL) {
		logerr("invalid PRODUCT string");
		return -1;
	}
	pos++;

	for(i=0; i < 5; i++) {
		if( pos[i] == '\0' ) {
			logwarn("invalid PRODUCT string");
			return -1;
		}
		if( pos[i] == '/' )
			break;

		product[i] = pos[i];
	}
	product[4] = '\0';

	val = (int)strtol(product, NULL, 16);

	printf( "product:str=%s, hex=%x\n", product, val);
	return val;
}


char *get_busid_from_env() {

	char *devpath;
	char *busid;

	logmsg("get_busid_from_env() called" );

	devpath = getenv("DEVPATH");
	if( devpath == NULL ) {
		logerr("DEVPATH is not set" );
		return NULL;
	}

	busid = rindex(devpath, '/');
	if( busid == NULL ) {
		logerr("illegal DEVPATH format: %s", devpath);
		return NULL;
	}
	if( strlen(busid) < 2 ) {
		logwarn("strange busid value: '%s'", busid );
		return NULL;
	}
	busid++;
	logmsg("get_busid_from_env():: returning '%s'", busid );

	return busid;
}


/*
 * initializes the current device with values obtained by the environment
 */
int fill_current_device(struct current_device *dev ) {

	char *busid;
	char *product;
	char *devpath;
	int ret;

	product = getenv("PRODUCT");
	if( product == NULL ) {
		logerr("PRODUCT is not set");
		return -1;
	}

	devpath = getenv("DEVPATH");
	if( devpath == NULL ) {
		logerr("DEVPATH is not set" );
		return -1;
	}
	printf( "PRODUCT = %s\n", product );
	printf( "DEVPATH = %s\n", devpath );

	busid = rindex(devpath, '/');
	if( busid == NULL ) {
		logerr("illegal DEVPATH format: %s", devpath);
		return -1;
	}
	busid++;

	dev->vendor  = get_vendorid(product);
	dev->product = get_productid(product);
	dev->devpath = devpath;
	dev->busid   = busid;
	dev->ifs     = NULL;

	ret = scan_interfaces(dev);
	if( ret < 0 ) {
		logerr( "scanning for interfaces failed" );
		return -1;
	}
	
	return 0;
}

int scan_interfaces(struct current_device *dev) {

	char *busid;
	DIR *path;
	char ifpath[MAX_LEN];
	int ret;
	char match[MAX_LEN];
	int match_len;
	char devpath[MAX_PATH];


	snprintf(devpath, MAX_PATH, "%s%s", SYSFS_ROOT, dev->devpath );

	busid = rindex(devpath, '/');
	if( busid == NULL ) {
		logerr("illegal DEVPATH format: %s", devpath);
		return -1;
	}
	busid++;

	printf( "busid: '%s'\n", busid );
	snprintf(match, MAX_LEN, "%s:", busid);
	match_len = strlen(match);
	//logmsg( "device '%s' has %d interfaces", busid, read_bNumInterfaces );
	
	path = opendir(devpath);
	if(!path) {
		logerr("could not open %s: %s",devpath, strerror(errno));
		return -1;
	}

	for(;;) {

		struct dirent *entry;
		FILE *f;
		int class;

		entry = readdir(path);
		if( !entry )
			break;

		if( !strncmp( match, entry->d_name, match_len)) {
			printf( "found interface\n" );
			snprintf(ifpath, MAX_LEN,
					"%s/%s/bInterfaceClass",
					devpath,
					entry->d_name );
			logmsg("dir: %s, ifpath: %s", entry->d_name, ifpath);

			f = fopen( ifpath, "r" );
			if( !f ) {
				logwarn("could not open %s: %s", ifpath, strerror(errno));
			}

			ret = fscanf(f, "%x", &class );
			if( ret != 1 ) {
				logwarn("error reading value from bInterfaceClass");
				class = 0;
			}
			fclose(f);

			printf( "found interface of class 0x%x\n", class );

			add_if_to_current(dev, class);
		}
	}

	return 0;
}

int add_if_to_current(struct current_device *dev, int class) {

	struct iflist *item;
	struct iflist *current;
	int el = 0;

	item = malloc(sizeof(struct iflist));
	item->if_num = class;
	item->next   = NULL;

	if( dev->ifs == NULL ) {
		dev->ifs = item;
		logmsg("added class '0x%x' as first item", class);
		return 0;
	}

	current = dev->ifs;
	while( current->next != NULL) {
		current = current->next;
		el++;
		if( el > 100 ) {
			logmsg("ENDLESS LOOP");
			break;
		}
	}

	current->next = item;
	logmsg("added class '0x%x' at end of list", class);

	return 0;
}


/* This is a bit complicated
 *
 * We look at each interface of the new device in turn.  If the class of this
 * interface was listed in the config file, we use the policy stated in the
 * config file for this class.
 * If the class was not listed, we use the global policy for this interface.
 *
 * If the policy for two interfaces is differen (i.e. one being auto_export,
 * and another being no_auto_export), we apply conflict resolution.
 *
 */
int check_export_class(struct current_device *dev) {

	struct interface_entry *entry;
	struct iflist *list;

	int policy = -1; /* -1 means 'has not been set, yet' */


	/* loop over interfaces of the device */
	for( list=dev->ifs  ;  list != NULL  ;  list=list->next  ) {
		logmsg("checking device interface: %d", list->if_num);
		int new_policy = -1;

		/* loop over interfaces listed in config file */
		entry = config->ifs;
		for( entry=config->ifs  ;  entry != NULL  ;  entry=entry->next ){
			logmsg("  checking config class: %d", entry->if_num );
			if( entry->if_num == list->if_num ) {
				new_policy = entry->policy;
				break;
			}
		}

		/* interface was not specified in config. Apply default policy */
		if( new_policy == -1 ) {
			logmsg("class '%d' not in config. applying global policy %d",
					list->if_num,
					config->policy );

			new_policy = config->policy;
		}

		if( policy == -1 ) { /* no policy has been set, yet */
			policy = new_policy;
		}
		else {
			if( policy != new_policy ) { /* conflict */
				logmsg("interface conflict! applying conflict resolution");
				policy = config->if_conflict;
				goto end_of_while_loops; /* break all while loops */
			}
		}
	} /* end for */

end_of_while_loops:

	if( policy == -1 )
		return NO_DECISION;
	else if( policy == auto_export ) 
		return EXPORT;
	else
		return NO_EXPORT;

}

int check_export_device(struct current_device *dev) {

	struct device_entry *entry;

	int product = dev->product;
	int vendor  = dev->vendor;

	entry = config->devs;

	while( entry != NULL ) {
		if( entry->vendor != vendor )
			entry = entry->next;
			continue;

		if( entry->product != product )
			entry = entry->next;
			continue;

		if( entry->policy == auto_export )
			return EXPORT;
		else
			return NO_EXPORT;
	}

	return NO_DECISION;
}


int create_tmp_file(char *server, char *busid) {

	char filename[MAX_PATH];
	FILE *fd;

	snprintf(filename, MAX_PATH, "%s%s", TMP_FILE_PATTERN, busid);
	
	fd = fopen(filename, "w" );
	if(!fd) {
		logerr("could not create '%s': %s", filename, strerror(errno) );
		return -1;
	}

	fprintf(fd, "%s", server );

	fclose(fd);

	return 0;
}

int get_tmp_file(char *server, char *busid) {

	char filename[MAX_PATH];
	FILE *fd;
	int ret;

	if( busid == NULL ) {
		logerr("busid is not set" );
		return -1;
	}
	snprintf(filename, MAX_PATH, "%s%s", TMP_FILE_PATTERN, busid);
	logmsg("opening file '%s'", filename);


	fd = fopen(filename, "r" );
	if(!fd) {
		logwarn( "could not open '%s': %s", filename, strerror(errno) );
		return -1;
	}

	/* FIXME buffer overflow */
	ret = fscanf(fd, "%s", server);
	if( ret != 1 ) {
		logerr("could not get server from '%s'", filename);
		fclose(fd);
		return -1;
	}

	fclose(fd);
	return 0;
}

int remove_tmp_file(char *server, char *busid) {

	char filename[MAX_PATH];
	int ret;

	if( busid == NULL ) {
		logerr("busid is not set" );
		return -1;
	}
	snprintf(filename, MAX_PATH, "%s%s", TMP_FILE_PATTERN, busid);
	logmsg("deleting %s", filename);

	ret = unlink(filename);
	if(ret) {
		logerr("failed to delete %s: %s", filename, strerror(errno));
		return -1;
	}

	return 0;
}
/*
char *conf_get_server() {
	return config->server_val;
}
*/

/*
char *current_dev_get_busid() {

	int ret;

	if( current_dev.busid == NULL ) {
		ret = fill_current_device(&current_dev);
		if(ret < 0)
			return NULL;
	}

	return current_dev.busid;
}
*/
