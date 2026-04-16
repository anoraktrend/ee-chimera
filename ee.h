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
#ifdef HAS_NCURSESW
#  if __has_include(<ncursesw/ncurses.h>)
#    include <ncursesw/ncurses.h>
#  else
#    include <ncurses.h>
#  endif
#elif defined(HAS_NCURSES)
#  include <ncurses.h>
#else
#  include <curses.h>
#endif
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
#ifdef HAS_ICU
#include <unicode/ures.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>
#endif
#ifdef HAS_TREESITTER
#include <tree_sitter/api.h>
#endif
#include <unistd.h>

#ifndef nullptr
#define nullptr NULL
#endif

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

enum { TAB = 9 };
#define max(a, b)                                                              \
  ({                                                                           \
    typeof(a) _a = (a);                                                        \
    typeof(b) _b = (b);                                                        \
    (_a & -(_a > _b)) | (_b & -(_b >= _a));                                    \
  })
#define min(a, b)                                                              \
  ({                                                                           \
    typeof(a) _a = (a);                                                        \
    typeof(b) _b = (b);                                                        \
    (_a & -(_a < _b)) | (_b & -(_b <= _a));                                    \
  })

constexpr int MAX_IN_STRING = 513;
constexpr int MAX_WORD_LEN = 150;
constexpr int MIN_LINE_ALLOC = 10;
constexpr int MAX_CMD_LEN = 256;
constexpr int TAB_WIDTH = 8;
constexpr int ASCII_DEL = 127;
constexpr int ASCII_BACKSPACE = 8;
constexpr int OFFSET_STEP = 8;
constexpr int BUFFER_SIZE = 512;
constexpr int G_STRING_BUF = 512;
constexpr int MAX_HELP_LINES = 23;
constexpr int MAX_EMACS_HELP_LINES = 22;
constexpr int MAX_INIT_STRINGS = 22;
constexpr int MAX_ALPHA_CHAR = 36;

/*
 |	defines for type of data to show in info window
 */

enum { CONTROL_KEYS = 1, COMMANDS = 2 };

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
#ifdef HAS_LSP
struct diagnostic {
  int line;
  int col;
  char *message;
  struct diagnostic *next;
} __attribute__((aligned(32)));
#endif

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

struct command_map {
  const char *name;
  void (*handler)(void);
  const char *description;
  const char *short_desc;
};

extern struct command_map commands_table[];

/* Control Key Definitions */
#ifdef HAS_MENU
#define K_MENU "Esc menu"
#define GK_MENU "Esc menu"
#else
#define K_MENU ""
#define GK_MENU "Esc exit"
#endif
#define K_PREV_PAGE "^P prev page"
#define K_DEL_CHAR "^K del char"
#define K_END_LINE "^O end of lin"
#define K_ADV_WORD "^Y adv word"
#define K_RETURN "^J carrg rtrn"
#define K_COMMAND "^E command"
#define K_DEL_LINE "^L del line"
#define K_MARK "^U mark"
#define K_REPLACE "^Z replace"
#define K_TOP_TEXT "^T top of txt"
#define K_SEARCH "^F search"
#define K_CUT "^X cut"
#define K_GOLD "^G GOLD"
#define K_BOTTOM_TEXT "^B end of txt"
#define K_DEL_WORD "^W del word"
#define K_COPY "^C copy"
#define K_ADV_CHAR "^A adv char"
#define K_NEXT_PAGE "^N next page"
#define K_BEG_LINE "^D beg of lin"
#define K_PASTE "^V paste"
#define K_REDRAW "^R redraw"

#define GK_PREV_BUFF "^P prev buff"
#define GK_FORWARD "^V forward"
#define GK_REVERSE "^R reverse"
#define GK_FMT_PARAG "^X fmt parag"
#define GK_SEARCH "^E search"
#define GK_UND_WORD "^W und word"
#define GK_UND_LINE "^K und line"
#define GK_REPL_PROMPT "^Z repl prmpt"
#define GK_APPEND "^B append"
#define GK_UND_CHAR "^K und char"

/* User Definable Controls Logic */
#define KEY_CTRL_A 1
#define KEY_CTRL_B 2
#define KEY_CTRL_C 3
#define KEY_CTRL_D 4
#define KEY_CTRL_E 5
#define KEY_CTRL_F 6
#define KEY_CTRL_G 7
#define KEY_CTRL_H 8
#define KEY_CTRL_I 9
#define KEY_CTRL_J 10
#define KEY_CTRL_K 11
#define KEY_CTRL_L 12
#define KEY_CTRL_M 13
#define KEY_CTRL_N 14
#define KEY_CTRL_O 15
#define KEY_CTRL_P 16
#define KEY_CTRL_Q 17
#define KEY_CTRL_R 18
#define KEY_CTRL_S 19
#define KEY_CTRL_T 20
#define KEY_CTRL_U 21
#define KEY_CTRL_V 22
#define KEY_CTRL_W 23
#define KEY_CTRL_X 24
#define KEY_CTRL_Y 25
#define KEY_CTRL_Z 26
#define KEY_CTRL_LSQB 27
#define KEY_CTRL_BSLASH 28
#define KEY_CTRL_RSQB 29
#define KEY_CTRL_CARET 30
#define KEY_CTRL_UNDERSCORE 31

/* Table Types */
enum { BASE_TABLE = 0, GOLD_TABLE = 1, EMACS_TABLE = 2 };

/* Global variable declarations */

#ifdef HAS_ICU
extern UResourceBundle *icu_bundle;
#endif

