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
 * edit.c -- Edit person or family record
 * Copyright(c) 1992-96 by T.T. Wetmore IV; all rights reserved
 *   2.3.4 - 24 Jun 93    2.3.5 - 01 Sep 93
 *   3.0.0 - 23 Sep 94    3.0.2 - 12 Dec 94
 *   3.0.3 - 15 Feb 96
 *===========================================================*/

#include "llstdlib.h"
#include "table.h"
#include "translat.h"
#include "gedcom.h"
#include "indiseq.h"
#include "liflines.h"
#include "feedback.h"
#include "lloptions.h"

#include "llinesi.h"

extern STRING qSiredit, qSfredit, qScfpupt, qScffupt, qSidpedt, qSidspse, qSidfbys;
extern STRING qSireditopt, qSfreditopt;
extern STRING qSntprnt, qSgdpmod, qSgdfmod, qSronlye;
extern STRING qSparadox;

/*=====================================
 * write_indi_to_file - write node tree into GEDCOM
 * (no user interaction)
 *===================================*/
void
write_indi_to_file (NODE indi, CNSTRING file)
{
	FILE *fp;
	XLAT ttmo = transl_get_predefined_xlat(MINED);
	NODE name, refn, sex, body, famc, fams;
	
	ASSERT(fp = fopen(file, LLWRITETEXT));
	split_indi_old(indi, &name, &refn, &sex, &body, &famc, &fams);
	write_nodes(0, fp, ttmo, indi, TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, name, TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, refn, TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, sex,   TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, body , TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, famc,  TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, fams,  TRUE, TRUE, TRUE);
	fclose(fp);
	join_indi(indi, name, refn, sex, body, famc, fams);
}
/*=====================================
 * edit_indi -- Edit person in database
 * (with user interaction)
 * returns TRUE if user makes changes (& saves them)
 *===================================*/
BOOLEAN
edit_indi (RECORD irec1)  /* may be NULL */
{
	NODE indi1, indi2=0;
	BOOLEAN emp;
	XLAT ttmi = transl_get_predefined_xlat(MEDIN);

/* Identify indi if necessary */
	if (!irec1 && !(irec1 = ask_for_indi(_(qSidpedt), NOCONFIRM, NOASK1)))
		return FALSE;
	indi1 = nztop(irec1);

/* Prepare file for user to edit */
	if (getoptint("ExpandRefnsDuringEdit", 0) > 0)
		expand_refn_links(indi1);
	write_indi_to_file(indi1, editfile);
	resolve_refn_links(indi1);

/* Have user edit file */
	do_edit();
	if (readonly) {
		STRING msg;
		indi2 = file_to_node(editfile, ttmi, &msg, &emp);
		if (!equal_tree(indi1, indi2))
			message(_(qSronlye));
		free_nodes(indi2);
		return FALSE;
	}
	while (TRUE) {
		INT cnt;
		STRING msg;
		indi2 = file_to_node(editfile, ttmi, &msg, &emp);
		if (!indi2) {
			if (ask_yes_or_no_msg(msg, _(qSiredit))) {
				do_edit();
				continue;
			} 
			break;
		}
		cnt = resolve_refn_links(indi2);
		/* validate for showstopper errors */
		if (!valid_indi_tree(indi2, &msg, indi1)) {
			/* if fail a showstopper error, must reedit or abort */
			if (ask_yes_or_no_msg(msg, _(qSiredit))) {
				do_edit();
				continue;
			}
			free_nodes(indi2);
			indi2 = NULL;
			break;
		}
		/* Allow user to reedit if desired if any refn links unresolved */
		/* this is not a showstopper, so alternative is to continue */
		if (cnt > 0) {
			char msgb[120];
			snprintf(msgb, sizeof(msgb)
				, get_unresolved_ref_error_string(cnt), cnt);
			if (ask_yes_or_no_msg(msgb, _(qSireditopt))) {
				write_indi_to_file(indi2, editfile);
				do_edit();
				continue;
			}
		}
		break;
	}

/* Editing done; see if database changes */

	if (!indi2) return FALSE;
	if (equal_tree(indi1, indi2) || !ask_yes_or_no(_(qScfpupt))) {
		free_nodes(indi2);
		return FALSE;
	}

/* Move new data (in indi2 children) into existing indi1 tree */
	replace_indi(indi1, indi2);

/* Note in change history */
	history_record_change(irec1);
	
	msg_status(_(qSgdpmod), indi_to_name(indi1, 35));
	return TRUE;
}
/*=====================================
 * write_fam_to_file -- write node tree into GEDCOM
 * (no user interaction)
 *===================================*/
