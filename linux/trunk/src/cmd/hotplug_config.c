/*
 * $Id$
 *
 * Copyright (C) 2007 Robert Leibl <robert.leibl@gmail.com>
 */


#include "hotplug_config.h"



void init_default_config() {

	int i;

	config = malloc(sizeof(struct _config));

	config->enabled     = 0;
	config->policy      = no_auto_export;
	config->if_conflict = no_auto_export;
	config->server_fac  = address;

	config->devs        = NULL;
	config->ifs         = NULL;

	for(i=0; i<MAX_LEN; i++ )
		(config->server_val)[i] = '\0';
};

void free_config() {
}

/*
 * return
 * 	0	ok
 * 	1	usbip is turned off
 * 	-1	error
 */
int read_config() {

	int ret;

	init_default_config();

	ret = parse_config();
	if ( ret < 0 ) {
		logerr( "could not parse config" );
		return -1;
	}
	if( config->enabled == 0 ) {
		logmsg( "usbip turned off" );
		return 1;
	}

	return 0;
}

int get_export_server(char *buffer, int len) {

	int maxl;

	maxl = (len>MAX_LEN) ? len : MAX_LEN;

	switch(config->server_fac) {
		case address:
			strncpy(buffer, config->server_val, maxl);
			break;
		case file:
			return read_server_from_file(buffer, len);
			break;
		case program:
			return read_server_from_program(buffer, len);
			break;
		default:
			logmsg("illegal server facility: %d", config->server_fac);
			return -1;
	}

	return 0;
}

int read_server_from_file(char *buffer, int len) {

	FILE *fd;
	int ret;

	fd = fopen(config->server_val, "r");
	if( !fd) {
		logerr("could not open '%s': %s", config->server_val, strerror(errno));
		return -1;
	}
	/* FIXME may cause buffer overflow */
	ret = fscanf(fd, "%s", buffer);
	if( ret != 1 ) {
		logwarn("error reading value from %s", config->server_val );
		return -1;
	}

	fclose(fd);

	return 0;
}
int read_server_from_program(char *buffer, int len) {

	int ret;
	FILE *pipe;

	pipe = popen( config->server_val, "r" );
	if(!pipe) {
		logerr("failed to create pipe: %s", strerror(errno) );
		return -1;
	}

	/* FIXME may cause buffer overflow */
	ret = fscanf(pipe, "%s", buffer);
	if( ret != 1 ) {
		logwarn("error reading value from pipe");
		return -1;
	}
	pclose(pipe);

	return 0;
}



int parse_config() {

	FILE *fd;
	char buffer[MAX_LEN];

	fd = fopen( CONF_FILE, "r" );
	if( ! fd ) {
		logerr( "could not open config file: %s", strerror(errno) );
		return -1;
	}

	while( fgets(buffer, MAX_LEN, fd) ) {

		/* skip empty lines, and lines starting with '#' */
		if ( buffer[0] == '#' )
			continue;
		if ( buffer[0] == '\n' )
			continue;

		parse_line( buffer );

	}

	/* test, if we have reached the end of the file, or if an error
	 * occured */
	if( feof( fd ) ) {
		logmsg( "config file parsed" );
		dump_config();
	}
	else {
		logmsg( "error parsing config file: %s", strerror(errno) );
		return -1;
	}

	fclose(fd);

	return 0;
}

int parse_line(char *line) {
	
	int len;
	char *param;


	/* enabled */
	len = strlen("enable_auto_export=");
	if( ! strncmp("enable_auto_export=", line, len) ) {
		param = line + len;
		logmsg( "param is '%s'", param );
		if( ! strncmp("yes", param, 3 )) {
			config->enabled = 1;
			return 0;
		}else {
			config->enabled = 0;
			return -1;
		}
	}



	/* policy */
	len = strlen("policy=");
	if( ! strncmp("policy=", line, len) ) {
		param = line + len;
		if( ! strncmp( "auto_export", param, 11 ))
			config->policy = auto_export;
		else
			config->policy = no_auto_export;

		return 0;
	}



	/* if_interface_conflict */
	len = strlen("if_interface_conflict=");
	if( ! strncmp("if_interface_conflict=", line, len) ) {
		param = line + len;
		if( ! strncmp( "auto_export", param, 11 ))
			config->if_conflict = auto_export;
		else
			config->if_conflict = no_auto_export;

		return 0;
	}



	/* server */
	len = strlen("server ");
	if( ! strncmp("server ", line, len) ) {
		param = line + len;
		return parse_for_server(param);
	}


	/* device */
	len = strlen("device ");
	if( ! strncmp("device ", line, len) ) {
		param = line + len;
		return parse_for_device(param);
	}


	/* class */
	len = strlen("class ");
	if( ! strncmp("class ", line, len) ) {
		param = line + len;
		return parse_for_class(param);
	}


	logmsg("parse error: %s", line);
	return -1;
}


