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
 * CLI routines
 *
 */

#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "config.h"
#include "scantool.h"
#include "scantool_cli.h"

CVSID("$Id: scantool_cli.c,v 1.11 2011/06/07 01:53:43 fenugrec Exp $");

char *progname;

FILE		*global_logfp;		/* Monitor log output file pointer */
#define LOG_FORMAT	"FREEDIAG log format 0.2"

FILE		*instream;

/* ROOT main menu */

/* Main menu commands */

static int cmd_help(int argc, char **argv);
//static int cmd_exit(int argc, char **argv);
static int cmd_monitor(int argc, char **argv);
static int cmd_watch(int argc, char **argv);
static int cmd_cleardtc(int argc, char **argv);
static int cmd_ecus(int argc, char **argv);

static int cmd_log(int argc, char **argv);
static int cmd_stoplog(int argc, char **argv);
#ifdef WIN32
static int cmd_play(int argc, char **argv);
#else
static int cmd_play(int argc, char **argv) __attribute__((unused));
#endif
static int cmd_scan(int argc, char **argv);

static int cmd_date(int argc, char **argv);
static int cmd_rem(int argc, char **argv);
static int cmd_source(int argc, char **argv);

static const struct cmd_tbl_entry root_cmd_table[]=
{
	{ "scan", "scan", "Start SCAN process", cmd_scan, 0, NULL},
	{ "monitor", "monitor [english/metric]", "Continuously monitor rpm etc",
		cmd_monitor, 0, NULL},

	{ "log", "log <filename>", "Log monitor data to <filename>",
		cmd_log, 0, NULL},
	{ "stoplog", "stoplog", "Stop logging", cmd_stoplog, 0, NULL},
#if notdef
	{ "play", "play filename", "Play back data from <filename>",
		cmd_play, FLAG_HIDDEN, NULL},
#endif
	{ "cleardtc", "cleardtc", "Clear DTCs from ECU", cmd_cleardtc, 0, NULL},
	{ "ecus", "ecus", "Show ECU information", cmd_ecus, 0, NULL},

	{ "watch", "watch [raw/nodecode/nol3]",
		"Watch the diagnostic bus and, if not in raw/nol3 mode, decode data",
		cmd_watch, 0, NULL},

	{ "set", "set <parameter value>",
		"Sets/displays parameters, \"set help\" for more info", NULL,
		0, set_cmd_table},

	{ "test", "test <command [params]>",
		"Perform various tests, \"test help\" for more info", NULL,
		0, test_cmd_table},

	{ "diag", "diag <command [params]>",
		"Extended diagnostic functions, \"diag help\" for more info", NULL,
		0, diag_cmd_table},

	{ "vw", "vw <command [params]",
		"VW diagnostic protocol functions, \"vw help\" for more info", NULL,
		0, vag_cmd_table},

	{ "dyno", "dyno <command [params]",
		"Dyno functions, \"dyno help\" for more info", NULL,
		0, dyno_cmd_table},

	{ "debug", "debug [parameter = debug]",
		"Sets/displays debug data and flags, \"debug help\" for available commands", NULL,
		0, debug_cmd_table},

	{ "date", "date", "Prints date & time", cmd_date, FLAG_HIDDEN, NULL},
	{ "#", "#", "Does nothing", cmd_rem, FLAG_HIDDEN, NULL},
	{ "source", "source <file>", "Read commands from a file", cmd_source, 0, NULL},

	{ "help", "help [command]", "Gives help for a command",
		cmd_help, 0, NULL },
	{ "exit", "exit", "Exits program", cmd_exit, 0, NULL},
	{ "quit", "quit", "Exits program", cmd_exit, FLAG_HIDDEN, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};

#define INPUT_MAX 1024

/*
 * Caller must free returned buffer. Used if we don't
 * have readline, and when reading init or command files.
 * No line editing or history.
 */
static char *
basic_get_input(const char *prompt)
{
	char *input;
	int do_prompt;

	if (diag_malloc(&input, INPUT_MAX))
			return NULL;

	do_prompt = 1;
	while (1) {
		if (do_prompt && prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		do_prompt = 1;
		if (fgets(input, INPUT_MAX, instream)) {
			break;
		} else {
			if (feof(instream)) {
				free(input);
				return NULL;
			} else {
				/* Ignore error and try again, but don't prompt. */
				clearerr(instream);
				do_prompt = 0;
			}
		}
	}
	input[strlen(input)-1] = '\0';	/* Remove trailing CR */
	return input;
}

#if HAVE_LIBREADLINE

#include <readline/readline.h>
#include <readline/history.h>

/* Caller must free returned buffer */
static char *
get_input(const char *prompt)
{
	char *input;
	/* XXX Does readline change the prompt? */
	char localprompt[128];
	strncpy(localprompt, prompt, sizeof(localprompt));

	input = readline(localprompt);
	if (input && *input)
		add_history(input);
	return input;
}

static void
readline_init(void)
{
	/*
	 * Could do fancy command completion someday, but for
	 * now, at least disable the default filename completion.
	 */
	rl_bind_key('\t', rl_insert);
}

#else

static char *
get_input(const char *prompt)
{
	return basic_get_input(prompt);
}

static void readline_init(void) {}

#endif

static char *
command_line_input(const char *prompt)
{
	if (instream == stdin)
		return get_input(prompt);

	/* Reading from init or command file; no prompting or history */
	return basic_get_input(NULL);
}

int 
help_common(int argc, char **argv, const struct cmd_tbl_entry *cmd_table)
{
/*	int i;*/
	const struct cmd_tbl_entry *ctp;

	if (argc > 1) {
		/* Single command help */
		int found = 0;
		ctp = cmd_table;
		while (ctp->command) {
			if (strcasecmp(ctp->command, argv[1]) == 0) {
				printf("%s: %s\n", ctp->command, ctp->help);
				printf("Usage: %s\n", ctp->usage);
				found++;
				break;
			}
			ctp++;
		}
		if (!found)
			printf("help: %s: no such command\n", argv[1]);
		
	} else {	
		/* Print help */
		printf("Available commands are :-\n");
		ctp = cmd_table;
		while (ctp->command) {
			if ((ctp->flags & FLAG_HIDDEN) == 0)
				printf("	%s\n", ctp->usage);
			ctp++;
		}
		printf("\nTry \"help <command>\" for further help\n");

	}

	return CMD_OK;
}

static int
cmd_help(int argc, char **argv)
{
	return help_common(argc, argv, root_cmd_table);
}

#ifdef WIN32
static int
cmd_date(int argc, char **argv)
#else
static int
cmd_date(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	struct tm *tm;
	time_t now;

	now = time(NULL);
	tm = localtime(&now);
	printf("%s", asctime(tm));

	return CMD_OK;
}

#ifdef WIN32
static int
cmd_rem(int argc, char **argv)
#else
static int
cmd_rem(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	return CMD_OK;
}

struct timeval log_start;

static void
log_timestamp(const char *prefix)
{
	struct timeval tv;
	long sec, usec;

	gettimeofday(&tv, NULL);
	if (tv.tv_usec < log_start.tv_usec) {
			tv.tv_usec += 1000*1000;
		tv.tv_sec--;
	}
	sec = tv.tv_sec - log_start.tv_sec;
	usec = tv.tv_usec - log_start.tv_usec;
	fprintf(global_logfp, "%s %04ld.%03ld ", prefix, sec, usec / 1000);
}

static void
log_command(int argc, char **argv)
{
	int i;

	if (!global_logfp)
		return;

	log_timestamp(">");
	for (i = 0; i < argc; i++)
			fprintf(global_logfp, " %s", argv[i]);
	fprintf(global_logfp, "\n");
}

static int
cmd_log(int argc, char **argv)
{
	char autofilename[20]="";
	char *file;
	file=autofilename;
	struct stat buf;
	time_t now;
	int i;

	if (global_logfp != NULL) {
		printf("Already logging\n");
		return CMD_FAILED;
	}

	/* Turn on logging */
	if (argc > 1) {
			file = argv[1];	//if a file name was specified, use that
	} else {
		//else, generate an auto log file
		for (i = 0; i < 99; i++) {
			sprintf(autofilename,"log.%02d",i);
			if (stat(file, &buf) == -1 && errno == ENOENT)
				break;
		}
		if (i > 99) {
			printf("Can't create log.%02d; remember to clean old auto log files\n",i);
			return CMD_FAILED;
		}
	}

	global_logfp = fopen(file, "a");	//add to end of log or create file

	if (global_logfp == NULL) {
		printf("Failed to create log file %s\n", file);
		return CMD_FAILED;
	}

	now = time(NULL);
	gettimeofday(&log_start, NULL);
	fprintf(global_logfp, "%s\n", LOG_FORMAT);
	log_timestamp("#");
	fprintf(global_logfp, "logging started at %s",
		asctime(localtime(&now)));

	printf("Logging to file %s\n", file);
	return CMD_OK;
}

#ifdef WIN32
static int
cmd_stoplog(int argc, char **argv)
#else
static int
cmd_stoplog(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	/* Turn off logging */
	if (global_logfp == NULL) {
		printf("Logging was not on\n");
		return CMD_FAILED;
	}

	fclose(global_logfp);
	global_logfp = NULL;

	return CMD_OK;
}

static int
cmd_play(int argc, char **argv)
{
	FILE *fp;
	int linenr;

	/* Turn on logging for monitor mode */
	if (argc < 2)
		return CMD_USAGE;

	fp = fopen(argv[1], "r");

	if (fp == NULL) {
		printf("Failed to open log file %s\n", argv[1]);
		return CMD_FAILED;
	}

	linenr = 0;

	/* Read data file in */
	/* XXX logging */

	/* Loop and call display routines */
	while (1) {	
		int ch;
		printf("Warning : incomplete code");
		printf("DATE:	+/- to step, S/E to goto start or end, Q to quit\n");
		ch = getc(stdin);
		switch (ch) {
			case '-':
			case '+':
			case 'E':
			case 'e':
			case 'S':
			case 's':
			case 'Q':
			case 'q':
				break;
		}
		
	}
	fclose(fp);

	return CMD_OK;
}

static int
cmd_watch(int argc, char **argv)
{
	int rv;
	struct diag_l2_conn *d_l2_conn;
	struct diag_l3_conn *d_l3_conn;
	struct diag_l0_device *dl0d;
	int rawmode = 0;
	int nodecode = 0;
	int nol3 = 0;

	if (argc > 1) {
		if (strcasecmp(argv[1], "raw") == 0)
			rawmode = 1;
		else if (strcasecmp(argv[1], "nodecode") == 0)
			nodecode = 1;
		else if (strcasecmp(argv[1], "nol3") == 0)
			nol3 = 1;
		else {
			printf("Don't understand \"%s\"\n", argv[1]);
			return CMD_USAGE;
		}
	}

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		return -1;
	}
	dl0d = diag_l2_open(l0_names[set_interface_idx].longname, set_subinterface, set_L1protocol);
	if (dl0d == 0) {
		rv = diag_geterr();
		printf("Failed to open hardware interface ");
		if (rv == DIAG_ERR_PROTO_NOTSUPP)
			printf(", does not support requested L1 protocol\n");
		else if (rv == DIAG_ERR_BADIFADAPTER)
			printf(", adapter probably not connected\n");
		else
			printf("\n");
		return CMD_FAILED;
	}
	if (rawmode)
		d_l2_conn = diag_l2_StartCommunications(dl0d, DIAG_L2_PROT_RAW,
			0, set_speed, 
			set_destaddr, 
			set_testerid);
	else
		d_l2_conn = diag_l2_StartCommunications(dl0d, set_L2protocol,
			DIAG_L2_TYPE_MONINIT, set_speed, set_destaddr, set_testerid);

	if (d_l2_conn == 0) {
		printf("Failed to connect to hardware in monitor mode\n");
		return CMD_FAILED;
	}

	if (rawmode == 0) {
		/* Put the SAE J1979 stack on top of the ISO device */

		if (nol3 == 0) {
			d_l3_conn = diag_l3_start("SAEJ1979", d_l2_conn);
			if (d_l3_conn == NULL) {
				printf("Failed to enable SAEJ1979 mode\n");
				return CMD_FAILED;
			}
		} else {
			d_l3_conn = NULL;
		}

		printf("Waiting for data to be received\n");
		while (1) {
			if (d_l3_conn)
				rv = diag_l3_recv(d_l3_conn, 10000,
					j1979_watch_rcv,
					(nodecode) ? NULL:(void *)d_l3_conn);
			else
				rv = diag_l2_recv(d_l2_conn, 10000,
					j1979_watch_rcv, NULL);
			if (rv == 0)
				continue;
			if (rv == DIAG_ERR_TIMEOUT)
				continue;
		}
	} else {
		/*
		 * And just read stuff, callback routine will print out the data
		 */
		printf("Waiting for data to be received\n");
		while (1) {
			rv = diag_l2_recv(d_l2_conn, 10000,
				j1979_data_rcv, (void *)RQST_HANDLE_WATCH);
			if (rv == 0)
				continue;
			if (rv == DIAG_ERR_TIMEOUT)
				continue;
			printf("recv returns %d\n", rv);
			break;
		}
	}

	return CMD_OK;
}


/*
 * Print the monitorable data out, use SI units by default, or "english"
 * units
 */
static void
print_current_data(int english)
{
	char buf[24];
	ecu_data_t *ep;
	unsigned int i;
	unsigned int j;

	printf("\n\nPress return to checkpoint then return to quit\n");
	printf("%-30.30s %-15.15s FreezeFrame\n",
		"Parameter", "Current");

	for (j = 0 ; get_pid ( j ) != NULL ; j++) {
		const struct pid *p = get_pid ( j ) ;

		for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
			if (DATA_VALID(p, ep->mode1_data) ||
				DATA_VALID(p, ep->mode2_data)) {
				printf("%-30.30s ", p->desc);

				if (DATA_VALID(p, ep->mode1_data))
					p->sprintf(buf, english, p,
						ep->mode1_data, 2);
				else
					sprintf(buf, "-----");
				
				printf("%-15.15s ", buf);

				if (DATA_VALID(p, ep->mode2_data))
					p->sprintf(buf, english, p,
						ep->mode2_data, 3);
				else
					sprintf(buf, "-----");
				
				printf("%-15.15s\n", buf);
			}
		}
	}
}

