#ifndef _DIAG_OS_H_
#define _DIAG_OS_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id: diag_os.h,v 1.2 2004/07/01 20:29:40 meisner Exp $
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 *************************************************************************
 */

#if defined(__cplusplus)
extern "C" {
#endif

/* CVSID macro that will avoid warnings. */
#ifdef WIN32
#define CVSID(ID) \
	static const char* const cvsid = (ID)
#else
#define CVSID(ID) \
	static const char* const cvsid __attribute__ ((unused)) = (ID)
#endif

/* Common prototypes but note that the source
// is different and defined in OS specific
// c files.
*/
void diag_os_sigalrm(int);
int diag_os_init(void);
int diag_os_millisleep(int ms);
int diag_os_ipending(int fd);

/* Scheduler */
int diag_os_sched(void);

#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_OS_H_ */
