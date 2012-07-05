/*
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
 * L2 driver for ISO 9141 protocol.
 *
 * NOTE: ISO9141-2 says that if the target address is 0x33, then the SAE-J1979
 * Scantool Application Protocol is used.
 * 
 * Other addresses are manufacturer-specific, and MAY EXCEED THIS IMPLEMENTATION.
 * (But we still let you TRY to use them... :) Just keep in mind that ISO9141 messages
 * have a maximum payload of 7 bytes.
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_raw.h"

#include "diag_l2_iso9141.h" /* prototypes for this file */

CVSID("$Id: diag_l2_iso9141.c,v 1.7 2011/08/07 02:17:46 fenugrec Exp $");

/*
 * This implements the message checksum as defined in the ISO9141-2 specs.
 * The cs variable will overflow almost every iteration;
 * this is by design, and not a problem, since we only
 * need the lower byte of the checksum.
 */
uint8_t
diag_l2_proto_iso9141_cs(uint8_t *msg_buf, int nbytes)
{
	uint8_t cs = 0;
	uint8_t i;

	for (i=0; i<nbytes; i++)
		cs += msg_buf[i];

	return cs;
}


/*
 * This implements the handshaking process between Tester and ECU.
 * It is used to wake up an ECU and get its KeyBytes.
 *
 * The process as defined in ISO9141 is:
 * 1 - Tester sends target address (0x33) at 5 baud;
 * 2 - ECU wakes up, sends synch pattern 0x55 at approx. 10400 baud;
 * 3 - Tester clocks synch pattern and defines baud rate (10400 baud);
 * 4 - ECU sends first KeyByte;
 * 5 - ECU sends second KeyByte;
 * 6 - Tester regulates p2 time according to KeyBytes;
 * 6 - Tester sends second KeyByte inverted;
 * 7 - ECU sends received address inverted (0xCC);
 * This concludes a successfull handshaking in ISO9141.
 */
int
diag_l2_proto_iso9141_wakeupECU(struct diag_l2_conn *d_l2_conn)
{
	struct diag_l1_initbus_args in;
	struct diag_serial_settings set;
	uint8_t kb1, kb2, address, inv_address, inv_kb2;
	int rv = 0;
	struct diag_l2_iso9141 *dp;

	kb1 = kb2 = address = inv_address = inv_kb2 = 0;
	dp = d_l2_conn->diag_l2_proto_data;
	
	// Flush unread input:
	(void)diag_tty_iflush(d_l2_conn->diag_link->diag_l2_dl0d);

	// Wait for idle bus:
	diag_os_millisleep(W5min);

	// Do 5Baud init (write Address, read Synch Pattern):
	address = dp->target;
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = address;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0)
		return rv;

	// The L1 device has read the 0x55, and may have changed the
	// speed that we are talking to the ECU at (NOT!!!).

	// Receive the first KeyByte:
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0, &kb1, 1, W2max);
	if (rv < 0)
		return rv;

	// Receive the second KeyByte:
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0, &kb2, 1, W3max);
	if (rv < 0)
		return rv;

	// Check keybytes, these can be 0x08 0x08 or 0x94 0x94:
	if ( (kb1 != kb2) || ( (kb1 != 0x08) && (kb1 != 0x94) ) )
		return diag_iseterr(DIAG_ERR_WRONGKB);

	// Copy KeyBytes to protocol session data:
	d_l2_conn->diag_l2_kb1 = kb1;
	d_l2_conn->diag_l2_kb2 = kb2;

	// set p2min according to KeyBytes:
	// P2min is 0 for kb 0x94, 25ms for kb 0x08;
	if (kb1 == 0x94)
		d_l2_conn->diag_l2_p2min = 0;
	else
		d_l2_conn->diag_l2_p2min = 25;

	// Now send inverted KeyByte2, and receive inverted address
	// (unless L1 deals with this):
	if ( (d_l2_conn->diag_link->diag_l2_l1flags
	  & DIAG_L1_DOESSLOWINIT) == 0)
	{
		//Wait W4min:
		diag_os_millisleep(W4min);

		//Send inverted kb2:
		inv_kb2 = (uint8_t) ~kb2;
		rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
					&inv_kb2, 1, 0);
		if (rv < 0)
			return rv;
		
		// Wait for the address byte inverted:
		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
					&inv_address, 1, W4max);
		if (rv < 0)
		{
			if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,
				FLFMT "startcomms con %p rx error %d\n",
				FL, d_l2_conn, rv);
			return rv;
		}

		// Check the received inverted address:
		if ( inv_address != (uint8_t)~address )
		{
			if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,
				FLFMT "startcomms 0x%02x != 0x%02x\n",
				FL, inv_address,
				~address);
			return diag_iseterr(DIAG_ERR_WRONGKB);
		}
	}

	//Success! Handshaking done.
	
	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_iso9141_wakeupECU con %p kb1 0x%x kb2 0x%x\n",
			FL, d_l2_conn, 
			kb1, kb2);

	return 0;
}


