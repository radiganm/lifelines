/* 
   Copyright (c) 1991-1999 Thomas T. Wetmore IV
   "The MIT license"
   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/* modified 05 Jan 2000 by Paul B. McBride (pmcbride@tiac.net) */
/*=============================================================
 * node.c -- Standard GEDCOM NODE operations
 * Copyright(c) 1992-96 by T.T. Wetmore IV; all rights reserved
 *   2.3.4 - 24 Jun 93    2.3.5 - 04 Sep 93
 *   3.0.0 - 29 Aug 94    3.0.2 - 23 Dec 94
 *   3.0.3 - 16 Jan 96
 *===========================================================*/

#include "llstdlib.h"
#include "table.h"
#include "translat.h"
#include "gedcom.h"
#include "gedcomi.h"
#include "feedback.h"
#include "warehouse.h"
#include "metadata.h"
#include "lloptions.h"
#include "date.h"

/*********************************************
 * global/exported variables
 *********************************************/

static BOOLEAN add_metadata = FALSE;

/*********************************************
 * external/imported variables
 *********************************************/

extern STRING qSfileof, qSreremp, qSrerlng, qSrernlv, qSrerinc;
extern STRING qSrerbln, qSrernwt, qSrerilv, qSrerwlv, qSunsupunix, qSunsupuniv;

/*********************************************
 * local types
 *********************************************/

/* node allocator's freelist */
typedef struct blck *NDALLOC;
struct blck { NDALLOC next; };

/*********************************************
 * local enums & defines
 *********************************************/

enum { NEW_RECORD, EXISTING_LACKING_WH_RECORD };

/*********************************************
 * local function prototypes, alphabetical
 *********************************************/

static RECORD alloc_record_from_key(STRING key);
static void alloc_record_wh(RECORD rec, INT isnew);
static NODE alloc_node(void);
static void assign_record(RECORD rec, char ntype, INT keynum);
static STRING fixup(STRING str);
static STRING fixtag (STRING tag);
static RECORD indi_to_prev_sib_impl(NODE indi);
static void hook_node_to_rec_recurse(RECORD rec, NODE node);
static void hook_record_to_root_node(RECORD rec, NODE node);

static INT node_strlen(INT levl, NODE node);

/*********************************************
 * unused local function prototypes
 *********************************************/

#ifdef UNUSED_CODE
static BOOLEAN all_digits (STRING);
NODE children_nodes(NODE faml);
NODE father_nodes(NODE faml);
NODE mother_nodes(NODE faml);
NODE parents_nodes(NODE faml);
#endif /* UNUSED_CODE */

/*********************************************
 * local variables
 *********************************************/

/* node allocator's free list */
static NDALLOC first_blck = (NDALLOC) 0;

/*********************************************
 * local function definitions
 * body of module
 *********************************************/

/*==============================
 * fixup -- Save non-tag strings
 *============================*/
static STRING
fixup (STRING str)
{
	if (!str || *str == 0) return NULL;
	return strsave(str);
}
/*=============================
 * fixtag -- Keep tags in table
 *===========================*/
static STRING
fixtag (STRING tag)
{
	STRING str;
	if ((str = valueof_str(tagtable, tag))) return str;
	str = strsave(tag);
	insert_table_str(tagtable, str, str);
	return str;
}
/*=====================================
 * alloc_node -- Special node allocator
 *===================================*/
static NODE
alloc_node (void)
{
	NODE node;
	NDALLOC blck;
	int i;
	if (first_blck == (NDALLOC) 0) {
		node = (NODE) stdalloc(100*sizeof(*node));
		first_blck = (NDALLOC) node;
		for (i = 1; i <= 99; i++) {
			blck = (NDALLOC) node;
			blck->next = (NDALLOC) (node + 1);
			node++;
		}
		((NDALLOC) node)->next = (NDALLOC) 0;
	}
	node = (NODE) first_blck;
	first_blck = first_blck->next;
	return node;
}
/*======================================
 * free_node -- Special node deallocator
 *====================================*/
void
free_node (NODE node)
{
	if (nxref(node)) stdfree(nxref(node));
	if (nval(node)) stdfree(nval(node));
/* MTE 2003/02/02
 * Is this the right thing to do?
 * Or do tags have to live longer than nodes?

	if (ntag(node)) {
          delete_table(tagtable,ntag(node));
          stdfree(ntag(node));
        }
*/
	((NDALLOC) node)->next = first_blck;
	first_blck = (NDALLOC) node;
	
}
/*===========================
 * create_node -- Create NODE
 *
 * STRING xref  [in] xref
 * STRING tag   [in] tag
 * STRING val:  [in] value
 * NODE prnt:   [in] parent
 *=========================*/
