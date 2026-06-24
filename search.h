#ifndef EE_SEARCH_H
#define EE_SEARCH_H

#include "ee.h"

extern struct text *srch_line;
extern bool case_sen;
extern unsigned char *srch_str;
extern unsigned char *u_srch_str;
extern unsigned char *srch_1;
extern unsigned char *srch_2;
extern unsigned char *srch_3;

bool compare(char *string1, char *string2, bool sensitive);
int search(int display_message);
void search_prompt(void);
void replace_prompt(void);
int search_reverse(int display_message);

#endif
