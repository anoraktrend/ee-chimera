#ifndef EE_H
#define EE_H

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <pwd.h>

#if defined(HAS_NCURSESW) || defined(HAS_NCURSES)
#include <curses.h>
#else
#include <ncurses.h>
#endif

#ifdef HAS_ICU
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/ures.h>
#include <unicode/uloc.h>
#include <unicode/utypes.h>
#include <unicode/utf8.h>
#endif
#ifdef HAS_TREESITTER
#include <tree_sitter/api.h>
#endif
#ifdef HAS_LIBEDIT
#include <histedit.h>
#endif
#include <unistd.h>

#include "ee_version.h"

#ifndef nullptr
#define nullptr NULL
#endif

#define MAX_INIT_STRINGS 32
#define MAX_EMACS_HELP_LINES 22
#define MIN_LINE_ALLOC 128
#define MAX_WORD_LEN 1024
#define MAX_IN_STRING 1024
#define MAX_ALPHA_CHAR 52

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define TAB 9

extern bool profiling_mode;

typedef void (*control_handler)(void);

struct text {
  unsigned char *line;
  int line_length;
  int max_length;
  int line_number;
  struct text *next_line;
  struct text *prev_line;
};

struct menu_entries {
  char *item_string;
  int (*procedure)(struct menu_entries *);
  struct menu_entries *ptr_menu;
  int (*procedure2)(int);
  void (*procedure3)(void);
  int value;
};

struct command_map {
  const char *name;
  void (*handler)(void);
  const char *desc;
  const char *short_desc;
};

struct files {
  unsigned char *name;
  struct files *next_name;
};

/*
 | Function Prototypes
 */
int quit_wrapper(int arg);
int file_op_wrapper(int arg);
int search_wrapper(int arg);
#ifdef HAS_MENU
int menu_op_wrapper(struct menu_entries *m);
#endif
void no_op(void);

#define ee_wmove(w, y, x) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      wmove(w, y, x); \
    } \
  } while (0)

#define ee_wclrtoeol(w) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      wclrtoeol(w); \
    } \
  } while (0)

#define ee_wrefresh(w) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      wrefresh(w); \
    } \
  } while (0)

#define ee_werase(w) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      werase(w); \
    } \
  } while (0)

#define ee_waddstr(w, s) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      waddstr(w, s); \
    } \
  } while (0)

#define ee_waddch(w, c) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      waddch(w, c); \
    } \
  } while (0)

#define ee_idlok(w, b) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      idlok(w, b); \
    } \
  } while (0)

#define ee_keypad(w, b) \
  do { \
    if (!profiling_mode && (w) != nullptr) { \
      keypad(w, b); \
    } \
  } while (0)

#define ee_wprintw(w, ...)                                                     \
  do {                                                                         \
    if (!profiling_mode && (w) != nullptr) {                                   \
      wprintw(w, __VA_ARGS__);                                                 \
    }                                                                          \
  } while (0)

#define ee_wdeleteln(w)                                                        \
  do {                                                                         \
    if (!profiling_mode && (w) != nullptr) {                                   \
      wdeleteln(w);                                                            \
    }                                                                          \
  } while (0)

#define ee_winsertln(w)                                                        \
  do {                                                                         \
    if (!profiling_mode && (w) != nullptr) {                                   \
      winsertln(w);                                                            \
    }                                                                          \
  } while (0)

#define ee_wclrtobot(w)                                                        \
  do {                                                                         \
    if (!profiling_mode && (w) != nullptr) {                                   \
      wclrtobot(w);                                                            \
    }                                                                          \
  } while (0)

extern control_handler base_control_table[1024];
extern control_handler gold_control_table[1024];
extern control_handler emacs_control_table[1024];

#ifdef HAS_LSP
struct diagnostic {
  int line;
  int col;
  char *message;
  struct diagnostic *next;
};
extern struct diagnostic *diagnostics_list;
#endif

extern struct menu_entries modes_menu[];
extern struct menu_entries config_dump_menu[];
extern struct menu_entries leave_menu[];
extern struct menu_entries file_menu[];
extern struct menu_entries search_menu[];
extern struct menu_entries spell_menu[];
extern struct menu_entries misc_menu[];
extern struct menu_entries main_menu[];