extern struct text *first_line;
extern struct text *curr_line;
extern unsigned char *point;
extern char *in_file_name;
extern bool text_changes;
extern int scr_vert, scr_horz, scr_pos;
extern int last_line, last_col;
extern int horiz_offset;
extern bool info_window;
extern int absolute_lin;
extern bool case_sen;
extern bool expand_tabs;
extern int right_margin;
extern bool observ_margins;
extern bool eightbit;
extern bool restricted;
extern bool nohighlight;
extern bool emacs_keys_mode;
extern bool ee_chinese;
extern char *print_command;
extern unsigned char *d_char;
extern unsigned char *d_word;
extern unsigned char *d_line;
extern struct text *dlt_line;
extern WINDOW *text_win, *com_win, *help_win, *info_win;

#ifdef HAS_AUTOFORMAT
extern bool auto_format;
#endif

#ifdef HAS_LSP
extern int lsp_to_child[2];
extern int lsp_from_child[2];
extern pid_t lsp_pid;
extern struct diagnostic *diagnostics_list;
#endif

#ifdef HAS_TREESITTER
extern TSParser *ts_parser;
extern TSTree *ts_tree;
#endif

extern struct menu_entries modes_menu[];
extern struct menu_entries config_dump_menu[];
extern struct menu_entries leave_menu[];
extern struct menu_entries file_menu[];
extern struct menu_entries search_menu[];
extern struct menu_entries spell_menu[];
extern struct menu_entries misc_menu[];
extern struct menu_entries main_menu[];

extern char *help_text[MAX_HELP_LINES];
extern char *control_keys[5];
extern char *gold_control_keys[5];
extern char *emacs_help_text[MAX_EMACS_HELP_LINES];
extern char *emacs_control_keys[5];
extern char *command_strings[5];
extern char *commands[32];
extern char *init_strings[MAX_INIT_STRINGS];
extern char *mode_strings[11];

enum { NUM_MODES_ITEMS = 10 };
enum { READ_FILE = 1, WRITE_FILE = 2, SAVE_FILE = 3 };
enum { MENU_WARN = 1 };

/* Localization strings */
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
extern char *usage0, *usage1, *usage2, *usage3, *usage4;
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
extern char *spell_in_prog_msg;
extern char *press_any_key_msg;
extern char *menu_too_lrg_msg;
extern char *more_above_str;
extern char *more_below_str;
extern char *spell_check_msg;
extern char *spell_command;
extern char *ispell_command;
extern char *shell_echo_msg;
extern char *menu_cancel_msg;
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
extern char *Echo;
extern char *PRINTCOMMAND;
extern char *RIGHTMARGIN;
extern char *HIGHLIGHT;
extern char *NOHIGHLIGHT;
extern char *EIGHTBIT;
extern char *NOEIGHTBIT;
extern char *EMACS_string;
extern char *NOEMACS_string;
extern char *conf_dump_err_msg;
extern char *conf_dump_success_msg;
extern char *conf_not_saved_msg;
extern char *ree_no_file_msg;
extern char *cancel_string;
extern char *chinese_cmd;
extern char *nochinese_cmd;
extern char const *separator;

extern char *table[];
extern char item_alpha[];
/*
 |	Function Prototypes
 */
int quit_wrapper(int arg);
int file_op_wrapper(int arg);
int search_wrapper(int arg);
int menu_op_wrapper(struct menu_entries *m);
void no_op(void);
typedef void (*control_handler)(void);
extern control_handler base_control_table[];
extern control_handler gold_control_table[];
extern control_handler emacs_control_table[];
#ifdef HAS_LSP
static void lsp_change_file(const char *filename);
static void lsp_start(void);
static void lsp_poll(void);
#endif
static int get_node_attribute(int line, int col);
static void finish(void);
int quit(int noverify);
int file_op(int arg);
int search(int display_message);
bool Blank_Line(struct text *test_line);
static void search_prompt(void);
#ifdef HAS_SPELL
static void spell_op(void);
static void ispell_op(void);
#endif
#ifdef HAS_AUTOFORMAT
static void Format(void);
static void Auto_Format(void);
#endif
static void shell_op(void);
#ifdef HAS_MENU
int menu_op(struct menu_entries menu_list[]);
static void paint_menu(struct menu_entries menu_list[], int max_width,
                       int max_height, int list_size, int top_offset,
                       WINDOW *menu_win, int off_start, int vert_size);
#endif
static void leave_op(void);
#ifdef HAS_HELP
static void help(void);
#endif
void resize_info_win(void);
void bind_key(const char *key_str, const char *cmd_name, int table_type);
void redraw(void);
static void modes_op(void);
static void dump_ee_conf(void);
static void strings_init(void);
void ee_init(void);
static void set_up_term(void);
static void get_options(int numargs, char *arguments[]);
static void check_fp(void);
static void edit_abort(int arg);
void delete_text(void);
#ifdef HAS_TREESITTER
static void reparse(void);
#endif
static void print_buffer(void);
struct text *txtalloc(void);
void nextline(void);
void prevline(void);
bool restrict_mode(void);
void resize_check(void);
void function_key(void);
static void insert(int character);
void delete_char_at_cursor(int disp);
void emacs_control(void);
void control(void);
int len_char(int character, int column);
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

/* | New Operations */
void set_mark(void);
void copy_region(bool cut);
void append_region(bool cut);
void paste_region(void);
void replace_prompt(void);
int search_reverse(int display_message);
#endif /* EE_H */