/*
 * This implements the start of a new protocol session.
 * It wakes up an ECU if not in monitor mode.
 */
static int
diag_l2_proto_iso9141_startcomms(struct diag_l2_conn *d_l2_conn,
				 flag_type flags, int bitrate,
				 target_type target, source_type source)
{
	int rv;
	struct diag_serial_settings set;
	struct diag_l2_iso9141 *dp;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_iso9141_startcomms conn %p\n",
			FL, d_l2_conn);

	if (diag_calloc(&dp, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	dp->srcaddr = source;
	dp->target = target;
	dp->state = STATE_CONNECTING;
	d_l2_conn->diag_l2_kb1 = 0;
	d_l2_conn->diag_l2_kb2 = 0;
	d_l2_conn->diag_l2_proto_data = (void *)dp;

	// Prepare the port for this protocol:
	// Data bytes are in {7bits, OddParity, 1stopbit}, but we
	// read and write as {8bits, NoParity, 1stopbit}.
	// That must be taken into account by the application / layer 3 !!!
	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;
	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;
	
	if (rv = diag_l1_setspeed(d_l2_conn->diag_link->diag_l2_dl0d, &set))
		return diag_iseterr(rv);

	// Initialize ECU (unless if in monitor mode):
	if ( (flags & DIAG_L2_TYPE_INITMASK) ==  DIAG_L2_TYPE_MONINIT)
		rv = 0;
	else
		rv = diag_l2_proto_iso9141_wakeupECU(d_l2_conn);

	//if (diag_l2_debug & DIAG_DEBUG_OPEN)
//			fprintf(stderr,
//			FLFMT "diag_l2_iso9141_startcomms returns %d\n",
//			FL, rv);

	if (rv)
		return diag_iseterr(rv);

	dp->state = STATE_ESTABLISHED;
	
	return rv;
}


/*
 * Free session-specific allocated data.
 */
static int
diag_l2_proto_iso9141_stopcomms(struct diag_l2_conn* d_l2_conn)
{
	struct diag_l2_iso9141 *dp;

	dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;
	if (dp)
		free(dp);

	//Always OK for now.
	return 0;
}


/*
 * This implements the interpretation of a response message.
 * Decodes the message header, returning the length
 * of the message if a whole message has been received.
 * Used by the _int_recv function to walk the list of received
 * messages.
 */
static int
diag_l2_proto_iso9141_decode(uint8_t *data, int len,
				 int *hdrlen, int *datalen, int *source, int *dest)
{
	int dl;

	if (diag_l2_debug & DIAG_DEBUG_PROTO)
	{
		int i;
		fprintf(stderr, FLFMT "decode len %d", FL, len);
		for (i = 0; i < len ; i++)
		{
			fprintf(stderr, " 0x%x", data[i]&0xff);
		}
		fprintf(stderr, "\n");
	}

	//Check header bytes:
	if(data[0] != 0x48 || data[1] != 0x6B )
		return diag_iseterr(DIAG_ERR_BADDATA);

	// The data length will depend only on the content of the message,
	// and is not present in it; therefore, only L3 / application
	// will be able to check it for real; so we just assume the
	// data length is always = received_len - (header + chksm):
	if(len - OHLEN_ISO9141 > 0)
		*datalen = len - OHLEN_ISO9141;
	else
	{
		if (diag_l2_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "decode len short \n", FL);

		return diag_iseterr(DIAG_ERR_INCDATA);
	}

	//Set header length (always the same):
	*hdrlen = OHLEN_ISO9141 - 1;

	// Set Source and Destination addresses:
	if (dest) *dest = 0xF1; //Always the Tester.
	if (source) *source = data[2]; // Originating ECU;

	if (diag_l2_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr, FLFMT "decode hdrlen = %d, datalen = %d, cksum = 1\n",
			FL, *hdrlen, *datalen);

	return (*hdrlen + *datalen + 1);
}

/*
 * This implements the reading of all the ECU responses to a Tester request.
 * One ECU may send multiple responses to one request.
 * Multiple ECUS may send responses to one request.
 * The end of all responses is marked by p3max timeout, but since that is very
 * long (5 seconds), we're using p3min (55ms).
 */
int
diag_l2_proto_iso9141_int_recv(struct diag_l2_conn *d_l2_conn, int timeout)
{
	int rv, l1_doesl2frame, l1flags;
	int tout = 0;
	int state;
	struct diag_l2_iso9141 *dp;
	struct diag_msg *tmsg, *lastmsg;

#define ST_STATE1 1 // Start - wait for a frame.
#define ST_STATE2 2 // In frame - wait for more bytes.
#define ST_STATE3 3 // End of frame - wait for more frames.

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_iso9141_int_recv offset %x\n",
			FL, d_l2_conn->rxoffset);

	state = ST_STATE1;
	dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;

	// Clear out last received message if not done already.
	if (d_l2_conn->diag_msg)
	{
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	// Check if L1 device does L2 framing:
	l1flags = d_l2_conn->diag_link->diag_l2_l1flags;
	l1_doesl2frame = (l1flags & DIAG_L1_DOESL2FRAME);
	if ( l1flags & ( DIAG_L1_DOESL2FRAME | DIAG_L1_DOESP4WAIT ) )
	{
		// Extend timeouts for the "smart" interfaces:
		if (timeout < SMART_TIMEOUT)
			timeout = SMART_TIMEOUT;
	}

	// Message read cycle: byte-per-byte for passive interfaces,
	// frame-per-frame for smart interfaces (DOESL2FRAME).
	// ISO-9141-2 says:
	// 	Inter-byte gap in a frame < p1max
	//	Inter-frame gap < p2max
	// We are a bit more flexible than that, see below.
	// Frames get acumulated in the protocol structure list.
	while (1)
	{
		switch(state)
		{
			case ST_STATE1:
				// Ready for first byte, use timeout
				// specified by user.
				tout = timeout;
				break;

			case ST_STATE2:
				// Inter-byte timeout within a frame.
				// ISO says p1max is the maximum, but in fact
				// we give ourselves up to p2min minus a little bit.
				tout = d_l2_conn->diag_l2_p2min - 2;
				if (tout < d_l2_conn->diag_l2_p1max)
				tout = d_l2_conn->diag_l2_p1max;
				break;

			case ST_STATE3:
				// This is the timeout waiting for any more
				// responses from the ECU. ISO says min is p3max
				// but we'll cut it short at p3min.
				// Aditionaly, for "smart" interfaces, we expand
				// the timeout to let them process the data.
				tout = d_l2_conn->diag_l2_p3min;
				if (l1_doesl2frame)
				tout += SMART_TIMEOUT;
		}

		// If L0/L1 does L2 framing, we get full frames, so we don't
		// need to do the read byte-per-byte (skip state2):
		if ( (state == ST_STATE2) && l1_doesl2frame )
			rv = DIAG_ERR_TIMEOUT;
		else
			// Receive data into the buffer:
			rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
					&dp->rxbuf[dp->rxoffset],
					MAXLEN_ISO9141 - dp->rxoffset,
					tout);
			
		// Timeout = end of message or end of responses.
		if (rv == DIAG_ERR_TIMEOUT)
		{
			switch (state)
			{
				case ST_STATE1:
					// If we got 0 bytes on the 1st read,
					// just return the timeout error.
					if (dp->rxoffset == 0)
						break;

					// Otherwise try to read more bytes into
					// this message.
					state = ST_STATE2;
					continue;

				case ST_STATE2:
					// End of that message, maybe more to come;
					// Copy data into a message.
					tmsg = diag_allocmsg((size_t)dp->rxoffset);
					tmsg->len = dp->rxoffset;
					tmsg->fmt |= DIAG_FMT_FRAMED ;
					memcpy(tmsg->data, dp->rxbuf,
						(size_t)dp->rxoffset);
					(void)gettimeofday(&tmsg->rxtime, NULL);

					if (diag_l2_debug & DIAG_DEBUG_READ)
					{
						fprintf(stderr, "l2_iso9141_recv: ");
						diag_data_dump(stderr, dp->rxbuf, (size_t)dp->rxoffset);
						fprintf(stderr, "\n");
					}

					dp->rxoffset = 0;

					// Add received message to response list:
					diag_l2_addmsg(d_l2_conn, tmsg);

					// Finished this one, get more:
					state = ST_STATE3;
					continue;

				case ST_STATE3:
					/*
					 * No more messages, but we did get one
					 */
					rv = d_l2_conn->diag_msg->len;
				break;
			}

			// end of all response messages:
			if (state == ST_STATE3)
				break;
		}

		// Other reception errors.
		if (rv < 0)
			break;
		
		// Data received OK.
		// Add length to offset.
		dp->rxoffset += rv;

		// This is where some tweaking might be needed if
		// we are in monitor mode... but not yet.

		// Got some data in state1/3, now we're in a message!
		if ( (state == ST_STATE1) || (state == ST_STATE3) )
			state = ST_STATE2;

	}//end while (read cycle).
	
	// Now walk through the response message list, 
	// and strip off their headers and checksums 
	// after verifying them.
	if (rv >= 0)
	{
		tmsg = d_l2_conn->diag_msg;
		lastmsg = NULL;
		
		while (tmsg)
		{
			int hdrlen, datalen, source, dest;

			dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;

			// If L1 doesn't strip the checksum byte, validate it:
			if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0)
			{
				uint8_t rx_cs = tmsg->data[tmsg->len - 1];
				if(rx_cs != diag_l2_proto_iso9141_cs(tmsg->data, tmsg->len - 1))
				{
					fprintf(stderr, FLFMT "Checksum error in received message!\n", FL);
					return -1;
				}
				// "Remove" the checksum byte:
				tmsg->len--;
			}

			// Process L2 framing, if L1 doesn't do it.
			if(l1_doesl2frame == 0)
			{
				// Get frame geometry and data:
				rv = diag_l2_proto_iso9141_decode( tmsg->data,
								tmsg->len,
								&hdrlen, &datalen, &source, &dest);

				if (rv < 0) // decode failure!
					return rv;

				//It is possible we have misframed this message and it is infact
				//more than one message, so see if we can decode it.
				if (rv < tmsg->len)
				{
					//This message contains more than one	
					//data frame (because it arrived with
					// odd timing), this means we have to
					// do horrible copy about the data
					// things ....
					struct diag_msg	*amsg;
					amsg = diag_dupsinglemsg(tmsg);
					amsg->len = rv;
					tmsg->len -=rv;
					tmsg->data += rv;

					/*  Insert new amsg before old msg */
					amsg->next = tmsg;
					if (lastmsg == NULL)
					d_l2_conn->diag_msg = amsg;
					else
					lastmsg->next = amsg;
					
					tmsg = amsg; /* Finish processing this one */
				}

				// "Remove" header:
				tmsg->data += hdrlen;
				tmsg->len -= hdrlen;

				// Set source address:
				tmsg->src = source;
				// Set destination address:
				tmsg->dest = dest;
			}

			// Message done. Flag it up:
			tmsg->fmt |= DIAG_FMT_FRAMED;  //???
			tmsg->fmt |= DIAG_FMT_DATAONLY; //We removed the frame;
			tmsg->fmt |= DIAG_FMT_CKSUMMED; //We checked the checksum;

			// Prepare to decode next message:
			lastmsg = tmsg;
			tmsg = tmsg->next;
		}
	}

	return rv;
}



