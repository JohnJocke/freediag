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
 * Mostly ODBII Compliant Scan Tool (as defined in SAE J1978)
 *
 * CLI routines - "set" commands
 *
 *
 */

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "scantool.h"
#include "scantool_cli.h"

CVSID("$Id: scantool_set.c,v 1.11 2011/06/09 01:11:25 fenugrec Exp $");

#define PROTO_NONE	"<not_used>"

int 	set_speed;	/* Comms speed */
unsigned char	set_testerid;	/* Our tester ID */
int	set_addrtype;	/* Use virtual addressing */
unsigned char	set_destaddr;	/* Dest ECU address */
int	set_L1protocol;	/* L1 protocol type */
int	set_L2protocol;	/* Protocol type */
int	set_initmode;

int set_display;		/* English (1), or Metric (0) */

const char *	set_vehicle;	/* Vehicle */
const char *	set_ecu;	/* ECU name */

//const char  *	set_interface;	/* H/w interface to use */
#define DEFAULT_INTERFACE 5	//index into l0_names below
const struct l0_name l0_names[] = { {"MET16", MET16}, {"SE9141", SE9141}, {"VAGTOOL", VAGTOOL},
			{"BR1", BR1}, {"ELM", ELM}, {"CARSIM", CARSIM}, {"DUMB", DUMB}, NULL};

enum l0_nameindex set_interface;	//hw interface to use

char set_subinterface[SUBINTERFACE_MAX];		/* and sub-interface ID */

char *set_simfile;	//source for simulation data
extern void diag_l0_sim_setfile(char * fname);

/*
 * XXX All commands should probably have optional "init" hooks.
 */
int set_init(void)
{
	/* Reset parameters to defaults. */

	set_speed = 10400;	/* Comms speed; ECUs will probably send at 10416 bps (96us per bit) */
	set_testerid = 0xf1;	/* Our tester ID */
	set_addrtype = 1;	/* Use virtual addressing */
	set_destaddr = 0x33;	/* Dest ECU address */
	set_L1protocol = DIAG_L1_ISO9141;	/* L1 protocol type */
	set_L2protocol = DIAG_L2_PROT_ISO9141;	/* Protocol type */
	set_initmode = DIAG_L2_TYPE_FASTINIT ;

	set_display = 0;		/* English (1), or Metric (0) */

	set_vehicle = "ODBII";	/* Vehicle */
	set_ecu = "ODBII";	/* ECU name */

	set_interface_idx= DEFAULT_INTERFACE;
	set_interface = l0_names[DEFAULT_INTERFACE].code;	/* Default H/w interface to use */

	strncpy(set_subinterface,"/dev/null",sizeof(set_subinterface));
	printf( "%s: Interface set to default: %s on %s\n", progname, l0_names[set_interface_idx].longname, set_subinterface);

	if (diag_calloc(&set_simfile, strlen(DB_FILE)+1))
		return diag_iseterr(DIAG_ERR_GENERAL);	
	strcpy(set_simfile, DB_FILE);			//default simfile for use with CARSIM
	diag_l0_sim_setfile(set_simfile);
	
	return 0;
}

void set_close(void)
{
	if (set_simfile)
		free(set_simfile);
	return;
}


/* Return values from the commands */
#define CMD_OK		0	/* OK */
#define CMD_USAGE	1	/* Bad usage, print usage info */
#define CMD_FAILED	2	/* Cmd failed */
#define CMD_EXIT	3	/* Exit called */
#define CMD_UP		4	/* Go up one level in command tree */

#define FLAG_HIDDEN	1	/* Hidden command */

/* SET sub menu */
static int cmd_set_help(int argc, char **argv);
//static int cmd_exit(int argc, char **argv);
static int cmd_set_show(int argc, char **argv);
static int cmd_set_speed(int argc, char **argv);
static int cmd_set_testerid(int argc, char **argv);
static int cmd_set_destaddr(int argc, char **argv);
static int cmd_set_addrtype(int argc, char **argv);
static int cmd_set_l1protocol(int argc, char **argv);
static int cmd_set_l2protocol(int argc, char **argv);
static int cmd_set_initmode(int argc, char **argv);
static int cmd_set_display(int argc, char **argv);
static int cmd_set_interface(int argc, char **argv);
static int cmd_set_simfile(int argc, char **argv);

