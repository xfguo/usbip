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

#include "logging.h"

int open_log() {

	logfile = fopen( LOGFILE, "a" );

	if ( ! logfile ) {
		fprintf( stderr, "error opening logfile: %s\n", strerror(errno) );
		fprintf( stderr, "logging will be to STDOUT\n" );
		logfile = stdout;
	}
	printf( "logfile open\n" );
	fprintf( logfile, "\n\n-------------------------------------------\n" );

	logfile_open = 1;

	return 0;
}

int close_log() {

	logfile_open = 0;

	if( logfile == stdout )
		return 0;

	fclose( logfile );


	return 0;
}
