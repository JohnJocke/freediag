/*
 *
 *	freediag - Vehicle Diagnostic Utility
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
 * Diag
 *
 * L2 driver for SAEJ1850
 *
 *
 * INCOMPLETE, will not work, but doesnt coredump. This has been checked in
 * because scantool.c was checked in for other reasons, and needs this so it
 * doesnt coredump ...
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_saej1850.h" /* prototypes for this file */

CVSID("$Id: diag_l2_saej1850.c,v 1.2 2004/07/01 20:24:21 meisner Exp $");

/*
 * SAEJ1850 specific data
 */
struct diag_l2_j1850
{
	uint8_t type;		/* FAST/SLOW/CARB */
	
	uint8_t srcaddr;	/* Src address used */
	uint8_t dstaddr;	/* Dest address used */
	uint16_t modeflags;	/* Flags */

	uint8_t state;
	uint8_t rxbuf[MAXRBUF];	/* Receive buffer, for building message in */
	int rxoffset;		/* Offset to write into buffer */
};

#define STATE_CLOSED	  0	/* Established comms */
#define STATE_CONNECTING  1	/* Connecting */
#define STATE_ESTABLISHED 2	/* Established */

/* Prototypes */
uint8_t diag_l2_proto_j1850_crc(uint8_t *msg_buf, int nbytes);

/* External interface */

/*
 * The complex initialisation routine for SAEJ1850
 */
#ifdef WIN32
static int
diag_l2_proto_j1850_startcomms(struct diag_l2_conn	*d_l2_conn,
flag_type flags,
int bitrate,
target_type target, source_type source)
#else
static int
diag_l2_proto_j1850_startcomms(struct diag_l2_conn	*d_l2_conn,
flag_type flags,
int bitrate __attribute__((unused)),
target_type target, source_type source)
#endif
{
	struct diag_l2_j1850 *dp;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_j1850_startcomms conn %p\n",
				FL, d_l2_conn);

	if (diag_calloc(&dp, 1))
		return(DIAG_ERR_NOMEM);

	d_l2_conn->diag_l2_proto_data = (void *)dp;

	dp->srcaddr = source;
	dp->dstaddr = target;
	dp->modeflags = flags;

	dp->state = STATE_CONNECTING;

	/* Empty our Receive buffer and wait for idle bus */
	/* XXX is the timeout value right ? It is 300 in other places. */

	/* Flush unread input, then wait for idle bus. */
	/* XXX is the timeout value right ? It is 300 in other places. */
	(void)diag_tty_iflush(d_l2_conn->diag_link->diag_l2_dl0d);
	diag_os_millisleep(50);

	/* Always OK */
	return(0);
}

/*
*/
static int
diag_l2_proto_j1850_stopcomms(struct diag_l2_conn* d_l2_conn)
{
	struct diag_l2_14230 *dp;

	dp = (struct diag_l2_14230 *)d_l2_conn->diag_l2_proto_data;

	if (dp)
		free(dp);


	/* Always OK for now */
	return (0);
}


/* Thanks to B. Roadman's web site for this CRC code */
uint8_t
diag_l2_proto_j1850_crc(uint8_t *msg_buf, int nbytes)
{
	uint8_t crc_reg=0xff,poly,i,j;
	uint8_t *byte_point;
	uint8_t bit_point;

	for (i=0, byte_point=msg_buf; i<nbytes; ++i, ++byte_point)
	{
		for (j=0, bit_point=0x80 ; j<8; ++j, bit_point>>=1)
		{
			if (bit_point & *byte_point)	// case for new bit = 1
			{
				if (crc_reg & 0x80)
					poly=1;	// define the polynomial
				else
					poly=0x1c;
				crc_reg= ( (crc_reg << 1) | 1) ^ poly;
			}
			else		// case for new bit = 0
			{
				poly=0;
				if (crc_reg & 0x80)
					poly=0x1d;
				crc_reg= (crc_reg << 1) ^ poly;
			}
		}
	}
	return ~crc_reg;	// Return CRC
}

/*
 * Just send the data
 *
 * We add the header and checksums here as appropriate
 */
static int
diag_l2_proto_j1850_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int l1flags, rv, l1protocol;
	struct diag_l2_j1850 *dp;

	uint8_t buf[MAXRBUF];
	int offset = 0;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_j1850_send %p msg %p len %d called\n",
				FL, d_l2_conn, msg, msg->len);

	dp = (struct diag_l2_j1850 *)d_l2_conn->diag_l2_proto_data;
	l1flags = d_l2_conn->diag_link->diag_l2_l1flags;

	// Add the J1850 header to the data
	// XXX 0x68 is no correct for all J1850 protocols

	l1protocol = d_l2_conn->diag_link->diag_l2_l1protocol;

	if (l1protocol == DIAG_L1_J1850_PWM)
		buf[0] = 0x61;
	else
		buf[0] = 0x68;
	buf[1] = dp->dstaddr;
	buf[2] = dp->srcaddr;
	offset += 3;
	
	// Now copy in data, should check for buffer overrun really
	memcpy(&buf[offset], msg->data, msg->len);
	offset += msg->len;

	if ((l1flags & DIAG_L1_DOESL2CKSUM) == 0)
	{
		// Add in J1850 CRC
		/* XXX This had nasty side effects.  I changed it to what I thought was wanted. */
		int curoff = offset;
		buf[offset++] = diag_l2_proto_j1850_crc(buf, curoff);
	}

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_j1850_send sending %d bytes to L1\n",
				FL, offset);

	// And send data to Layer 1
	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
				buf, (size_t)offset, 0);

	return(rv);
}