int parse_for_server(char *line) {
	
	char fac[MAX_LEN];
	char val[MAX_LEN];

	split_in_two( line, fac, val );

	if( ! strncmp( "address", fac, 7 )) {
		config->server_fac = address;
	}
	else if( ! strncmp("file", fac, 4) ) {
		config->server_fac = file;
	}
	else if( ! strncmp( "program", fac, 7) ) {
		config->server_fac = program;
	}
	else {
		logerr( "illegal parameter for server option" );
		return -1;
	}

	chomp(val);
	strncpy( config->server_val, val, strlen(val) );

	return 0;
}


int parse_for_device(char *line) {

	char dev[MAX_LEN];
	char pol[MAX_LEN];
	enum policies policy = no_auto_export;

	printf( "parse_for_device\n" );
	split_in_two( line, dev, pol );

	if( !strncmp("auto_export", pol, strlen("auto_export")))
		policy = auto_export;

	conf_add_device( dev, policy );

	return 0;
}


int parse_for_class(char *line) {

	char cls[MAX_LEN];
	char pol[MAX_LEN];
	enum policies policy = no_auto_export;

	printf( "parse_for_class\n" );
	split_in_two( line, cls, pol );

	if( !strncmp("auto_export", pol, strlen("auto_export")))
		policy = auto_export;

	conf_add_interface( cls, policy );

	return 0;
}

int split_in_two( char *src, char *dest1, char *dest2 ) {

	int len;
	int count1;
	int count2;
	char *ind;

	//printf( "input: src='%s'\n", src );
	len = strlen(src);
	ind = index( src, ' ' );
	if( ! ind ) {
		printf( "parse error" );
		return -1;
	}

	count1 = (ind-src);
	//printf("count1 = %d\n", count1 );
	strncpy( dest1, src, count1 );
	dest1[count1] = '\0';

	//printf("count2 = %d\n", count1 );
	count2 = (src+len) - ind;
	strncpy ( dest2, ind+1, count2 );
	dest2[count2] = '\0';

	//logmsg( "dest1: '%s'", dest1 );
	//logmsg( "dest2: '%s'", dest2 );

	return 0;
}
void dump_config() {

	struct interface_entry *ifs;
	struct device_entry *dev;

	logmsg( "\n\n+ config:\n+ =====================================" );
	logmsg( "+ enabled = '%s'", (config->enabled) ? "yes":"no");
	logmsg( "+ policy = '%s'", 
			(config->policy==auto_export) ? "auto_export" : "no_auto_export" );
	logmsg( "+ if_conflict = '%s'",
			(config->if_conflict==auto_export)?"auto_export":"no_auto_export");
	logmsg( "+ server_fac = '%d'", config->server_fac);
	logmsg( "+ server_val = '%s'", config->server_val);

	logmsg( "+\n+ interfaces\n+ ======================================");
	if( config->ifs != NULL ) {
		ifs = config->ifs;
		do {
			logmsg( "+  class: %d -> %s", 
				ifs->if_num, 
				(ifs->policy==auto_export)?"auto_export":"no_auto_export");
			ifs = ifs->next;
		}
		while( ifs != NULL );
	}

	logmsg( "+\n+ devices\n+ =========================================");
	if( config->devs != NULL ) {
		dev = config->devs;
		do {
			logmsg( "+  device: %x:%x -> %s",
				dev->vendor,
				dev->product,
				(dev->policy==auto_export)?"auto_export":"no_auto_export");
			dev = dev->next;
		}while( dev != NULL );
	}
	logmsg("+ config done\n");

}



int conf_add_interface( char *class, enum policies policy ) {

	struct interface_entry *ifs;
	struct interface_entry *current;

	ifs = malloc(sizeof(struct interface_entry));
	ifs->if_num = get_if_number( class );
	ifs->policy = policy;
	ifs->next   = NULL;

	current = config->ifs;
	if( current == NULL ) {
		config->ifs = ifs;
		return 0;
	}

	while( current->next != NULL )
		current = current->next;

	current->next = ifs;

	return 0;
}

