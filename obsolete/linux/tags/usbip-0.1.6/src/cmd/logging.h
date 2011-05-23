/*
 * logging.h
 *
 *
 * Copyright (C) 2007 by Robert Leibl <robert.leibl@gmail.com>
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

/*
 * As the hotplugging part may run mostly unattended, we use a separate
 * logfile.
 */

#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define LOGFILE "/var/log/usbip_hotplug.log"

FILE *logfile;

extern int logfile_open;

int open_log(void);
int close_log(void);


/* debugging */
#define DEBUG 

#ifdef DEBUG
#define logdbg(fmt, args...) do {\
	if(logfile_open) { \
		fprintf( logfile, fmt "\n", ##args ); \
	} else { \
		fprintf( stdout, fmt "\n", ##args ); \
	} \
} while( 0 )
#else

#define logdbg(fmt, args...) ;

#endif



#define logmsg(fmt, args...) do {\
	if(logfile_open) { \
		fprintf( logfile, fmt "\n", ##args ); \
	} else { \
		fprintf( stdout, fmt "\n", ##args ); \
	} \
} while( 0 )


#define logwarn(fmt, args...) do { \
	if(logfile_open) { \
		fprintf( logfile, "WARNING: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__, ##args); \
	} else { \
		fprintf( stderr, "WARNING: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__, ##args); \
	} \
} while( 0 )

#define logerr(fmt, args...) do { \
	if(logfile_open) { \
		fprintf( logfile, "ERROR: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__, ##args); \
	} else { \
		fprintf( stderr, "ERROR: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__, ##args); \
	} \
} while( 0 )



#endif /*  _LOGGING_H_ */
