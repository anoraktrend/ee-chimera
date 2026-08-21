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

// Control handler tables
extern control_handler base_control_table[1024];
extern control_handler gold_control_table[1024];
extern control_handler emacs_control_table[1024];

#endif // INPUT_H