NODE
create_node (STRING xref, STRING tag, STRING val, NODE prnt)
{
	NODE node = alloc_node();
	memset(node, 0, sizeof(*node));
	nxref(node) = fixup(xref);
	ntag(node) = fixtag(tag);
	nval(node) = fixup(val);
	nparent(node) = prnt;
	nrefcnt(node) = 1;
	if (prnt)
		node->n_rec = prnt->n_rec;
	return node;
}
/*===========================
 * create_temp_node -- Create NODE for temporary use
 *  (not to be connected to a record)
 * [All arguments are duplicated, so caller doesn't have to]
 * STRING xref  [in] xref
 * STRING tag   [in] tag
 * STRING val:  [in] value
 * NODE prnt:   [in] parent
 * Created: 2003-02-01 (Perry Rapp)
 *=========================*/
NODE
create_temp_node (STRING xref, STRING tag, STRING val, NODE prnt)
{
	NODE node = create_node(xref, tag, val, prnt);
	nflag(node) = ND_TEMP;
	return node;
}
/*===========================
 * free_temp_node_tree -- Free a node created by create_temp_node
 * Created: 2003-02-01 (Perry Rapp)
 *=========================*/
void
free_temp_node_tree (NODE node)
{
	NODE n2;
	if ((n2 = nchild(node))) {
		free_temp_node_tree(n2);
		nchild(node) = 0;
	}
	if ((n2 = nsibling(node))) {
		free_temp_node_tree(n2);
		nsibling(node) = 0;
	}
	free_node(node);
}
/*===================================
 * is_temp_node -- Return whether node is a temp
 * Created: 2003-02-04 (Perry Rapp)
 *=================================*/
BOOLEAN
is_temp_node (NODE node)
{
	return !!(nflag(node) & ND_TEMP);
}
/*===================================
 * set_temp_node -- make node temp (or not)
 * Created: 2003-02-04 (Perry Rapp)
 *=================================*/
void
set_temp_node (NODE node, BOOLEAN temp)
{
	if (is_temp_node(node) ^ temp)
		nflag(node) ^= ND_TEMP;
}
/*===================================
 * alloc_new_record -- record allocator
 *  perhaps should use special allocator like nodes
 * Created: 2001/01/25, Perry Rapp
 *=================================*/
RECORD
alloc_new_record (void)
{
	RECORD rec;
	rec = (RECORD)stdalloc(sizeof(*rec));
	memset(rec, 0, sizeof(*rec));
	/* these must be filled in by caller */
	rec->nkey.key = "";
	rec->nkey.keynum = 0;
	rec->nkey.ntype = 0;
	return rec;
}
/*===================================
 * alloc_record_from_key -- allocate record for key
 * Created: 2001/01/25, Perry Rapp
 *=================================*/
static RECORD
alloc_record_from_key (STRING key)
{
	RECORD rec = alloc_new_record();
	assign_record(rec, key[0], atoi(key+1));
	return rec;
}
/*===================================
 * assign_record -- put key info into record
 * Created: 2001/02/04, Perry Rapp
 *=================================*/
static void
assign_record (RECORD rec, char ntype, INT keynum)
{
	char xref[12];
	char key[9];
	NODE node;
	sprintf(key, "%c%d", ntype, keynum);
	sprintf(xref, "@%s@", key);
	if ((node = nztop(rec))) {
		if (nxref(node)) stdfree(nxref(node));
		nxref(node) = strsave(xref);
	}
	rec->nkey.key = strsave(key);
	rec->nkey.keynum = keynum;
	rec->nkey.ntype = ntype;
}
/*===================================
 * init_new_record -- put key info into record
 *  of brand new record
 * Created: 2001/02/04, Perry Rapp
 *=================================*/
void
init_new_record (RECORD rec, char ntype, INT keynum)
{
	assign_record(rec, ntype, keynum);
	alloc_record_wh(rec, NEW_RECORD);
}
/*===================================
 * alloc_record_wh -- allocate warehouse for
 *  a record without one (new or existing)
 * Created: 2001/02/04, Perry Rapp
 *=================================*/
