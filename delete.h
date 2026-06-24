#ifndef EE_DELETE_H
#define EE_DELETE_H

#include "ee.h"

extern struct text *dlt_line;
extern struct text *mark_line;
extern int mark_position;
extern char *clipboard_buf;
extern int d_wrd_len;
extern unsigned char *d_char;
extern unsigned char *d_word;
extern unsigned char *d_line;

void update_line_numbers(struct text *line, int delta);
void delete_char_at_cursor(int disp);
void delete_text(void);
void set_mark(void);
void copy_region(bool cut);
void paste_region(void);
void append_region(bool cut);
void del_char(void);
void undel_char(void);
void del_word(void);
void undel_word(void);
void del_line(void);
void undel_line(void);

#endif
