/*
 * Input handling declarations for ee (easy editor)
 */

#ifndef INPUT_H
#define INPUT_H

#include "ee.h"

// Function declarations
void control(void);
void emacs_control(void);
void prev_word(void);
void adv_word(void);
void vi_command(int c);
void function_key(void);
void bind_key(const char *key_str, const char *cmd_name, int table_type);
void gold_toggle(void);
void no_op(void);

void control_adv_word(void);
void control_backspace(void);
void control_bol(void);
void control_bottom(void);
void control_command_prompt(void);
void control_copy(void);
void control_cut(void);
void control_del_char(void);
void control_del_line(void);
void control_del_word(void);
void control_down(void);
void control_eol(void);
void control_esc(void);
void control_format(void);
void control_gold_esc(void);
void control_gold_toggle(void);
void control_help(void);
void control_insert_ascii(void);
void control_left(void);
void control_mark(void);
void control_newline(void);
void control_next_page(void);
void control_paste(void);
void control_prev_page(void);
void control_prev_word(void);
void control_redraw(void);
void control_replace_prompt(void);
void control_right(void);
void control_search(void);
void control_search_prompt(void);
void control_top(void);
void control_und_char(void);
void control_und_line(void);
void control_und_word(void);
void control_up(void);
void gold_append(void);
void gold_search_reverse(void);

// Control handler tables
extern control_handler base_control_table[1024];
extern control_handler gold_control_table[1024];
extern control_handler emacs_control_table[1024];
extern struct command_map commands_table[];

#endif // INPUT_H