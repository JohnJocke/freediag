#ifndef _DIAG_DTC_H_
#define _DIAG_DTC_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id: diag_dtc.h,v 1.2 2004/07/01 20:22:28 meisner Exp $
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
 *
 *
 * DTC handling routines
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef WIN32
typedef enum {
	dtc_proto_j2012	=	1,	/* SAE J2012 */
	dtc_proto_int8	=	2,	/* 8 bit integer */
	dtc_proto_int16	=	3,	/* 16 bit integer */
	dtc_proto_int32	=	4,	/* 32 bit integer */
	dtc_proto_text	=	5	/* Text String */
} diag_dtc_protocol;
#else
enum diag_dtc_protocol {
	dtc_proto_j2012	=	1,	/* SAE J2012 */
	dtc_proto_int8	=	2,	/* 8 bit integer */
	dtc_proto_int16	=	3,	/* 16 bit integer */
	dtc_proto_int32	=	4,	/* 32 bit integer */
	dtc_proto_text	=	5	/* Text String */
};
#endif


void diag_dtc_init(void);
#ifdef WIN32
char* diag_dtc_decode(uint8_t *data, int len,
	const char *vehicle, const char *ecu, diag_dtc_protocol protocol,
	char *buf, const size_t bufsize);
#else
char* diag_dtc_decode(uint8_t *data, int len,
	const char *vehicle, const char *ecu, enum diag_dtc_protocol protocol,
	char *buf, const size_t bufsize);
#endif
#if defined(__cplusplus)
}
#endif
#endif // _DIAG_DTC_H_