static void
log_response(int ecu, response_t *r)
{
	int i;

	/* Only print good records */
	if (r->type != TYPE_GOOD)
		return;

	printf("%d: ", ecu);
	for (i = 0; i < r->len; i++) {
		fprintf(global_logfp, "%02x ", r->data[i]);
	}
	fprintf(global_logfp, "\n");
}

static void
log_current_data(void)
{
	response_t *r;
	ecu_data_t *ep;
	unsigned int i;

	if (!global_logfp)
		return;

	log_timestamp("D");
	fprintf(global_logfp, "MODE 1 DATA\n");
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (r = ep->mode1_data;
			r < &ep->mode1_data[ARRAY_SIZE(ep->mode1_data)]; r++) {
				log_response((int)i, r);
		}
	}

	log_timestamp("D");
	fprintf(global_logfp, "MODE 2 DATA\n");
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (r = ep->mode2_data;
			r < &ep->mode2_data[ARRAY_SIZE(ep->mode2_data)]; r++) {
			log_response((int)i, r);
		}
	}
}

static int
cmd_monitor(int argc, char **argv)
{
	int rv;
	struct diag_l3_conn *d_conn;
	int english = 0;

	d_conn = global_l3_conn;

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_FAILED;
	}

	// If user states English or Metric, use that, else use config item
	if (argc > 1) {
		if (strcasecmp(argv[1], "english") == 0)
			english = 1;
		else if (strcasecmp(argv[1], "metric") == 0)
			english = 0;
		else
			return CMD_USAGE;
	} else {
		english = set_display;
	}

	printf("Please wait\n");

	/*
	 * Now just receive data and log it for ever
	 */

	while (1) {
		rv = do_j1979_getdata(1);
		/* Key pressed */
		if (rv == 1) {
			/*
			 * XX, if ' ' then this is a mark,
			 * if return, then quit
			 */
			break;
		}
		/* print the data */
		print_current_data(english);

		/* Save the data */
		log_current_data();

		/* Get/Print current DTCs */
		do_j1979_cms();
	}
	return CMD_OK;
}