void
write_fam_to_file (NODE fam, CNSTRING file)
{
	FILE *fp;
	XLAT ttmo = transl_get_predefined_xlat(MINED);
	NODE refn, husb, wife, chil, body;

	ASSERT(fp = fopen(file, LLWRITETEXT));
	split_fam(fam, &refn, &husb, &wife, &chil, &body);
	write_nodes(0, fp, ttmo, fam,  TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, refn, TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, husb,  TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, wife,  TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, body,  TRUE, TRUE, TRUE);
	write_nodes(1, fp, ttmo, chil,  TRUE, TRUE, TRUE);
	join_fam(fam, refn, husb, wife, chil, body);
	fclose(fp);
}
/*====================================
 * edit_fam -- Edit family in database
 * (with user interaction)
 *==================================*/
BOOLEAN
edit_family (RECORD frec1) /* may be NULL */
{
	NODE fam1=0, fam2=0;
	RECORD irec=0;
	XLAT ttmi = transl_get_predefined_xlat(MEDIN);
	STRING msg;
	BOOLEAN emp;

/* Identify family if necessary */
	if (!frec1) {
		irec = ask_for_indi(_(qSidspse), NOCONFIRM, NOASK1);
		if (!irec) return FALSE;
		if (!FAMS(nztop(irec))) {
			message(_(qSntprnt));
			return FALSE;
		} 
		frec1 = choose_family(irec, _(qSparadox), _(qSidfbys), TRUE);
		if (!frec1) return FALSE; 
	}
	fam1 = nztop(frec1);

/* Prepare file for user to edit */
	if (getoptint("ExpandRefnsDuringEdit", 0) > 0)
		expand_refn_links(fam1);
	write_fam_to_file(fam1, editfile);
	resolve_refn_links(fam1);

/* Have user edit record */
	do_edit();
	if (readonly) {
		fam2 = file_to_node(editfile, ttmi, &msg, &emp);
		if (!equal_tree(fam1, fam2))
			message(_(qSronlye));
		free_nodes(fam2); 
		return FALSE;
	}
	while (TRUE) {
		INT cnt;
		fam2 = file_to_node(editfile, ttmi, &msg, &emp);
		if (!fam2) {
			if (ask_yes_or_no_msg(msg, _(qSfredit))) {
				do_edit();
				continue;
			}
			break;
		}
		cnt = resolve_refn_links(fam2);
		/* check validation & allow user to reedit if invalid */
		/* this is a showstopper, so alternative is to abort */
		if (!valid_fam_tree(fam2, &msg, fam1)) {
			if (ask_yes_or_no_msg(msg, _(qSfredit))) {
				do_edit();
				continue;
			}
			free_nodes(fam2);
			fam2 = NULL;
			break;
		}
		/* Allow user to reedit if desired if any refn links unresolved */
		/* this is not a showstopper, so alternative is to continue */
		if (cnt > 0) {
			char msgb[120];
			snprintf(msgb, sizeof(msgb)
				, get_unresolved_ref_error_string(cnt), cnt);
			if (ask_yes_or_no_msg(msgb, _(qSfreditopt))) {
				write_fam_to_file(fam2, editfile);
				do_edit();
				continue;
			}
		}
		break;
	}

/* If error or user backs out return */

	if (!fam2) return FALSE;
	if (equal_tree(fam1, fam2) || !ask_yes_or_no(_(qScffupt))) {
		free_nodes(fam2);
		return FALSE;
	}

/* Move new data (in fam2 children) into existing fam1 tree */
	replace_fam(fam1, fam2);

	msg_status(_(qSgdfmod));
	return TRUE;
}