static int
diag_l2_proto_iso9141_recv(struct diag_l2_conn *d_l2_conn, int timeout,
			void (*callback)(void *handle, struct diag_msg *msg), void *handle)
{
	int rv;

	rv = diag_l2_proto_iso9141_int_recv(d_l2_conn, timeout);
	if ((rv >= 0) && d_l2_conn->diag_msg)
	{
		if (diag_l2_debug & DIAG_DEBUG_READ)
			fprintf(stderr, FLFMT "rcv callback calling %p(%p)\n", FL,
				callback, handle);
		/*
		 * Call user callback routine
		 */
		if (callback)
			callback(handle, d_l2_conn->diag_msg);

		/* No longer needed */
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	return rv;
}

/*
 * Package the data into a message frame with header and checksum.
 * Addresses were supplied by the protocol session initialization.
 * Checksum is calculated on-the-fly.
 * Apply inter-frame delay (p2).
 */
static int
diag_l2_proto_iso9141_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;
	int sleeptime;
	uint8_t buf[MAXLEN_ISO9141];
	int offset;
	struct diag_l2_iso9141 *dp;

	dp = d_l2_conn->diag_l2_proto_data;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
		FLFMT "diag_l2_send 0p%p 0p%p called\n",
		FL, d_l2_conn, msg);

	// Check if the payload plus the overhead (and checksum) exceed protocol packet size:
	if(msg->len + OHLEN_ISO9141 > MAXLEN_ISO9141)
	{
		fprintf(stderr, FLFMT "send: Message payload exceeds maximum allowed by protocol!\n", FL);
		return -1;
	}
	
	/*
	 * Make sure enough time between last receive and this send
	 * In fact, because of the timeout on recv(), this is pretty small, but
	 * we take the safe road and wait the whole of p3min plus whatever
	 * delay happened before
	 */
	sleeptime = d_l2_conn->diag_l2_p3min;
	if (sleeptime > 0)
		diag_os_millisleep(sleeptime);

	offset = 0;

	// If the interface doesn't do ISO9141-2 header, add it before the data:
	if ((d_l2_conn->diag_link->diag_l2_l1flags & DIAG_L1_DOESL2FRAME) == 0)
	{
		buf[offset++] = 0x68; //defined by spec;
		buf[offset++] = 0x6A; //defined by spec;
		buf[offset++] = dp->srcaddr;
	}
	
	// Now copy in data, should check for buffer overrun really.
	memcpy(&buf[offset], msg->data, msg->len);
	offset += msg->len;

	// If the interface doesn't do ISO9141-2 checksum, add it in:
	if ((d_l2_conn->diag_link->diag_l2_l1flags & DIAG_L1_DOESL2CKSUM) == 0)
	{
		uint8_t curoff = offset;
		buf[offset++] = diag_l2_proto_iso9141_cs(buf, curoff);
	}

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, "l2_iso9141_send: ");
		diag_data_dump(stderr, buf, (size_t)offset);
		fprintf(stderr, "\n");
	}

	// Send it over the L1 link:
	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			buf, (size_t)offset, d_l2_conn->diag_l2_p4min);
	
	if (diag_l2_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, "l2_iso9141_send: ");
		diag_data_dump(stderr, buf, (size_t)offset);
		fprintf(stderr, "\n");
		fprintf(stderr, FLFMT "about to return %d\n", FL, rv);
	}

	return rv;
}