#ifdef WIN32
static int
cmd_scan(int argc, char **argv)
#else
static int
cmd_scan(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	int rv;

	if (global_state >= STATE_CONNECTED) {
		printf("Already connected, please disconnect first\n");
		return CMD_FAILED;
	}

	rv = ecu_connect();

	if (rv == 0) {
		printf("Connection to ECU established\n");

		/* Now ask basic info from ECU */
		do_j1979_basics();
		/* Now get test results for continuously monitored systems */
		do_j1979_cms();
		/* And the non continuously monitored tests */
		printf("Non-continuously monitored system tests (failures only): -\n");
		do_j1979_ncms(0);
	} else {
		printf("Connection to ECU failed\n");
		printf("Please check :-\n");
		printf("	Adapter is connected to PC\n");
		printf("	Cable is connected to Vehicle\n");
		printf("	Vehicle is switched on\n");
		printf("	Vehicle is OBDII compliant\n");
		return CMD_FAILED;
	}
	return CMD_OK;
}


#ifdef WIN32
static int
cmd_cleardtc(int argc,
char **argv)
#else
static int
cmd_cleardtc(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	char *input;

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	input = basic_get_input("Are you sure you wish to clear the Diagnostic "
			"Trouble Codes (y/n) ? ");
	if (!input)
		return CMD_OK;

	if ((strcasecmp(input, "yes") == 0) || (strcasecmp(input, "y")==0)) {
		if (diag_cleardtc() == 0)
			printf("Done\n");
		else
			printf("Failed\n");
	} else {
		printf("Not done\n");
	}

	free(input);
	return CMD_OK;
}