const struct cmd_tbl_entry set_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_set_help, 0, NULL},

	{ "interface", "interface NAME [id]", "Shows/Sets the interface to use. Use set interface ? to get a list of names",
		cmd_set_interface, 0, NULL},
		
	{ "simfile", "simfile [filename]", "Select simulation file to use as data input. See freediag_carsim.db for an example",
		cmd_set_simfile, 0, NULL},

	{ "display", "display [english/metric]", "Sets english or metric display",
		cmd_set_display, 0, NULL},

	{ "speed", "speed [speed]", "Shows/Sets the speed to connect",
		cmd_set_speed, 0, NULL},
	{ "testerid", "testerid [testerid]",
		"Shows/Sets the source ID for us to use",
		cmd_set_testerid, 0, NULL},
	{ "destaddr", "destaddr [destaddr]",
		"Shows/Sets the destination ID to connect",
		cmd_set_destaddr, 0, NULL},

	{ "addrtype", "addrtype [func/phys]", "Shows/Sets the address type to use",
		cmd_set_addrtype, 0, NULL},


	{ "l1protocol", "l1protocol [protocolname]", "Shows/Sets the hardware protocol to use. Use set l1protocol ? to get a list of protocols",
		cmd_set_l1protocol, 0, NULL},

	{ "l2protocol", "l2protocol [protocolname]", "Shows/Sets the software protocol to use. Use set l2protocol ? to get a list of protocols",
		cmd_set_l2protocol, 0, NULL},

	{ "initmode", "initmode [modename]", "Shows/Sets the initialisation mode to use. Use set initmode ? to get a list of protocols",
		cmd_set_initmode, 0, NULL},

	{ "show", "show", "Shows all set'able values",
		cmd_set_show, 0, NULL},

	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Return to previous menu level",
		cmd_up, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

const char * const l1_names[] =
{
	"ISO9141", "ISO14230",
	"J1850-VPW", "J1850-PWM", "CAN", "", "", "RAW", NULL
};

const char * const l2_names[] =
{
	"RAW", "ISO9141", PROTO_NONE, "ISO14230",
	"J1850", "CAN", "VAG", "MB1", NULL
};

const char * const l2_initmodes[] =
{
	"5BAUD", "FAST", "CARB", NULL
};

#ifdef WIN32
static int
cmd_set_show(int argc,
char **argv)
#else
static int
cmd_set_show(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	/* Show stuff */
	int offset;
	for (offset=0; offset < 8; offset++)
	{
		if (set_L1protocol == (1 << offset))
			break;
	}

	printf("interface: %s on %s\n", l0_names[set_interface_idx].longname, set_subinterface);
	if (set_interface==CARSIM)
		printf("simfile: %s\n", set_simfile);
	printf("speed:    Connect speed: %d\n", set_speed);
	printf("display:  %s units\n", set_display?"english":"metric");
	printf("testerid: Source ID to use: 0x%x\n", set_testerid);
	printf("addrtype: %s addressing\n",
		set_addrtype ? "functional" : "physical");
	printf("destaddr: Destination address to connect to: 0x%x\n",
		set_destaddr);
	printf("l1protocol: Layer 1 (H/W) protocol to use %s\n",
		l1_names[offset]);
	printf("l2protocol: Layer 2 (S/W) protocol to use %s\n",
		l2_names[set_L2protocol]);
	printf("initmode: Initmode to use with above L2 protocol is %s\n",
		l2_initmodes[set_initmode]);

	return (CMD_OK);
}


static int cmd_set_interface(int argc, char **argv)
{
	if (argc > 1) {
		int i, helping = 0, found = 0;
		if (strcmp(argv[1], "?") == 0) {
			helping = 1;
			printf("hardware interface: use \"set interface NAME [id]\" .\n"
			"[id] is either an integer to be appended as /dev/obdII[id] or\n"
			"a complete device name such as \"/dev/ttyS0\".\n"
			"Valid interface names are: \n");
		}
		for (i=0; l0_names[i].longname != NULL; i++) {
			//loop through l0 interface names, either printing or comparing to argv[1]
			if (helping)
				printf("%s ", l0_names[i]);
			else
				if (strcasecmp(argv[1], l0_names[i].longname) == 0) {
					set_interface = l0_names[i].code;
					set_interface_idx=i;
					found = 1;
				}
		}
		if (helping) {
			//"?" was entered
			printf("\n");
		} else if (!found) {
			printf("interface: invalid interface %s\n", argv[1]);
			printf("interface: use \"set interface ?\" to see list of names\n");
		} else {
			if (argc > 2)
				strncpy(set_subinterface, argv[2], sizeof(set_subinterface));
			printf("interface is now %s on %s\n",
					l0_names[set_interface_idx].longname, set_subinterface);
			if (set_interface==VAGTOOL)
				diag_l0_dumb_setflags(1);
			else
				diag_l0_dumb_setflags(0);	//not strictly correct usage, but will do for hack.
		}
	} else {
		printf("interface: using %s on %s\n",
			l0_names[set_interface_idx].longname, set_subinterface);
	}
	return (CMD_OK);
}


//Update simfile name to be used.
//Current behaviour : updates the simfile even if the interface isn't set to CARSIM.
static int cmd_set_simfile(int argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "?") == 0) {
			printf("Simulation file: with CARSIM interface, this file contains\n"
			"message bytes to be transferred between host and ECU.\n"
			"Defaults to " DB_FILE "\n");
			return CMD_OK;
		}

		if (set_simfile)
			free(set_simfile);		//free old simfile
		if (diag_calloc(&set_simfile, strlen(argv[1])+1))
			return CMD_FAILED;
		strcpy(set_simfile, argv[1]);
		diag_l0_sim_setfile(set_simfile);
		printf("Simulation file: now using %s\n", set_simfile);

		if (set_interface!=CARSIM) {
			printf("Note: simfile only needed with CARSIM interface.\n");
		}
	} else {
		//no arguments
		printf("Simulation file: using %s\n", set_simfile);
	}
	return CMD_OK;
}