static void
alloc_record_wh (RECORD rec, INT isnew)
{
	LLDATE creation;
	ASSERT(!rec->mdwh); /* caller must know what it is doing */
	if (!add_metadata)
		return;
	rec->mdwh = (WAREHOUSE)stdalloc(sizeof(*(rec->mdwh)));
	wh_allocate(rec->mdwh);
	get_current_lldate(&creation);
	wh_add_block_var(rec->mdwh, MD_CREATE_DATE, &creation, sizeof(creation));
	if (isnew == EXISTING_LACKING_WH_RECORD)
		wh_add_block_int(rec->mdwh, MD_CONVERTED_BOOL, 1);
}
/*===================================
 * create_record -- create record to wrap top node
 * Created: 2001/01/29, Perry Rapp
 *=================================*/
RECORD
create_record (NODE node)
{
	RECORD rec = 0;
	if (nxref(node))
		rec = alloc_record_from_key(node_to_key(node));
	else
		rec = alloc_new_record();
	hook_record_to_root_node(rec, node);
	return rec;
}
/*===================================
 * hook_record_to_root_node -- Connect record to node tree
 * Created: 2003-02-04 (Perry Rapp)
 *=================================*/
static void
hook_record_to_root_node (RECORD rec, NODE node)
{
	rec->top = node;
	hook_node_to_rec_recurse(rec, node);
}
/*===================================
 * hook_node_to_rec_recurse -- Connect node subtree to record
 * Created: 2003-02-04 (Perry Rapp)
 *=================================*/
static void
hook_node_to_rec_recurse (RECORD rec, NODE node)
{
	while (node) {
		if (nchild(node))
			hook_node_to_rec_recurse(rec, nchild(node));
		node->n_rec = rec;
		node = nsibling(node);
	}
}
/*===================================
 * init_new_record_and_just_read_node -- 
 *  initializer used by nodeio, after having read a node
 * Created: 2003-02-04 (Perry Rapp)
 *=================================*/
void
init_new_record_and_just_read_node (RECORD rec, NODE node, CNSTRING key)
{
	hook_record_to_root_node(rec, node);
	assign_record(rec, key[0], atoi(key+1));
	if (!rec->mdwh)
		alloc_record_wh(rec, EXISTING_LACKING_WH_RECORD);
}
/*===================================
 * free_rec -- record deallocator
 * Created: 2000/12/30, Perry Rapp
 *=================================*/
void
free_rec (RECORD rec)
{
	if (rec->top)
		free_nodes(rec->top);
	if (rec->mdwh) {
		stdfree(rec->mdwh);
	}
	if (rec->nkey.key[0])
		stdfree(rec->nkey.key);
	stdfree(rec);
}
/*=====================================
 * free_nodes -- Free all NODEs in tree
 *===================================*/
void
free_nodes (NODE node)
{
	NODE sib;
	while (node) {
		if (nchild(node)) free_nodes(nchild(node));
		sib = nsibling(node);
		free_node(node);
		node = sib;
	}
}
/*==============================================================
 * tree_strlen -- Compute string length of tree -- don't count 0
 *============================================================*/
INT
tree_strlen (INT levl,       /* level */
             NODE node)      /* root */
{
	INT len = 0;
	while (node) {
		len += node_strlen(levl, node);
		if (nchild(node))
			len += tree_strlen(levl + 1, nchild(node));
		node = nsibling(node);
	}
	return len;
}
/*================================================================
 * node_strlen -- Compute NODE string length -- count \n but not 0
 *==============================================================*/
static INT
node_strlen (INT levl,       /* level */
             NODE node)      /* node */
{
	INT len;
	char scratch[10];
	sprintf(scratch, "%d", levl);
	len = strlen(scratch) + 1;
	if (nxref(node)) len += strlen(nxref(node)) + 1;
	len += strlen(ntag(node));
	if (nval(node)) len += strlen(nval(node)) + 1;
	return len + 1;
}
/*==========================================
 * unknown_node_to_dbase -- Store node of unknown type
 *  in database
 * Created: 2001/04/06, Perry Rapp
 *========================================*/
void
unknown_node_to_dbase (NODE node)
{
	/* skip tag validation */
	node_to_dbase(node, NULL);
}
/*==========================================
 * indi_to_dbase -- Store person in database
 *========================================*/
void
indi_to_dbase (NODE node)
{
	/*
	To start storing metadata, we need the RECORD here
	If we were using RECORD everywhere, we'd pass it in here
	We could look it up in the cache, but it might not be there
	(new records)
	(and this applies to fam_to_dbase, sour_to_dbase, etc)
	Perry, 2001/01/15
	*/
	node_to_dbase(node, "INDI");
}
/*=========================================
 * fam_to_dbase -- Store family in database
 *=======================================*/
