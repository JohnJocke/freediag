/*
 *	freediag - Vehicle Diagnostic Utility
 *
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
#include <string.h>

#include "diag.h"
#include "diag_dtc.h"

CVSID("$Id: diag_dtc.c,v 1.2 2004/07/01 20:22:28 meisner Exp $");


void diag_dtc_init(void)
{
}

/*
 * DTC decoding routine
 *
 *
 * Passed
 *	data, len	:-	Data representing the DTC
 *	char *vehicle	:-	Vehicle name
 *	char *ecu	:-	ECU Name
 *	int protocol	:-	Protocol (see include file)
 */

#ifdef WIN32
char *
diag_dtc_decode(uint8_t *data, int len, 
const char *vehicle,
const char *ecu,
diag_dtc_protocol protocol,
char *buf, const size_t bufsize)
#else
char *
diag_dtc_decode(uint8_t *data, int len, 
const char *vehicle __attribute__((unused)),
const char *ecu __attribute__((unused)),
enum diag_dtc_protocol protocol,
char *buf, const size_t bufsize)
#endif
{
	char area;

	switch (protocol)
	{
	case dtc_proto_j2012:
		if (len != 2) {
			strncpy(buf, "DTC too short for J1850 decode\n", bufsize);
			return buf;
		}

		switch ((data[0] >> 6) & 0x03)	/* Top 2 bits are area */
		{
		case 0:
			area = 'P';
			break;
		case 1:
			area = 'C';
			break;
		case 2:
			area = 'B';
			break;
		case 3:
			area = 'U';
			break;
		default:
			fprintf(stderr, "Illegal data[0] value\n");
			area = 'X';
			break;
		}
		snprintf(buf, bufsize, "%c%02x%02x ", area, data[0] & 0x3f, data[1]&0xff);
		break;

	case dtc_proto_int8:
	case dtc_proto_int16:
	case dtc_proto_int32:
	case dtc_proto_text:
		snprintf(buf, bufsize, "Unimplimented Protocol %d\n", protocol);
		break;

	default:
		snprintf(buf, bufsize, "Unknown Protocol %d\n", protocol);
		break;
	}
	return(buf);
}