#ifdef HAS_TREESITTER
extern TSParser *ts_parser;
extern TSTree *ts_tree;
#endif

#ifdef HAS_LIBEDIT
extern EditLine *el;
extern History *hist;
#endif

extern char *help_text[];
extern char *control_keys[5];
extern char *gold_control_keys[5];
extern char *emacs_help_text[];
extern char *emacs_control_keys[5];
extern char *command_strings[5];
extern char *commands[32];
extern char *init_strings[32];
extern char *mode_strings[12];

enum { NUM_MODES_ITEMS = 11 };
enum { READ_FILE = 1, WRITE_FILE = 2, SAVE_FILE = 3 };
enum { MENU_WARN = 1 };
enum { CONTROL_KEYS = 0, COMMANDS = 1 };
enum { BASE_TABLE = 0, GOLD_TABLE = 1, EMACS_TABLE = 2 };

#define ASCII_DEL 127
#define ASCII_BACKSPACE 8

extern struct text *curr_line;
extern struct text *first_line;
extern struct text *dlt_line;
extern unsigned char *point;
extern unsigned char *srch_str;
extern unsigned char *d_char;
extern unsigned char *d_word;
extern unsigned char *d_line;
extern int position;
extern int scr_pos;
extern int scr_vert;
extern int scr_horz;
extern int absolute_lin;
extern int horiz_offset;
extern int last_line;
extern int last_col;
extern int info_type;
extern int right_margin;
extern int info_win_height;
extern bool edit;
extern bool gold;
extern bool case_sen;
extern bool info_window;
extern bool expand_tabs;
extern bool observ_margins;
extern bool text_changes;
extern bool clear_com_win;
extern bool recv_file;
extern bool ee_chinese;
extern bool eightbit;
extern bool nohighlight;
extern bool restricted;
extern char *in_file_name;
extern char *print_command;
extern FILE *bit_bucket;
extern WINDOW *com_win;
extern WINDOW *text_win;
extern WINDOW *help_win;
extern WINDOW *info_win;

extern char *com_win_message;
extern char *no_file_string;
extern char *ascii_code_str;
extern char *printer_msg_str;
extern char *command_str;
extern char *file_write_prompt_str;
extern char *file_read_prompt_str;
extern char *char_str;
extern char *unkn_cmd_str;
extern char *non_unique_cmd_msg;
extern char *line_num_str;
extern char *line_len_str;
extern char *current_file_str;
extern char *usage0;
extern char *usage1;
extern char *usage2;
extern char *usage3;
extern char *usage4;
extern char *file_is_dir_msg;
extern char *new_file_msg;
extern char *cant_open_msg;
extern char *open_file_msg;
extern char *file_read_fin_msg;
extern char *reading_file_msg;
extern char *read_only_msg;
extern char *file_read_lines_msg;
extern char *save_file_name_prompt;
extern char *file_not_saved_msg;
extern char *changes_made_prompt;
extern char *yes_char;
extern char *file_exists_prompt;
extern char *create_file_fail_msg;
extern char *writing_file_msg;
extern char *file_written_msg;
extern char *searching_msg;
extern char *str_not_found_msg;
extern char *search_prompt_str;
extern char *exec_err_msg;
extern char *continue_msg;
extern char *menu_cancel_msg;
extern char *menu_size_err_msg;
extern char *press_any_key_msg;
extern char *shell_prompt;
extern char *formatting_msg;
extern char *shell_echo_msg;
extern char *spell_in_prog_msg;
extern char *margin_prompt;
extern char *restricted_msg;
extern char *STATE_ON;
extern char *STATE_OFF;
extern char *HELP;
extern char *WRITE;
extern char *READ;
extern char *LINE;
extern char *FILE_str;
extern char *CHARACTER;
extern char *REDRAW;
extern char *RESEQUENCE;
extern char *AUTHOR;
extern char *VERSION;
extern char *CASE;
extern char *NOCASE;
extern char *EXPAND;
extern char *NOEXPAND;
extern char *Exit_string;
extern char *QUIT_string;
extern char *INFO;
extern char *NOINFO;
extern char *MARGINS;
extern char *NOMARGINS;
extern char *AUTOFORMAT;
extern char *NOAUTOFORMAT;
extern char *ECHO;
extern char *PRINTCOMMAND;
extern char *RIGHTMARGIN;
extern char *HIGHLIGHT;
extern char *NOHIGHLIGHT;
extern char *EIGHTBIT;
extern char *NOEIGHTBIT;
extern char *EMACS_string;
extern char *NOEMACS_string;
extern char *VI_string;
extern char *NOVI_string;
extern char *BIND;
extern char *GBIND;
extern char *EBIND;
extern char *ree_no_file_msg;
extern char *menu_too_lrg_msg;
extern char *more_above_str;
extern char *more_below_str;
extern char *chinese_cmd;
extern char *nochinese_cmd;