void
fam_to_dbase (NODE node)
{
	node_to_dbase(node, "FAM");
}
/*=========================================
 * even_to_dbase -- Store event in database
 *=======================================*/
void
even_to_dbase (NODE node)
{
	node_to_dbase(node, "EVEN");
}
/*==========================================
 * sour_to_dbase -- Store source in database
 *========================================*/
void
sour_to_dbase (NODE node)
{
	node_to_dbase(node, "SOUR");
}
/*================================================
 * othr_to_dbase -- Store other record in database
 *==============================================*/
void
othr_to_dbase (NODE node)
{
	node_to_dbase(node, NULL);
}
/*===============================================
 * node_to_dbase -- Store GEDCOM tree in database
 *=============================================*/
void
node_to_dbase (NODE node,
               STRING tag)
{
	STRING str;
	ASSERT(node);
	if (tag) { ASSERT(eqstr(tag, ntag(node))); }
	str = node_to_string(node);
	ASSERT(store_record(rmvat(nxref(node)), str, strlen(str)));
	stdfree(str);
}
/*==================================================
 * indi_to_famc -- Return family-as-child for person
 *================================================*/
NODE
indi_to_famc (NODE node)
{
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), "FAMC"))) return NULL;
	return key_to_fam(rmvat(nval(node)));
}
/*========================================
 * fam_to_husb_node -- Return husband of family
 *======================================*/
NODE
fam_to_husb_node (NODE node)
{
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), "HUSB"))) return NULL;
	return key_to_indi(rmvat(nval(node)));
}
/*========================================
 * fam_to_husb -- Return husband of family
 *======================================*/
RECORD
fam_to_husb (RECORD frec)
{
	NODE fam = nztop(frec), husb;
	if (!fam) return NULL;
	if (!(husb = find_tag(nchild(fam), "HUSB"))) return NULL;
	return key_to_irecord(rmvat(nval(husb)));
}
/*=====================================
 * fam_to_wife_node -- Return wife of family
 *===================================*/
NODE
fam_to_wife_node (NODE node)
{
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), "WIFE"))) return NULL;
	return key_to_indi(rmvat(nval(node)));
}
/*========================================
 * fam_to_wife -- Return husband of family
 *======================================*/
RECORD
fam_to_wife (RECORD frec)
{
	NODE fam = nztop(frec), husb;
	if (!fam) return NULL;
	if (!(husb = find_tag(nchild(fam), "WIFE"))) return NULL;
	return key_to_irecord(rmvat(nval(husb)));
}
/*===============================================
 * fam_to_spouse -- Return other spouse of family
 *=============================================*/
NODE
fam_to_spouse (NODE fam,
               NODE indi)
{
    	INT num;
	if (!fam) return NULL;
	FORHUSBS(fam, husb, num)
		if(husb != indi) return(husb);
	ENDHUSBS
	FORWIFES(fam, wife, num)
	  if(wife != indi) return(wife);
	ENDWIFES
	return NULL;
}
/*==================================================
 * fam_to_first_chil -- Return first child of family
 *================================================*/
NODE
fam_to_first_chil (NODE node)
{
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), "CHIL"))) return NULL;
	return key_to_indi(rmvat(nval(node)));
}
/*=================================================
 * fam_to_last_chil -- Return last child of family
 *===============================================*/
NODE
fam_to_last_chil (NODE node)
{
	NODE prev = NULL;
	if (!node) return NULL;
	/* find first CHIL in fam */
	if (!(node = find_tag(nchild(node), "CHIL"))) return NULL;
	/* cycle thru all remaining nodes, keeping most recent CHIL node */
	while (node) {
		if (eqstr(ntag(node),"CHIL"))
			prev = node;
		node = nsibling(node);
	}
	return key_to_indi(rmvat(nval(prev)));
}
/*========================================
 * indi_to_fath -- Return father of person
 *======================================*/
NODE
indi_to_fath (NODE node)
{
	return fam_to_husb_node(indi_to_famc(node));
}
/*========================================
 * indi_to_moth -- Return mother of person
 *======================================*/
NODE
indi_to_moth (NODE node)
{
	return fam_to_wife_node(indi_to_famc(node));
}
/*==================================================
 * indi_to_prev_sib -- Return previous sib of person
 *================================================*/
