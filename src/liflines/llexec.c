/* 
   Copyright (c) 2000-2002 Perry Rapp
   "The MIT license"
   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*=============================================================
 * llexec.c -- Frontend code for lifelines report generator
 * Copyright(c) 2002 by Perry Rapp; all rights reserved
 *===========================================================*/

#include "llstdlib.h"
/* llstdlib.h pulls in standard.h, config.h, sys_inc.h */
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#include "btree.h"
#include "table.h"
#include "translat.h"
#include "gedcom.h"
#include "liflines.h"
#include "arch.h"
#include "lloptions.h"
#include "interp.h"
#include "feedback.h"
#include "llexec.h"

#ifdef HAVE_GETOPT
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif /* HAVE_GETOPT_H */
#endif /* HAVE_GETOPT */

/*********************************************
 * external variables (no header)
 *********************************************/

extern STRING qSidldir,qSidldrp,qSnodbse,qScrdbse,qSiddbse;
extern STRING qSmtitle,qSnorwandro,qSnofandl,qSbdlkar;
extern STRING qSusgFinnOpt,qSusgFinnAlw,qSusgNorm;
extern STRING qSbaddb;

extern INT csz_indi, icsz_indi;
extern INT csz_fam, icsz_fam;
extern INT csz_sour, icsz_sour;
extern INT csz_even, icsz_even;
extern INT csz_othr, icsz_othr;

extern int opterr;

/*********************************************
 * required global variables
 *********************************************/


static STRING usage = "";      /* usage string */
int opt_finnish  = FALSE;      /* Finnish Language sorting order if TRUE */
BOOLEAN debugmode = FALSE;     /* no signal handling, so we can get coredump */
BOOLEAN opt_nocb  = FALSE;     /* no cb. data is displayed if TRUE */
BOOLEAN keyflag   = TRUE;      /* show key values */
BOOLEAN readonly  = FALSE;     /* database is read only */
BOOLEAN writeable = FALSE;     /* database must be writeable */
BOOLEAN immutable = FALSE;     /* make no changes at all to database, for access to truly read-only medium */
BOOLEAN cursesio  = TRUE;      /* use curses i/o */
INT alldone       = 0;         /* completion flag */
BOOLEAN progrunning = FALSE;   /* program is running */
BOOLEAN progparsing = FALSE;   /* program is being parsed */
INT     progerror = 0;         /* error count during report program */
BOOLEAN traditional = TRUE;    /* use traditional family rules */
BOOLEAN showusage = FALSE;     /* show usage */
STRING  btreepath = NULL;      /* database path given by user */
STRING  readpath = NULL;       /* database path used to open */
STRING  ext_codeset = 0;       /* default codeset from locale */

/*********************************************
 * local function prototypes
 *********************************************/

/* alphabetical */
static void init_browse_module(void);
static void init_show_module(void);
static BOOLEAN is_unadorned_directory(STRING path);
static void load_usage(void);
static void main_db_notify(STRING db, BOOLEAN opening);
static BOOLEAN open_or_create_database(INT alteration, STRING dbrequested
	, STRING *dbused);
static void parse_arg(const char * optarg, char ** optname, char **optval);
static void platform_init(void);
static void show_open_error(INT dberr);
static void term_browse_module(void);
static void term_show_module(void);

/*********************************************
 * local function definitions
 * body of module
 *********************************************/


/*==================================
 * main -- Main routine of LifeLines
 *================================*/