int conf_get_vendorid(char *device) {

	char vendor[5];
	int val;
	int i;

	for(i=0;i<5;i++)
		vendor[i] = '\0';

	for(i=0; i < 5; i++) {
		if( device[i] == '\0' ) {
			logwarn("invalid 'device'entry format");
			return -1;
		}
		if( device[i] == ':' )
			break;

		vendor[i] = device[i];
	}
	vendor[4] = '\0';

	val = (int)strtol(vendor, NULL, 16);

	printf( "vendor:str=%s, hex=%x\n", vendor, val);
	return val;
}
int conf_get_productid(char *device) {

	char product[5];
	int val;
	int i;
	char *pos;

	for(i=0;i<5;i++)
		product[i] = '\0';

	pos = index(device, ':');
	if(pos == NULL) {
		logerr("invalid 'device' entry format");
		return -1;
	}
	pos++;

	for(i=0; i < 5; i++) {
		if( pos[i] == '\0' ) {
			break;
		}
		product[i] = pos[i];
	}
	product[4] = '\0';

	val = (int)strtol(product, NULL, 16);

	printf( "product:str=%s, hex=%x\n", product, val);
	return val;
}

int conf_add_device( char *device, enum policies policy ) {

	struct device_entry *dev;
	struct device_entry *current;

	if( strlen(device) < 9 )
		return -1;


	dev = malloc(sizeof(struct device_entry));
	dev->vendor  = conf_get_vendorid(device);
	dev->product = conf_get_productid(device);
	dev->policy  = policy;


	current = config->devs;
	if( current == NULL ) {
		config->devs = dev;
		return 0;
	}

	while( current->next != NULL )
		current = current->next;

	current->next = dev;

	return 0;
}


int get_if_number( char *interface ) {

	if( !strncmp("per_interface",interface, sizeof("per_interface"))) {
		return USB_CLASS_PER_INTERFACE;
	}
	if( !strncmp("audio",interface,sizeof("audio"))) {
		return USB_CLASS_AUDIO;
	}
	if( !strncmp("comm",interface,sizeof("comm"))) {
		return USB_CLASS_COMM;
	}
	if( !strncmp("hid",interface,sizeof("hid"))) {
		return USB_CLASS_HID;
	}
	if( !strncmp("physical",interface,sizeof("physical"))) {
		return USB_CLASS_PHYSICAL;
	}
	if( !strncmp("still_image",interface,sizeof("still_image"))) {
		return USB_CLASS_STILL_IMAGE;
	}
	if( !strncmp("printer",interface,sizeof("printer"))) {
		return USB_CLASS_PRINTER;
	}
	if( !strncmp("mass_storage",interface,sizeof("mass_storage"))) {
		return USB_CLASS_MASS_STORAGE;
	}
	if( !strncmp("hub",interface,sizeof("hub"))) {
		return USB_CLASS_HUB;
	}
	if( !strncmp("cdc_data",interface,sizeof("cdc_date"))) {
		return USB_CLASS_CDC_DATA;
	}
	if( !strncmp("cscid",interface,sizeof("cscid"))) {
		return USB_CLASS_CSCID	;
	}
	if( !strncmp("content_sec",interface,sizeof("content_sec"))) {
		return USB_CLASS_CONTENT_SEC;
	}
	if( !strncmp("video",interface,sizeof("video"))) {
		return USB_CLASS_VIDEO;
	}
	if( !strncmp("wireless_controller",interface,sizeof("wireless_controller"))) {
		return USB_CLASS_WIRELESS_CONTROLLER;
	}
	if( !strncmp("misc",interface,sizeof("misc"))) {
		return USB_CLASS_MISC;
	}
	if( !strncmp("app_spec",interface,sizeof("app_spec"))) {
		return USB_CLASS_APP_SPEC;
	}
	if( !strncmp("vendor_spec",interface,sizeof("vendor_spec"))) {
		return USB_CLASS_VENDOR_SPEC;
	}

	return -1;
}

int chomp(char *buffer) {

	char *pos = rindex(buffer, '\n');
	if( pos == NULL )
		return 0;

	*pos = '\0';
	return 1;
}