static RECORD
indi_to_prev_sib_impl (NODE indi)
{
	NODE fam, prev, node;
	if (!indi) return NULL;
	if (!(fam = indi_to_famc(indi))) return NULL;
	prev = NULL;
	node = CHIL(fam);
	/* loop thru all nodes following first child, keeping most recent CHIL */
	while (node) {
		if (eqstr(nxref(indi), nval(node))) {
			if (!prev) return NULL;
			return key_to_record(rmvat(nval(prev)));
		}
		if (eqstr(ntag(node),"CHIL"))
			prev = node;
		node = nsibling(node);
	}
	return NULL;
}
NODE
indi_to_prev_sib_old (NODE indi)
{
	return nztop(indi_to_prev_sib_impl(indi));
}
RECORD
indi_to_prev_sib (RECORD irec)
{
	return indi_to_prev_sib_impl(nztop(irec));
}
/*==============================================
 * indi_to_next_sib -- Return next sib of person
 *============================================*/
static RECORD
indi_to_next_sib_impl (NODE indi)
{
	NODE fam, node;
	BOOLEAN found;
	if (!indi) return NULL;
	if (!(fam = indi_to_famc(indi))) return NULL;
	node = CHIL(fam);
	found = FALSE;  /* until we find indi */
	while (node) {
		if (!found) {
			if (eqstr(nxref(indi), nval(node)))
				found = TRUE;
		} else {
			if (eqstr(ntag(node),"CHIL"))
				return key_to_record(rmvat(nval(node)));
		}
		node = nsibling(node);
	}
	return NULL;
}
NODE
indi_to_next_sib_old (NODE indi)
{
	return nztop(indi_to_next_sib_impl(indi));
}
RECORD
indi_to_next_sib (RECORD irec)
{
	return indi_to_next_sib_impl(nztop(irec));
}
/*======================================
 * indi_to_name -- Return name of person
 *====================================*/
STRING
indi_to_name (NODE node, INT len)
{
	if (node)
		node = find_tag(nchild(node), "NAME");
	if (!node)
		return _("NO NAME");
	return manip_name(nval(node), TRUE, TRUE, len);
}
/*======================================
 * indi_to_title -- Return title of person
 *====================================*/
STRING
indi_to_title (NODE node, INT len)
{
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), "TITL"))) return NULL;
	return manip_name(nval(node), FALSE, TRUE, len);
}
/*======================================
 * node_to_tag -- Return a subtag of a node
 * (presumably top level, but not necessarily)
 *====================================*/
STRING node_to_tag (NODE node, STRING tag, INT len)
{
	static char scratch[MAXGEDNAMELEN+1];
	STRING refn;
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), tag)))
		return NULL;
	refn = nval(node);
	if (len > (INT)sizeof(scratch)-1)
		len = sizeof(scratch)-1;
	llstrsets(scratch, len, uu8, refn);
	return scratch;
}
/*==============================================
 * fam_to_event -- Convert event tree to string
 *============================================*/
STRING
fam_to_event (NODE node, STRING tag, STRING head
	, INT len, RFMT rfmt)
{
	return indi_to_event(node, tag, head, len, rfmt);
}
/*==============================================
 * record_to_date_place -- Find event info
 *  record:  [IN]  record to search
 *  tag:     [IN]  desired tag (eg, "BIRT")
 *  date:    [OUT] date found (optional)
 *  plac:    [OUT] place found (option)
 * Created: 2003-01-12 (Perry Rapp)
 *============================================*/
void
record_to_date_place (RECORD record, STRING tag, STRING * date, STRING * plac)
{
	NODE node;
	for (node = record_to_first_event(record, tag)
		; node
		; node = node_to_next_event(node, tag)) {
		event_to_date_place(node, date, plac);
		if (date && *date && plac && *plac)
			return;
	}
}
/*==============================================
 * record_to_first_event -- Find requested event subtree
 *  record:  [IN]  record to search
 *  tag:     [IN]  desired tag (eg, "BIRT")
 * Created: 2003-01-12 (Perry Rapp)
 *============================================*/
NODE
record_to_first_event (RECORD record, CNSTRING tag)
{
	NODE node = nztop(record);
	if (!node) return NULL;
	return find_tag(nchild(node), tag);
}
/*==============================================
 * node_to_next_event -- Find next event after node
 *  node:  [IN]  start search after node
 *  tag:   [IN]  desired tag (eg, "BIRT")
 * Created: 2003-01-12 (Perry Rapp)
 *============================================*/
