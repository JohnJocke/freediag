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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * L3 code to do SAE J1979 messaging
 *
 */
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_l3_saej1979.h"

CVSID("$Id: diag_l3_saej1979.c,v 1.8 2011/06/09 01:11:25 fenugrec Exp $");

/*
 * Return the expected J1979 packet length for a given mode byte
 * This includes the 3 header bytes, up to 7 data bytes, 1 ERR byte
 *
 * XXX DOESN'T COPE WITH in-frame-response - will break check routine as well
 * Also doesn't support 15765 (CAN) which has more modes.
 *
 * Get this wrong and all will fail, it's used to frame the incoming messages
 * properly
 */
static int diag_l3_j1979_getlen(uint8_t *data, int len)
{
	static const int rqst_lengths[] = { -1, 6, 7, 5, 5, 6, 6, 5, 11, 6 };
	int rv;
	uint8_t mode;

	if (len < 5)	/* Need 3 header bytes and 1 data byte, and 1 cksum*/
		return diag_iseterr(DIAG_ERR_INCDATA);

	mode = data[3];

	//J1979 specifies 9 modes (0x01 - 0x09) except with iso15765 (CAN) which has 0x0A modes.

	if (mode > 0x49)
		return diag_iseterr(DIAG_ERR_BADDATA);

	/* Mode 8 responses vary in length dependent on request */
	//J1979 section 6.8+ 
	//the requests, however, are fixed length
	// There is a filler byte inserted when using TID 00 to request supported TIDs. But length is still 7+4
	//if ((mode == 8) && ((data[4]&0x1f) == 0))
	//	len = 6;

	if (mode < 0x41) {
		if (mode <= 9)
			return rqst_lengths[mode];
		return diag_iseterr(DIAG_ERR_BADDATA);
	}

	rv = DIAG_ERR_BADDATA;

	//Here, all modes < 0x0A are taken care of.
	//Modes > 0x40 are responses and therefore need specific treatment
	//data[4] contains the PID / TID number.
	switch (mode) {
	case 0x41:
	case 0x42:		//almost identical modes except PIDS 1,2
		//len<5 already covered at top of function
		//if (len < 5) {
			/* Need 1st 2 bytes of data to find length */
		//	rv = DIAG_ERR_INCDATA;
		//	break;
		// }
		if ((data[4] & 0x1f) == 0) {	
			/* PID 00 or 0x20 : return supported PIDs */
			rv = 10;
			break;
		}

		switch (data[4]) {
		case 1:	//Status. Only with service 01 (mode 0x41)
			rv=10;
			if (mode==0x42)
				rv=DIAG_ERR_BADDATA;
			break;
		case 2:	//request freeze DTC. Only with Service 02 (mode=0x42)
			rv=8;
			if (mode==0x41)
				rv = DIAG_ERR_BADDATA;
			break;
		case 3:
			rv = 8;
			break;
		case 0x04:
		case 0x05:
			rv=7;
			break;
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
			//XXX For PIDs 0x06 thru 0x09, there may be an additional data byte based on PID 0x13 / 0x1D results
			// (presence of a bank3 O2 sensor. Not implemented...
			rv=7;
			break;
		case 0x0A:
		case 0x0B:
			rv=7;
			break;
		case 0x0C:
			rv=8;	
			break;
		case 0x0D:
		case 0x0E:
		case 0x0F:
			rv=7;
			break;
		case 0x10:
			rv=8;
			break;
		case 0x11:
		case 0x12:
		case 0x13:
			rv = 7;
			break;
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
			rv = 8;
			break;
		case 0x1C:
		case 0x1D:
		case 0x1E:
			rv = 7;
			break;
		case 0x1F:
			rv = 8;
			break;
		default:
			/* Sometime add J2190 support (PID>0x1F) */
			rv = DIAG_ERR_BADDATA;
			break;
		}
		break;
	case 0x43:
		rv=11;
		break;
	case 0x44:
		rv = 5;
		break;
	case 0x45:
		//if len>=5
		if ((data[4] & 0x1f) == 0)
			rv = 11;		// Read supported TIDs
		else if (data[4]<4)
			rv=8;			//J1979 sec 6.5.2.4 : conditional TIDs.
		else
			rv = 10;		//Request TID result
		break;
	case 0x46:
		//J1979 sec6.6 : response length should depend on one of the data bytes. Screwed.
	case 0x47:
	case 0x48:
		rv = 11;
		break;
	case 0x49:
		//if (len < 5)
		if ((data[4] & 0x01) ==0) {
			rv=11;	//7+4 bytes if even
			break;
		} else {
			rv=7;		//else just 3+4
			break;
		}
	default:
		break;
	}
	return rv;
}


