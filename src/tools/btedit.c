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
/*=================================================================
 * btedit.c -- Command that allows individual BTREE records to be
 *   edited directly.  Can only be used on records that are in pure
 *   ASCII format.
 * Copyright(c) 1991-94 by T.T. Wetmore IV; all rights reserved
 *   3.0.2 - 10 Dec 94
 *===============================================================*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include "standard.h"
#include "btree.h"

int opt_finnish = 0;

/*=========================================
 * main -- Main procedure of btedit command
 *=======================================*/
main (argc, argv)
int argc;
char **argv;
{
	BTREE btree;
	char cmdbuf[512];
	char *editor;

#ifdef WIN32
	_fmode = O_BINARY;	/* default to binary rather than TEXT mode */
#endif
	if (argc != 3) {
		printf("usage: btedit <btree> <rkey>\n");
		exit(1);
	}
	if (!(btree = openbtree(argv[1], FALSE, TRUE))) {
		printf("could not open btree: %s\n", argv[1]);
		exit(1);
	}
	if (!getfile(btree, str2rkey(argv[2]), "btedit.tmp")) {
		printf("there is no record with that key\n");
		closebtree(btree);
		exit(0);
	}

	if((editor = getenv("LLEDITOR")) && *editor);
	else editor = "vi";
	sprintf(cmdbuf, "%s btedit.tmp", editor);
#ifdef WIN32
	w32system(cmdbuf);	/* start program and wait for completion */
#else
	system(cmdbuf);
#endif
	addfile(btree, str2rkey(argv[2]), "btedit.tmp");
	unlink("btedit.tmp");
	closebtree(btree);
}
/*=========================================================
 * __allocate -- Allocate memory - called by stdalloc macro
 *========================================================*/
char *__allocate (len, file, line)
int len;    /* number of bytes to allocate */
STRING file; /* not used */
int line;   /* not used */
{
	char *p;
	if ((p = malloc(len)) == NULL)  FATAL();
	return p;
}

/*=======================================================
 * __deallocate - Return memory - called by stdfree macro
 *=====================================================*/
__deallocate (ptr, file, line)
char *ptr;  /* memory being returned */
STRING file; /* not used */
int line;   /* not used */
{
	free(ptr);
}
/*=============================
 * fatal -- Fatal error routine
 *===========================*/
__fatal (file, line)
STRING file;
int line;
{
	printf("FATAL ERROR: %s: line %d\n", file, line);
	exit(1);
}