static struct diag_msg *
diag_l2_proto_iso9141_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
				  int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0)
	{
		*errval = rv;
		return NULL;
	}

	/* And wait for response */

	rv = diag_l2_proto_iso9141_int_recv(d_l2_conn, 1000);
	if ((rv >= 0) && d_l2_conn->diag_msg)
	{
		/* OK */
		rmsg = d_l2_conn->diag_msg;
		d_l2_conn->diag_msg = NULL;
	}
	else
	{
		/* Error */
		*errval = DIAG_ERR_TIMEOUT;
		rmsg = NULL;
	}

	return rmsg;
}

static const struct diag_l2_proto diag_l2_proto_iso9141 = 
{
	DIAG_L2_PROT_ISO9141,
	DIAG_L2_FLAG_FRAMED | DIAG_L2_FLAG_DATA_ONLY |  DIAG_L2_FLAG_DOESCKSUM,
	diag_l2_proto_iso9141_startcomms,
	diag_l2_proto_iso9141_stopcomms,
	diag_l2_proto_iso9141_send,
	diag_l2_proto_iso9141_recv,
	diag_l2_proto_iso9141_request,
	NULL
};


int diag_l2_iso9141_add(void) 
{
	return diag_l2_add_protocol(&diag_l2_proto_iso9141);
}

