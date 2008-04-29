
#ifndef _LOG_UTIL_H_
#define _LOG_UTIL_H_
/*
 * log_util.h
 *
 * Copyright (C) 2007 by Robert Leibl <robert.leibl@gmail.com> and
 *                    by Takahiro Hirofuchi
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
 * moved here from usbip_common.h to separate logging from non-logging stuff.
 *
 * syslog-ng:
 * if you use syslog you may want to use a separate file for logging usbip
 * messages. Add the following lines to your /etc/syslog-ng/syslog-ng.conf
 *
 * destination usbip { file("/var/log/usbip.log"); };
 * filter f_usbip { match(usbip.*); };
 * log { source(src); filter(f_usbip); destination(usbip); };
 * 
 * your 'src' may vary.
 * Also note, that this will match for the 'usbip' keyword, so info() messages
 * will not show up in this logfile.
 */

extern int usbip_use_syslog;
extern int usbip_use_stderr;
extern int usbip_use_debug ;

#define err(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_ERR, "usbip err: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__,  ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, "usbip err: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__,  ##args); \
	} \
} while (0)

#define notice(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, "usbip: " fmt, ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, "usbip: " fmt "\n",  ##args); \
	} \
} while (0)

#define info(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, fmt, ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, fmt "\n",  ##args); \
	} \
} while (0)

#define dbg(fmt, args...)	do { \
	if (usbip_use_debug) { \
		if (usbip_use_syslog) { \
			syslog(LOG_DEBUG, "usbip dbg: %13s:%4d (%-12s) " fmt, \
				__FILE__, __LINE__, __FUNCTION__,  ##args); \
		} \
		if (usbip_use_stderr) { \
			fprintf(stderr, "usbip dbg: %13s:%4d (%-12s) " fmt "\n", \
				__FILE__, __LINE__, __FUNCTION__,  ##args); \
		} \
	} \
} while (0)



#ifdef TRACE_DEBUGGING
#define trace_start do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, "usbip trace: %13s:%4d entering %-12s\n" \
				__FILE__, __LINE__, __FUNCTION__); \
	} \
	if (usbip_use_stderr) { \
		printf("usbip trace: %13s:%4d entering %-12s\n" \
				__FILE__, __LINE__, __FUNCTION__); \
	} \
} while(0)

#define trace_end do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, "usbip trace: %13s:%4d exiting %-12s\n" \
				__FILE__, __LINE__, __FUNCTION__); \
	} \
	if (usbip_use_stderr) { \
		printf("usbip trace: %13s:%4d exiting %-12s\n" \
				__FILE__, __LINE__, __FUNCTION__); \
	} \
} while(0)

#else /* TRACE_DEBUGGING */

#define trace_start ;
#define trace_end ;

#endif /* TRACE_DEBUGGING */



#define BUG()	do { err("sorry, it's a bug"); abort(); } while (0)


#endif /* _LOG_UTIL_H_ */
