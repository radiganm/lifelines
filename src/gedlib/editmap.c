/*==========================================================
 * editmap.c -- LifeLines character mapping feature
 * Copyright(c) 1994 by T.T. Wetmore IV; all rights reserved
 * pre-SourceForge version information:
 *   3.0.2 - 09 Nov 1994
 *========================================================*/

#include "sys_inc.h"
#include <curses.h>
#include "llstdlib.h"
#include "table.h"
#include "translat.h"
#include "gedcom.h"
#include "liflines.h"
#include "feedback.h"
#include "gedcomi.h"


extern STRING map_keys[];
extern STRING cmperr,aredit,ronlye,dataerr,badttnum;
extern STRING sepch;

/*==============================================
 * edit_mapping -- Edit character mapping record
 *  code: [in] which translation table (see defn of map_keys)
 *============================================*/
BOOLEAN
edit_mapping (INT ttnum)
{
	if (ttnum < 0 || ttnum >= NUM_TT_MAPS) {
		msg_error(badttnum);
		return FALSE;
	}
	if (readonly) {
		msg_error(ronlye);
		return FALSE;
	}
	endwin();

	unlink(editfile);

	if (tran_tables[ttnum]) {
		if (!save_tt_to_file(ttnum, editfile)) {
			msg_error(dataerr);
			return FALSE;
		}
	}
	do_edit();
	while (TRUE) {
		char buffer[128];
		STRING ptr=buffer;
		INT mylen = sizeof(buffer);
		if (load_new_tt(editfile, ttnum))
			return TRUE;
		appendstrf(&ptr, &mylen, "%s ", cmperr);
		appendstrf(&ptr, &mylen, sepch, "<tab>"); /* (separator is %s) */
		if (ask_yes_or_no_msg(buffer, aredit))
			do_edit();
		else {
			return FALSE;
		}
	}
}
/*==============================================
 * save_tt_to_file -- Save one translation table
 *  to external file
 * returns TRUE if successful
 * Created: 2001/12/24, Perry Rapp
 *============================================*/
BOOLEAN
save_tt_to_file (INT ttnum, STRING filename)
{
	INT rtn;
	TRANSLFNC translfnc = NULL; /* Must not translate the translation table! */
	ASSERT(ttnum>=0 && ttnum<NUM_TT_MAPS);
	rtn = retrieve_to_textfile(map_keys[ttnum], filename, translfnc);
	return (rtn != RECORD_ERROR);
}
/*==============================================
 * load_new_tt -- Load translation table from file
 *  also update permanent record in database
 *  filepath:  [IN]  file whence to get new table
 *  ttnum:     [IN]  which translation table to load
 * Created: 2001/12/26, Perry Rapp
 *============================================*/
BOOLEAN
load_new_tt (STRING filepath, INT ttnum)
{
	TRANSLFNC transfnc = NULL; /* don't translate translation tables ! */
	TRANTABLE tt=0;
	if (!init_map_from_file(filepath, ttnum, &tt)) {
		if (tt)
			remove_trantable(tt);
		return FALSE;
	}
	/* change from old one to new one */
	if (tran_tables[ttnum])
		remove_trantable(tran_tables[ttnum]);
	tran_tables[ttnum] = tt;
	/* store new one in permanent record in database */
	store_text_file_to_db(map_keys[ttnum], filepath, transfnc);
	return TRUE;
}