static int
cmd_set_display(int argc, char **argv)
{
	if (argc > 1)
	{
		if (strcasecmp(argv[1], "english") == 0)
			set_display = 1;
		else if (strcasecmp(argv[1], "metric") == 0)
			set_display = 0;
		else
			return (CMD_USAGE);
	}
	else
		printf("display: %s units\n", set_display?"english":"metric");

	return (CMD_OK);
}

static int
cmd_set_speed(int argc, char **argv)
{
	if (argc > 1)
	{
		set_speed = htoi(argv[1]);
	}
	else
		printf("speed: Connect speed: %d\n", set_speed);

	return (CMD_OK);
}

static int
cmd_set_testerid(int argc, char **argv)
{
	if (argc > 1)
	{
		int tmp;
		tmp = htoi(argv[1]);
		if ( (tmp < 0) || (tmp > 0xff))
			printf("testerid: must be between 0 and 0xff\n");
		else
			set_testerid = tmp;
	}
	else
		printf("testerid: Source ID to use: 0x%x\n", set_testerid);

	return (CMD_OK);
}
static int
cmd_set_destaddr(int argc, char **argv)
{
	if (argc > 1)
	{
		int tmp;
		tmp = htoi(argv[1]);
		if ( (tmp < 0) || (tmp > 0xff))
			printf("destaddr: must be between 0 and 0xff\n");
		else
			set_destaddr = tmp;
	}
	else
	{
		printf("destaddr: Destination address to connect to: 0x%x\n",
			set_destaddr);
	}

	return (CMD_OK);
}
static int
cmd_set_addrtype(int argc, char **argv)
{
	if (argc > 1)
	{
		if (strncmp(argv[1], "func", 4) == 0)
			set_addrtype = 1;
		else if (strncmp(argv[1], "phys", 4) == 0)
			set_addrtype = 0;
		else
			return(CMD_USAGE);
	}
	else
	{
		printf("addrtype: %s addressing\n",
			set_addrtype ? "functional" : "physical");
	}

	return (CMD_OK);
}

static int cmd_set_l2protocol(int argc, char **argv)
{
	if (argc > 1)
	{
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0)
		{
			prflag = 1;
			printf("L2 protocol: valid names are ");
		}
		for (i=0; l2_names[i] != NULL; i++)
		{
			if (prflag)
			{
				if (strcasecmp(l2_names[i], PROTO_NONE)) 
				{
					printf("%s ", l2_names[i]);
				}
			}
			else
				if (strcasecmp(argv[1], l2_names[i]) == 0)
				{
					found = 1;
					set_L2protocol = i;
				}
		}
		if (prflag)
			printf("\n");
		else if (! found)
		{
			printf("l2protocol: invalid protocol %s\n", argv[1]);
			printf("l2protocol: use \"set l2protocol ?\" to see list of protocols\n");
		}
	}
	else
	{
		printf("l2protocol: Layer 2 protocol to use %s\n",
			l2_names[set_L2protocol]);
	}
	return (CMD_OK);
}

static int cmd_set_l1protocol(int argc, char **argv)
{
	if (argc > 1)
	{
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0)
		{
			prflag = 1;
			printf("L1 protocol: valid names are ");
		}
		for (i=0; l1_names[i] != NULL; i++)
		{
			if (prflag && *l1_names[i])
				printf("%s ", l1_names[i]);
			else
				if (strcasecmp(argv[1], l1_names[i]) == 0)
				{
					set_L1protocol = 1 << i;
					found = 1;
				}
		}
		if (prflag)
			printf("\n");
		else if (! found)
		{
			printf("L1protocol: invalid protocol %s\n", argv[1]);
			printf("l1protocol: use \"set l1protocol ?\" to see list of protocols\n");
		}
	}
	else
	{
		int offset;

		for (offset=0; offset < 8; offset++)
		{
			if (set_L1protocol == (1 << offset))
				break;
		}
		printf("l1protocol: Layer 1 (H/W) protocol to use %s\n",
			l1_names[offset]);

	}
	return (CMD_OK);
}

static int cmd_set_initmode(int argc, char **argv)
{
	if (argc > 1)
	{
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0)
			prflag = 1;
		for (i=0; l2_initmodes[i] != NULL; i++)
		{
			if (prflag)
				printf("%s ", l2_initmodes[i]);
			else
			{
				if (strcasecmp(argv[1], l2_initmodes[i]) == 0)
				{
					found = 1;
					set_initmode = i;
				}
			}
		}
		if (prflag)
			printf("\n");
		else if (! found)
		{
			printf("initmode: invalid mode %s\n", argv[1]);
			printf("initmode: use \"set initmode ?\" to see list of initmodes\n");
		}
	}
	else
	{
		printf("initmode: Initmode to use with above protocol is %s\n",
			l2_initmodes[set_initmode]);
	}
	return(CMD_OK);
}

static int
cmd_set_help(int argc, char **argv)
{
	return help_common(argc, argv, set_cmd_table);
}