NODE
node_to_next_event (NODE node, CNSTRING tag)
{
	return find_tag(nsibling(node), tag);
}
/*==============================================
 * indi_to_event -- Convert event tree to string
 *  node: [IN]  event subtree to search
 *  ttm:  [IN]  translation table to apply to event strings
 *  tag:  [IN]  desired tag (eg, "BIRT")
 *  head: [IN]  header to print in output (eg, "born: ")
 *  len:  [IN]  max length output desired
 * Searches node substree for desired tag
 *  returns formatted string (event_to_string) if found,
 *  else NULL
 *============================================*/
STRING
indi_to_event (NODE node, STRING tag, STRING head
	, INT len, RFMT rfmt)
{
	static char scratch[200];
	STRING p = scratch;
	INT mylen = sizeof(scratch)/sizeof(scratch[0]);
	STRING event;
	STRING omit;
	if (mylen > len+1) mylen = len+1; /* incl. trailing 0 */
	if (!node) return NULL;
	if (!(node = find_tag(nchild(node), tag))) return NULL;
	event = event_to_string(node, rfmt);
	if (!event) return NULL;
	/* need at least room for head + 1 character + "..." or no point */
	if ((INT)strlen(head)+4>len) return NULL;
	p[0] = 0;
	/* TODO: break out following code into a subroutine for disp_person_birth to call */
	llstrcatn(&p, head, &mylen);
	if (mylen<(INT)strlen(event)+1) {
		omit = getoptstr("ShortOmitString", NULL);
		if (omit) {
			mylen -= strlen(omit)+1; /* plus trailing 0 */
			llstrcatn(&p, event, &mylen);
			mylen += strlen(omit)+1;
			llstrcatn(&p, omit, &mylen);
		} else {
			llstrcatn(&p, event, &mylen);
		}
	} else {
		llstrcatn(&p, event, &mylen);
		if (mylen && p[-1]!='.')
		llstrcatn(&p, ".", &mylen);
	}
	return scratch;
}
/*===========================================
 * event_to_date_place  -- Find date & place
 *  node:  [IN]  node tree of event to describe
 *  date:  [OUT] value of first DATE line (optional)
 *  plac:  [OUT] value of first PLACE line (optional)
 *=========================================*/
void
event_to_date_place (NODE node, STRING * date, STRING * plac)
{
	INT count=0;
	if (date) {
		*date = NULL;
	} else {
		++count;
	}
	if (plac) {
		*plac = NULL;
	} else {
		++count;
	}
	if (!node) return;
	node = nchild(node);
	while (node && count<2) {
		if (eqstr("DATE", ntag(node)) && date && !*date) {
			*date = nval(node);
			++count;
		}
		if (eqstr("PLAC", ntag(node)) && plac && !*plac) {
			*plac = nval(node);
			++count;
		}
		node = nsibling(node);
	}
}
/*===========================================
 * event_to_string -- Convert event to string
 * Finds DATE & PLACE nodes, and prints a string
 * representation of them.
 *  node:  [IN]  node tree of event to describe
 *  ttm:   [IN]  translation table to use
 *  rfmt:  [IN]  reformatting info (may be NULL)
 *=========================================*/
STRING
event_to_string (NODE node, RFMT rfmt)
{
	static char scratch1[MAXLINELEN+1];
	STRING date, plac;
	event_to_date_place(node, &date, &plac);
	if (!date && !plac) return NULL;
	/* Apply optional, caller-specified date & place reformatting */
	if (rfmt && date && rfmt->rfmt_date)
		date = (*rfmt->rfmt_date)(date);
	if (rfmt && plac && rfmt->rfmt_plac)
		plac = (*rfmt->rfmt_plac)(plac);
	if (rfmt && rfmt->combopic && date && date[0] && plac && plac[0]) {
		sprintpic2(scratch1, sizeof(scratch1), uu8, rfmt->combopic, date, plac);
	} else if (date && date[0]) {
		llstrncpy(scratch1, date, sizeof(scratch1), uu8);
	} else if (plac && plac[0]) {
		llstrncpy(scratch1, plac, sizeof(scratch1), uu8);
	} else {
		return NULL;
	}
	return scratch1;
}
/*=======================================
 * event_to_date -- Convert event to date
 *  node: [IN]  event node
 *  ttm:  [IN]  translation table to apply
 *  shrt: [IN]  flag - use short form if set
 *=====================================*/