int
main (INT argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	char * msg;
	int c;
	BOOLEAN ok=FALSE;
	STRING dbrequested=NULL; /* database (path) requested */
	STRING dbused=NULL; /* database (path) found */
	BOOLEAN forceopen=FALSE, lockchange=FALSE;
	char lockarg = 0; /* option passed for database lock */
	INT alteration=0;
	STRING dbdir = 0;
	LIST exprogs=NULL;
	TABLE exargs=NULL;
	STRING progout=NULL;
	BOOLEAN graphical=TRUE;
	STRING configfile=0;

#if HAVE_SETLOCALE
	/* initialize locales */
	setlocale(LC_ALL, "");
#endif /* HAVE_SETLOCALE */
	
	/* capture user's default codeset */
	ext_codeset = strsave(ll_langinfo());
	/* TODO: We can use this info for default conversions */

#if ENABLE_NLS
	/* setup gettext translation */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif

	save_original_locales();
	load_usage();


	/* Parse Command-Line Arguments */
	opterr = 0;	/* turn off getopt's error message */
	while ((c = getopt(argc, argv, "adkrwil:fmntc:Fu:x:o:zC:I:")) != -1) {
		switch (c) {
		case 'c':	/* adjust cache sizes */
			while(optarg && *optarg) {
				if(isalpha((uchar)*optarg) && isupper((uchar)*optarg))
					*optarg = tolower((uchar)*optarg);
				if(*optarg == 'i') {
					sscanf(optarg+1, "%d,%d", &csz_indi, &icsz_indi);
				}
				else if(*optarg == 'f') {
					sscanf(optarg+1, "%d,%d", &csz_fam, &icsz_fam);
				}
				else if(*optarg == 's') {
					sscanf(optarg+1, "%d,%d", &csz_sour, &icsz_sour);
				}
				else if(*optarg == 'e') {
					sscanf(optarg+1, "%d,%d", &csz_even, &icsz_even);
				}
				else if((*optarg == 'o') || (*optarg == 'x')) {
					sscanf(optarg+1, "%d,%d", &csz_othr, &icsz_othr);
				}
				optarg++;
				while(*optarg && isdigit((uchar)*optarg)) optarg++;
				if(*optarg == ',') optarg++;
				while(*optarg && isdigit((uchar)*optarg)) optarg++;
			}
			break;
#ifdef FINNISH
# ifdef FINNISHOPTION
		case 'F':	/* Finnish sorting order [toggle] */
			opt_finnish = !opt_finnish;
			/*
			TO DO - need to mark Finnish databases, as 
			name records are not interoperable, because of
			different soundex encoding
			2001/02/17, Perry Rapp
			*/
			break;
# endif
#endif
		case 'a':	/* debug allocation */
			alloclog = TRUE;
			break;
		case 'd':	/* debug = no signal catchers */
			debugmode = TRUE;
			break;
		case 'k':	/* don't show key values */
			keyflag = FALSE;
			break;
		case 'r':	/* request for read only access */
			readonly = TRUE;
			break;
		case 'w':	/* request for write access */
			writeable = TRUE;
			break;
		case 'i': /* immutable access */
			immutable = TRUE;
			readonly = TRUE;
			break;
		case 'l': /* locking switch */
			lockchange = TRUE;
			lockarg = *optarg;
			break;
		case 'm':
			cursesio = FALSE;
			break;
		case 'f':	/* force database open in all cases */
			forceopen = TRUE;
			break;
		case 'n':	/* use non-traditional family rules */
			traditional = FALSE;
			break;
		case 't': /* show lots of trace statements for debugging */
			prog_trace = TRUE;
			break;
		case 'x': /* execute program */
			if (!exprogs) {
				exprogs = create_list();
				set_list_type(exprogs, LISTDOFREE);
			}
			push_list(exprogs, strdup(optarg ? optarg : ""));
			break;
		case 'I': /* program arguments */
			{
				STRING optname=0, optval=0;
				parse_arg(optarg, &optname, &optval);
				if (optname && optval) {
					if (!exargs) {
						exargs = create_table();
					}
					insert_table_str(exargs, strdup(optname), strdup(optval));
				}
				strfree(&optname);
				strfree(&optval);
			}
			break;
		case 'o': /* output directory */
			progout = optarg;
			break;
		case 'z': /* nongraphical box */
			graphical = FALSE;
			break;
		case 'C': /* specify config file */
			configfile = optarg;
			break;
		case '?':
			showusage = TRUE;
			goto usage;
			break;
		}
	}
prompt_for_db:

	/* catch any fault, so we can close database */
	if (debugmode)
		stdstring_hardfail();
	else
		set_signals();

	platform_init();
	set_displaykeys(keyflag);
	/* initialize options & misc. stuff */
	if (!init_lifelines_global(configfile, &msg, &main_db_notify)) {
		llwprintf("%s", msg);
		goto finish;
	}
	/* setup crashlog in case init_screen fails (eg, bad menu shortcuts) */
	crash_setcrashlog(getoptstr("CrashLog_llexec", NULL));
	init_interpreter(); /* give interpreter its turn at initialization */

	/* Validate Command-Line Arguments */
	if ((readonly || immutable) && writeable) {
		llwprintf(_(qSnorwandro));
		goto finish;
	}
	if (forceopen && lockchange) {
		llwprintf(_(qSnofandl));
		goto finish;
	}
	if (lockchange && lockarg != 'y' && lockarg != 'n') {
		llwprintf(_(qSbdlkar));
		goto finish;
	}
	if (forceopen)
		alteration = 3;
	else if (lockchange) {
		if (lockarg == 'y')
			alteration = 2;
		else
			alteration = 1;
	}
	c = argc - optind;
	if (c > 1) {
		showusage = TRUE;
		goto usage;
	}

	dbdir = getoptstr("LLDATABASES", ".");
	/* Get Database Name (Prompt or Command-Line) */
	if (alldone || c <= 0) {
		char dbname[MAXPATHLEN];
		/* ask_for_db_filename returns static buffer, we save it below */
		if (!ask_for_db_filename(_(qSidldir), _(qSidldrp), dbdir, dbname, sizeof(dbname))
			|| !dbname[0]) {
			dbrequested = NULL;
			llwprintf(_(qSiddbse));
			alldone = 0;
			goto finish;
		}
		dbrequested = strsave(dbname);
		if (eqstr(dbrequested, "?")) {
			INT n=0;
			LIST dblist=0, dbdesclist=0;
			strfree(&dbrequested);
			if ((n=get_dblist(dbdir, &dblist, &dbdesclist)) > 0) {
				INT i;
				i = choose_from_list(
					_("Choose database to open")
					, dbdesclist);
				if (i >= 0) {
					dbrequested = strsave(get_list_element(dblist, i+1));
				}
				release_dblist(dblist);
				release_dblist(dbdesclist);
			} else {
				llwprintf(_("No databases found in database path"));
				goto finish;
			}
			if (!dbrequested) {
				llwprintf(_(qSiddbse));
				goto finish;
			}
		}
	} else {
		dbrequested = strsave(argv[optind]);
	}

	/* search for database */
	/* search for file in lifelines path */
	dbused = filepath(dbrequested, "r", dbdir, NULL);
	if (!dbused) dbused = dbrequested;
	dbused = strsave(dbused);

	if (!open_or_create_database(alteration, dbrequested, &dbused))
		goto finish;

	/* Start Program */
	if (!init_lifelines_db()) {
		llwprintf(_(qSbaddb));
		goto finish;
	}
	init_show_module();
	init_browse_module();
	if (exargs) {
		set_cmd_options(exargs);
	}
	if (exprogs) {
		interp_program_list("main", 0, NULL, exprogs, progout, FALSE);
		make_list_empty(exprogs);
		remove_list(exprogs, 0);
	} else {
		/* TODO: prompt for report filename */
	}
	term_show_module();
	term_browse_module();
	ok=TRUE;

finish:
	/* we free this not because we care so much about these tiny amounts
	of memory, but to ensure we have the memory management right */
	/* strfree frees memory & nulls pointer */
	if (dbused) strfree(&dbused);
	if (dbrequested) strfree(&dbrequested);
	if (btreepath) strfree(&btreepath);
	shutdown_interpreter();
	close_lifelines();
	shutdown_ui(!ok);
	if (alldone == 2)
		goto prompt_for_db; /* changing databases */
	termlocale();

usage:
	/* Display Command-Line Usage Help */
	if (showusage) puts(usage);

	/* Exit */
	return !ok;
}
/*==================================
 * parse_arg -- Break argument into name & value
 *  eg, parse_arg("main_indi=I3", &a, &b)
 *   yields a="main_indi" and b="I3"
 *  (a & b are newly allocated from heap)
 *================================*/
