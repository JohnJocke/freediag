#ifndef _DIAG_L3_ISO14230_H_
#define _DIAG_L3_ISO14230_H_
/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id: diag_l3_iso14230.h,v 1.2 2011/06/09 01:11:25 fenugrec Exp $
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
 */
#if defined(__cplusplus)
extern "C" {
#endif

#define ISO14230_KEEPALIVE 3500		//ms timeout before keepalive signal on OBD bus.
extern const diag_l3_proto_t diag_l3_iso14230;

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L3_ISO14230_H_ */
