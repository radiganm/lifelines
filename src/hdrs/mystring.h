/* String routines that know about ISO Latin 1 8 bit letters.
 * To be used with LifeLines genealogy program.
 * Copyright(c) 1996 Hannu V�is�nen; all rights reserved.
*/

#ifndef MY_STRING_H
#define MY_STRING_H

void appendstr(char ** pdest, int * len, const char * src);
void appendstrf(char ** pdest, int * len, const char * fmt,...);
#define llstrcatn(dest, src, len) appendstr(dest, len, src)
char *llstrncpy(char *dest, const char *src, size_t n);
int my_chrcmp(const int s1, const int s2);
int my_isalpha(const int c);
int my_iscntrl(const int c);
int my_islower(const int c);
int my_isprint(const int c);
int my_isupper(const int c);
int my_tolower(const int c);
int my_toupper(const int c);
int my_strcmp(const char *s1, const char *s2, const int cmp_table[]);
int my_strncmp(const char *s1,
                const char *s2,
                const int n,
                const int cmp_table[]);
void stdstring_hardfail(void);
void vappendstrf(char ** pdest, int * len, const char * fmt, va_list args);


extern const int my_ISO_Latin1_Finnish[];


/* Change this if you want to change the sort order. */
#define MY_SORT_TABLE my_ISO_Latin1_Finnish

#define MY_STRCMP(s1,s2) \
	my_strcmp((s1), (s2), MY_SORT_TABLE)
#define MY_STRNCMP(s1,s2,n) \
	my_strncmp((s1), (s2), (n), MY_SORT_TABLE)


#endif