static void
parse_arg (const char * optarg, char ** optname, char **optval)
{
	const char * ptr;
	*optname = *optval = 0;
	for (ptr = optarg; *ptr; ++ptr) {
		if (*ptr == '=') {
			char * namebuff = 0;
			char * valbuff = 0;
			INT namelen = ptr - optarg;
			if (!namelen)
				return;
			namebuff = (char *)malloc(namelen+1);
			llstrncpy(namebuff, optarg, namelen+1, 0);
			*optname = namebuff;
			valbuff = strdup(ptr+1);
			*optval = valbuff;
			return;

		}
	}
}
/*===================================================
 * shutdown_ui -- (Placeholder, we don't need it)
 *=================================================*/
void
shutdown_ui (BOOLEAN pause)
{
	pause=pause; /* unused */
}
/*===================================================
 * show_open_error -- Display database opening error
 *=================================================*/
static void
show_open_error (INT dberr)
{
	char buffer[256];
	describe_dberror(dberr, buffer, ARRSIZE(buffer));
	llwprintf(buffer);
	sleep(5);
}
/*==================================================
 * platform_init -- platform specific initialization
 *================================================*/
static void
platform_init (void)
{
	/* TODO: We could do wtitle just like llines, but its declaration needs
	to be moved somewhere more sensible for that (ie, not in curses.h!) */
}
/*==================================================
 * is_unadorned_directory -- is it a bare directory name,
 *  with no subdirectories ?
 * Created: 2001/01/24, Perry Rapp
 *================================================*/