STRING
event_to_date (NODE node, BOOLEAN shrt)
{
	static char scratch[MAXLINELEN+1];
	if (!node) return NULL;
	if (!(node = DATE(node))) return NULL;
	llstrsets(scratch, sizeof(scratch),uu8, nval(node));
	if (shrt) return shorten_date(scratch);
	return scratch;
}
/*========================================
 * event_to_plac -- Convert event to place
 *======================================*/
STRING
event_to_plac (NODE node, BOOLEAN shrt)
{
	if (!node) return NULL;
	node = PLAC(node);
	if (!node) return NULL;
	if (shrt) return shorten_plac(nval(node));
	return nval(node);
}
/*================================
 * show_node -- Show tree -- DEBUG
 *==============================*/
void
show_node (NODE node)
{
	if (!node) llwprintf("(NIL)");
	show_node_rec(0, node);
}
/*================================================
 * show_node_rec -- Recursive version of show_node
 *==============================================*/
void
show_node_rec (INT levl,
               NODE node)
{
	INT i;
	if (!node) return;
	for (i = 1;  i < levl;  i++)
		llwprintf("  ");
	llwprintf("%d", levl);
	if (nxref(node)) llwprintf(" %s", nxref(node));
	llwprintf(" %s", ntag(node));
	if (nval(node)) llwprintf(" %s", nval(node));
	llwprintf("\n");
	show_node_rec(levl + 1, nchild(node));
	show_node_rec(levl    , nsibling(node));
}
/*===========================================
 * length_nodes -- Return length of NODE list
 *=========================================*/
INT
length_nodes (NODE node)
{
	INT len = 0;
	while (node) {
		len++;
		node = nsibling(node);
	}
	return len;
}
/*=================================================
 * shorten_plac -- Return short form of place value
 * Returns modified input string, or value from placabbr table
 *===============================================*/
STRING
shorten_plac (STRING plac)
{
	STRING plac0 = plac, comma, val;
	if (!plac) return NULL;
	comma = (STRING) strrchr(plac, ',');
	if (comma) plac = comma + 1;
	while (*plac++ == ' ')
		;
	plac--;
	if (*plac == 0) return plac0;
	if ((val = valueof_str(placabbvs, plac))) return val;
	return plac;
}

#ifdef UNUSED_CODE
/*============================================
 * all_digits -- Check if string is all digits
 * UNUSED CODE
 *==========================================*/
static BOOLEAN
all_digits (STRING s)
{
	INT c;
	while ((c = *s++)) {
		if (c < '0' || c > '9') return FALSE;
	}
	return TRUE;
}
#endif /* UNUSED_CODE */
/*=======================
 * copy_node -- Copy node
 *=====================*/
NODE
copy_node (NODE node)
{
	return create_node(nxref(node), ntag(node), nval(node), NULL);
}
/*========================
 * copy_nodes -- Copy tree
 *======================*/
NODE
copy_nodes (NODE node, BOOLEAN kids, BOOLEAN sibs)
{
	NODE newn, kin;
	if (!node) return NULL;
	newn = copy_node(node);
	if (kids && nchild(node)) {
		kin = copy_nodes(nchild(node), TRUE, TRUE);
		ASSERT(kin);
		nchild(newn) = kin;
		while (kin) {
			nparent(kin) = newn;
			kin = nsibling(kin);
		}
	}
	if (sibs && nsibling(node)) {
		kin = copy_nodes(nsibling(node), kids, TRUE);
		ASSERT(kin);
		nsibling(newn) = kin;
	}
	return newn;
}
/*===============================================================
 * traverse_nodes -- Traverse nodes in tree while doing something
 * NODE node:    root of tree to traverse
 * func:         function to call at each node (returns FALSE to stop traversal)
 * param:        opaque pointer for client use, passed thru to callback
 *=============================================================*/
BOOLEAN
traverse_nodes (NODE node, BOOLEAN (*func)(NODE, VPTR), VPTR param)
{
	while (node) {
		if (!(*func)(node, param)) return FALSE;
		if (nchild(node)) {
			if (!traverse_nodes(nchild(node), func, param))
				return FALSE;
		}
		node = nsibling(node);
	}
	return TRUE;
}
/*==================================================
 * num_spouses_of_indi -- Returns number of spouses of person
 *================================================*/
