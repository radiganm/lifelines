/* 
   Copyright (c) 1991-1999 Thomas T. Wetmore IV

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/
/* modified 05 Jan 2000 by Paul B. McBride (pmcbride@tiac.net) */
/*=============================================================
 * interp.c -- Interpret program statements
 * Copyright(c) 1991-95 by T.T. Wetmore IV; all rights reserved
 *   2.3.4 - 24 Jun 93    2.3.5 - 16 Aug 93
 *   3.0.0 - 26 Jul 94    3.0.2 - 25 Mar 95
 *   3.0.3 - 22 Sep 95
 *===========================================================*/

#include <stdarg.h>
#include "sys_inc.h"
#include "llstdlib.h"
#include "table.h"
#include "translat.h"
#include "gedcom.h"
#include "cache.h"
#include "interpi.h"
#include "indiseq.h"
#include "liflines.h"
#include "feedback.h"
#include "arch.h"
#include "lloptions.h"
#include "parse.h"
#include "bfs.h"
#include "icvt.h"

/*********************************************
 * global/exported variables
 *********************************************/

/*
 TODO: Move most of these into parseinfo structure
 Perry 2002.07.14
 One problem -- finishrassa closes foutfp, so it may need longer lifetime
 that parseinfo currently has
*/
TABLE proctab=0, functab=0;
SYMTAB globtab; /* assume all zero is null SYMTAB */
STRING progname = NULL;       /* starting program name */
FILE *Poutfp = NULL;          /* file to write program output to */
STRING Poutstr = NULL;	      /* string to write program output to */
INT Perrors = 0;
LIST Plist = NULL;     /* list of program files still to read */
PNODE Pnode = NULL;           /* node being interpreted */
BOOLEAN explicitvars = FALSE; /* all vars must be declared */
BOOLEAN rpt_cancelled = FALSE;

/*********************************************
 * external/imported variables
 *********************************************/

extern BOOLEAN progrunning, progparsing;
extern INT progerror;
extern STRING qSwhatrpt,qSidrpt;
extern STRING nonint1, nonintx, nonstr1, nonstrx, nullarg1, nonfname1;
extern STRING nonnodstr1, nonind1, nonindx, nonfam1, nonfamx;
extern STRING nonrecx, nonnod1;
extern STRING nonnodx, nonvar1, nonvarx, nonboox, nonlst1, nonlstx;
extern STRING badargs, badargx, nonrecx;
extern STRING qSunsupuniv;

/*********************************************
 * local function prototypes
 *********************************************/

static STRING check_rpt_requires(PACTX pactx, STRING fname);
static void disp_symtab(STRING title, SYMTAB stab);
static BOOLEAN disp_symtab_cb(STRING key, PVALUE val, VPTR param);
static BOOLEAN find_program(STRING fname, STRING *pfull);
static void init_pactx(PACTX pactx);
static void parse_file(PACTX pactx, STRING fname, STRING fullpath);
static void progmessage(MSG_LEVEL level, STRING);
static void remove_tables(PACTX pactx);
static STRING vprog_error(PNODE node, STRING fmt, va_list args);
static void wipe_pactx(PACTX pactx);

/*********************************************
 * local variables
 *********************************************/

INT dbg_mode = 0;

/*********************************************
 * local function definitions
 * body of module
 *********************************************/


/*====================================+
 * initinterp -- Initialize interpreter
 *===================================*/
void
initinterp (void)
{
	initrassa();
	initset();
	Perrors = 0;
	rpt_cancelled = FALSE;
}
/*==================================+
 * finishinterp -- Finish interpreter
 *=================================*/
void
finishinterp (void)
{
	finishrassa();

	if (progerror) {
		refresh_stdout();
		/* we used to sleep for 5 seconds here */
	}

}
/*================================================================+
 * progmessage -- Display a status message about the report program
 *  level: [IN]  error, info, status (use enum MSG_LEVEL)
 *  msg:   [IN]  string to display (progname is added if there is room)
 *===============================================================*/
static void
progmessage (MSG_LEVEL level, STRING msg)
{
	char buf[120];
	char *ptr=buf;
	INT mylen=sizeof(buf);
	INT maxwidth = msg_width();
	INT msglen = strlen(msg);
	STRING program = _(qSidrpt);
	if (maxwidth >= 0 && maxwidth < mylen)
		mylen = maxwidth;
	if(progname && *progname && msglen+20 < maxwidth) {
		INT len = 99999;
		if (msg_width() >= 0) {
			len = sizeof(buf)-strlen(program)-1-1-msglen;
		}
		llstrcatn(&ptr, program, &mylen);
		llstrcatn(&ptr, " ", &mylen);
		llstrcatn(&ptr, compress_path(progname, len), &mylen);
		llstrcatn(&ptr, " ", &mylen);
		llstrcatn(&ptr, msg, &mylen);
	} else {
		llstrcatn(&ptr, program, &mylen);
		llstrcatn(&ptr, msg, &mylen);
	}
	msg_output(level, buf);
}
/*=============================================+
 * new_pathinfo -- Return new, filled-out pathinfo object
 *  all memory is newly heap-allocated
 *============================================*/
static struct pathinfo_s *
new_pathinfo (STRING fname, STRING fullpath)
{
	struct pathinfo_s * pathinfo = (struct pathinfo_s *)malloc(sizeof(*pathinfo));
	memset(pathinfo, 0, sizeof(*pathinfo));
	pathinfo->fname = strdup(fname);
	pathinfo->fullpath = strdup(fullpath);
	return pathinfo;
}
/*=============================================+
 * new_pathinfo -- Return new, filled-out pathinfo object
 *  all memory is newly heap-allocated
 *============================================*/
static void
delete_pathinfo (struct pathinfo_s ** pathinfo)
{
	if (pathinfo && *pathinfo) {
		strfree(&(*pathinfo)->fname);
		strfree(&(*pathinfo)->fullpath);
		stdfree(*pathinfo);
		*pathinfo = 0;
	}
}
/*=============================================+
 * interp_program_list -- Interpret LifeLines program
 *  proc:     [IN]  proc to call
 *  nargs:    [IN]  number of arguments
 *  args:     [IN]  arguments
 *  ifiles:   [IN]  program files
 *  ofile:    [IN]  output file - can be NULL
 *  picklist: [IN]  see list of existing reports
 *============================================*/
