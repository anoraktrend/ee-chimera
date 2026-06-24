#ifndef EE_MENU_H
#define EE_MENU_H

#include "ee.h"

extern bool nohighlight;
extern struct menu_entries modes_menu[];
extern struct menu_entries config_dump_menu[];
extern struct menu_entries leave_menu[];
extern struct menu_entries misc_menu[];
extern struct menu_entries main_menu[];
extern char *mode_strings[12];

int menu_op(struct menu_entries menu_list[]);
void paint_menu(struct menu_entries menu_list[], int max_width,
                int max_height, int list_size, int top_offset,
                WINDOW *menu_win, int off_start, int vert_size);
void modes_op(void);
void theme_select_op(void);

#endif