/*
 * Send a J1979 packet - we know the length (from looking at the data)
 */
static int
diag_l3_j1979_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	struct diag_l2_conn *d_conn;
	uint8_t buf[32];
	struct diag_msg newmsg;
	uint8_t cksum;
	int i;

	/* Get l2 connection info */
	d_conn = d_l3_conn->d_l3l2_conn;

	if (diag_l3_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,FLFMT "send %d bytes, l2 flags 0x%x\n",
			FL, msg->len, d_l3_conn->d_l3l2_flags);

	/* Note source address on 1st send */
	if (d_l3_conn->src == 0)
		d_l3_conn->src = msg->src;


	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DATA_ONLY) {
		/* L2 does framing, adds addressing and CRC, so do nothing */
		rv = diag_l2_send(d_conn, msg);
	} else {
		/* Put data in buffer */
		memcpy(&buf[3], msg->data, msg->len);

		/*
		 * Add addresses. Were using default addresses here, suitable
		 * for ISO9141 and one of the J1850 protocols. However our
		 * L2 J1850 code does framing for us, so thats no issue.
		 */
		if (msg->data[0] >= 0x40) {
			/* Response */
			buf[0] = 0x48;
			buf[1] = 0x6B;	/* We chose to overide msg->dest */
		} else {
			/* Request */
			buf[0] = 0x68;
			buf[1] = 0x6A;	/* We chose to overide msg->dest */
		}
		buf[2] = msg->src;

		/*
		 * We do an ISO type checksum as default. This wont be
		 * right for J1850, but that is handled by our L2 J1850 code
		 * so thats no issue.
		 */
		if ( ((d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DOESCKSUM)==0)
			&& ((d_l3_conn->d_l3l1_flags & DIAG_L1_DOESL2CKSUM)==0)) {
			/* No one else does checksum, so we do it */
			for (i=0, cksum = 0; i<msg->len+3; i++)
				cksum += buf[i];
			buf[msg->len+3] = cksum;

			newmsg.len = msg->len + 4; /* Old len + hdr + cksum */
		} else {
			newmsg.len = msg->len + 3;	/* Old len + hdr */
		}

		newmsg.data = buf;

		/* And send message */
		rv = diag_l2_send(d_conn, &newmsg);
	}
	return rv;
}

/*
 * RX callback, called as data received from L2. If we get a full message,
 * call L3 callback routine
 */
static void
diag_l3_rcv_callback(void *handle, struct diag_msg *msg)
{
	/*
	 * Got some data from L2, build it into a L3 message, if
	 * message is complete call next layer callback routine
	 */
	struct diag_l3_conn *d_l3_conn = (struct diag_l3_conn *)handle;

	if (diag_l3_debug & DIAG_DEBUG_READ)
		fprintf(stderr,FLFMT "rcv_callback for %d bytes fmt 0x%x conn rxoffset %d\n",
			FL, msg->len, msg->fmt, d_l3_conn->rxoffset);

	if (msg->fmt & DIAG_FMT_FRAMED) {
		if ( (msg->fmt & DIAG_FMT_DATAONLY) == 0) {
			/* Remove header etc */
			struct diag_msg *tmsg;
			/*
			 * Have to remove L3 header and checksum from each response
			 * on the message
			 *
			 * XXX checksum check needed ...
			 */
			for (tmsg = msg ; tmsg; tmsg = tmsg->next) {
				tmsg->fmt |= DIAG_FMT_ISO_FUNCADDR;
				tmsg->fmt |= DIAG_FMT_DATAONLY;
				tmsg->type = tmsg->data[0];
				tmsg->dest = tmsg->data[1];
				tmsg->src = tmsg->data[2];
				/* Length sanity check */
				if (tmsg->len >= 4) {
					tmsg->data += 3;
					tmsg->len -= 4;	/* Remove header and checksum */
				}
			}
		} else {
			/* XXX check checksum */

		}
		/* And send data upward if needed */
		if (d_l3_conn->callback)
			d_l3_conn->callback(d_l3_conn->handle, msg);
	} else {
		/* Add data to the receive buffer on the L3 connection */
		memcpy(&d_l3_conn->rxbuf[d_l3_conn->rxoffset],
			msg->data, msg->len);
		d_l3_conn->rxoffset += msg->len;
	}
}