#ifdef WIN32
static int
cmd_ecus(int argc, char **argv)
#else
static int
cmd_ecus(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	ecu_data_t *ep;
	unsigned int i;

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_OK;
	}

	printf("%d ECUs found\n", ecu_count);

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		printf("ECU %d: Address 0x%02x ", i, ep->ecu_addr & 0xff);
		if (ep->supress)
			printf("output supressed for monitor mode\n");
		else
			printf("\n");
	}
	return CMD_OK;
}


/*
 * CLI, returns results as CMD_xxx (such as CMD_EXIT)
 * If argc is supplied, then this is one shot cli, ie run the command
 */
static int
do_cli(const struct cmd_tbl_entry *cmd_tbl, const char *prompt, int argc, char **argv)
{
	/* Build up argc/argv */
	const struct cmd_tbl_entry *ctp;
	int cmd_argc;
	char *cmd_argv[20];
	char *input = NULL;
	int rv, done;
	int i;

	char promptbuf[80];	/* Was 1024, who needs that long a prompt? */
	static char nullstr[2];

	rv = 0, done = 0;
	snprintf(promptbuf, sizeof(promptbuf), "%s> ", prompt);
	while (!done) {
		char *inptr, *s;

		if (argc == 0) {
			/* Get Input */
			if (input)
				free(input);
			input = command_line_input(promptbuf);
			if (!input) {
					if (instream == stdin)
						printf("\n");
					break;
			}

			/* Parse it */
			inptr = input;
			if (*inptr == '@') 	//printing comment
			{
				printf("%s\n", inptr);
				continue;
			}
			if (*inptr == '#')		//non-printing comment
			{
				continue;
			}
			cmd_argc = 0;
			while ( (s = strtok(inptr, " 	")) != NULL ) {
				cmd_argv[cmd_argc] = s;
				cmd_argc++;
				inptr = NULL;
			}
			nullstr[0] = 0;
			nullstr[1] = 0;
			cmd_argv[cmd_argc] = nullstr;
		} else {
			/* Use supplied argc */
			cmd_argc = argc;
			for (i=0; i<=argc; i++)
				cmd_argv[i] = argv[i];
		}

		if (cmd_argc != 0) {
			ctp = cmd_tbl;
			while (ctp->command) {
				if (strcasecmp(ctp->command, cmd_argv[0]) == 0) {
					if (ctp->sub_cmd_tbl) {
						log_command(1, cmd_argv);
						snprintf(promptbuf, sizeof(promptbuf),"%s/%s",
							prompt, ctp->command);
						/* Sub menu */
						rv = do_cli(ctp->sub_cmd_tbl,
							promptbuf,
							cmd_argc-1,
							&cmd_argv[1]);
						if (rv==CMD_EXIT)	//allow exiting prog. from a submenu
							done=1;
						snprintf(promptbuf, sizeof(promptbuf), "%s> ", prompt);
					} else {
						/* Found command */
						log_command(cmd_argc, cmd_argv);
						rv = ctp->routine(cmd_argc, cmd_argv);
						switch (rv) {
							case CMD_USAGE:
								printf("Usage: %s\n", ctp->usage);
								break;
							case CMD_EXIT:
								rv = CMD_EXIT;
								done = 1;
								break;
							case CMD_UP:
								rv = CMD_UP;
								done = 1;
								break;
						}
					}
					break;
				}
				if (!done)
					ctp++;
			}
			if (ctp->command == NULL) {
				printf("Huh? Try \"help\"\n");
			}
			if (argc) {	
				/* Single command */
				done = 1;
				break;
			}
		}
		if (done)
			break;
	}
	if (input)
			free(input);
	if (rv == CMD_UP)
		return CMD_OK;
	return rv;
}