static BOOLEAN
is_unadorned_directory (STRING path)
{
	for ( ; *path; path++) {
		if (is_dir_sep(*path))
			return FALSE;
	}
	return TRUE;
}
/*==================================================
 * open_or_create_database -- open database, prompt for
 *  creating new one if it doesn't exist
 * if fails, displays error (show_open_error) and returns 
 *  FALSE
 *  alteration:   [IN]  flags for locking, forcing open...
 *  dbrequested:  [IN]  database specified by user (usually relative)
 *  dbused:       [I/O] actual database path (may be relative)
 * If this routine creates new database, it will alter dbused
 * Created: 2001/04/29, Perry Rapp
 *================================================*/
static BOOLEAN
open_or_create_database (INT alteration, STRING dbrequested, STRING *dbused)
{
	/* Open Database */
	if (open_database(alteration, dbrequested, *dbused)) {
		return TRUE;
	}
	/* filter out real errors */
	if (bterrno != BTERR_NODB && bterrno != BTERR_NOKEY)
	{
		show_open_error(bterrno);
		return FALSE;
	}
	if (readonly || immutable || alteration)
	{
		llwprintf("Cannot create new database with -r, -i, -l, or -f flags.");
		return FALSE;
	}
	/*
	error was only that db doesn't exist, so lets try
	making a new one 
	If no database directory specified, add prefix llnewdbdir
	*/
	if (is_unadorned_directory(*dbused)) {
		STRING newdbdir = getoptstr("LLNEWDBDIR", 0);
		STRING temp = *dbused;
		if (newdbdir) {
			char tempth[MAXPATHLEN];
			concat_path(newdbdir, *dbused, tempth, sizeof(tempth));
			*dbused = strsave(tempth);
			stdfree(temp);
		}
	}

	/* Is user willing to make a new db ? */
	if (!ask_yes_or_no_msg(_(qSnodbse), _(qScrdbse))) 
		return FALSE;

	/* try to make a new db */
	if (create_database(dbrequested, *dbused))
		return TRUE;

	show_open_error(bterrno);
	return FALSE;
}

/* Finnish language support modifies the soundex codes for names, so
 * a database created with this support is not compatible with other
 * databases. 
 *
 * define FINNISH for Finnish Language support
 *
 * define FINNISHOPTION to have a runtime option -F which will enable
 * 	  	Finnish language support, but the name indices will all be
 *      wrong if you make modifications whilst in the wrong mode.
 */

static void
load_usage (void)
{
#ifdef FINNISH
# ifdef FINNISHOPTION
	opt_finnish  = FALSE;/* Finnish Language sorting order if TRUE */
	usage = _(qSusgFinnOpt);
# else
	opt_finnish  = TRUE;/* Finnish Language sorting order if TRUE */
	usage = _(qSusgFinnAlw);
# endif
#else
	opt_finnish  = FALSE;/* Finnish Language sorting order id disabled*/
	usage = _(qSusgNorm);
#endif
}
/*==================================================
 * main_db_notify -- callback called whenever a db is
 *  opened or closed
 * Created: 2002/06/16, Perry Rapp
 *================================================*/
static void
main_db_notify (STRING db, BOOLEAN opening)
{
	if (opening)
		crash_setdb(db);
	else
		crash_setdb("");
}
/*===============================================
 * init_show_module -- (Stripped down version)
 *=============================================*/
static void
init_show_module (void)
{
	init_disp_reformat();
}
/*===============================================
 * term_show_module -- (Placeholder, we don't need it)
 *=============================================*/
static void
term_show_module (void)
{
}
/*==================================================
 * init_browse_module -- (Placeholder, we don't need it)
 *================================================*/
static void
init_browse_module (void)
{
}
/*==================================================
 * term_browse_module -- (Placeholder, we don't need it)
 *================================================*/
static void
term_browse_module (void)
{
}
/*=============================================================
 * Implement all the required feedback functions as simple
 * prints to sdtout
 * Refer to feedback.h for information about these functions.
 *===========================================================*/