/*
 * Process_data() - this is the routine that works out the framing
 * of the data , if recv() was always called at the correct time and
 * in the correct way then we would have one and one complete message
 * received by the L2 code normally - however, we cant be sure of that
 * so we employ this algorithm
 *
 * Look at the data and try and work out the length of the message based
 * on the J1979 protocol.
 * VVVVVVVV
 * Note: J1979 doesn't specify particular checksums beyond what 9141 and 14230 already provide,
 * i.e. a J1979 message is maximum 7 bytes long except on CANBUS.
 * headers, address and checksum should be handled at the l2 level (9141, 14230, etc)
 * The code currently doesn't verify checksums but have provisions for stripping headers + checksums.
 * Also, the _getlen() function assumes 3 header bytes + 1 checksum byte.
 *
 * Another validity check is comparing message length (expected vs real).
 * Upper levels can also verify that the responses correspond to the requests (i.e. Service ID 0x02 -> 0x42 )
 *
 *
 */
static void
diag_l3_j1979_process_data(struct diag_l3_conn *d_l3_conn)
{
	/* Process the received data into messages if complete */
	struct diag_msg *msg;
	int sae_msglen;
	int i;

	while (d_l3_conn->rxoffset) {
		int badpacket=0;

		sae_msglen = diag_l3_j1979_getlen(d_l3_conn->rxbuf,
					d_l3_conn->rxoffset);	//set expected packet length based on SID + TID

		if (diag_l3_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr,FLFMT "process_data rxoffset is %d sae_msglen is %ld\n",
				FL, d_l3_conn->rxoffset, (long)sae_msglen);
			fprintf(stderr,FLFMT "process_data hex data is ",
				FL);
			for (i=0; i < d_l3_conn->rxoffset; i++)
				fprintf(stderr,"%02x ", d_l3_conn->rxbuf[i]);
			fprintf(stderr,"\n");
		}

		if (sae_msglen < 0) {
			if (sae_msglen == DIAG_ERR_INCDATA) {
				/* Not enough data in this frame */
				return;
			} else {
				/* Duff data received, bad news ! */
				badpacket = 1;
			}
		}

		if (badpacket || (sae_msglen <= d_l3_conn->rxoffset )) {

			/* Bad packet, or full packet, need to tell user */
			uint8_t *data = NULL;
			struct diag_msg *lmsg;

			if (diag_calloc(&msg, 1)) {
				/* Stuffed, no memory, cant do anything */
				return;
			}

			if (!badpacket)
				if (diag_malloc(&data, (size_t)sae_msglen))
					return;
	
			if (badpacket || (data == NULL)) {
				/* Failure indicated by zero len msg */
				msg->data = NULL;
				msg->len = 0;
			} else {
				msg->fmt = DIAG_FMT_ISO_FUNCADDR;
				msg->type = d_l3_conn->rxbuf[0];
				msg->dest = d_l3_conn->rxbuf[1];
				msg->src = d_l3_conn->rxbuf[2];
				/* Copy in J1979 part of message */
				memcpy(data, &d_l3_conn->rxbuf[3], (size_t)(sae_msglen - 4));
				/* remove whole message from rx buf */
				memcpy(d_l3_conn->rxbuf, 
					&d_l3_conn->rxbuf[sae_msglen],
					(size_t)sae_msglen);

				d_l3_conn->rxoffset -= sae_msglen;

				msg->data = data;
				msg->len = sae_msglen - 4;
			}
			free(data);

			gettimeofday(&msg->rxtime, NULL);

			/* Add it to the list */
			if (d_l3_conn->msg == NULL) {
				d_l3_conn->msg = msg;
			} else {
				lmsg = d_l3_conn->msg;
				while (lmsg->next != NULL) {
					lmsg = lmsg->next;
				}
				lmsg->next = msg;
			}
			free(msg);
			if (badpacket) {
				/* No point in continuing */
				break;
			}
		} else {
			/* Need some more data */
			break;
		}
	}
}

/*
 * Receive a J1979 frame (building it as we get small amounts of data)
 *
 * - timeout expiry will cause return before complete packet
 *
 * Successful packet receive will call the callback routine with the message
 */
