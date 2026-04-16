#ifndef EE_H
#define EE_H

#define _GNU_SOURCE
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "ee_version.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <ncurses.h>
#include <nl_types.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <tree_sitter/api.h>
#include <unistd.h>

#ifndef nullptr
#define nullptr NULL
#endif

/*
 |	Structure for text lines
 */
struct text {
  unsigned char *line;    /* line of characters		*/
  int line_number;        /* line number			*/
  int line_length;        /* actual number of characters in the line */
  int max_length;         /* maximum number of characters the line handles */
  struct text *next_line; /* next line of text		*/
  struct text *prev_line; /* previous line of text	*/
} __attribute__((packed)) __attribute__((aligned(64)));

/*
 |	Structure for file stack
 */
struct files {         /* structure to store names of files to be edited*/
  unsigned char *name; /* name of file				*/
  struct files *next_name;
} __attribute__((aligned(16)));

/*
 |	LSP Diagnostic structure
 */
struct diagnostic {
  int line;
  int col;
  char *message;
  struct diagnostic *next;
} __attribute__((aligned(32)));

/*
 |	Structure for menu entries
 */
struct menu_entries {
  char *item_string;
  int (*procedure)(struct menu_entries *);
  struct menu_entries *ptr_argument;
  int (*iprocedure)(int);
  void (*nprocedure)();
  int argument;
} __attribute__((packed)) __attribute__((aligned(64)));

/*
 |	Function Prototypes
 */
static void lsp_change_file(const char *filename);
static int get_node_attribute(int line, int col);
static void finish(void);
int quit(int noverify);
int file_op(int arg);
int search(int display_message);
bool Blank_Line(struct text *test_line);
static void search_prompt(void);
static void spell_op(void);
static void ispell_op(void);
static void Format(void);
static void shell_op(void);
int menu_op(struct menu_entries menu_list[]);
static void leave_op(void);
static void help(void);
void redraw(void);
static void modes_op(void);
static void dump_ee_conf(void);
static void strings_init(void);
static void ee_init(void);
static void set_up_term(void);
static void get_options(int numargs, char *arguments[]);
static void check_fp(void);
static void edit_abort(int arg);
void delete_text(void);
static void lsp_start(void);
static void lsp_poll(void);
static void reparse(void);
static void print_buffer(void);
struct text *txtalloc(void);
bool restrict_mode(void);
void resize_check(void);
void function_key(void);
static void insert(int character);
void delete_char_at_cursor(int disp);
void emacs_control(void);
void control(void);
int len_char(int character, int column);
static void Auto_Format(void);
int out_char(WINDOW *window, int character, int column);
static void prev_word(void);
void right(int disp);
void insert_line(int disp);
void midscreen(int line, unsigned char *pnt);
static void draw_line(int vertical, int horiz, struct text *line, int t_pos);
void scanline(const unsigned char *pos);
void left(int disp);
int tabshift(int temp_int);
char *get_string(char *prompt, int advance);
void bottom(void);
void command_prompt(void);
void down(void);
void undel_char(void);
void bol(void);
void del_char(void);
void del_word(void);
void undel_word(void);
void del_line(void);
void undel_line(void);
void move_rel(int direction, int lines);
void eol(void);
void top(void);
void up(void);
void adv_word(void);
void paint_info_win(void);
void adv_line(void);
void command(char *cmd_str1);
static void goto_line(char *cmd_str);
void sh_command(const char *string);
int get_string_len(char *line, int offset, int column);
int unique_test(char *string, char *list[]);
int from_top(struct text *test_line);
void echo_string(char *string);
bool compare(char *string1, char *string2, bool sensitive);
char *resolve_name(const char *name);
int write_file(char *file_name, bool warn_if_exists);
char *get_token(char *string, char *substring);
char *catgetlocal(int number, char *string);
void *next_word(void *string);
void draw_screen(void);
void get_file(const char *file_name);
void get_line(int length, unsigned char *in_string, int *append);

/*
 |	Safe string copy
 */
static ssize_t strscpy(char *dest, const char *src, size_t count);

static void paint_menu(struct menu_entries menu_list[], int max_width,
                       int max_height, int list_size, int top_offset,
                       WINDOW *menu_win, int off_start, int vert_size);

/* | New Operations */
void set_mark(void);
void copy_region(bool cut);
void append_region(bool cut);
void paste_region(void);
void replace_prompt(void);
int search_reverse(int display_message);
#endif /* EE_H */