void
llwprintf (char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
void
llvwprintf (STRING fmt, va_list args)
{
	vprintf(fmt, args);
}
void
msg_error (char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
void
msg_info (char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
void
msg_status (char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
void
msg_output (MSG_LEVEL level, STRING fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
/*=====================================
 * msg_width -- get max width of msgs
 *===================================*/
INT
msg_width (void)
{
	return 999;
}
/* some strange report UI stuff */
void
refresh_stdout (void)
{
	/* We don't need to do anything as we're using stdout */
}
void
call_system_cmd (STRING cmd)
{
#ifndef WIN32
	system("clear");
#endif
	system(cmd);
}
extern STRING qSaskynq,qSaskynyn,qSaskyY,qSaskint;
FILE *
ask_for_program (STRING mode,
                 STRING ttl,
                 STRING *pfname,
                 STRING *pfullpath,
                 STRING path,
                 STRING ext,
                 BOOLEAN picklist)
{
	/* TODO: We probably want to use the real implementation in askprogram.c */
	return NULL;
}
FILE *
ask_for_output_file (STRING mode,
                     STRING ttl,
                     STRING *pfname,
                     STRING *pfullpath,
                     STRING path,
                     STRING ext)
{
	/* TODO: Do we want to steal the existing one, but reimplement underneath ? */
	return NULL;
}
BOOLEAN
ask_for_int (STRING ttl, INT * prtn)
{
	INT ival, c, neg;
	char buffer[MAXPATHLEN];
	while (TRUE) {
		STRING p = buffer;
		if (!ask_for_string(ttl, _(qSaskint), buffer, sizeof(buffer)))
			return FALSE;
		neg = 1;
		while (iswhite(*p++))
			;
		--p;
		if (*p == '-') {
			neg = -1;
			p++;
			while (iswhite(*p++))
				;
			--p;
		}
		if (chartype(*p) == DIGIT) {
			ival = *p++ - '0';
			while (chartype(c = *p++) == DIGIT)
				ival = ival*10 + c - '0';
			--p;
			while (iswhite(*p++))
				;
			--p;
			if (*p == 0) {
				*prtn = ival*neg;
				return TRUE;
			}
		}
	}
}
BOOLEAN
ask_for_string (STRING ttl, STRING prmpt, STRING buffer, INT buflen)
{
	printf(ttl); printf("\n");
	printf(prmpt);
	fgets(buffer, buflen, stdin);
	chomp(buffer);
	return strlen(buffer)>0;
}
STRING
ask_for_indi_key (STRING ttl,
                  CONFIRMQ confirmq,
                  ASK1Q ask1)
{
	/* TODO */
	return "";
}
RECORD
ask_for_fam (STRING pttl, STRING sttl)
{
	/* TODO */
	return NULL;
}
INDISEQ
ask_for_indi_list (STRING ttl,
                   BOOLEAN reask)
{
	/* TODO */
	return NULL;
}
RECORD
choose_from_indiseq (INDISEQ seq, ASK1Q ask1, STRING titl1, STRING titln)
{
	/* TODO */
	/* Ok, here is a problem, this implementation is not curses-specific,
	but calls thru to something curses-specific */
	return NULL;
}
/* send string to output, & terminate line */
static void
outputln (const char * txt)
{
	printf(txt);
	printf("\n");
}
/* send string to output */
static void
output (const char * txt)
{
	printf(txt);
}
static INT
interact (STRING ptrn)
{
	char buffer[2];
	STRING t;
	while (1) {
		fgets(buffer, sizeof(buffer), stdin);
		for (t=ptrn; *t; ++t) {
			if (buffer[0]==*t)
				return buffer[0];
		}
		printf("Invalid option: choose one of %s\n", ptrn);
	}
}
static INT
ask_for_char_msg (STRING msg, STRING ttl, STRING prmpt, STRING ptrn)
{
	INT rv;
	outputln(msg);
	outputln(ttl);
	output(prmpt);
	rv = interact(ptrn);
	return rv;
}
BOOLEAN
ask_yes_or_no_msg (STRING msg, STRING ttl)
{
	STRING ptr;
	INT c = ask_for_char_msg(msg, ttl, _(qSaskynq), _(qSaskynyn));
	for (ptr = _(qSaskyY); *ptr; ptr++) {
		if (c == *ptr) return TRUE;
	}
	return FALSE;
}
INT
choose_from_array (STRING ttl, INT no, STRING *pstrngs)
{
	/* TODO */
	/* Another one where we may want implementation, as it is above curses level */
	return -1;
}
void
view_array (STRING ttl, INT no, STRING *pstrngs)
{
	/* TODO */
	/* Another one where we may want implementation, as it is above curses level */
}
INT
choose_from_list (STRING ttl, LIST list)
{
	/* TODO */
	/* Another one where we may want implementation, as it is above curses level */
	return 0;
}
BOOLEAN
ask_for_db_filename (STRING ttl, STRING prmpt, STRING basedir, STRING buffer, INT buflen)
{
	return ask_for_string(ttl, prmpt, buffer, buflen);
}
INT
choose_list_from_indiseq (STRING ttl, INDISEQ seq)
{
	/* TODO */
	/* Another one where we may want implementation, as it is above curses level */
	return 0;
}