static int
diag_l3_j1979_recv(struct diag_l3_conn *d_l3_conn, int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle)
{
	int rv;
	struct diag_msg *msg;
	int tout;
	int state;
//State machine. XXX states should be described somewhere...
#define ST_STATE1 1
#define ST_STATE2 2
#define ST_STATE3 3
#define ST_STATE4 4

	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_FRAMED) {
		/*
		 * L2 does framing stuff , which means we get one message
		 * with nicely formed frames
		 */
		state = ST_STATE4;
		tout = timeout;
	} else {
		state = ST_STATE1;
		tout = 0;
	}

	/* Store the callback routine for use if needed */
	d_l3_conn->callback = rcv_call_back;
	d_l3_conn->handle = handle;

	/*
	 * This works by doing a read with a timeout of 0, to collect
	 * any data that was present on the link, if no messages complete
	 * then read with the normal timeout, then read with a timeout
	 * of p4max ms until no more data is left (or timeout), then call
	 * the callback routine (if there is a message complete)
	 */
	while (1) {
		/* State machine for setting timeout values */
		switch (state) {
			case ST_STATE1:
				tout = 0;
				break;
			case	ST_STATE2:
				tout = timeout;
				break;
			case ST_STATE3:
				tout = 5; /* XXX should be p4max */
				break;
			case ST_STATE4:
				tout = timeout;
				break;
			default:
				break;
		}

		if (diag_l3_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,FLFMT "recv state %d tout %d\n",
				FL, state, tout);

		/*
		 * Call L2 receive, L2 will build up the datapacket and
		 * call the callback routine if needed, we use a timeout
		 * of zero to see if data exists *now*,
		 */
		rv = diag_l2_recv(d_l3_conn->d_l3l2_conn, tout,
			diag_l3_rcv_callback, (void *)d_l3_conn);

		if (diag_l3_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,FLFMT "recv returns %d\n",
				FL, rv);

		if ((rv < 0) && (rv != DIAG_ERR_TIMEOUT))
			break;		/* Some nasty failure */

		if (rv == DIAG_ERR_TIMEOUT) {
			if ( (state == ST_STATE3) || (state == ST_STATE4) ) {
				/* Finished */
				break;
			}
			if ( (state == ST_STATE1) && (d_l3_conn->msg == NULL) ) {
				/*
				 * Try again, with real timeout
				 * (and thus sleep)
				 */
				state = ST_STATE2;
				tout = timeout;
				continue;
			}
		}

		if (state != ST_STATE4) {
			/* Process the data into messages */
			diag_l3_j1979_process_data(d_l3_conn);

			if (diag_l3_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr,FLFMT "recv process_data called, msg %p rxoffset %d\n",
					FL, d_l3_conn->msg,
					d_l3_conn->rxoffset);

			/*
			 * If there is a full message, remove it, call back
			 * the user call back routine with it, and free it
			 */
			msg = d_l3_conn->msg;
			if (msg) {
				d_l3_conn->msg = msg->next;

				if ( (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DATA_ONLY) == 0) {
					/* Strip hdr/checksum */
					msg->data += 3;
					msg->len -= 4;
				}
				rcv_call_back(handle, msg);
				if (msg->len)
					free (msg->data);
				free (msg);
				rv = 0;
				/* And quit while we are ahead */
				break;
			}
		}
		/* We do not have a complete message (yet) */
		if (state == ST_STATE2) {
			/* Part message, see if we get some more */
			state = ST_STATE3;
		}
		if (state == ST_STATE1) {
			/* Ok, try again with proper timeout */
			state = ST_STATE2;
		}
		if ((state == ST_STATE3) || (state == ST_STATE4)) {
			/* Finished, we only do read once in this state */
			break;
		}
	}

	return rv;
}

/*
 * This is called without the ADDR_ADDR_1 on it, ie it contains
 * just the SAEJ1979 data
 * Returns a string with the description + data associated with a J1979 message.
 * Doesn't do any data scaling / conversion.
 */