static int
command_file(char *filename)
{
		FILE *prev_instream = instream;	//assume it was already initted...

	if (instream=fopen(filename, "r")) {
		do_cli(root_cmd_table, progname, 0, NULL);
		fclose(instream);
		instream=prev_instream;
		return 0;
	}
	instream=prev_instream;
	return diag_iseterr(DIAG_ERR_CMDFILE);
}

static int
cmd_source(int argc, char **argv)
{
	char *file;

	if (argc < 2) {
			printf("No filename\n");
		return CMD_USAGE;
	}

	file = argv[1];
	if (command_file(file)) {
			printf("Couldn't read %s\n", file);
		return CMD_FAILED;
	}

		return CMD_OK;
}

static int
rc_file(void)
{
	char *homeinit;
	//this loads either a $home/.<progname>.rc or ./<progname>.ini (in order of preference)
	//to load general settings. For now, only set_interface and set_subinterface can be specified

	/*
	 * "." files don't play that well on some systems.
	 * You can turn it off and turn on a .ini file by setting both
	 *	"DONT_USE_RCFILE" and "USE_INIFILE",
	 * or support both by setting USE_INIFILE.
	* as set by ./configure
	 */

#ifndef DONT_USE_RCFILE
	char *homedir;
	homedir = getenv("HOME");

	if (homedir) {
		/* we add "/." and "rc" ... 4 characters */
		if (diag_malloc(&homeinit, strlen(homedir) + strlen(progname) + 5)) {
			return diag_iseterr(DIAG_ERR_RCFILE);
		}
		strcpy(homeinit, homedir);
		strcat(homeinit, "/.");
		strcat(homeinit, progname);
		strcat(homeinit, "rc");
		if (command_file(homeinit)) {
			//should return 0 with a success
/* 			if (newrcfile=fopen(homeinit,"a"))	//create the file if it didn't exist
 * 			{
 * 				fprintf(newrcfile, "\n#empty rcfile auto created by %s\n",progname);
 * 				fclose(newrcfile);
 * 				printf("%s not found, empty file created\n",homeinit);
 * 			}
 * 			else	//could not create empty rcfile
 * 			{
 * 				fprintf(stderr, FLFMT "%s not found, could not create empty file", FL, homeinit);
 * 				free(homeinit);
 * 				return diag_iseterr(DIAG_ERR_GENERAL);
 * 			}
 */
		} else {
			//command_file was at least partly successful (rc file exists)
			printf("%s: Settings loaded from %s\n",progname,homeinit);
			free(homeinit);
			return 0;
		}
		//fall here if command_file failed
		fprintf(stderr, FLFMT "Could not load rc file %s\n", FL, homeinit);
		free(homeinit);
	}
#endif


#ifdef USE_INIFILE
	if (diag_malloc(&homeinit, strlen(progname) + strlen(".ini") + 1)) {
		return diag_iseterr(DIAG_ERR_RCFILE);
	}

	strcpy(homeinit, progname);
	strcat(homeinit, ".ini");
	if (command_file(homeinit)) {
		fprintf(stderr, FLFMT "%s not found, no configuration loaded\n", FL, homeinit);
		free(homeinit);
		return diag_iseterr(DIAG_ERR_RCFILE);
	}
	printf("%s: Settings loaded from %s\n", progname, homeinit);
	free(homeinit);
	return 0;
#endif

}