void strings_init(void);
void ee_init(void);
void echo_string(char *string);
bool compare(char *string1, char *string2, bool sensitive);
char *resolve_name(const char *name);
int write_file(char *file_name, bool warn_if_exists);
char *get_token(char *string, char *substring);
char *catgetlocal(const char *key, char *string);
void *next_word(void *string);
void draw_screen(void);
void get_file(const char *file_name);
void get_line(int length, unsigned char *in_string, int *append);
void insert(int character);
void delete_char_at_cursor(int no_verify);
void bol(void);
void eol(void);
void top(void);
void bottom(void);
void right(int no_verify);
void left(int no_verify);
void up(void);
void down(void);
void nextline(void);
void prevline(void);
void del_char(void);
void prev_word(void);
void adv_word(void);
void insert_line(int no_verify);
void control_up(void);
void control_down(void);
void control_next_page(void);
void control_prev_page(void);
void control_newline(void);
void control_backspace(void);
void control_search(void);
void control_copy(void);
void control_cut(void);
void control_insert_ascii(void);
void set_mark(void);
void paste_region(void);
void del_word(void);
void del_line(void);
void undel_char(void);
void undel_word(void);
void undel_line(void);
void search_prompt(void);
void replace_prompt(void);
void command_prompt(void);
void gold_toggle(void);
void gold_append(void);
void gold_search_reverse(void);
void redraw(void);
void help_op(void);
void menu_op_call(void);
void modes_op(void);
void resize_info_win(void);
void update_help_strings(void);
void command(char *cmd_str);
char *get_string(char *prompt, int advance);
void edit_abort(int arg);
void cleanup(void);
int unique_test(char *string, char *list[]);
void get_options(int argc, char *argv[]);
void set_up_term(void);
void resize_check(void);
void paint_info_win(void);
int menu_op(struct menu_entries menu_list[]);
void sh_command(const char *string);
void check_fp(void);
bool restrict_mode(void);
void dump_ee_conf(void);
struct text *txtalloc(void);

void help(void);
void Format(void);
int quit(int noverify);
int file_op(int arg);
int search(int display_message);
int len_char(int character, int column);
int out_char(WINDOW *window, int character, int column);
void midscreen(int line, unsigned char *pnt);
void draw_line(int vertical, int horiz, struct text *line, int t_pos);
void scanline(const unsigned char *pos);
int tabshift(int column);
void adv_line(void);
void delete_text(void);
void paint_menu(struct menu_entries menu_list[], int max_width,
                int max_height, int list_size, int top_offset, WINDOW *window,
                int menu_top, int menu_left);
void copy_region(bool cut);
void append_region(bool cut);
void move_rel(int direction, int lines);
void control(void);
void emacs_control(void);
void vi_command(int c);
void function_key(void);
void goto_line(char *cmd_str);
int search_reverse(int display_message);
int scanline_step(unsigned char *ptr, const unsigned char *pos, int temp);

#ifdef HAS_ICU
extern UResourceBundle *icu_bundle;
static int u_char_width(UChar32 c, int column);
#endif

#ifdef HAS_LSP
void lsp_start(void);
void lsp_poll(void);
void lsp_change_file(const char *filename);
void lsp_open_file(const char *filename);
#endif

#endif /* EE_H */
