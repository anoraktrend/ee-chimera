/*
 * Editor state declarations for ee (easy editor)
 */

#ifndef STATE_H
#define STATE_H

#include "ee.h"

// Global state
extern struct text *first_line; /* first line of current buffer        */
extern struct text *curr_line;  /* current line cursor is on        */
extern struct text *tmp_line;   /* temporary line pointer        */
extern struct files *top_of_stack;
extern undo_buffer undo_state;

extern int position;     /* offset in bytes from begin of line    */
extern int scr_pos;      /* horizontal position            */
extern int scr_vert;     /* vertical position on screen        */
extern int scr_horz;     /* horizontal position on screen        */
extern int absolute_lin; /* number of lines from top        */
extern int tmp_vert, tmp_horz;
extern bool edit;                    /* continue executing while true    */
extern bool gold;                    /* 'gold' function key pressed        */
extern int last_line;                /* last line for text display        */
extern int last_col;                 /* last column for text display        */
extern int horiz_offset;             /* offset from left edge of text    */
extern bool clear_com_win;           /* flag to indicate com_win needs clearing */
extern bool text_changes;            /* indicate changes have been made to text */
extern bool info_window;             /* flag to indicate if help window visible */
extern int info_type;                /* flag to indicate type of info to display */
extern bool expand_tabs;             /* flag for expanding tabs        */
extern bool formatted;
extern bool pasting_mode;
extern bool formatting_in_progress;
extern bool profiling_mode;           /* flag indicating paragraph formatted    */
#ifdef HAS_AUTOFORMAT
extern bool auto_format;               /* flag for auto_format mode        */
#endif
extern bool restricted;                /* flag to indicate restricted mode    */
extern bool undo_enabled;
extern char theme_name[128];
extern bool eightbit;                 /* eight bit character flag        */
extern int local_LINES;              /* copy of LINES, to detect when win resizes */
extern int local_COLS;               /* copy of COLS, to detect when win resizes  */
extern bool curses_initialized;      /* flag indicating if curses has been started*/
extern bool emacs_keys_mode;         /* mode for if emacs key binings are used    */
extern bool vi_keys_mode;
extern bool vi_insert_mode;
extern bool ee_chinese;               /* allows handling of multi-byte characters  */

// Global variables
extern unsigned char *point; /* points to current position in line    */
extern char *print_command;         /* string to use for the print command    */
extern char *start_at_line;          /* move to this line at start of session*/
extern int in;                       /* input character            */
extern char *const table[];

extern WINDOW *com_win;
extern WINDOW *text_win;
extern WINDOW *help_win;
extern WINDOW *info_win;

// Function declarations
void bottom(void);
void top(void);
void nextline(void);
void prevline(void);
void left(int disp);
void right(int disp);
void find_pos(void);
void bol(void);
void eol(void);
void resize_check(void);

#endif // STATE_H