INT
num_spouses_of_indi (NODE indi)
{
	INT nsp;
	if (!indi) return 0;
	FORSPOUSES(indi, spouse, fam, nsp) ENDSPOUSES
	return nsp;
}
/*===================================================
 * find_node -- Find node with specific tag and value
 *
 * NODE prnt:   [in] parent node
 * STRING tag:  [in] tag, may be NULL
 * STRING val:  [in] value, may be NULL
 * NODE *plast: [out] previous node, may be NULL
 *=================================================*/
NODE
find_node (NODE prnt, STRING tag, STRING val, NODE *plast)
{
	NODE last, node;

	if (plast) *plast = NULL;
	if (!prnt || (!tag && !val)) return NULL;
	for (last = NULL, node = nchild(prnt); node;
	     last = node, node = nsibling(node)) {
		if (tag && nestr(tag, ntag(node))) continue;
		if (val && nestr(val, nval(node))) continue;
		if (plast) *plast = last;
		return node;
	}
	return NULL;
}

#ifdef UNUSED_CODE
/*=======================================================================
 * father_nodes -- Given list of FAMS or FAMC nodes, returns list of HUSB
 *   lines they contain
 * UNUSED CODE
 *=====================================================================*/
NODE
father_nodes (NODE faml)      /* list of FAMC and/or FAMS nodes */
{
	NODE fam, refn, husb, wife, chil, rest;
	NODE old = NULL, new = NULL;
	while (faml) {
		ASSERT(eqstr("FAMC", ntag(faml)) || eqstr("FAMS", ntag(faml)));
		ASSERT(fam = key_to_fam(rmvat(nval(faml))));
		split_fam(fam, &refn, &husb, &wife, &chil, &rest);
		new = union_nodes(old, husb, FALSE, TRUE);
		free_nodes(old);
		old = new;
		join_fam(fam, refn, husb, wife, chil, rest);
		faml = nsibling(faml);
	}
	return new;
}
/*=======================================================================
 * mother_nodes -- Given list of FAMS or FAMC nodes, returns list of WIFE
 *   lines they contain
 *=====================================================================*/
NODE
mother_nodes (NODE faml)      /* list of FAMC and/or FAMS nodes */
{
	NODE fam, refn, husb, wife, chil, rest;
	NODE old = NULL, new = NULL;
	while (faml) {
		ASSERT(eqstr("FAMC", ntag(faml)) || eqstr("FAMS", ntag(faml)));
		ASSERT(fam = key_to_fam(rmvat(nval(faml))));
		split_fam(fam, &refn, &husb, &wife, &chil, &rest);
		new = union_nodes(old, wife, FALSE, TRUE);
		free_nodes(old);
		old = new;
		join_fam(fam, refn, husb, wife, chil, rest);
		faml = nsibling(faml);
	}
	return new;
}
/*=========================================================================
 * children_nodes -- Given list of FAMS or FAMC nodes, returns list of CHIL
 *   lines they contain
 *=======================================================================*/
NODE
children_nodes (NODE faml)      /* list of FAMC and/or FAMS nodes */
{
	NODE fam, refn, husb, wife, chil, rest;
	NODE old = NULL, new = NULL;
	while (faml) {
		ASSERT(eqstr("FAMC", ntag(faml)) || eqstr("FAMS", ntag(faml)));
		ASSERT(fam = key_to_fam(rmvat(nval(faml))));
		split_fam(fam, &refn, &husb, &wife, &chil, &rest);
		new = union_nodes(old, chil, FALSE, TRUE);
		free_nodes(old);
		old = new;
		join_fam(fam, refn, husb, wife, chil, rest);
		faml = nsibling(faml);
	}
	return new;
}
/*========================================================================
 * parents_nodes -- Given list of FAMS or FAMC nodes, returns list of HUSB
 *   and WIFE lines they contain
 *======================================================================*/
NODE
parents_nodes (NODE faml)      /* list of FAMC and/or FAMS nodes */
{
	NODE fam, refn, husb, wife, chil, rest;
	NODE old = NULL, new = NULL;
	while (faml) {
		ASSERT(eqstr("FAMC", ntag(faml)) || eqstr("FAMS", ntag(faml)));
		ASSERT(fam = key_to_fam(rmvat(nval(faml))));
		split_fam(fam, &refn, &husb, &wife, &chil, &rest);
		new = union_nodes(old, husb, FALSE, TRUE);
		free_nodes(old);
		old = new;
		new = union_nodes(old, wife, FALSE, TRUE);
		free_nodes(old);
		old = new;
		join_fam(fam, refn, husb, wife, chil, rest);
		faml = nsibling(faml);
	}
	return new;
}
#endif /* UNUSED_CODE */
