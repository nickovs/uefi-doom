/* Minimal <ctype.h> for the UEFI build: ASCII only ("C" locale). */
#ifndef UD_LIBC_CTYPE_H
#define UD_LIBC_CTYPE_H

int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

#endif