#ifdef WIN32
static char *
diag_l3_j1979_decode(struct diag_l3_conn *d_l3_conn,
struct diag_msg *msg, char *buf, size_t bufsize)
#else
static char *
diag_l3_j1979_decode(struct diag_l3_conn *d_l3_conn __attribute__((unused)),
struct diag_msg *msg, char *buf, size_t bufsize)
#endif
{
	int i, j;

	char buf2[16];

	char area;	//for DTCs

	if (msg->data[0] & 0x40)
		snprintf(buf, bufsize, "J1979 response ");
	else
		snprintf(buf, bufsize, "J1979 request ");

	switch (msg->data[0]) {
		case 0x01:
			snprintf(buf2, sizeof(buf2), "Mode 1 PID 0x%x", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x41:
			snprintf(buf2, sizeof(buf2),"Mode 1 Data: PID 0x%x ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x02:
			snprintf(buf2, sizeof(buf2), "Mode 2 PID 0x%x Frame 0x%x", msg->data[1],
				msg->data[2]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x42:
			snprintf(buf2, sizeof(buf2),"Mode 2 FreezeFrame Data: PID 0x%x Frame 0x%x ",
				msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf2);
			for (i=3; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x03:
			snprintf(buf2, sizeof(buf2),"Mode 3 (Powertrain DTCs)");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x07:
			snprintf(buf2, sizeof(buf2),
				"Request Non-Continuous Monitor System Test Results");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x47:
			snprintf(buf2, sizeof(buf2), "Non-Continuous Monitor System ");
			smartcat(buf, bufsize, buf2);
			/* Fallthru */
		case 0x43:
			snprintf(buf2, sizeof(buf2),"DTCs: ");
			smartcat(buf, bufsize, buf2);
			for (i=0, j=1; i<3; i++, j+=2) {
				if ((msg->data[j]==0) && (msg->data[j+1]==0))
					continue;
				
				switch ((msg->data[j] >> 6) & 0x03) {
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
						fprintf(stderr, "Illegal msg->data[%d] value\n", j);
						area = 'X';
						break;
				}
				snprintf(buf2, sizeof(buf2), "%c%02x%02x  ", area, msg->data[j] & 0x3f,
					msg->data[j+1]&0xff);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x04:
			snprintf(buf2, sizeof(buf2), "Clear DTCs");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x44:
			snprintf(buf2, sizeof(buf2), "DTCs cleared");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x05:
			snprintf(buf2, sizeof(buf2), "Oxygen Sensor Test ID 0x%x Sensor 0x%x",
					msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x45:
			snprintf(buf2, sizeof(buf2), "Oxygen Sensor TID 0x%x Sensor 0x%x ",
				msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf);
			for (i=3; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x06:
			snprintf(buf2, sizeof(buf2), "Onboard monitoring test request TID 0x%x", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x46:
			snprintf(buf2, sizeof(buf2),"Onboard monitoring test result TID 0x%x ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x08:
			snprintf(buf2, sizeof(buf2), "Request control of onboard system TID 0x%x", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x48:
			snprintf(buf2, sizeof(buf2), "Control of onboard system response TID 0x%x ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
		case 0x09:
			snprintf(buf2, sizeof(buf2), "Request vehicle information infotype 0x%x", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x49:
			snprintf(buf2, sizeof(buf2), "Vehicle information infotype 0x%x ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
		default:
			snprintf(buf2, sizeof(buf2),"UnknownType 0x%x: Data Dump: ", msg->data[0]);
			smartcat(buf, bufsize, buf2);
			for (i=0; i < msg->len; i++)
			{
				snprintf(buf2, sizeof(buf2), "0x%x ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
	}
	return buf;
}


/*
 * Timer routine, called with time (in ms) since the "timer" value in
 * the L3 structure
 */
static void
diag_l3_j1979_timer(struct diag_l3_conn *d_l3_conn, int ms)
{
	struct diag_msg msg;
	uint8_t data[6];

	/* J1979 needs keepalive at least every 5 seconds, we use 3.5s */
	// Not needed for l0_elm devices.
	if (ms < J1979_KEEPALIVE)
		return;

	/* Does L2 do keepalive for us ? */
	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_KEEPALIVE)
		return;

	/* OK, do keep alive on this connection */

	if (diag_l3_debug & DIAG_DEBUG_TIMER) {
		/* XXX Not async-signal-safe */
		fprintf(stderr, FLFMT "timeout impending for %p %d ms\n",
				FL, d_l3_conn, ms);
	}

	/*
	 * Mode 1 Pid 0 request is the SAEJ1979 idle message
	 * XXX Need to get the address bytes correct
	 */
	msg.data = data;
	msg.len = 2;
	data[0] = 1 ;		/* Mode 1 */
	data[1] = 0; 		/* Pid 0 */

	/*
	 * And set the source address, if no sends have happened, then
	 * the src address will be 0, so use the default used in J1979
	 */
	if (d_l3_conn->src)
		msg.src = d_l3_conn->src;
	else
		msg.src = 0xF1;		/* Default as used in SAE J1979 */

	/* Send it */
	(void)diag_l3_send(d_l3_conn, &msg);

	/* Get and ignore the response */
	(void)diag_l3_recv(d_l3_conn, 50, NULL, NULL);
	
	return;
}

const diag_l3_proto_t diag_l3_j1979 = {
	"SAEJ1979", diag_l3_base_start, diag_l3_base_stop,
	diag_l3_j1979_send, diag_l3_j1979_recv, NULL,
	diag_l3_j1979_decode, diag_l3_j1979_timer 
};