void
enter_cli(char *name)
{
	global_logfp = NULL;
	progname = name;

	printf("%s: version %s\n", progname, PACKAGE_VERSION);
	printf("%s: Type HELP for a list of commands\n", progname);
	printf("%s: Type SCAN to start ODBII Scan\n", progname);
	printf("%s: Then use MONITOR to monitor real-time data\n", progname);

	readline_init();
	set_init();
	rc_file();
	printf("\n");
	/* And go start CLI */
	instream = stdin;
	(void)do_cli(root_cmd_table, progname, 0, NULL);
	set_close();

}

/*
 * ************
 * Useful, non specific routines
 * ************
 */

/*
 * Decimal/Octal/Hex to integer routine
 */
int htoi(char *buf)
{
	/* Hex text to int */
	int rv = 0;
	int base = 10;

	if (buf[0] == '$') {
		base = 16;
		buf++;
	} else if (buf[0] == '0') {
		base = 8;
		buf++;
		if (tolower(buf[0]) == 'x') {
			base = 16;
			buf++;
		}
	}

	while (*buf) {
		char upp = toupper(*buf);
		int val;

		if ((upp >= '0') && (upp <= '9')) {
			val = ((*buf) - '0');
		} else if ((upp >= 'A') && (upp <= 'F')) {
			val = (upp - 'A' + 10);
		} else {
			return -1;
		}
		if (val >= base)	/* Value too big for this base */
			return 0;
		rv *= base;
		rv += val;
		
		buf++;
	}
	return rv;
}

/*
 * Wait until ENTER is pressed
 */
void wait_enter(const char *message)
{
	printf(message);
	while (1) {
		int ch = getc(stdin);
		if (ch == '\n')
		break;
	}
}

/*
 * Determine whether ENTER has been pressed
 */
int pressed_enter()
{
	return diag_os_ipending(fileno(stdin));
}