void
interp_program_list (STRING proc, INT nargs, VPTR *args, LIST lifiles
	, STRING ofile, BOOLEAN picklist)
{
	FILE *fp;
	LIST plist=0, donelist=0;
	SYMTAB stab = null_symtab();
	PVALUE dummy;
	INT i;
	INT nfiles = length_list(lifiles);
	PNODE first, parm;
	struct pactx_s pact;
	PACTX pactx = &pact;
	init_pactx(pactx);

	pvalues_begin();

   /* Get the initial list of program files */
	plist = create_list();
	/* list of pathinfos finished */
	donelist = create_list();

	if (nfiles > 0) {
		for (i = 1; i < nfiles+1; i++) {
			STRING fullpath = 0;
			STRING progfile = get_list_element(lifiles, i);
			if (find_program(progfile, &fullpath)) {
				struct pathinfo_s * pathinfo = new_pathinfo(progfile, fullpath);
				strfree(&fullpath);
				enqueue_list(plist, pathinfo);
			} else {
				/* what to do when file not found ? 2002-07-23, Perry */
			}
		}
	} else {
		struct pathinfo_s * pathinfo = 0;
		STRING fname=0, fullpath=0;
		STRING programsdir = getoptstr("LLPROGRAMS", ".");
		fp = ask_for_program(LLREADTEXT, _(qSwhatrpt), &fname, &fullpath
			, programsdir, ".ll", picklist);
		if (fp == NULL) {
			if (fname)  {
				/* tried & failed to open report program */
				llwprintf(_("Error: file <%s> not found"), fname);
			}
			strfree(&fname);
			strfree(&fullpath);
			goto interp_program_exit;
		}
		fclose(fp);
		progname = strsave(fullpath);
		pathinfo = new_pathinfo(fname, fullpath);
		strfree(&fname);
		strfree(&fullpath);

		enqueue_list(plist, pathinfo);
	}

	progparsing = TRUE;

   /* Parse each file in the list -- don't reparse any file */
	/* 
	NB: filename handling is a bit mixed. For the first file on the list,
	we already 	resolved its path (ask_for_programs did it). For later files
	added to the list (by handle_include), they are not resolved when they're put
	on the list. - Perry, 2002.06.18
	*/

	proctab = create_table();
	create_symtab(&globtab);
	functab = create_table();
	initinterp();
	while (!is_empty_list(plist)) {
		struct pathinfo_s * pathinfo = (struct pathinfo_s *)dequeue_list(plist);
		if (!in_table(pactx->filetab, pathinfo->fullpath)) {
			STRING str;
			insert_table_ptr(pactx->filetab, pathinfo->fullpath, 0);
#ifdef DEBUG
			llwprintf("About to parse file %s.\n", pathinfo->fname);
#endif
			Plist = plist;
			parse_file(pactx, pathinfo->fname, pathinfo->fullpath);
			if ((str = check_rpt_requires(pactx, pathinfo->fullpath)) != 0) {
				progmessage(MSG_ERROR, str);
				goto interp_program_exit;
			}
		} else {
			/* can't delete pathinfo, because pnodes use those strings */
			enqueue_list(donelist, pathinfo);
		}
	}
	remove_list(plist, NULL);
	plist=NULL;


	if (Perrors) {
		progmessage(MSG_ERROR, _("contains errors."));
		goto interp_program_exit;
	}

   /* Find top procedure */

	if (!(first = (PNODE) valueof_ptr(proctab, proc))) {
		progmessage(MSG_ERROR, _("needs a starting procedure."));
		goto interp_program_exit;
	}

   /* Open output file if name is provided */

	if (ofile && !(Poutfp = fopen(ofile, LLWRITETEXT))) {
		msg_error(_("Error: output file <%s> could not be created.\n")
			, ofile);
		goto interp_program_exit;
	}
	if (Poutfp) setbuf(Poutfp, NULL);

   /* Link arguments to parameters in symbol table */

	parm = (PNODE) iargs(first);
	if (nargs != num_params(parm)) {
		msg_error(_("Proc %s must be called with %d (not %d) parameters."),
			proc, num_params(parm), nargs);
		goto interp_program_exit;
	}
	create_symtab(&stab);
	for (i = 0; i < nargs; i++) {
		insert_symtab(stab, iident(parm), args[0]);
		parm = inext(parm);
	}

   /* Interpret top procedure */

	progparsing = FALSE;
	progrunning = TRUE;
	progerror = 0;
	progmessage(MSG_STATUS, _("is running..."));
	switch (interpret((PNODE) ibody(first), stab, &dummy)) {
	case INTOKAY:
	case INTRETURN:
		progmessage(MSG_INFO, _("was run successfully."));
		break;
	default:
		if (rpt_cancelled)
			progmessage(MSG_STATUS, _("was cancelled."));
		else
			progmessage(MSG_STATUS, _("was not run because of errors."));
		break;
	}

   /* Clean up and return */

	progrunning = FALSE;
	finishinterp(); /* includes 5 sec delay if errors on-screen */
	if (Poutfp) fclose(Poutfp);
	Poutfp = NULL;

interp_program_exit:

	
	remove_tables(pactx);
	remove_symtab(&stab);
	pvalues_end();
	wipe_pactx(pactx);

	/* kill any orphaned pathinfos */
	while (!is_empty_list(plist)) {
		struct pathinfo_s * pathinfo = (struct pathinfo_s *)dequeue_list(plist);
		delete_pathinfo(&pathinfo);
	}
	/* Assumption -- pactx->fullpath stays live longer than all pnodes */
	while (!is_empty_list(donelist)) {
		struct pathinfo_s * pathinfo = (struct pathinfo_s *)dequeue_list(donelist);
		delete_pathinfo(&pathinfo);
	}
	
	return;
}
/*===============================================
 * init_pactx -- initialize global parsing context
 *=============================================*/
static void
init_pactx (PACTX pactx)
{
	memset(pactx, 0, sizeof(*pactx));
	pactx->filetab = create_table();
}
/*===============================================
 * wipe_pactx -- destroy global parsing context
 *=============================================*/
static void
wipe_pactx (PACTX pactx)
{
/* we can't free the keys in filetab, because the
parser put those pointers into pvalues for files
named in include statements */
	remove_table(pactx->filetab, DONTFREE);
	pactx->filetab=NULL;
	memset(pactx, 0, sizeof(*pactx));
}
/*===========================================+
 * remove_tables - Remove interpreter's tables
 *==========================================*/
static void
remove_tables (PACTX pactx)
{
	/* proctab has PNODESs that yacc.y put in there */
	remove_table(proctab, DONTFREE);
	proctab=NULL;
	remove_symtab(&globtab);
	/* functab has PNODESs that yacc.y put in there */
	remove_table(functab, DONTFREE);
	functab=NULL;
}
/*======================================+
 * find_program - search for program file
 *  fname: [IN]  filename desired
 *  pfull: [OUT] full path found (stdalloc'd)
 * Returns TRUE if found
 *=====================================*/
static BOOLEAN
find_program (STRING fname, STRING *pfull)
{
	STRING programsdir = getoptstr("LLPROGRAMS", ".");
	FILE * fp = 0;
	if (!fname || *fname == 0) return FALSE;
	fp = fopenpath(fname, LLREADTEXT, programsdir, ".ll", pfull);
	if (fp) {
		fclose(fp);
		return TRUE;
	}
	return FALSE;
}
/*======================================+
 * parse_file - Parse single program file
 *  pactx: [I/O] pointer to global parsing context
 *  ifile: [IN]  file to parse
 * Parse file (yyparse may wind up adding entries to plist, via include statements)
 *=====================================*/
static void
parse_file (PACTX pactx, STRING fname, STRING fullpath)
{
	STRING unistr=0;

	ASSERT(!pactx->Pinfp);
	if (!fullpath || !fullpath[0]) return;
	pactx->Pinfp = fopen(fullpath, LLREADTEXT);
	if (!pactx->Pinfp) {
		llwprintf(_("Error: file <%s> not found: %s\n"), fname, fullpath);
		Perrors++;
		return;
	}

	if ((unistr=check_file_for_unicode(pactx->Pinfp)) && !eqstr(unistr, "UTF-8")) {
		msg_error(_(qSunsupuniv), unistr);
		Perrors++;
		return;
	}

	/* Assumption -- pactx->fullpath stays live longer than all pnodes */
	pactx->ifile = fname;
	pactx->fullpath = fullpath;
	pactx->lineno = 0;
	pactx->charpos = 0;

	yyparse(pactx);
	
	closefp(&pactx->Pinfp);
	pactx->ifile = 0;
	pactx->fullpath = 0;
	pactx->lineno = 0;
	pactx->charpos = 0;
}
/*====================================+
 * interp_main -- Interpreter main proc
 *===================================*/