/*
 * Protocol receive routine
 *
 * Will sleep until a complete set of responses has been received, or fail
 * with a timeout error
 */
static int
diag_l2_proto_j1850_int_recv(struct diag_l2_conn *d_l2_conn, int timeout)
{
	int rv;
	struct diag_l2_j1850 *dp;
	int tout;
	struct diag_msg	*tmsg;
	int l1flags = d_l2_conn->diag_link->diag_l2_l1flags;

	dp = (struct diag_l2_j1850 *)d_l2_conn->diag_l2_proto_data;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_j1850_int_recv offset %x\n",
				FL, dp->rxoffset);

	if (l1flags & DIAG_L1_DOESL2FRAME)
	{
		tout = timeout;
		if (tout < 100)	/* Extend timeouts for clever interfaces */
			tout = 100;

		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
				&dp->rxbuf[dp->rxoffset],
				sizeof(dp->rxbuf) - dp->rxoffset,
				tout);
		if (rv < 0)
		{
			// Error
			return(rv);
		}
		dp->rxoffset += rv;
	}
	else
	{
		// No support for non framing L2 interfaces yet ...
		return(diag_iseterr(DIAG_ERR_PROTO_NOTSUPP));
	}

	// Ok, got a complete frame to send upward

	if (dp->rxoffset)
	{
		// There is data left to add to the message list ..
		tmsg = diag_allocmsg((size_t)dp->rxoffset);
		tmsg->len = dp->rxoffset;
		memcpy(tmsg->data, dp->rxbuf, (size_t)dp->rxoffset);

		/*
		 * Minimum message length is 3 header bytes
		 * 1 data, 1 checksum
		 */
		if (tmsg->len >= 5)
		{
			if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0)
			{
				/* XXX check checksum */
			}
			tmsg->dest = tmsg->data[1];
			tmsg->src = tmsg->data[2];
			tmsg->data +=3;
			tmsg->len -=3;

			/* remove checksum byte if needed */
			if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0)
				tmsg->len--;
		}
		else
		{
			diag_freemsg(tmsg);
			return(diag_iseterr(DIAG_ERR_BADDATA));
		}

		(void)gettimeofday(&tmsg->rxtime, NULL);
		dp->rxoffset = 0;

		/*
		 * ADD message to list
		 */
		diag_l2_addmsg(d_l2_conn, tmsg);
	}

	dp->state = STATE_ESTABLISHED;
	return(0);
}


static int
diag_l2_proto_j1850_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg),
	void *handle)
{
	int rv;
	struct diag_msg	*tmsg;

	rv = diag_l2_proto_j1850_int_recv(d_l2_conn, timeout);
	if (rv < 0)	/* Failed */
		return(rv);

	/*
	 * We now have data stored on the L2 descriptor
	 */
	if (diag_l2_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr, FLFMT "calling rcv callback %p handle %p msg %p\n",
			FL, d_l2_conn->diag_msg,
			callback, handle);
	}

	tmsg = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;

	tmsg->fmt |= DIAG_FMT_FRAMED | DIAG_FMT_DATAONLY ;
	tmsg->fmt |= DIAG_FMT_CKSUMMED;


	/* Call used callback */
	if (callback)
		callback(handle, tmsg);

	/* message no longer needed */
	diag_freemsg(tmsg);

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "rcv callback completed\n", FL);

	return(0);
}

/*
 * Send a request and wait for a response
 */
static struct diag_msg *
diag_l2_proto_j1850_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;

	/* First send the message */
	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0)
	{
		*errval = rv;
		return(NULL);
	}

	/* And now wait for a response */
	/* XXX, whats the correct timeout for this ??? */
	rv = diag_l2_proto_j1850_int_recv(d_l2_conn, 250);
	if (rv < 0)
	{
		*errval = rv;
		return(NULL);
	}

	/* Return the message to user, who is responsible for freeing it */
	rmsg = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;
	return(rmsg);
}

static const struct diag_l2_proto diag_l2_proto_j1850 = {
	DIAG_L2_PROT_SAEJ1850, DIAG_L2_FLAG_FRAMED | DIAG_L2_FLAG_DATA_ONLY
	| DIAG_L2_FLAG_DOESCKSUM | DIAG_L2_FLAG_CONNECTS_ALWAYS,
	diag_l2_proto_j1850_startcomms,
	diag_l2_proto_j1850_stopcomms,
	diag_l2_proto_j1850_send,
	diag_l2_proto_j1850_recv,
	diag_l2_proto_j1850_request,
	NULL
};

int diag_l2_j1850_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_j1850);
}