void
interp_main (BOOLEAN picklist)
{
	/* whilst still in uilocale, check if we need to reload report strings
	(in case first time or uilocale changed) */
	interp_load_lang();

	rptlocale();
	interp_program_list("main", 0, NULL, NULL, NULL, picklist);
	uilocale();
	/*
	TO DO: unlock all cache elements (2001/03/17, Perry)
	in case any were left locked by report
	*/
}
/*======================================
 * interpret -- Interpret statement list
 * PNODE node:   first node to interpret
 * TABLE stab:   current symbol table
 * PVALUE *pval: possible return value
 *====================================*/
INTERPTYPE
interpret (PNODE node, SYMTAB stab, PVALUE *pval)
{
	STRING str;
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	PVALUE val;

	*pval = NULL;

	while (node) {
		Pnode = node;
		if (prog_trace) {
			trace_out("d%d: ", iline(node)+1);
			trace_pnode(node);
			trace_endl();
		}
		switch (itype(node)) {
		case ISCONS:
			poutput(pvalue(ivalue(node)), &eflg);
			if (eflg)
				goto interp_fail;
			break;
		case IIDENT:
			val = eval_and_coerce(PSTRING, node, stab, &eflg);
			if (eflg) {
				prog_error(node, _("identifier: %s should be a string\n"),
				    iident(node));
				goto interp_fail;
			}
			str = pvalue_to_string(val);
			if (str) {
				poutput(str, &eflg);
				if (eflg) {
					goto interp_fail;
				}
			}
			delete_pvalue(val);
			break;
		case IBCALL:
			val = evaluate_func(node, stab, &eflg);
			if (eflg) {
				goto interp_fail;
			}
			if (!val) break;
			if (ptype(val) == PSTRING && pvalue(val)) {
				poutput(pvalue(val), &eflg);
				if (eflg)
					goto interp_fail;
			}
			delete_pvalue(val);
			break;
		case IFCALL:
			val = evaluate_ufunc(node, stab, &eflg);
			if (eflg) {
				goto interp_fail;
			}
			if (!val) break;
			if (ptype(val) == PSTRING && pvalue(val)) {
				poutput(pvalue(val), &eflg);
				if (eflg)
					goto interp_fail;
			}
			delete_pvalue(val);
			break;
		case IPDEFN:
			FATAL();
		case ICHILDREN:
			switch (irc = interp_children(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case ISPOUSES:
			switch (irc = interp_spouses(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IFAMILIES:
			switch (irc = interp_families(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IFATHS:
			switch (irc = interp_fathers(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IMOTHS:
			switch (irc = interp_mothers(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IFAMCS:
			switch (irc = interp_parents(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case ISET:
			switch (irc = interp_indisetloop(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IINDI:
			switch (irc = interp_forindi(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IFAM:
			switch (irc = interp_forfam(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case ISOUR:
			switch (irc = interp_forsour(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IEVEN:
			switch (irc = interp_foreven(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IOTHR:
			switch (irc = interp_forothr(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case ILIST:
			switch (irc = interp_forlist(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case INOTES:
			switch (irc = interp_fornotes(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case INODES:
			switch (irc = interp_fornodes(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case ITRAV:
			switch (irc = interp_traverse(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IIF:
			switch (irc = interp_if(node, stab, pval)) {
			case INTOKAY:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IWHILE:
			switch (irc = interp_while(node, stab, pval)) {
			case INTOKAY:
			case INTBREAK:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IPCALL:
			switch (irc = interp_call(node, stab, pval)) {
			case INTOKAY:
				break;
			case INTERROR:
				goto interp_fail;
			default:
				return irc;
			}
			break;
		case IBREAK:
			return INTBREAK;
		case ICONTINUE:
			return INTCONTINUE;
		case IRETURN:
			if (iargs(node))
				*pval = evaluate(iargs(node), stab, &eflg);
			if (eflg && getoptint("FullReportCallStack", 0) > 0)
				prog_error(node, "in return statement");
			return INTRETURN;
		default:
			llwprintf("itype(node) is %d\n", itype(node));
			llwprintf("HUH, HUH, HUH, HUNH!\n");
			goto interp_fail;
		}
		node = inext(node);
	}
	return TRUE;

interp_fail:
	if (getoptint("FullReportCallStack", 0) > 0) {
		llwprintf("e%d: ", iline(node)+1);
		debug_show_one_pnode(node);
		llwprintf("\n");
	}
	return INTERROR;
}
/*========================================+
 * interp_children -- Interpret child loop
 *  usage: children(INDI,INDI_V,INT_V) {...}
 *=======================================*/
INTERPTYPE
interp_children (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nchil;
	CACHEEL fcel, cel;
	INTERPTYPE irc;
	PVALUE val;
	NODE fam = (NODE) eval_fam(iloopexp(node), stab, &eflg, &fcel);
	if (eflg) {
		prog_error(node, nonfamx, "children", "1");
		return INTERROR;
	}
	if (fam && nestr(ntag(fam), "FAM")) {
		prog_error(node, badargx, "children", "1");
		return INTERROR;
	}
	if (!fam) return INTOKAY;
	lock_cache(fcel);
	FORCHILDRENx(fam, chil, nchil)
		val = create_pvalue_from_indi(chil);
		insert_symtab(stab, ichild(node), val);
		insert_symtab(stab, inum(node), create_pvalue_from_int(nchil));
		/* val should be real person, because it came from FORCHILDREN */
		cel = get_cel_from_pvalue(val);
		lock_cache(cel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(cel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto aloop;
		default:
			goto aleave;
		}
aloop:	;
	ENDCHILDRENx
	irc = INTOKAY;
aleave:
	delete_symtab(stab, ichild(node));
	delete_symtab(stab, inum(node));
	unlock_cache(fcel);
	return irc;
}
/*==============================================+
 * interp_spouses -- Interpret spouse loop
 *  usage: spouses(INDI,INDI_V,FAM_V,INT_V) {...}
 *=============================================*/
INTERPTYPE
interp_spouses (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nspouses;
	CACHEEL icel, scel, fcel;
	INTERPTYPE irc;
	PVALUE sval, fval, nval;
	NODE indi = (NODE) eval_indi(iloopexp(node), stab, &eflg, &icel);
	if (eflg) {
		prog_error(node, nonindx, "spouses", "1");
		return INTERROR;
	}
	if (indi && nestr(ntag(indi), "INDI")) {
		prog_error(node, badargx, "spouses", "1");
		return INTERROR;
	}
	if (!indi) return TRUE;
	lock_cache(icel);
	FORSPOUSES(indi, spouse, fam, nspouses)
		sval = create_pvalue_from_indi(spouse);
		insert_symtab(stab, ispouse(node), sval);
		fval = create_pvalue_from_fam(fam);
		insert_symtab(stab, ifamily(node), fval);
		nval = create_pvalue_from_int(nspouses);
		insert_symtab(stab, inum(node), nval);
		/* sval should be real person, because it came from FORSPOUSES */
		scel = get_cel_from_pvalue(sval);
		/* fval should be real person, because it came from FORSPOUSES */
		fcel = get_cel_from_pvalue(fval);
		lock_cache(scel);
		lock_cache(fcel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(scel);
		unlock_cache(fcel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto bloop;
		default:
			goto bleave;
		}
bloop:	;
	ENDSPOUSES
	irc = INTOKAY;
bleave:
	delete_symtab(stab, ispouse(node));
	delete_symtab(stab, ifamily(node));
	delete_symtab(stab, inum(node));
	unlock_cache(icel);
	return irc;
}
/*===============================================+
 * interp_families -- Interpret family loop
 *  usage: families(INDI,FAM_V,INDI_V,INT_V) {...}
 * 2001/03/17 Revised by Perry Rapp
 *  to call insert_symtab_pvalue, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *==============================================*/
INTERPTYPE
interp_families (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nfams;
	CACHEEL icel, fcel, scel;
	INTERPTYPE irc;
	PVALUE fval, sval, nval;
	NODE indi = (NODE) eval_indi(iloopexp(node), stab, &eflg, &icel);
	if (eflg) {
		prog_error(node, nonindx, "families", "1");
		return INTERROR;
	}
	if (indi && nestr(ntag(indi), "INDI")) {
		prog_error(node, badargx, "families", "1");
		return INTERROR;
	}
	if (!indi) return INTOKAY;
	lock_cache(icel);
	FORFAMSS(indi, fam, spouse, nfams)
		fval = create_pvalue_from_fam(fam);
		insert_symtab(stab, ifamily(node), fval);
		sval = create_pvalue_from_indi(spouse);
		insert_symtab(stab, ispouse(node), sval);
		nval = create_pvalue_from_int(nfams);
		insert_symtab(stab, inum(node), nval);
		/* fval should be real person, because it came from FORFAMSS */
		fcel = get_cel_from_pvalue(fval);
		/* sval may not be a person -- so scel may be NULL */
		scel = get_cel_from_pvalue(sval);
		lock_cache(fcel);
		if (scel) lock_cache(scel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(fcel);
		if (scel) unlock_cache(scel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto cloop;
		default:
			goto cleave;
		}
cloop:	;
	ENDFAMSS
	irc = INTOKAY;
cleave:
	delete_symtab(stab, ifamily(node));
	delete_symtab(stab, ispouse(node));
	delete_symtab(stab, inum(node));
	unlock_cache(icel);
	return irc;
}
/*========================================+
 * interp_fathers -- Interpret fathers loop
 * 2001/03/17 Revised by Perry Rapp
 *  to call insert_symtab_pvalue, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *=======================================*/
INTERPTYPE
interp_fathers (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nfams;
	INT ncount = 1;
	CACHEEL icel, fcel, scel;
	INTERPTYPE irc;
	PVALUE sval, fval;
	NODE indi = (NODE) eval_indi(iloopexp(node), stab, &eflg, &icel);
	if (eflg) {
		prog_error(node, nonindx, "fathers", "1");
		return INTERROR;
	}
	if (indi && nestr(ntag(indi), "INDI")) {
		prog_error(node, badargx, "fathers", "1");
		return INTERROR;
	}
	if (!indi) return TRUE;
	lock_cache(icel);
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	FORFAMCS(indi, fam, husb, wife, nfams)
		sval = create_pvalue_from_indi(husb);
		scel = get_cel_from_pvalue(sval);
		if (!scel) goto dloop;
		fval = create_pvalue_from_fam(fam);
		fcel = get_cel_from_pvalue(fval);
		insert_symtab(stab, ifamily(node), fval);
		insert_symtab(stab, iiparent(node), create_pvalue_from_cel(scel));
		insert_symtab(stab, inum(node), create_pvalue_from_int(ncount++));
		lock_cache(fcel);
		lock_cache(scel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(fcel);
		unlock_cache(scel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			irc = INTOKAY;
			goto dloop;
		default:
			goto dleave;
		}
dloop:	;
	ENDFAMCS
	irc = INTOKAY;
dleave:
	delete_symtab(stab, ifamily(node));
	delete_symtab(stab, iiparent(node));
	delete_symtab(stab, inum(node));
	unlock_cache(icel);
	return irc;
}
/*========================================+
 * interp_mothers -- Interpret mothers loop
 * 2001/03/18 Revised by Perry Rapp
 *  to call insert_symtab_pvalue, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *=======================================*/
INTERPTYPE
interp_mothers (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nfams;
	INT ncount = 1;
	CACHEEL icel, fcel, scel;
	INTERPTYPE irc;
	PVALUE sval, fval;
	NODE indi = (NODE) eval_indi(iloopexp(node), stab, &eflg, &icel);
	if (eflg) {
		prog_error(node, nonindx, "mothers", "1");
		return INTERROR;
	}
	if (indi && nestr(ntag(indi), "INDI")) {
		prog_error(node, badargx, "mothers", "1");
		return INTERROR;
	}
	if (!indi) return TRUE;
	lock_cache(icel);
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	FORFAMCS(indi, fam, husb, wife, nfams)
		sval = create_pvalue_from_indi(wife);
		scel = get_cel_from_pvalue(sval);
		if (!scel) goto eloop;
		fval = create_pvalue_from_fam(fam);
		fcel = get_cel_from_pvalue(fval);
		insert_symtab(stab, ifamily(node), fval);
		insert_symtab(stab, iiparent(node), create_pvalue_from_cel(scel));
		insert_symtab(stab, inum(node), create_pvalue_from_int(ncount++));
		lock_cache(fcel);
		lock_cache(scel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(fcel);
		unlock_cache(scel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto eloop;
		default:
			goto eleave;
		}
eloop:	;
	ENDFAMCS
eleave:
	delete_symtab(stab, ifamily(node));
	delete_symtab(stab, iiparent(node));
	delete_symtab(stab, inum(node));
	unlock_cache(icel);
	return irc;
}
/*========================================+
 * interp_parents -- Interpret parents loop
 * 2001/03/18 Revised by Perry Rapp
 *  to call insert_symtab_pvalue, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *=======================================*/
INTERPTYPE
interp_parents (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INT nfams;
	CACHEEL icel, fcel;
	INTERPTYPE irc;
	PVALUE fval;
	NODE indi = (NODE) eval_indi(iloopexp(node), stab, &eflg, &icel);
	if (eflg) {
		prog_error(node, nonindx, "parents", "1");
		return INTERROR;
	}
	if (indi && nestr(ntag(indi), "INDI")) {
		prog_error(node, badargx, "parents", "1");
		return INTERROR;
	}
	if (!indi) return TRUE;
	lock_cache(icel);
	FORFAMCS(indi, fam, husb, wife, nfams)
		fval = create_pvalue_from_fam(fam);
		insert_symtab(stab, ifamily(node), fval);
		insert_symtab(stab, inum(node), create_pvalue_from_int(nfams));
		fcel = get_cel_from_pvalue(fval);
		lock_cache(fcel);
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(fcel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto floop;
		default:
			goto fleave;
		}
floop:	;
	ENDFAMCS
	irc = INTOKAY;
fleave:
	delete_symtab(stab, ifamily(node));
	delete_symtab(stab, inum(node));
	unlock_cache(icel);
	return INTOKAY;
}
/*=======================================
 * interp_fornotes -- Interpret NOTE loop
 *=====================================*/
INTERPTYPE
interp_fornotes (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	NODE root;
	PVALUE val = eval_and_coerce(PGNODE, iloopexp(node), stab, &eflg);
	if (eflg) {
		prog_error(node, nonrecx, "fornotes", "1");
		return INTERROR;
	}
	root = (NODE) pvalue(val);
	delete_pvalue(val);
	if (!root) return INTOKAY;
	FORTAGVALUES(root, "NOTE", sub, vstring)
		insert_symtab(stab, ielement(node), create_pvalue_from_string(vstring));
		irc = interpret((PNODE) ibody(node), stab, pval);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			goto gloop;
		default:
			goto gleave;
		}
gloop:      ;
	ENDTAGVALUES
	irc = INTOKAY;
gleave:
	delete_symtab(stab, ielement(node));
	return irc;
}
/*==========================================+
 * interp_fornodes -- Interpret fornodes loop
 *  usage: fornodes(NODE,NODE_V) {...}
 * 2001/03/19 Revised by Perry Rapp
 *  to delete its loop pvalue when finished
 *=========================================*/
INTERPTYPE
interp_fornodes (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	NODE sub, root=NULL;
	PVALUE val = eval_and_coerce(PGNODE, iloopexp(node), stab, &eflg);
	if (eflg) {
		prog_error(node, nonrecx, "fornodes", "1");
		return INTERROR;
	}
	root = (NODE) pvalue(val);
	delete_pvalue(val);
	if (!root) return INTOKAY;
	sub = nchild(root);
	while (sub) {
		insert_symtab(stab, ielement(node), create_pvalue_from_node(sub));
		irc = interpret((PNODE) ibody(node), stab, pval);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			sub = nsibling(sub);
			goto hloop;
		default:
			goto hleave;
		}
hloop: ;
	}
	irc = INTOKAY;
hleave:
	delete_symtab(stab, ielement(node));
	return irc;
}
/*========================================+
 * printkey -- Make key from keynum
 *=======================================*/
#ifdef UNUSED_CODE
static void
printkey (STRING key, char type, INT keynum)
{
	if (keynum>9999999 || keynum<0)
		keynum=0;
	sprintf(key, "%c%d", type, keynum);
}
#endif
/*========================================+
 * interp_forindi -- Interpret forindi loop
 *  usage: forindi(INDI_V,INT_V) {...}
 * 2001/03/18 Revised by Perry Rapp
 *  to call create_pvalue_from_indi_keynum, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *=======================================*/
INTERPTYPE
interp_forindi (PNODE node, SYMTAB stab, PVALUE *pval)
{
	CACHEEL icel=NULL;
	INTERPTYPE irc;
	PVALUE ival=NULL;
	INT count = 0;
	INT icount = 0;
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	while (TRUE) {
		count = xref_nexti(count);
		if (!count) {
			irc = INTOKAY;
			goto ileave;
		}
		ival = create_pvalue_from_indi_keynum(count);
		icel = get_cel_from_pvalue(ival);
		icount++;
		lock_cache(icel); /* keep current indi in cache during loop body */
		/* set loop variables */
		insert_symtab(stab, ielement(node), ival);
		insert_symtab(stab, inum(node), create_pvalue_from_int(count));
		/* execute loop body */
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(icel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			goto ileave;
		}
	}
ileave:
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*========================================+
 * interp_forsour -- Interpret forsour loop
 *  usage: forsour(SOUR_V,INT_V) {...}
 * 2001/03/20 Revised by Perry Rapp
 *  to call create_pvalue_from_indi_keynum, get_cel_from_pvalue
 *  to delete its loop pvalues when finished
 *=======================================*/
INTERPTYPE
interp_forsour (PNODE node, SYMTAB stab, PVALUE *pval)
{
	CACHEEL scel=NULL;
	INTERPTYPE irc;
	PVALUE sval=NULL;
	INT count = 0;
	INT scount = 0;
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	while (TRUE) {
		count = xref_nexts(count);
		if (!count) {
			irc = INTOKAY;
			goto sourleave;
		}
		sval = create_pvalue_from_sour_keynum(count);
		scel = get_cel_from_pvalue(sval);
		scount++;
		lock_cache(scel); /* keep current source in cache during loop body */
		/* set loop variables */
		insert_symtab(stab, ielement(node), sval);
		insert_symtab(stab, inum(node), create_pvalue_from_int(count));
		/* execute loop body */
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(scel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			goto sourleave;
		}
	}
sourleave:
	/* remove loop variables from symbol table */
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*========================================+
 * interp_foreven -- Interpret foreven loop
 *  usage: foreven(EVEN_V,INT_V) {...}
 *=======================================*/
INTERPTYPE
interp_foreven (PNODE node, SYMTAB stab, PVALUE *pval)
{
	CACHEEL ecel=NULL;
	INTERPTYPE irc;
	PVALUE eval=NULL;
	INT count = 0;
	INT ecount = 0;
	insert_symtab(stab, inum(node), create_pvalue_from_int(count));
	while (TRUE) {
		count = xref_nexte(count);
		if (!count) {
			irc = INTOKAY;
			goto evenleave;
		}
		eval = create_pvalue_from_even_keynum(count);
		ecel = get_cel_from_pvalue(eval);
		ecount++;
		lock_cache(ecel); /* keep current event in cache during loop body */
		/* set loop variables */
		insert_symtab(stab, ielement(node), eval);
		insert_symtab(stab, inum(node), create_pvalue_from_int(count));
		/* execute loop body */
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(ecel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			goto evenleave;
		}
	}
evenleave:
	/* remove loop variables from symbol table */
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*========================================+
 * interp_forothr -- Interpret forothr loop
 *  usage: forothr(OTHR_V,INT_V) {...}
 *=======================================*/
INTERPTYPE
interp_forothr (PNODE node, SYMTAB stab, PVALUE *pval)
{
	CACHEEL xcel;
	INTERPTYPE irc;
	PVALUE xval;
	INT count = 0;
	INT xcount = 0;
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	while (TRUE) {
		count = xref_nextx(count);
		if (!count) {
			irc = INTOKAY;
			goto othrleave;
		}
		xval = create_pvalue_from_othr_keynum(count);
		xcel = get_cel_from_pvalue(xval);
		xcount++;
		lock_cache(xcel); /* keep current source in cache during loop body */
		/* set loop variables */
		insert_symtab(stab, ielement(node), xval);
		insert_symtab(stab, inum(node), create_pvalue_from_int(count));
		/* execute loop body */
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(xcel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			goto othrleave;
		}
	}
othrleave:
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*======================================+
 * interp_forfam -- Interpret forfam loop
 *  usage: forfam(FAM_V,INT_V) {...}
 *=====================================*/
INTERPTYPE
interp_forfam (PNODE node, SYMTAB stab, PVALUE *pval)
{
	CACHEEL fcel=NULL;
	INTERPTYPE irc;
	PVALUE fval=NULL;
	INT count = 0;
	INT fcount = 0;
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	while (TRUE) {
		count = xref_nextf(count);
		if (!count) {
			irc = INTOKAY;
			goto mleave;
		}
		fval = create_pvalue_from_fam_keynum(count);
		fcel = get_cel_from_pvalue(fval);
		fcount++;
		lock_cache(fcel);
		insert_symtab(stab, ielement(node), fval);
		insert_symtab(stab, inum(node), create_pvalue_from_int(count));
		irc = interpret((PNODE) ibody(node), stab, pval);
		unlock_cache(fcel);
		switch (irc) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			goto mleave;
		}
	}
mleave:
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*============================================+
 * interp_indisetloop -- Interpret indiset loop
 * 2001/03/21 Revised by Perry Rapp
 *  to delete its loop pvalues when finished
 *===========================================*/
INTERPTYPE
interp_indisetloop (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	PVALUE indival, loopval;
	INDISEQ seq = NULL;
	PVALUE val = evaluate(iloopexp(node), stab, &eflg);
	if (eflg || !val || ptype(val) != PSET) {
		prog_error(node, "1st arg to forindiset must be set expr");
		return INTERROR;
	}
	seq = (INDISEQ) pvalue(val);
	if (!seq) {
		delete_pvalue(val); /* delete temp evaluated val - may destruct seq */
		return INTOKAY;
	}
	/* can't delete val until we're done with seq */
	/* initialize counter */
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	FORINDISEQ(seq, el, ncount)
		/* put current indi in symbol table */
		indival = create_pvalue_from_indi_key(skey(el));
		insert_symtab(stab, ielement(node), indival);
		/* put current indi's value in symbol table */
		loopval = sval(el).w;
		if (loopval)
			loopval = copy_pvalue(loopval);
		else
			loopval = create_pvalue_any();
		insert_symtab(stab, ivalvar(node), loopval);
		/* put counter in symbol table */
		insert_symtab(stab, inum(node), create_pvalue_from_int(ncount + 1));
		switch (irc = interpret((PNODE) ibody(node), stab, pval)) {
		case INTCONTINUE:
		case INTOKAY:
			goto hloop;
		default:
			goto hleave;
		}
hloop:	;
	ENDINDISEQ
	irc = INTOKAY;
hleave:
	delete_pvalue(val); /* delete temp evaluated val - may destruct seq */
	delete_symtab(stab, ielement(node)); /* remove indi */
	delete_symtab(stab, ivalvar(node)); /* remove indi's value */
	delete_symtab(stab, inum(node)); /* remove counter */
	return irc;
}
/*=====================================+
 * interp_forlist -- Interpret list loop
 * 2001/03/21 Revised by Perry Rapp
 *  to delete its loop pvalues when finished
 *====================================*/
INTERPTYPE
interp_forlist (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	INT ncount = 1;
	LIST list;
	PVALUE val = eval_and_coerce(PLIST, iloopexp(node), stab, &eflg);
	if (eflg || !val || ptype(val) != PLIST) {
		prog_error(node, "1st arg to forlist is not a list");
		return INTERROR;
	}
	list = pvalue_to_list(val);
	/* can't delete val until we're done with list */
	if (!list) {
		delete_pvalue(val); /* delete temp evaluated val - may destruct list */
		prog_error(node, "1st arg to forlist is in error");
		return INTERROR;
	}
	insert_symtab(stab, inum(node), create_pvalue_from_int(0));
	FORLIST(list, el)
		/* insert/update current element in symbol table */
		insert_symtab(stab, ielement(node), copy_pvalue(el));
		/* insert/update counter in symbol table */
		insert_symtab(stab, inum(node), create_pvalue_from_int(ncount++));
		switch (irc = interpret((PNODE) ibody(node), stab, pval)) {
		case INTCONTINUE:
		case INTOKAY:
			goto iloop;
		default:
			goto ileave;
		}
iloop:	;
	ENDLIST
	irc = INTOKAY;
ileave:
	delete_pvalue(val); /* delete temp evaluated val - may destruct list */
	/* remove element & counter from symbol table */
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, inum(node));
	return irc;
}
/*===================================+
 * interp_if -- Interpret if structure
 *==================================*/
INTERPTYPE
interp_if (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE;
	BOOLEAN cond = evaluate_cond(icond(node), stab, &eflg);
	if (eflg) return INTERROR;
	if (cond) return interpret((PNODE) ithen(node), stab, pval);
	if (ielse(node)) return interpret((PNODE) ielse(node), stab, pval);
	return INTOKAY;
}
/*=========================================+
 * interp_while -- Interpret while structure
 *========================================*/
INTERPTYPE
interp_while (PNODE node, SYMTAB stab, PVALUE *pval)
{
	BOOLEAN eflg = FALSE, cond;
	INTERPTYPE irc;
	while (TRUE) {
		cond = evaluate_cond(icond(node), stab, &eflg);
		if (eflg) return INTERROR;
		if (!cond) return INTOKAY;
		switch (irc = interpret((PNODE) ibody(node), stab, pval)) {
		case INTCONTINUE:
		case INTOKAY:
			continue;
		default:
			return irc;
		}
	}
}
/*=======================================+
 * interp_call -- Interpret call structure
 *======================================*/
INTERPTYPE
interp_call (PNODE node, SYMTAB stab, PVALUE *pval)
{
	SYMTAB newstab=null_symtab();
	INTERPTYPE irc=INTERROR;
	PNODE arg=NULL, parm=NULL, proc = (PNODE) valueof_ptr(proctab, iname(node));
	if (!proc) {
		llwprintf("``%s'': undefined procedure\n", iname(node));
		irc = INTERROR;
		goto call_leave;
	}
	create_symtab(&newstab);
	arg = (PNODE) iargs(node);
	parm = (PNODE) iargs(proc);
	while (arg && parm) {
		BOOLEAN eflg = FALSE;
		PVALUE value = evaluate(arg, stab, &eflg);
		if (eflg) {
			irc = INTERROR;
			goto call_leave;
		}
		insert_symtab(newstab, iident(parm), value);
		arg = inext(arg);
		parm = inext(parm);
	}
	if (arg || parm) {
		prog_error(node, "``%s'': mismatched args and params\n", iname(node));
		irc = INTERROR;
		goto call_leave;
	}
	irc = interpret((PNODE) ibody(proc), newstab, pval);
	switch (irc) {
	case INTRETURN:
	case INTOKAY:
		irc = INTOKAY;
		break;
	case INTBREAK:
	case INTCONTINUE:
	case INTERROR:
	default:
		irc = INTERROR;
		break;
	}

call_leave:
	remove_symtab(&newstab);
	return irc;
}
/*==============================================+
 * interp_traverse -- Interpret traverse iterator
 *  usage: traverse(NODE,NODE_V,INT_V) {...}
 * TO DO - doesn't clean up its symtab entries (2001/03/24)
 *=============================================*/
INTERPTYPE
interp_traverse (PNODE node, SYMTAB stab, PVALUE *pval)
{
	NODE snode, stack[100];
	BOOLEAN eflg = FALSE;
	INTERPTYPE irc;
	INT lev = -1;
	NODE root;
	PVALUE val = eval_and_coerce(PGNODE, iloopexp(node), stab, &eflg);
	if (eflg) {
		prog_var_error(node, stab, iloopexp(node), val, nonrecx,  "traverse", "1");
		irc = INTERROR;
		goto traverse_leave;
	}
	root = (NODE) pvalue(val);
	if (!root) {
		irc = INTOKAY;
		goto traverse_leave;
	}
	stack[++lev] = snode = root;
	while (TRUE) {
		insert_symtab(stab, ielement(node), create_pvalue_from_node(snode));
		insert_symtab(stab, ilev(node), create_pvalue_from_int(lev));
		switch (irc = interpret((PNODE) ibody(node), stab, pval)) {
		case INTCONTINUE:
		case INTOKAY:
			break;
		case INTBREAK:
			irc = INTOKAY;
			goto traverse_leave;
		default:
			goto traverse_leave;
		}
		if (nchild(snode)) {
			snode = stack[++lev] = nchild(snode);
			continue;
		}
		if (lev>0 && nsibling(snode)) {
			snode = stack[lev] = nsibling(snode);
			continue;
		}
		while (--lev >= 1 && !nsibling(stack[lev]))
			;
		if (lev <= 0) break;
		snode = stack[lev] = nsibling(stack[lev]);
	}
	irc = INTOKAY;
traverse_leave:
	delete_symtab(stab, ielement(node));
	delete_symtab(stab, ilev(node));
	delete_pvalue(val);
	val=NULL;
	return irc;
}
/*=============================================+
 * prog_var_error -- Report a run time program error
 *  due to mistyping of a particular variable
 *  node:  [IN]  current parse node
 *  stab:  [IN]  current symbol table (lexical scope)
 *  arg:   [IN]  if non-null, parse node of troublesome argument
 *  val:   [IN]  if non-null, PVALUE of troublesome argument
 *  fmt... [IN]  message
 * See vprog_error
 * Created: 2002/02/17, Perry Rapp
 *============================================*/
struct dbgsymtab_s
{
	STRING * locals;
	INT count;
	INT current;
};
void
prog_var_error (PNODE node, SYMTAB stab, PNODE arg, PVALUE val, STRING fmt, ...)
{
	STRING choices[4];
	STRING titl;
	INT rtn;
	va_list args;

	arg = arg; /* unused */
	val = val; /* unused */

	va_start(args, fmt);
	titl = vprog_error(node, fmt, args);
	va_end(args);

	if (!getoptint("debugger", 0))
		return;

	if (dbg_mode != -99) {
		char buf[64];
		INT n=0,i=0;
		/* report debugger: Option to display list of local symbols */
		n = (stab.tab ? get_table_count(stab.tab) : 0);
		llstrncpyf(buf, sizeof(buf), uu8, _("Display locals (%d)"), n);
		choices[i++] = strsave(buf);
		n = (globtab.tab ? get_table_count(globtab.tab) : 0);
		llstrncpyf(buf, sizeof(buf), uu8, _("Display globals (%d)"), n);
		choices[i++] = strsave(buf);
		choices[i++] = strsave(_("Pop one level"));
		choices[i++] = strsave(_("Quit debugger"));
		ASSERT(i==ARRSIZE(choices));
dbgloop:
		rtn = choose_from_array(titl, ARRSIZE(choices), choices);
		if (rtn == 3 || rtn == -1)
			dbg_mode = -99;
		else if (rtn == 0) {
			disp_symtab(_("Local variables"), stab);
			goto dbgloop;
		}
		else if (rtn == 1) {
			disp_symtab(_("Global variables"), globtab);
			goto dbgloop;
		}
		free_array_strings(ARRSIZE(choices), choices);
	}
}
/*====================================================
 * disp_symtab -- Display contents of a symbol table
 *  This is part of the report language debugger
 *==================================================*/
static void
disp_symtab (STRING title, SYMTAB stab)
{
	struct symtab_iter_s symtabits;
	INT n = (stab.tab ? get_table_count(stab.tab) : 0);
	struct dbgsymtab_s sdata;
	INT bytes = n * sizeof(STRING);
	if (!n) return;
	memset(&sdata, 0, sizeof(sdata));
	sdata.count = n;
	sdata.locals = (STRING *)malloc(bytes);
	memset(sdata.locals, 0, bytes);
	/* Now traverse & print the actual entries via disp_symtab_cb() */
	if (begin_symtab(stab, &symtabits)) {
		STRING key=0;
		PVALUE pval=0;
		while (next_symtab_entry(&symtabits, &key, &pval)) {
			disp_symtab_cb(key, pval, &sdata.locals);
		}
	}
	/* Title of report debugger's list of local symbols */
	view_array(title, n, sdata.locals);
	free_array_strings(n, sdata.locals);
}
/*====================================================
 * disp_symtab_cb -- Display one entry in symbol table
 *  This is part of the report language debugger
 *  key:   [IN]  name of current symbol
 *  val:   [IN]  value of current symbol
 *  param: [I/O] points to dbgsymtab_s, where list is being printed
 *==================================================*/
static BOOLEAN
disp_symtab_cb (STRING key, PVALUE val, VPTR param)
{
	struct dbgsymtab_s * sdata = (struct dbgsymtab_s *)param;
	char line[64];
	ASSERT(sdata->current < sdata->count);
	llstrncpyf(line, sizeof(line), uu8, "%s: %s", key
		, debug_pvalue_as_string(val));
	line[sizeof(line)-1] = 0;
	sdata->locals[sdata->current++] = strsave(line);
	return TRUE; /* continue */
}
/*=============================================+
 * prog_error -- Report a run time program error
 *  node:   current parsed node
 *  fmt:    printf style format string
 *  ...:    printf style varargs
 *  See vprog_error
 *============================================*/
void
prog_error (PNODE node, STRING fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprog_error(node, fmt, args);
	va_end(args);
}
/*=============================================+
 * vprog_error -- Report a run time program error
 *  node:        current parsed node
 *  fmt, args:   printf style message
 *  ...:    printf style varargs
 * Prints error to the stdout-style curses window
 * and to the report log (if one was specified in config file)
 * Always includes line number of node, if available
 * Only includes file name if not same as previous error
 * Returns static buffer with one-line description
 *============================================*/
static STRING
vprog_error (PNODE node, STRING fmt, va_list args)
{
	INT num;
	STRING rptfile;
	char msgf[320]=""; /* file msg */
	char msglineno[60]; /* main msg */
	static char msgbuff[600];
	static char prevfile[MAXPATHLEN]="";
	STRING ptr = msgbuff;
	INT mylen = sizeof(msgbuff);
	if (rpt_cancelled)
		return _("Report cancelled");
	rptfile = getoptstr("ReportLog", NULL);
	if (node) {
		STRING fname = ifname(node);
		/* only display filename if different (or first error) */
		if (!prevfile[0] || !eqstr(prevfile, fname)) {
			if (progparsing)
				llstrncpyf(msgf, ARRSIZE(msgf), uu8
					, _("\nParsing Error in <%s>"), fname);
			else
				llstrncpyf(msgf, ARRSIZE(msgf), uu8
					, _("\nRuntime Error in: <%s>"), fname);
			llstrncpy(prevfile, ifname(node), ARRSIZE(prevfile), uu8);
		}
		/* But always display the line & error */
		if (progparsing)
			llstrncpyf(msglineno, sizeof(msglineno), uu8
				, _("Parsing Error at line %d: "), iline(node)+1);
		else
			llstrncpyf(msglineno, sizeof(msglineno), uu8
				, _("Runtime Error at line %d: "), iline(node)+1);
	} else {
		llstrncpyf(msglineno, sizeof(msglineno), uu8, _("Aborting: "));
	}
	appendstr(&ptr, &mylen, uu8, msglineno);
	appendstrvf(&ptr, &mylen, uu8, fmt, args);
	if (msgf[0])
		llwprintf(msgf);
	llwprintf("\n");
	llwprintf(msgbuff);
	llwprintf(".");
	++progerror;
	/* if user specified a report error log (in config file) */
	if (rptfile && rptfile[0]) {
		FILE * fp = fopen(rptfile, LLAPPENDTEXT);
		if (fp) {
			if (progerror == 1) {
				LLDATE creation;
				get_current_lldate(&creation);
				fprintf(fp, _("\nReport Errors: %s")
					, creation.datestr);
			}
			if (msgf[0])
				fprintf(fp, msgf);
			fprintf(fp, "\n");
			fprintf(fp, msgbuff);
			fclose(fp);
		}
	}
	if ((num = getoptint("PerErrorDelay", 0)))
		sleep(num);
	return msgbuff;
}
/*=============================================+
 * pa_handle_global -- declare global variable
 * Called directly from generated parser code (ie, from code in yacc.y)
 *=============================================*/
void
pa_handle_global (STRING iden)
{
	insert_symtab(globtab, iden, create_pvalue_any());
}
/*=============================================+
 * pa_handle_option -- process option specified in report
 * Called directly from generated parser code (ie, from code in yacc.y)
 *=============================================*/
void
pa_handle_option (PVALUE optval)
{
	STRING optstr;
	ASSERT(ptype(optval)==PSTRING); /* grammar only allows strings */
	optstr = pvalue(optval);
	if (eqstr(optstr,"explicitvars")) {
		explicitvars = 1;
	} else {
		/* TO DO - figure out how to set the error flag & report error */
	}
}
/*=============================================+
 * set_rptfile_prop -- set a property for a report file
 * These are stored in a table, hanging off the entry in the filetab
 *=============================================*/
void
set_rptfile_prop (PACTX pactx, STRING fname, STRING key, STRING value)
{
	VPTR * ptr = access_value_ptr(pactx->filetab, fname);
	TABLE tabprop;
	if (!*ptr) {
		tabprop = create_table();
		*ptr = tabprop;
	} else {
		tabprop = (TABLE)(*ptr);
	}
	replace_table_str(tabprop, key, value, FREEBOTH);
	
}
/*=============================================+
 * get_rptfile_prop -- get a property for a report file
 * These are stored in a table, hanging off the entry in the filetab
 *=============================================*/
STRING
get_rptfile_prop (PACTX pactx, STRING fname, STRING key)
{
	VPTR * ptr = access_value_ptr(pactx->filetab, fname);
	STRING value = 0;
	TABLE tabprop;
	if (ptr && *ptr) {
		tabprop = (TABLE)(*ptr);
		value = valueof_str(tabprop, key);
	}
	return value;
}
/*=============================================+
 * pa_handle_char_encoding -- report command char_encoding("...")
 *  parse-time handling of report command
 *  node:   [IN]  current parse node
 *  vpinfo: [I/O] pointer to parseinfo structure (parse globals)
 * Called directly from generated parser code (ie, from code in yacc.y)
 *=============================================*/
void
pa_handle_char_encoding (PACTX pactx, PNODE node)
{
	STRING fname = ifname(node);
	PVALUE pval = ivalue(node);
	STRING ccs;
	ASSERT(ptype(pval)==PSTRING); /* grammar only allows strings */
	ccs = pvalue(pval);
	set_rptfile_prop(pactx, fname, strsave("char_encoding"), strsave(ccs));
}
/*=============================================+
 * make_internal_string_node -- make string node
 *  do any needed codeset conversion here
 *  lexical analysis time (same as parse-time) handling of string constants
 *=============================================*/
PNODE
make_internal_string_node (PACTX pactx, STRING str)
{
	PNODE node = 0;
	if (int_codeset && int_codeset[0] && str && str[0]) {
		STRING fname = pactx->fullpath;
		STRING rcodeset = get_rptfile_prop(pactx, fname, "char_encoding");
		if (rcodeset && rcodeset[0] && !eqstr(int_codeset, rcodeset)) {
			bfptr bfs=0;
			BOOLEAN success;
			struct tranmapping_s tm;
			memset(&tm, 0, sizeof(tm));
			tm.iconv_src = rcodeset;
			tm.iconv_dest = int_codeset;

			bfs = bfNew(strlen(str)*4+3);
			bfCpy(bfs, str);
			bfs = iconv_trans(rcodeset, int_codeset, bfs, "?", &success);
			node = string_node(pactx, bfStr(bfs));
			bfDelete(bfs);
		}
	}
	if (!node)
		node = string_node(pactx, str);
	return node;
}
/*=============================================+
 * pa_handle_include -- report command include("...")
 *  parse-time handling of report command
 *=============================================*/
void
pa_handle_include (PNODE node)
{
	STRING fname = ifname(node); /* current file */
	PVALUE pval = ivalue(node);
	STRING newfname;
	STRING fullpath;
	struct pathinfo_s * pathinfo = 0;
	ASSERT(ptype(pval)==PSTRING); /* grammar only allows strings */
	newfname = pvalue(pval);
	
	/* be nice to extract path from fname, and pass to find_program 
	so we can search directory of current file */

	if (find_program(newfname, &fullpath)) {
		pathinfo = new_pathinfo(newfname, fullpath);
		strfree(&fullpath);
		enqueue_list(Plist, pathinfo);
	} else {
		prog_error(node, "included file not found: %s\n", newfname);
		++Perrors;
	}
}
/*=============================================+
 * pa_handle_require -- report command require("...")
 *  pactx: [I/O] pointer to parseinfo structure (parse globals)
 *  node:  [IN]  current parse node
 *=============================================*/
void
pa_handle_require (PACTX pactx, PNODE node)
{
	char propname[128];
	STRING fname = ifname(node);
	PVALUE pval = ivalue(node);
	STRING str;
	ASSERT(ptype(pval)==PSTRING);
	str = pvalue(pval);
	llstrncpy(propname, "requires_", sizeof(propname), uu8);
	llstrapp(propname, sizeof(propname), uu8, str);
	set_rptfile_prop(pactx, fname, strsave(propname), strsave(str));
}
/*=============================================+
 * parse_error -- handle bison parse error
 *  pactx: [I/O] pointer to parseinfo structure (parse globals)
 *  ploc:  [IN]  token location
 *  node:  [IN]  current parse node
 *=============================================*/
void
parse_error (PACTX pactx, STRING str)
{
	/* TO DO - how to pass current pnode ? */
	prog_error(NULL, "Syntax Error (%s): %s: line %d, char %d\n"
		, str, pactx->fullpath, pactx->lineno+1, pactx->charpos+1);
	Perrors++;
}
/*=============================================+
 * check_rpt_requires -- check any prerequisites for
 *  this report file -- return desc. string if fail
 *=============================================*/
static STRING
check_rpt_requires (PACTX pactx, STRING fname)
{
	TABLE tab = (TABLE)valueof_ptr(pactx->filetab, fname);
	STRING str, propstr;
	INT ours=0, desired=0;
	STRING optr=LIFELINES_REPORTS_VERSION;
	STRING dptr=0;
	if (!tab)
		return 0;
	propstr = "requires_lifelines-reports.version:";
	str = valueof_str(tab, propstr);
	if (str) {
		dptr=str+strlen(propstr)-strlen("requires_");
		while (1) {
			ours=0;
			desired=0;
			while (optr[0] && !isdigit(optr[0]))
				++optr;
			while (dptr[0] && !isdigit(dptr[0]))
				++dptr;
			if (!optr[0] && !dptr[0])
				return 0;
			/* no UTF-8 allowed here, only regular digits */
			while (isdigit(optr[0]))
				ours = ours*10 + (*optr++ -'0');
			while (isdigit(dptr[0]))
				desired = desired*10 + (*dptr++ - '0');
			if (desired > ours)
				return _("This report requires a newer program to run");
		}
	}
	return 0;

}

