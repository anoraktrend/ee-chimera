/*
 |	ee (easy editor)
 |
 |	An easy to use, simple screen oriented editor.
 |
 |	written by Hugh Mahon
 |
 |
 |      Copyright (c) 2009, Hugh Mahon
 |      All rights reserved.
 |
 |      Redistribution and use in source and binary forms, with or without
 |      modification, are permitted provided that the following conditions
 |      are met:
 |
 |          * Redistributions of source code must retain the above copyright
 |            notice, this list of conditions and the following disclaimer.
 |          * Redistributions in binary form must reproduce the above
 |            copyright notice, this list of conditions and the following
 |            disclaimer in the documentation and/or other materials provided
 |            with the distribution.
 |
 |      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 |      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 |      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 |      FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 |      COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 |      INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 |      BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 |      LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 |      CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 |      LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 |      ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 |      POSSIBILITY OF SUCH DAMAGE.
 |
 |     -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
 |
 |	This editor was purposely developed to be simple, both in
 |	interface and implementation.  This editor was developed to
 |	address a specific audience: the user who is new to computers
 |	(especially UNIX).
 |
 |	ee is not aimed at technical users; for that reason more
 |	complex features were intentionally left out.  In addition,
 |	ee is intended to be compiled by people with little computer
 |	experience, which means that it needs to be small, relatively
 |	simple in implementation, and portable.
 |
 |	This software and documentation contains
 |	proprietary information which is protected by
 |	copyright.  All rights are reserved.
 |
 |	$Header: /home/hugh/sources/old_ae/RCS/ee.c,v 1.104 2010/06/04 01:55:31
 hugh Exp hugh $
 |
 */

#define _GNU_SOURCE
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "ee.h"
#include "lsp.h"
#include "delete.h"
#include "search.h"
#include "format.h"
#include "menu.h"
#include "fileio.h"
#include "theme.h"

static_assert(MAX_WORD_LEN > 0, "MAX_WORD_LEN must be positive");
static_assert(MAX_IN_STRING > 0, "MAX_IN_STRING must be positive");
static_assert(MIN_LINE_ALLOC > 0, "MIN_LINE_ALLOC must be positive");
static_assert(MAX_INIT_STRINGS == 32, "MAX_INIT_STRINGS must be 32");
static_assert(MAX_UNDO_STEPS > 0, "MAX_UNDO_STEPS must be positive");

void help(void);
void shell_op(void);
void leave_op(void);
void spell_op(void);
void ispell_op(void);
void print_buffer(void);
int quit(int noverify);
int file_op(int arg);
void redraw(void);
int unique_test(char *string, char *list[]);
void command(char *cmd_str);
void set_up_term(void);
[[noreturn]] void edit_abort(int arg);
void cleanup(void);
void insert_line(int no_verify);
void bol(void);
void eol(void);
void top(void);
void bottom(void);
void right(int no_verify);
void left(int no_verify);
void prev_word(void);
void adv_word(void);
void command_prompt(void);
void gold_toggle(void);
void gold_append(void);
void gold_search_reverse(void);
void resize_info_win(void);
#ifdef HAS_ICU
[[maybe_unused]] static int u_char_width(UChar32 c, int column);
#endif
void insert(int character);
char *ee_copyright_message = "Copyright (c) 1986, 1990, 1991, 1992, 1993, "
                             "1994, 1995, 1996, 2009 Hugh Mahon ";

static char version[] = "@(#) ee, version " EE_VERSION " $Revision: 1.104 $";

// Correct prototypes for menu callbacks which expect int (*)(int) or int
// (*)(struct menu_entries *)
int quit_wrapper(int arg) {
  return quit(arg);
  return 0;
}
int file_op_wrapper(int arg) {
  return file_op(arg);
  return 0;
}
int search_wrapper(int arg) {
  return search(arg);
  return 0;
}
[[nodiscard]] int menu_op_wrapper(struct menu_entries *m) { return menu_op(m); }

/**
 * strscpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the source string to a destination buffer, up to a maximum
 * of count characters.
 *
 * The copy is guaranteed to be NUL-terminated, as long as count is
 * greater than zero.
 *
 * Returns the number of characters copied (not including the terminating
 * NUL) or -E2BIG if count is 0 or source was truncated.
 */
ssize_t strscpy(char *dest, const char *src, size_t count) {
  size_t res = 0;

  if (count == 0) {
    return -E2BIG;
  }

  while (res < count) {
    dest[res] = src[res];
    if (dest[res] == '\0') {
      return res;
    }
    res++;
  }

  /* Truncation occurred */
  dest[count - 1] = '\0';
  return -E2BIG;
}

// Tree-Sitter C language
struct text *first_line; /* first line of current buffer		*/
struct text *curr_line;  /* current line cursor is on		*/
struct text *tmp_line;   /* temporary line pointer		*/

struct files *top_of_stack = nullptr;

undo_buffer undo_state;

static constexpr int char_len_table[256] = {
    [0 ... 8] = 2,   [9] = -1,        [10 ... 31] = 2, [32 ... 126] = 1,
    [127] = 2,       [128 ... 255] = 1};

void cleanup(void);
const char *get_key_name(int i);
static const char *get_key_binding(control_handler handler,
                                   control_handler *table);

#ifdef HAS_TREESITTER
const TSLanguage *tree_sitter_c(void);

// Tree-Sitter Globals
#endif

#ifdef HAS_LIBEDIT
EditLine *el = nullptr;
History *hist = nullptr;

char *libedit_prompt(EditLine *e) {
  (void)e;
  return (char *)"";
}

int libedit_getc(EditLine *e, wchar_t *cp) {
  (void)e;
  int c = wgetch(com_win);
  if (c == ERR)
    return 0;
  *cp = (wchar_t)c;
  return 1;
}
#endif

// LSP Globals
#ifdef HAS_LSP

#endif


#ifdef HAS_LSP




#endif

#ifdef HAS_ICU
UResourceBundle *icu_bundle = nullptr;
#endif

int position;     /* offset in bytes from begin of line	*/
int scr_pos;      /* horizontal position			*/
int scr_vert;     /* vertical position on screen		*/
int scr_horz;     /* horizontal position on screen	*/
int absolute_lin; /* number of lines from top		*/
int tmp_vert, tmp_horz;
bool edit;                 /* continue executing while true	*/
bool gold;                 /* 'gold' function key pressed		*/
int last_line;             /* last line for text display		*/
int last_col;              /* last column for text display		*/
int horiz_offset = 0;      /* offset from left edge of text	*/
bool clear_com_win;        /* flag to indicate com_win needs clearing */
bool text_changes = false; /* indicate changes have been made to text */
bool info_window = true;   /* flag to indicate if help window visible */
int info_type =
    CONTROL_KEYS;               /* flag to indicate type of info to display */
bool expand_tabs = true; /* flag for expanding tabs		*/
bool formatted = false;
bool pasting_mode = false;
bool formatting_in_progress = false;
bool profiling_mode = false;   /* flag indicating paragraph formatted	*/
#ifdef HAS_AUTOFORMAT
bool auto_format = false; /* flag for auto_format mode		*/
#endif
bool restricted = false;  /* flag to indicate restricted mode	*/
bool undo_enabled = true;
char theme_name[128] = "";
bool eightbit = true;     /* eight bit character flag		*/
int local_LINES = 0;      /* copy of LINES, to detect when win resizes */
int local_COLS = 0;       /* copy of COLS, to detect when win resizes  */
bool curses_initialized =
    false; /* flag indicating if curses has been started*/
bool emacs_keys_mode =
    false;                      /* mode for if emacs key binings are used    */
bool vi_keys_mode = false;
bool vi_insert_mode = false;
bool ee_chinese = false; /* allows handling of multi-byte characters  */
                                /* by checking for high bit in a byte the    */
                                /* code recognizes a two-byte character      */
                                /* sequence				     */

unsigned char *point;      /* points to current position in line	*/
char *print_command = (char *)"lpr"; /* string to use for the print command 	*/
char *start_at_line = nullptr; /* move to this line at start of session*/
int in; /* input character			*/


static char *const table[] = {"^@", "^A", "^B", "^C", "^D",  "^E", "^F", "^G",
                        "^H", "\t", "^J", "^K", "^L",  "^M", "^N", "^O",
                        "^P", "^Q", "^R", "^S", "^T",  "^U", "^V", "^W",
                        "^X", "^Y", "^Z", "^[", "^\\", "^]", "^^", "^_"};

WINDOW *com_win;
WINDOW *text_win;
WINDOW *help_win;
WINDOW *info_win;

/*
 |	The following structure allows menu items to be flexibly declared.
 |	The first item is the string describing the selection, the second
 |	is the address of the procedure to call when the item is selected,
 |	and the third is the argument for the procedure.
 |
 |	For those systems with i18n, the string should be accompanied by a
 |	catalog number.  The 'int *' should be replaced with 'void *' on
 |	systems with that type.
 |
 |	The first menu item will be the title of the menu, with nullptr
 |	parameters for the procedure and argument, followed by the menu items.
 |
 |	If the procedure value is nullptr, the menu item is displayed, but no
 |	procedure is called when the item is selected.  The number of the
 |	item will be returned.  If the third (argument) parameter is -1, no
 |	argument is given to the procedure when it is called.
 */

#undef P_
/*
 |	allocate space here for the strings that will be in the menu
 */



#define MAX_INFO_LINES 12
char *dynamic_info_lines[MAX_INFO_LINES];
int num_info_lines = 0;



struct menu_entries search_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, 0},
    {"", nullptr, nullptr, nullptr, search_prompt, -1},
    {"", nullptr, nullptr, search_wrapper, nullptr, 1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries spell_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
#ifdef HAS_SPELL
    {"", nullptr, nullptr, nullptr, spell_op, -1},
    {"", nullptr, nullptr, nullptr, ispell_op, -1},
#endif
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};



char *help_text[23];
char *control_keys[5];
char *gold_control_keys[5];
char *emacs_help_text[22];
char *emacs_control_keys[5];

char *command_strings[5];
char *commands[32];
char *init_strings[32];

/*
 |	Declarations for strings for localization
 */

char *com_win_message; /* to be shown in com_win if no info window */
static time_t last_redraw_time = 0;
char *no_file_string;
char *ascii_code_str;
char *printer_msg_str;
char *command_str;
char *char_str;
char *unkn_cmd_str;
char *non_unique_cmd_msg;
char *line_num_str;
char *line_len_str;
char *current_file_str;
char *usage0;
char *usage1;
char *usage2;
char *usage3;
char *usage4;
char *searching_msg;
char *str_not_found_msg;
char *search_prompt_str;
char *exec_err_msg;
char *continue_msg;
char *menu_cancel_msg;
char *menu_size_err_msg;
char *press_any_key_msg;
char *shell_prompt;
char *formatting_msg;
char *shell_echo_msg;
char *spell_in_prog_msg;
char *margin_prompt;
char *restricted_msg;
char *STATE_ON;
char *STATE_OFF;
char *HELP;
char *MARK_str;
char *WRITE;
char *READ;
char *LINE;
char *FILE_str;
char *CHARACTER;
char *REDRAW;
char *RESEQUENCE;
char *AUTHOR;
char *VERSION;
char *CASE;
char *NOCASE;
char *EXPAND;
char *NOEXPAND;
char *Exit_string;
char *QUIT_string;
char *INFO;
char *NOINFO;
char *MARGINS;
char *NOMARGINS;
char *AUTOFORMAT;
char *NOAUTOFORMAT;
char *Echo;
char *PRINTCOMMAND;
char *RIGHTMARGIN;
char *HIGHLIGHT;
char *NOHIGHLIGHT;
char *EIGHTBIT;
char *NOEIGHTBIT;
char *EMACS_string;
char *NOEMACS_string;
char *VI_string;
char *NOVI_string;
char *BIND;
char *GBIND;
char *EBIND;
char *conf_dump_err_msg;
char *conf_dump_success_msg;
char *conf_not_saved_msg;
char *ree_no_file_msg;
char *cancel_string;
char *menu_too_lrg_msg;
char *more_above_str;
char *more_below_str;
char const *separator =
    " ^ = Ctrl key  ---- access HELP through menu ---"
    "============================================================"
    "===================";

char *chinese_cmd;
char *nochinese_cmd;

/* Control handler wrappers for jump tables */
void control_right(void) { right(1); }
void control_left(void) { left(1); }
void control_up(void) { up(); }
void control_down(void) { down(); }
static void control_bol(void) { bol(); }
static void control_eol(void) { eol(); }
void control_next_page(void) { nextline(); }
void control_prev_page(void) { prevline(); }
static void control_top(void) { top(); }
static void control_bottom(void) { bottom(); }
static void control_del_char(void) { delete_char_at_cursor(1); }
static void control_del_word(void) { del_word(); }
static void control_del_line(void) { del_line(); }
static void control_und_char(void) { undel_char(); }
void control_insert_ascii(void) {
  char *string = get_string(ascii_code_str, 1);
  if (*string != '\0') {
    in = atoi(string);
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    insert(in);
  }
  free(string);
}
static void control_und_word(void) { undel_word(); }
static void control_und_line(void) { undel_line(); }
void control_copy(void) { copy_region(false); }
void control_cut(void) { copy_region(true); }
static void control_paste(void) { paste_region(); }
void gold_append(void) { append_region(false); }
static void control_mark(void) { set_mark(); }
void control_search(void) { search(1); }
void gold_search_reverse(void) { search_reverse(1); }
static void control_search_prompt(void) { search_prompt(); }
static void control_replace_prompt(void) { replace_prompt(); }
static void control_command_prompt(void) { command_prompt(); }
void gold_toggle(void);
static void control_gold_toggle(void) { gold_toggle(); }
static void control_redraw(void) { redraw(); }
static void control_help(void) {
#ifdef HAS_HELP
  help();
#endif
}
static void control_esc(void) {
#ifdef HAS_MENU
  menu_op(main_menu);
#endif
}
static void control_format(void) {
#ifdef HAS_AUTOFORMAT
  Format();
#endif
}
static void control_adv_word(void) { adv_word(); }
static void control_prev_word(void) { prev_word(); }
void control_newline(void) { insert_line(1); }
void control_backspace(void) { delete_char_at_cursor(1); }

struct command_map commands_table[] = {
    {"right", control_right, "move right one character", "right"},
    {"left", control_left, "move left one character", "left"},
    {"up", control_up, "move up one line", "up"},
    {"down", control_down, "move down one line", "down"},
    {"bol", control_bol, "move to beginning of line", "beg of lin"},
    {"eol", control_eol, "move to end of line", "end of lin"},
    {"next_page", control_next_page, "move to next page", "next page"},
    {"prev_page", control_prev_page, "move to previous page", "prev page"},
    {"top_of_txt", control_top, "move to top of text", "top of txt"},
    {"bottom_of_txt", control_bottom, "move to bottom of text", "end of txt"},
    {"del_char", control_del_char, "delete character at cursor", "del char"},
    {"del_word", control_del_word, "delete word at cursor", "del word"},
    {"del_line", control_del_line, "delete current line", "del line"},
    {"und_char", control_und_char, "undelete last character", "und char"},
    {"und_word", control_und_word, "undelete last word", "und word"},
    {"und_line", control_und_line, "undelete last line", "und line"},
    {"copy", control_copy, "copy region to clipboard", "copy"},
    {"cut", control_cut, "cut region to clipboard", "cut"},
    {"paste", control_paste, "paste clipboard at cursor", "paste"},
    {"append", gold_append, "append region to clipboard", "append"},
    {"mark", control_mark, "set mark for region", "mark"},
    {"search", control_search, "search for string", "search"},
    {"search_reverse", gold_search_reverse, "search reverse", "reverse"},
    {"search_prompt", control_search_prompt, "prompt for search string", "srch prmpt"},
    {"replace_prompt", control_replace_prompt, "prompt for replace string",
     "repl prmpt"},
    {"command_prompt", control_command_prompt, "enter command mode", "command"},
    {"gold_toggle", control_gold_toggle, "toggle GOLD mode", "GOLD"},
    {"redraw", control_redraw, "redraw the screen", "redraw"},
    {"help", control_help, "display help information", "help"},
    {"menu", control_esc, "open main menu", "menu"},
    {"format", control_format, "format paragraph", "fmt parag"},
    {"adv_word", control_adv_word, "advance to next word", "adv word"},
    {"prev_word", control_prev_word, "move to previous word", "prev word"},
    {"newline", control_newline, "insert newline", "newline"},
    {"backspace", control_backspace, "delete previous character", "backspace"},
    {"undo", control_undo, "undo last change", "undo"},
    {"redo", control_redo, "redo last change", "redo"},
    {nullptr, nullptr, nullptr, nullptr}};

void bind_key(const char *key_str, const char *cmd_name, int table_type) {
  int key_idx = -1;
  if (key_str[0] == '^' && key_str[1] != '\0') {
    if (key_str[1] >= 'A' && key_str[1] <= 'Z') {
      key_idx = key_str[1] - 'A' + 1;
    } else if (key_str[1] >= 'a' && key_str[1] <= 'z') {
      key_idx = key_str[1] - 'a' + 1;
    } else if (key_str[1] == '[') {
      key_idx = 27;
    } else if (key_str[1] == '\\') {
      key_idx = 28;
    } else if (key_str[1] == ']') {
      key_idx = 29;
    } else if (key_str[1] == '^') {
      key_idx = 30;
    } else if (key_str[1] == '_') {
      key_idx = 31;
    } else if (key_str[1] == '@') {
      key_idx = 0;
    }
  } else if (strlen(key_str) >= 3 && key_str[1] == '-') {
    char mod = toupper((unsigned char)key_str[0]);
    int base_key = (unsigned char)key_str[2];
    if (mod == 'M') { // Meta / Alt - map to 512 + base
      key_idx = 512 + base_key;
    } else if (mod == 'W') { // Windows / Super - map to 768 + base
      key_idx = 768 + base_key;
    } else if (mod == 'C') { // Ctrl
      if (base_key >= '@' && base_key <= '_')
        key_idx = base_key - '@';
      else if (base_key >= 'a' && base_key <= 'z')
        key_idx = base_key - 'a' + 1;
    } else if (mod == 'S') { // Shift
      key_idx = base_key; // Standard key, but we can differentiate if needed
    }
  } else if (strncmp(key_str, "code:", 5) == 0) {
    key_idx = atoi(key_str + 5);
  }

  if (key_idx < 0 || key_idx >= 1024)
    return;

  control_handler handler = no_op;
  for (int i = 0; commands_table[i].name != nullptr; i++) {
    if (strcmp(commands_table[i].name, cmd_name) == 0) {
      handler = (control_handler)commands_table[i].handler;
      break;
    }
  }

  control_handler *target_table;
  if (table_type == GOLD_TABLE) {
    target_table = gold_control_table;
  } else if (table_type == EMACS_TABLE) {
    target_table = emacs_control_table;
  } else {
    target_table = base_control_table;
  }

  target_table[key_idx] = handler;
}


static void control_gold_esc(void) {
#ifdef HAS_MENU
  menu_op(main_menu);
#else
  finish();
#endif
}

void gold_toggle(void) {
  gold = true;
  if (info_window) {
    resize_info_win();
  }
}
void no_op(void) {}

control_handler base_control_table[1024] = {
    [1] = control_right,      [2] = bottom,
    [3] = control_copy,       [4] = bol,
    [5] = command_prompt,     [6] = control_search,
    [7] = gold_toggle,        [8] = control_backspace,
    [10] = control_newline,   [11] = del_char,
    [12] = del_line,          [13] = control_newline,
    [14] = control_next_page, [15] = eol,
    [16] = control_prev_page, [18] = redraw,
    [20] = top,               [21] = set_mark,
    [22] = paste_region,      [23] = del_word,
    [24] = control_cut,       [25] = adv_word,
    [26] = replace_prompt,    [27] = control_esc};

control_handler gold_control_table[1024] = {
    [2] = gold_append,        [3] = del_line,
    [6] = search_prompt,      [11] = undel_char,
    [12] = undel_line,        [18] = gold_search_reverse,
    [21] = set_mark,          [22] = control_search,
    [23] = undel_word,        [24] = Format,
    [25] = prev_word,         [26] = replace_prompt,
    [27] = control_gold_esc};

control_handler emacs_control_table[1024] = {
    [1] = bol,                [2] = control_left,
    [3] = command_prompt,     [4] = del_char,
    [5] = eol,                [6] = control_right,
    [7] = control_prev_page,  [8] = control_backspace,
    [10] = undel_char,        [11] = del_line,
    [12] = undel_line,        [13] = control_newline,
    [14] = control_down,      [15] = control_insert_ascii,
    [16] = control_up,        [18] = undel_word,
    [20] = top,               [21] = bottom,
    [22] = control_next_page, [23] = del_word,
    [24] = control_search,    [25] = search_prompt,
    [26] = adv_word,          [27] = control_esc};

/* beginning of main program          */
int main(int argc, char *argv[]) {
  int counter;

  for (counter = 1; counter < 24; counter++) {

    signal(counter, SIG_IGN);
  }

  if (getenv("PROPELLER_PROFILE") != nullptr) {
    profiling_mode = true;
  }

  if ((isatty(STDIN_FILENO) == 0) || (isatty(STDOUT_FILENO) == 0)) {
    profiling_mode = true;
  }

  /* Always read from (and write to) a terminal. */
  if (!profiling_mode && ((isatty(STDIN_FILENO) == 0) || (isatty(STDOUT_FILENO) == 0))) {
    fprintf(stderr, "ee's standard input and output must be a terminal\n");
    exit(1);
  }

  signal(SIGCHLD, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGINT, edit_abort);
  d_char =
      (unsigned char *)malloc(8); /* provide a buffer for multi-byte chars */
  d_word = (unsigned char *)malloc(MAX_WORD_LEN);
  *d_word = '\0';
  d_line = nullptr;
  dlt_line = txtalloc();
  dlt_line->line = d_line;
  dlt_line->line_length = 0;
  curr_line = first_line = txtalloc();
  curr_line->line = point = (unsigned char *)malloc(MIN_LINE_ALLOC);
  curr_line->line_length = 1;
  curr_line->max_length = MIN_LINE_ALLOC;
  curr_line->prev_line = nullptr;
  curr_line->next_line = nullptr;
  curr_line->line_number = 1;
  srch_str = nullptr;
  u_srch_str = nullptr;
  position = 1;
  scr_pos = 0;
  scr_vert = 0;
  scr_horz = 0;
  absolute_lin = 1;
  bit_bucket = fopen("/dev/null", "we");
  edit = true;
  gold = case_sen = false;
  shell_fork = 1;
  strings_init();
  undo_init(&undo_state);
  ee_init();
  if (argc > 0) {
    get_options(argc, argv);
  }
  if (profiling_mode) {
    if (LINES == 0) LINES = 24;
    if (COLS == 0) COLS = 80;
  }
  set_up_term();
  apply_startup_theme();
  if (right_margin == 0) {
    right_margin = COLS - 1;
  }
  if (top_of_stack == nullptr) {
    if (restrict_mode()) {
      ee_wmove(com_win, 0, 0);
      ee_werase(com_win);
      ee_wprintw(com_win, "%s", ree_no_file_msg);
      ee_wrefresh(com_win);
      edit_abort(0);
    }
    ee_wprintw(com_win, "%s", no_file_string);
    ee_wrefresh(com_win);
  } else {
    {
      check_fp();
    }
  }

  clear_com_win = true;

  counter = 0;

#ifdef HAS_LSP
  lsp_start();
  if (in_file_name != nullptr) {
    lsp_open_file((const char *)in_file_name);
  }
#endif

  if (profiling_mode) {
    char buf[512];
    int ed_insert_mode = 0;
    while (fgets(buf, sizeof(buf), stdin) != nullptr) {
      size_t len = strlen(buf);
      if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
      if (ed_insert_mode) {
        if (strcmp(buf, ".") == 0) {
          ed_insert_mode = 0;
        } else {
          for (int i = 0; buf[i]; i++) insert(buf[i]);
          insert('\n');
        }
      } else {
        if (strcmp(buf, "q") == 0 || strcmp(buf, "quit") == 0 || strcmp(buf, ":quit") == 0) {
          edit = false;
        } else if (strcmp(buf, "a") == 0 || strcmp(buf, "i") == 0 || strcmp(buf, "c") == 0) {
          if (buf[0] == 'c') delete_char_at_cursor(1); // very basic change
          ed_insert_mode = 1;
        } else if (strcmp(buf, "d") == 0) {
          del_line();
        } else if (strcmp(buf, "w") == 0) {
          if (in_file_name) write_file(in_file_name, false);
        } else if (buf[0] == 'w' && buf[1] == ' ') {
          write_file(buf + 2, false);
        } else if (buf[0] == ':') {
          command(buf + 1);
        } else {
          command(buf); // Fallback
        }
      }
      if (!edit) break;
    }
    cleanup();
    return 0;
  }

  last_redraw_time = time(nullptr);
  while (edit) {
#ifdef HAS_LSP
    lsp_poll();
#endif
    /*
     |  display line and column information
     */
    if (info_window && !pasting_mode) {
#ifdef HAS_INFO_WIN
      paint_info_win();
#endif
    }

    ee_wrefresh(text_win);
#ifdef HAS_NCURSESW
    wint_t wch;
    int res;
    
    // Set a small timeout to detect if more characters are waiting (paste)
    if (!profiling_mode) wtimeout(text_win, 10);
    res = wget_wch(text_win, &wch);
    
    if (res == ERR) {
      pasting_mode = false;
      if (errno == EINTR)
        continue;
      // ... same timeout/redraw logic ...
      time_t now = time(nullptr);
      if (now - last_redraw_time >= 5) {
        redraw();
        last_redraw_time = now;
      }
      continue;
    }
    
    // Check if another character is immediately available
    wtimeout(text_win, 0);
    wint_t next_wch;
    if (wget_wch(text_win, &next_wch) != ERR) {
      pasting_mode = true;
      unget_wch(next_wch);
    } else {
      pasting_mode = false;
    }
    if (!profiling_mode) wtimeout(text_win, -1); // Restore blocking

    in = wch;
#else
    in = wgetch(text_win);
    if (in == -1) {
      if (errno == EINTR)
        continue;
      time_t now = time(nullptr);
      if (now - last_redraw_time >= 5) {
        redraw();
        last_redraw_time = now;
      }
      /* If wgetch returns ERR and it's not a timeout (or if we really want to
       * exit on true EOF/error), we should be careful. Standard curses ERR is
       * -1. With wtimeout, it returns ERR on timeout. */
      continue;
    }
#endif
    last_redraw_time = time(nullptr);

    resize_check();

    if (clear_com_win) {
      clear_com_win = false;
      ee_wmove(com_win, 0, 0);
      ee_werase(com_win);
      if (!info_window) {
        ee_wprintw(com_win, "%s", com_win_message);
      }
      ee_wrefresh(com_win);
    }

    if (in == 27) { // ESC - could be Meta/Alt or standalone
      int next_in;
      wtimeout(text_win, 50); // Short timeout for Meta
#ifdef HAS_NCURSESW
      wint_t next_wch;
      int res_next = wget_wch(text_win, &next_wch);
      next_in = (res_next == ERR) ? -1 : next_wch;
#else
      next_in = wgetch(text_win);
#endif
      wtimeout(text_win, 5000); // Restore timeout
      if (next_in != -1) {
        in = 512 + next_in;
      } else if (vi_keys_mode && vi_insert_mode) {
        vi_insert_mode = false;
        left(1);
        continue;
      }
    }

    if (in > 255) {
      if (in < 512) {
        function_key();
      } else {
        // Handle Meta/Extended keys via control tables
        if (emacs_keys_mode) {
          if (emacs_control_table[in % 1024] != no_op)
            emacs_control_table[in % 1024]();
        } else {
          if (base_control_table[in % 1024] != no_op)
            base_control_table[in % 1024]();
        }
      }
    } else if ((in == '\10') || (in == ASCII_DEL)) {
      in = ASCII_BACKSPACE; /* make sure key is set to backspace */
      delete_char_at_cursor(1);
    } else if ((in > 31) || (in == 9)) {
      if (vi_keys_mode && !vi_insert_mode) {
        vi_command(in);
      } else {
        insert(in);
      }
    } else if ((in >= 0) && (in <= 31)) {
      if (emacs_keys_mode) {
        emacs_control();
      } else {
        control();
      }
    }

    if (text_changes) {
#ifdef HAS_TREESITTER
      reparse();
#endif
#ifdef HAS_LSP
      if (in_file_name != nullptr) {
        lsp_change_file((const char *)in_file_name);
      }
#endif
      text_changes = false;
    }
  }
  return 0;
}

/* resize the line to length + factor*/
unsigned char *resiz_line(int factor, struct text *restrict rline, int rpos) {
  int new_max = rline->max_length + factor;
  if (ckd_add(&new_max, rline->max_length, factor)) return nullptr;
  unsigned char *new_line = realloc(rline->line, new_max);
  if (!new_line) return nullptr;
  rline->line = new_line;
  rline->max_length = new_max;
  return rline->line + rpos - 1;
}

/* insert character into line		*/
void insert(int character) {
  int counter;
  int value;

  if ((character == '\011') && expand_tabs) {
    counter = len_char('\011', scr_horz);
    for (; counter > 0; counter--) {
      insert(' ');
    }
#ifdef HAS_AUTOFORMAT
    if (auto_format && !formatting_in_progress && !pasting_mode) {
      formatting_in_progress = true;
      Auto_Format();
      formatting_in_progress = false;
    }
#endif
    return;
  }

#ifdef HAS_ICU
  uint8_t utf8_buf[4];
  int32_t utf8_len = 0;
  UErrorCode status = U_ZERO_ERROR;
  U8_APPEND(utf8_buf, utf8_len, 4, character, status);
  if (U_FAILURE(status)) {
    utf8_buf[0] = (uint8_t)character;
    utf8_len = 1;
  }
#else
  unsigned char utf8_buf[1] = {(unsigned char)character};
  int utf8_len = 1;
#endif

  // Make sure we have enough space for the full sequence
  if ((curr_line->max_length - curr_line->line_length) < (utf8_len + 1)) {
    point = resiz_line(10 + utf8_len, curr_line, position);
  }

  text_changes = true;
  size_t move_len = curr_line->line_length - position;
  /* memmove safely handles overlapping memory regions */
  memmove(point + utf8_len, point, move_len);

  for (int i = 0; i < utf8_len; i++) {
    point[i] = utf8_buf[i];
  }
  curr_line->line_length += utf8_len;

  // Update screen once for the whole character
#ifdef HAS_ICU
  if (ee_chinese) {
    if (character == '\t' || character < 32 || character == 127) {
      int w = u_char_width(character, scr_horz);
      out_char(text_win, character, scr_horz);
      scr_horz += w;
    } else {
      // Direct output for printable multi-byte
      for (int i = 0; i < utf8_len; i++) {
        ee_waddch(text_win, utf8_buf[i]);
      }
      scr_horz += u_char_width(character, scr_horz);
    }
  } else {
    // Treat as individual bytes
    for (int i = 0; i < utf8_len; i++) {
      int c = utf8_buf[i];
      if (isprint(c) == 0) {
        scr_horz += out_char(text_win, c, scr_horz);
      } else {
        ee_waddch(text_win, (unsigned char)c);
        scr_horz++;
      }
    }
  }
#else
  int c = (unsigned char)character;
  if (isprint(c) == 0) {
    scr_horz += out_char(text_win, c, scr_horz);
  } else {
    ee_waddch(text_win, (unsigned char)c);
    scr_horz++;
  }
#endif

  scr_pos = scr_horz;
  point += utf8_len;
  position += utf8_len;

  ee_wclrtoeol(text_win);

  if (observ_margins && (right_margin < scr_pos)) {
    counter = position;
    while (scr_pos > right_margin) {
      prev_word();
    }
    if (scr_pos == 0) {
      while (position < counter) {
        right(1);
      }
    } else {
      counter -= position;
      insert_line(1);
      for (value = 0; value < counter; value++) {
        right(1);
      }
    }
  }

  if ((scr_horz - horiz_offset) > last_col) {
    horiz_offset += 8;
    midscreen(scr_vert, point);
  }

#ifdef HAS_AUTOFORMAT
  if (auto_format && (character == ' ') && (!formatted) && !formatting_in_progress && !pasting_mode) {
    formatting_in_progress = true;
    Auto_Format();
    formatting_in_progress = false;
  } else 
#endif
  if ((character != ' ') && (character != '\t')) {
    formatted = false;
  }

  draw_line(scr_vert, scr_horz, curr_line, position);
  
  if (undo_enabled) {
    undo_record_insert(&undo_state, curr_line->line_number, position, utf8_len, (unsigned char *)utf8_buf);
  }
}


/* delete character		*/

#ifdef HAS_ICU
[[maybe_unused]] static int u_char_width(UChar32 c, int column) {
  if (c == '\t')
    return tabshift(column);
  if (c < 32 || c == 127)
    return 2;

  int eaw = u_getIntPropertyValue(c, UCHAR_EAST_ASIAN_WIDTH);
  if (eaw == U_EA_FULLWIDTH || eaw == U_EA_WIDE) {
    return 2;
  }
#endif
  return 1;
}

int scanline_step(unsigned char *ptr, const unsigned char *pos,
                         int temp) {
  int current_temp = temp;
  unsigned char *current_ptr = (unsigned char *)ptr;
  while (current_ptr < pos) {
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(current_ptr, i, (int32_t)(pos - current_ptr), c);
      if (c < 0) { // Invalid UTF-8
        current_temp += 1;
        current_ptr += 1;
      } else {
        current_temp += u_char_width(c, current_temp);
        current_ptr += i;
      }
    } else {
      current_temp += len_char(*current_ptr, current_temp);
      current_ptr += 1;
    }
#else
    current_temp += len_char(*current_ptr, current_temp);
    current_ptr += 1;
#endif
  }
  return current_temp;
}

/* find the proper horizontal position for the pointer */
void scanline(const unsigned char *pos) {
  scr_horz = scanline_step(curr_line->line, pos, 0);

  int beyond_last = (scr_horz - horiz_offset) > last_col;
  int below_offset = scr_horz < horiz_offset;

  if (beyond_last || below_offset) {
    int new_off_high = (scr_horz - (scr_horz % 8)) - (COLS - 8);
    int new_off_low = scr_horz - (scr_horz % 8);
    if (new_off_low < 0)
      new_off_low = 0;

    horiz_offset = (beyond_last ? new_off_high : new_off_low);

    // Call draw_screen instead of midscreen to avoid recursion,
    // as midscreen calls scanline.
    ee_wmove(text_win, 0, 0);
    draw_screen();
  }
}

/* give the number of spaces to shift	*/
int tabshift(int temp_int) {
  return 8 - (temp_int & 7);
}

int out_char(WINDOW * restrict window, int character, int column) {
  int i1;
  int i2;
  char *string;
  char string2[16];

  if (character == TAB) {
    i1 = tabshift(column);
    for (i2 = 0; (i2 < i1) && (((column + i2 + 1) - horiz_offset) < last_col);
         i2++) {
      ee_waddch(window, ' ');
    }
    return i1;
  }
  if ((character >= 0) && (character < 32)) {
    string = table[character];
  } else if (character == 127) {
    string = "^?";
  } else if (character > 127) {
    if (!eightbit) {
      snprintf(string2, sizeof(string2), "<%d>", (character < 0) ? (character + 256) : character);
      string = string2;
    } else {
      ee_waddch(window, (unsigned char)character);
      return 1;
    }
  } else {
    ee_waddch(window, (unsigned char)character);
    return 1;
  }
  for (i2 = 0;
       (string[i2] != '\0') && (((column + i2 + 1) - horiz_offset) < last_col);
       i2++) {
    ee_waddch(window, (unsigned char)string[i2]);
  }
  return (strlen(string));
}

/* return the length of the character   */
int len_char(int character, int column) {
  unsigned char c = (unsigned char)character;
  int len = char_len_table[c];

  // If eightbit is off and it's high-bit, it's 5 (e.g. <255>)
  bool high_bit_not_127 = (c > 126) & (c != 127);
  bool replace_with_5 = (!eightbit) & high_bit_not_127;

  len = (replace_with_5 * 5) + (!replace_with_5 * len);

  // Branchless selection for tab: if c is TAB, use tabshift, else use len
  int is_tab = (c == '\t');
  return (is_tab * tabshift(column)) + (!is_tab * len);
}

#ifdef HAS_TREESITTER
[[maybe_unused]] static int get_node_attribute(int line, int col) {
  if (ts_tree == nullptr) {
    return A_NORMAL;
  }
  TSNode root = ts_tree_root_node(ts_tree);
  TSPoint p = {(uint32_t)line - 1, (uint32_t)col};
  TSNode node = ts_node_descendant_for_point_range(root, p, p);
  const char *type = ts_node_type(node);

  if (strcmp(type, "comment") == 0) {
    return COLOR_PAIR(1);
  }
  if (strcmp(type, "string_literal") == 0 ||
      strcmp(type, "system_lib_string") == 0) {
    return COLOR_PAIR(2);
  }
  if (strcmp(type, "number_literal") == 0) {
    return COLOR_PAIR(3);
  }
  if (strcmp(type, "primitive_type") == 0 ||
      strcmp(type, "type_identifier") == 0) {
    return COLOR_PAIR(4);
  }
  if (strcmp(type, "identifier") == 0) {
    TSNode parent = ts_node_parent(node);
    const char *p_type = ts_node_type(parent);
    if (strcmp(p_type, "function_declarator") == 0 ||
        strcmp(p_type, "call_expression") == 0) {
      return COLOR_PAIR(5);
    }
    return COLOR_PAIR(6);
  }
  if (!ts_node_is_named(node)) {
    if (isalpha((unsigned char)type[0])) {
      return COLOR_PAIR(7);
    }
    return A_NORMAL;
  }

  return A_NORMAL;
}
#endif

#ifdef HAS_LSP
#endif

/* redraw line from current position */
void draw_line(int vertical, int horiz, struct text *restrict line, int t_pos) {
  int d;               /* partial length of special or tab char to display  */
  unsigned char *temp; /* temporary pointer to position in line	     */
  int abs_column;      /* offset in screen units from begin of line	     */
  int column;          /* horizontal position on screen		     */
  int row;             /* vertical position on screen			     */
  int posit;           /* temporary position indicator within line	     */

  abs_column = horiz;
  column = horiz - horiz_offset;
  row = vertical;
  temp = line->line + t_pos - 1;
  d = 0;
  posit = t_pos;

  int line_no = line->line_number;

  if (column < 0) {
    ee_wmove(text_win, row, 0);
    ee_wclrtoeol(text_win);
  }
  while (column < 0) {
    d = len_char(*temp, abs_column);
    abs_column += d;
    column += d;
    posit++;
    temp++;
  }
  ee_wmove(text_win, row, column);
  ee_wclrtoeol(text_win);
  while ((posit < line->line_length) && (column <= last_col)) {
    int attr = A_NORMAL;
#ifdef HAS_TREESITTER
    attr = get_node_attribute(line_no, posit - 1);
#endif

#ifdef HAS_LSP
    // Check diagnostics
    struct diagnostic const *diag = diagnostics_list;
    while (diag != nullptr) {
      if (diag->line == line_no && diag->col == posit - 1) {
        attr |= A_REVERSE | COLOR_PAIR(8); // Highlight error
        break;
      }
      diag = diag->next;
    }
#endif

    if (text_win != nullptr) wattron(text_win, attr);
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(temp, i, (int32_t)(line->line_length - posit + 1), c);
      if (c < 0) {
        // Invalid UTF-8, just print byte
        abs_column++;
        column++;
        ee_waddch(text_win, *temp);
        posit++;
        temp++;
      } else {
        if (c == '\t' || c < 32 || c == 127) {
          column += u_char_width(c, abs_column);
          abs_column += out_char(text_win, (int)c, abs_column);
        } else {
          // Use addwstr or similar for better support, but waddch with UTF-8
          // bytes also works in ncursesw if we add them correctly.
          // For simplicity, we add bytes one by one but they form a sequence.
          for (int j = 0; j < i; j++) {
            ee_waddch(text_win, temp[j]);
          }
          int w = u_char_width(c, abs_column);
          abs_column += w;
          column += w;
        }
        posit += i;
        temp += i;
      }
    } else {
      if (isprint(*temp) == 0) {
        column += len_char(*temp, abs_column);
        abs_column += out_char(text_win, *temp, abs_column);
      } else {
        abs_column++;
        column++;
        ee_waddch(text_win, *temp);
      }
      posit++;
      temp++;
    }
#else
    if (isprint(*temp) == 0) {
      column += len_char(*temp, abs_column);
      abs_column += out_char(text_win, *temp, abs_column);
    } else {
      abs_column++;
      column++;
      ee_waddch(text_win, *temp);
    }
    posit++;
    temp++;
#endif
    if (text_win != nullptr) wattroff(text_win, attr);
  }
  if (column < last_col) {
    ee_wclrtoeol(text_win);
  }
  ee_wmove(text_win, vertical, (horiz - horiz_offset));
}

/* insert new line		*/
void insert_line(int disp) {
  int temp_pos;
  int temp_pos2;
  unsigned char *temp;
  unsigned char *extra;
  struct text *temp_nod;

  text_changes = true;
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  ee_wclrtoeol(text_win);
  temp_nod = txtalloc();
  temp_nod->line = extra = malloc(10);
  temp_nod->line_length = 1;
  temp_nod->max_length = 10;
  temp_nod->line_number = curr_line->line_number + 1;
  temp_nod->next_line = curr_line->next_line;
  if (temp_nod->next_line != nullptr) {
    temp_nod->next_line->prev_line = temp_nod;
  }
  temp_nod->prev_line = curr_line;
  curr_line->next_line = temp_nod;
  update_line_numbers(temp_nod->next_line, 1);
  temp_pos2 = position;
  temp = point;
  if (temp_pos2 < curr_line->line_length) {
    size_t split_len = curr_line->line_length - temp_pos2 + 1;
    if (split_len > (size_t)temp_nod->max_length) {
      int new_max;
      if (ckd_add(&new_max, (int)split_len, 10)) return;
      unsigned char *new_line = realloc(temp_nod->line, new_max);
      if (!new_line) return;
      temp_nod->line = new_line;
      temp_nod->max_length = new_max;
    }
    memcpy(temp_nod->line, temp, split_len);
    temp_nod->line_length = split_len;
    *temp = '\0';
    curr_line->line_length = temp_pos2;
    point = resiz_line(0, curr_line, position);
  }
  absolute_lin++;
  curr_line = temp_nod;
  curr_line->line[curr_line->line_length - 1] = '\0';
  position = 1;
  point = curr_line->line;
  if (disp != 0) {
    if (scr_vert < last_line) {
      scr_vert++;
      ee_wclrtoeol(text_win);
      ee_wmove(text_win, scr_vert, 0);
      ee_winsertln(text_win);
    } else {
      ee_wmove(text_win, 0, 0);
      ee_wdeleteln(text_win);
      ee_wmove(text_win, last_line, 0);
      ee_wclrtobot(text_win);
    }
    scr_pos = scr_horz = 0;
    if (horiz_offset != 0) {
      horiz_offset = 0;
      midscreen(scr_vert, point);
    }
    draw_line(scr_vert, scr_horz, curr_line, position);
  }
}

[[nodiscard]] struct text *txtalloc(void) {
  return ((struct text *)malloc(sizeof(struct text)));
}

[[nodiscard]] struct files *name_alloc(void) {
  return ((struct files *)malloc(sizeof(struct files)));
}

/* return the length of the first word in the line */

/* move to next word in string		*/
void *next_word(void *s) {
  char *string = (char *)s;
  /* strcspn counts characters until a space, tab, or null is found */
  string += strcspn(string, " \t");
  /* strspn counts characters that ARE spaces or tabs */
  string += strspn(string, " \t");
  return string;
}

/* move to start of previous word in text	*/
static unsigned char *skip_spaces_back(unsigned char *start,
                                       unsigned char *ptr) {
  unsigned char *current = ptr;
  while (current > start && (*(current - 1) == ' ' || *(current - 1) == '\t')) {
    current--;
  }
  return current;
}

static unsigned char *skip_word_back(unsigned char *start, unsigned char *ptr) {
  unsigned char *current = ptr;
  while (current > start && (*(current - 1) != ' ' && *(current - 1) != '\t')) {
    current--;
  }
  return current;
}

void prev_word() {
  if (position != 1) {
    unsigned char *new_p = point;
    if ((new_p > curr_line->line) &&
        ((new_p[-1] == ' ') || (new_p[-1] == '\t'))) {
      if ((*new_p != ' ') && (*new_p != '\t')) {
        new_p--;
      }
    }
    new_p = skip_spaces_back(curr_line->line, new_p);
    new_p = skip_word_back(curr_line->line, new_p);

    if ((new_p > curr_line->line) && ((*new_p == ' ') || (*new_p == '\t'))) {
      new_p++;
    }
    position -= (point - new_p);
    point = new_p;
    scanline(point);
    scr_pos = scr_horz;
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  } else {
    left(1);
  }
}

void vi_command(int c) {
  switch (c) {
    case 'h': left(1); break;
    case 'j': down(); break;
    case 'k': up(); break;
    case 'l': right(1); break;
    case 'i': vi_insert_mode = true; break;
    case 'I': bol(); vi_insert_mode = true; break;
    case 'a': right(1); vi_insert_mode = true; break;
    case 'A': eol(); vi_insert_mode = true; break;
    case 'o': eol(); control_newline(); vi_insert_mode = true; break;
    case 'O': bol(); control_newline(); up(); vi_insert_mode = true; break;
    case 'x': delete_char_at_cursor(1); break;
    case 'X': left(1); delete_char_at_cursor(1); break;
    case '0': bol(); break;
    case '$': eol(); break;
    case 'g': top(); break;
    case 'G': bottom(); break;
    case 'w': adv_word(); break;
    case 'b': prev_word(); break;
    case 'u': undel_char(); break;
    case ':': command_prompt(); break;
    case '/': search_prompt(); break;
  }
}

/* use control for commands		*/
void control() {
  bool was_gold = gold;
  control_handler const *table_ptr =
      gold ? gold_control_table : base_control_table;
  int index = in * ((in >= 0) & (in <= 31));
  control_handler handler = table_ptr[index];
  handler = handler ? handler : no_op;

  gold = false;
  if (was_gold && info_window) {
    resize_info_win();
  }
  handler();
}

/*
 |	Emacs control-key bindings
 */

void emacs_control() {
  int index = in * ((in >= 0) & (in <= 31));
  control_handler handler = emacs_control_table[index];
  handler = handler ? handler : no_op;

  handler();
}

/* go to bottom of file			*/
void bottom() {
  while (curr_line->next_line != nullptr) {
    curr_line = curr_line->next_line;
    absolute_lin++;
  }
  point = curr_line->line;
  if (horiz_offset != 0) {
    horiz_offset = 0;
  }
  position = 1;
  midscreen(last_line, point);
  scr_pos = scr_horz;
}

/* go to top of file			*/
void top() {
  while (curr_line->prev_line != nullptr) {
    curr_line = curr_line->prev_line;
    absolute_lin--;
  }
  point = curr_line->line;
  if (horiz_offset != 0) {
    horiz_offset = 0;
  }
  position = 1;
  midscreen(0, point);
  scr_pos = scr_horz;
}

/* move pointers to start of next line	*/
void nextline() {
  curr_line = curr_line->next_line;
  absolute_lin++;
  point = curr_line->line;
  position = 1;
  if (scr_vert == last_line) {
    ee_wmove(text_win, 0, 0);
    ee_wdeleteln(text_win);
    ee_wmove(text_win, last_line, 0);
    ee_wclrtobot(text_win);
    draw_line(last_line, 0, curr_line, 1);
  } else {
    {
      scr_vert++;
    }
  }
}

/* move pointers to start of previous line*/
void prevline() {
  curr_line = curr_line->prev_line;
  absolute_lin--;
  if (scr_vert == 0) {
    ee_winsertln(text_win);
    draw_line(0, 0, curr_line, 1);
  } else {
    scr_vert--;
  }
  position = curr_line->line_length;
  point = curr_line->line + position - 1;
}

/* move left one character	*/
void left(int disp) {
  if (point != curr_line->line) /* if not at begin of line	*/
  {
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = (int32_t)(point - curr_line->line);
      U8_BACK_1(curr_line->line, 0, i);
      unsigned char *new_p = curr_line->line + i;
      position -= (point - new_p);
      point = new_p;
    } else {
      point--;
      position--;
    }
#else
    if (ee_chinese && (position >= 2) && (*(point - 2) > 127)) {
      point--;
      position--;
    }
    point--;
    position--;
#endif
    scanline(point);
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    scr_pos = scr_horz;
  } else if (curr_line->prev_line != nullptr) {
    if (disp == 0) {
      absolute_lin--;
      curr_line = curr_line->prev_line;
      point = curr_line->line + curr_line->line_length;
      position = curr_line->line_length;
      return;
    }
    position = 1;
    prevline();
    scanline(point);
    scr_pos = scr_horz;
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
}

/* move right one character	*/
void right(int disp) {
  if (position < curr_line->line_length) {
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(point, i, curr_line->line_length - position + 1, c);
      point += i;
      position += i;
    } else {
      point++;
      position++;
    }
#else
    if (ee_chinese && (*point > 127) &&
        ((curr_line->line_length - position) >= 2)) {
      point++;
      position++;
    }
    point++;
    position++;
#endif
    scanline(point);
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    scr_pos = scr_horz;
  } else if (curr_line->next_line != nullptr) {
    if (disp == 0) {
      absolute_lin++;
      curr_line = curr_line->next_line;
      point = curr_line->line;
      position = 1;
      return;
    }
    nextline();
    scr_pos = scr_horz = 0;
    if (horiz_offset != 0) {
      horiz_offset = 0;
      midscreen(scr_vert, point);
    }
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    position = 1;
  }
}

/* move to the same column as on other line	*/
void find_pos() {
  scr_horz = 0;
  position = 1;
  while ((scr_horz < scr_pos) && (position < curr_line->line_length)) {
    scr_horz += len_char(*point, scr_horz);
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(point, i, curr_line->line_length - position + 1, c);
      point += i;
      position += i;
    } else {
      point++;
      position++;
    }
#else
    if (ee_chinese && (*point > 127) &&
        ((curr_line->line_length - position) >= 2)) {
      point++;
      position++;
    }
    position++;
    point++;
#endif
  }

  int beyond_last = (scr_horz - horiz_offset) > last_col;
  int below_offset = scr_horz < horiz_offset;

  int new_off_high = (scr_horz - (scr_horz % 8)) - (COLS - 8);
  int new_off_low = scr_horz - (scr_horz % 8);
  new_off_low *= (new_off_low > 0);

  horiz_offset = (beyond_last * new_off_high) + (below_offset * new_off_low) +
                 (!(beyond_last | below_offset) * horiz_offset);

  if (beyond_last | below_offset) {
    midscreen(scr_vert, point);
  }

  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* move up one line		*/
void up() {
  if (curr_line->prev_line != nullptr) {
    prevline();
    point = curr_line->line;
    find_pos();
  }
}

/* move down one line		*/
void down() {
  if (curr_line->next_line != nullptr) {
    nextline();
    find_pos();
  }
}

/* process function key		*/
void function_key() {
  if (in == KEY_LEFT) {
    {
      left(1);
    }
  } else if (in == KEY_RIGHT) {
    {
      right(1);
    }
  } else if (in == KEY_HOME) {
    {
      bol();
    }
  } else if (in == KEY_END) {
    {
      eol();
    }
  } else if (in == KEY_UP) {
    {
      up();
    }
  } else if (in == KEY_DOWN) {
    {
      down();
    }
  } else if (in == KEY_NPAGE) {
    {
      move_rel('d', max(5, (last_line - 5)));
    }
  } else if (in == KEY_PPAGE) {
    {
      move_rel('u', max(5, (last_line - 5)));
    }
  } else if (in == KEY_DL) {
    {
      del_line();
    }
  } else if (in == KEY_DC) {
    {
      del_char();
    }
  } else if (in == KEY_BACKSPACE) {
    {
      delete_char_at_cursor(1);
    }
  } else if (in == KEY_IL) { /* insert a line before current line	*/
    insert_line(1);
    left(1);
  } else if (in == KEY_F(1)) {
    {
      gold = !gold;
    }
  } else if (in == KEY_F(2)) {
    if (gold) {
      gold = false;
      undel_line();
    } else {
      {
        undel_char();
      }
    }
  } else if (in == KEY_F(3)) {
    if (gold) {
      gold = false;
      undel_word();
    } else {
      {
        del_word();
      }
    }
  } else if (in == KEY_F(4)) {
    if (gold) {
      gold = false;
      resize_info_win();
      midscreen(scr_vert, point);
    } else {
      {
        adv_word();
      }
    }
  } else if (in == KEY_F(5)) {
    if (gold) {
      gold = false;
      search_prompt();
    } else {
      {
        search(1);
      }
    }
  } else if (in == KEY_F(6)) {
    if (gold) {
      gold = false;
      bottom();
    } else {
      {
        top();
      }
    }
  } else if (in == KEY_F(7)) {
    if (gold) {
      gold = false;
      eol();
    } else {
      {
        bol();
      }
    }
  } else if (in == KEY_F(8)) {
    if (gold) {
      gold = false;
      command_prompt();
    } else {
      {
        adv_line();
      }
    }
  }
}

void print_buffer() {
  char buffer[256];

  snprintf(buffer, sizeof(buffer), ">!%s", print_command);
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, printer_msg_str, print_command);
  ee_wrefresh(com_win);
  command(buffer);
}

void command_prompt() {
  char *cmd_str;
  int result;

  info_type = COMMANDS;
  if (info_window) {
    resize_info_win();
  }
  cmd_str = get_string(command_str, 1);
  if ((result = unique_test(cmd_str, commands)) != 1) {
    ee_werase(com_win);
    ee_wmove(com_win, 0, 0);
    if (result == 0) {
      ee_wprintw(com_win, unkn_cmd_str, cmd_str);
    } else {
      ee_wprintw(com_win, "%s", non_unique_cmd_msg);
    }

    ee_wrefresh(com_win);

    info_type = CONTROL_KEYS;
    if (info_window) {
      resize_info_win();
    }

    if (cmd_str != nullptr) {
      free(cmd_str);
    }
    return;
  }
  command(cmd_str);
  ee_wrefresh(com_win);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  info_type = CONTROL_KEYS;
  if (info_window) {
    resize_info_win();
  }
  if (cmd_str != nullptr) {
    free(cmd_str);
  }
}

/* process commands from keyboard	*/
void command(char *cmd_str1) {
  char *cmd_str2 = nullptr;
  char *cmd_str = cmd_str1;

  clear_com_win = true;
  if (compare(cmd_str, HELP, false)) {
    {
      help();
    }
  } else if (compare(cmd_str, WRITE, false)) {
    if (restrict_mode()) {
      return;
    }
    cmd_str = next_word(cmd_str);
    if (*cmd_str == '\0') {
      cmd_str = cmd_str2 = get_string(file_write_prompt_str, 1);
    }
    tmp_file = resolve_name(cmd_str);
    write_file(tmp_file, true);
    if (tmp_file != cmd_str) {
      free(tmp_file);
    }
  } else if (compare(cmd_str, READ, false)) {
    if (restrict_mode()) {
      return;
    }
    cmd_str = next_word(cmd_str);
    if (*cmd_str == '\0') {
      cmd_str = cmd_str2 = get_string(file_read_prompt_str, 1);
    }
    tmp_file = cmd_str;
    recv_file = true;
    tmp_file = resolve_name(cmd_str);
    check_fp();
    if (tmp_file != cmd_str) {
      free(tmp_file);
    }
  } else if (compare(cmd_str, LINE, false)) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, line_num_str, curr_line->line_number);
    ee_wprintw(com_win, line_len_str, curr_line->line_length);
  } else if (compare(cmd_str, FILE_str, false)) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    if (in_file_name == nullptr) {
      ee_wprintw(com_win, "%s", no_file_string);
    } else {
      ee_wprintw(com_win, current_file_str, in_file_name);
    }
  } else if (compare(cmd_str, MARK_str, false)) {
    set_mark();
  } else if ((*cmd_str >= '0') && (*cmd_str <= '9')) {
    {
      goto_line(cmd_str);
    }
  } else if (compare(cmd_str, CHARACTER, false)) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, char_str, *point);
  } else if (compare(cmd_str, REDRAW, false)) {
    {
      redraw();
    }
  } else if (compare(cmd_str, RESEQUENCE, false)) {
    tmp_line = first_line->next_line;
    while (tmp_line != nullptr) {
      tmp_line->line_number = tmp_line->prev_line->line_number + 1;
      tmp_line = tmp_line->next_line;
    }
  } else if (compare(cmd_str, AUTHOR, false)) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "written by Hugh Mahon");
  } else if (compare(cmd_str, VERSION, false)) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "%s", version);
  } else if (compare(cmd_str, CASE, false)) {
    {
      case_sen = true;
    }
  } else if (compare(cmd_str, NOCASE, false)) {
    {
      case_sen = false;
    }
  } else if (compare(cmd_str, EXPAND, false)) {
    {
      expand_tabs = true;
    }
  } else if (compare(cmd_str, NOEXPAND, false)) {
    {
      expand_tabs = false;
    }
  } else if (compare(cmd_str, chinese_cmd, false)) {
    ee_chinese = true;
#ifdef NCURSE
    nc_setattrib(A_NC_BIG5);
#endif /* NCURSE */
  } else if (compare(cmd_str, nochinese_cmd, false)) {
    ee_chinese = false;
#ifdef NCURSE
    nc_clearattrib(A_NC_BIG5);
#endif /* NCURSE */
  } else if (*cmd_str == '!') {
    cmd_str++;
    if ((*cmd_str == ' ') || (*cmd_str == 9)) {
      cmd_str = next_word(cmd_str);
    }
    sh_command(cmd_str);
  } else if ((*cmd_str == '<') && (!in_pipe)) {
    in_pipe = true;
    shell_fork = 0;
    cmd_str++;
    if ((*cmd_str == ' ') || (*cmd_str == '\t')) {
      cmd_str = next_word(cmd_str);
    }
    command(cmd_str);
    in_pipe = false;
    shell_fork = 1;
  } else if ((*cmd_str == '>') && (!out_pipe)) {
    out_pipe = true;
    cmd_str++;
    if ((*cmd_str == ' ') || (*cmd_str == '\t')) {
      cmd_str = next_word(cmd_str);
    }
    command(cmd_str);
    out_pipe = false;
  } else {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, unkn_cmd_str, cmd_str);
  }
  if (cmd_str2 != nullptr) {
    free(cmd_str2);
  }
}

/* determine horizontal position for get_string */
int get_string_len(char *line, int offset, int column) {
  char *stemp = line;
  int i = 0;
  int j = column;
  while (i < offset) {
    i++;
    j += len_char(*stemp, j);
    stemp++;
  }
  return j;
}

/* read string from input on command line */
char *get_string(char *prompt, int advance) {
  char *string;
#ifdef HAS_LIBEDIT
  if (el != nullptr) {
    const char *line;
    int count;

    // Position cursor at the bottom
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_waddstr(com_win, prompt);
    ee_wrefresh(com_win);

    // libedit needs to know the prompt. We've already printed it via ncurses
    // but we can also set it in libedit if we want it to handle redraws.
    // For now, we'll just use el_gets.
    
    // We need to temporarily leave curses mode so libedit can use the terminal
    if(!profiling_mode) def_prog_mode();
    if(!profiling_mode) endwin();
    
    // Print prompt again since we just did endwin
    printf("\r%s", prompt);
    fflush(stdout);

    line = el_gets(el, &count);
    
    if(!profiling_mode) reset_prog_mode();
    if(!profiling_mode) refresh();
    if(!profiling_mode) touchwin(text_win);
    ee_wrefresh(text_win);

    if (line != nullptr && count > 0) {
      string = malloc(count + 1);
      strscpy(string, line, count + 1);
      // Remove trailing newline
      char *nl = strchr(string, '\n');
      if (nl) *nl = '\0';
      nl = strchr(string, '\r');
      if (nl) *nl = '\0';

      if (string[0] != '\0') {
        HistEvent ev;
        history(hist, &ev, H_ENTER, string);
      }

      char *ptr = string;
      if (((*ptr == ' ') || (*ptr == 9)) && (advance != 0)) {
        ptr = next_word(ptr);
        size_t new_len = strlen(ptr) + 1;
        char *new_str = malloc(new_len);
        strscpy(new_str, ptr, new_len);
        free(string);
        string = new_str;
      }
      return string;
    }
    size_t empty_len = 1;
    char *empty_str = malloc(empty_len);
    empty_str[0] = '\0';
    return empty_str;
  }
#endif
  char *tmp_string;
  char *nam_str;
  char *g_point;
  int tmp_int;
  int g_horz;
  int g_position;
  int g_pos;
  int esc_flag;

  g_point = tmp_string = malloc(512);
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);

  ee_waddstr(com_win, prompt);
  ee_wrefresh(com_win);
  nam_str = tmp_string;
  clear_com_win = true;
  g_horz = g_position = get_string_len(prompt, strlen(prompt), 0);
  g_pos = 0;
  do {
    esc_flag = 0;
    in = wgetch(com_win);
    if (in == -1) {
      edit_abort(0);
    }
    if (((in == 8) || (in == 127) || (in == KEY_BACKSPACE)) && (g_pos > 0)) {
      tmp_int = g_horz;
      g_pos--;
      g_horz = get_string_len(g_point, g_pos, g_position);
      tmp_int = tmp_int - g_horz;
      for (; 0 < tmp_int; tmp_int--) {
        if ((g_horz + tmp_int) < (last_col - 1)) {
          ee_waddch(com_win, '\010');
          ee_waddch(com_win, ' ');
          ee_waddch(com_win, '\010');
        }
      }
      nam_str--;
    } else if ((in != 8) && (in != 127) && (in != '\n') && (in != '\r') &&
               (in < 256)) {
      if (in == '\026') /* control-v, accept next character verbatim	*/
      {                 /* allows entry of ^m, ^j, and ^h	*/
        esc_flag = 1;
        in = wgetch(com_win);
        if (in == -1) {
          edit_abort(0);
        }
      }
      *nam_str = in;
      g_pos++;
      if ((isprint((unsigned char)in) == 0) && (g_horz < (last_col - 1))) {
        {
          g_horz += out_char(com_win, in, g_horz);
        }
      } else {
        g_horz++;
        if (g_horz < (last_col - 1)) {
          ee_waddch(com_win, (unsigned char)in);
        }
      }
      nam_str++;
    }
    ee_wrefresh(com_win);
    if (esc_flag != 0) {
      in = '\0';
    }
  } while ((in != '\n') && (in != '\r'));
  *nam_str = '\0';
  nam_str = tmp_string;
  if (((*nam_str == ' ') || (*nam_str == 9)) && (advance != 0)) {
    nam_str = next_word(nam_str);
  }
  size_t string_len = strlen(nam_str) + 1;
  string = malloc(string_len);
  strscpy(string, nam_str, string_len);

  free(tmp_string);
  ee_wrefresh(com_win);
  return string;
}

/* compare two strings  */

struct line_search_res {
  struct text *line;
  int distance;
  char direction;
};

static struct line_search_res find_line_recursive(struct text *line, int target,
                                                  int dist) {
  if (line->line_number == target)
    return (struct line_search_res){line, dist, '\0'};
  if (line->line_number > target && line->prev_line) {
    struct line_search_res res =
        find_line_recursive(line->prev_line, target, dist + 1);
    res.direction = 'u';
    return res;
  }
  if (line->line_number < target && line->next_line) {
    struct line_search_res res =
        find_line_recursive(line->next_line, target, dist + 1);
    res.direction = 'd';
    return res;
  }
  return (struct line_search_res){line, dist, '\0'};
}

void goto_line(char *cmd_str) {
  int number = atoi(cmd_str);
  struct line_search_res res = find_line_recursive(curr_line, number, 0);

  if ((res.distance < 30) && (res.distance > 0)) {
    move_rel(res.direction, res.distance);
  } else {
    curr_line = res.line;
    absolute_lin = curr_line->line_number;
    point = curr_line->line;
    position = 1;
    midscreen((last_line / 2), point);
    scr_pos = scr_horz;
  }
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, line_num_str, curr_line->line_number);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

struct text *find_next_recursive(struct text *line, int count,
                                 int *actual_count) {
  struct text *curr = line;
  int i = 0;
  while (curr != nullptr && curr->next_line != nullptr && i < count) {
    curr = curr->next_line;
    i++;
  }
  if (actual_count != nullptr)
    *actual_count += i;
  return curr;
}

struct text *find_prev_recursive(struct text *line, int count,
                                 int *actual_count) {
  struct text *curr = line;
  int i = 0;
  while (curr != nullptr && curr->prev_line != nullptr && i < count) {
    curr = curr->prev_line;
    i++;
  }
  if (actual_count != nullptr)
    *actual_count += i;
  return curr;
}

/* put current line in middle of screen	*/
void midscreen(int line, unsigned char *pnt) {
  struct text *mid_line;
  int i = 0;

  line = min(line, last_line);
  mid_line = curr_line;
  curr_line = find_prev_recursive(curr_line, line, &i);

  scr_vert = scr_horz = 0;
  ee_wmove(text_win, 0, 0);
  draw_screen();
  scr_vert = i;
  curr_line = mid_line;
  scr_horz = scanline_step(curr_line->line, pnt, 0);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* get arguments from command line	*/
void get_options(int numargs, char *arguments[]) {
  char *buff;
  int count;
  struct files *temp_names = nullptr;
  char *name;
  unsigned char *ptr;
  int no_more_opts = 0;

  /*
   |	see if editor was invoked as 'ree' (restricted mode)
   */

  if ((name = strrchr(arguments[0], '/')) == nullptr) {
    name = arguments[0];
  } else {
    name++;
  }
  if (strcmp(name, "ree") == 0) {
    restricted = true;
  }

  top_of_stack = nullptr;
  input_file = false;
  recv_file = false;
  count = 1;
  while ((count < numargs) && (no_more_opts == 0)) {
    buff = arguments[count];
    if (strcmp("-i", buff) == 0) {
      info_window = false;
    } else if (strcmp("-e", buff) == 0) {
      expand_tabs = false;
    } else if (strcmp("-h", buff) == 0) {
      nohighlight = true;
    } else if (strcmp("-?", buff) == 0) {
      fprintf(stderr, usage0, arguments[0]);
      fputs(usage1, stderr);
      fputs(usage2, stderr);
      fputs(usage3, stderr);
      fputs(usage4, stderr);
      exit(1);
    } else if ((*buff == '+') && (start_at_line == nullptr)) {
      buff++;
      start_at_line = buff;
    } else if ((strcmp("--", buff)) == 0) {
      {
        no_more_opts = 1;
      }
    } else {
      count--;
      no_more_opts = 1;
    }
    count++;
  }
  while (count < numargs) {
    buff = arguments[count];
    if (top_of_stack == nullptr) {
      temp_names = top_of_stack = name_alloc();
    } else {
      temp_names->next_name = name_alloc();
      temp_names = temp_names->next_name;
    }
    ptr = temp_names->name = malloc(strlen(buff) + 1);
    while (*buff != '\0') {
      *ptr = *buff;
      buff++;
      ptr++;
    }
    *ptr = '\0';
    temp_names->next_name = nullptr;
    input_file = true;
    recv_file = true;
    count++;
  }
}

/* open or close files according to flags */

/* read specified file into current buffer	*/

/* read string and split into lines */
void get_line(int length, unsigned char *restrict in_string, int *restrict append) {
  unsigned char *str1;
  unsigned char *str2;
  int num;            /* offset from start of string		*/
  int char_count;     /* length of new line (or added portion	*/
  int temp_counter;   /* temporary counter value		*/
  struct text *tline; /* temporary pointer to new line	*/
  int first_time;     /* if true, the first time through the loop */

  str2 = in_string;
  num = 0;
  first_time = 1;
  while (num < length) {
    if (first_time == 0) {
      if (num < length) {
        str2++;
        num++;
      }
    } else {
      {
        first_time = 0;
      }
    }
    str1 = str2;
    char_count = 1;
    /* find end of line	*/
    while ((*str2 != '\n') && (num < length)) {
      str2++;
      num++;
      char_count++;
    }
    if ((*append) == 0) /* if not append to current line, insert new one */
    {
      tline = txtalloc(); /* allocate data structure for next line */
      tline->line_number = curr_line->line_number + 1;
      tline->next_line = curr_line->next_line;
      tline->prev_line = curr_line;
      curr_line->next_line = tline;
      update_line_numbers(tline->next_line, 1);
      if (tline->next_line != nullptr) {
        tline->next_line->prev_line = tline;
      }
      curr_line = tline;
      curr_line->line = point = (unsigned char *)malloc(char_count);
      curr_line->line_length = char_count;
      curr_line->max_length = char_count;
    } else {
      point = resiz_line(char_count, curr_line, curr_line->line_length);
      curr_line->line_length += (char_count - 1);
    }
    memcpy(point, str1, char_count - 1);
    point += char_count - 1;
    *point = '\0';
    *append = 0;
    if ((num == length) && (*str2 != '\n')) {
      *append = 1;
    }
  }
}

void draw_screen() /* redraw the screen from current postion       */
{
  struct text *line = curr_line;
  int vertical = scr_vert;

  ee_wclrtobot(text_win);
  while (line != nullptr && vertical <= last_line) {
    draw_line(vertical, 0, line, 1);
    line = line->next_line;
    vertical++;
  }
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* prepare to exit edit session	*/

/* exit editor			*/
int quit(int noverify) {
  char *ans;

  if(!profiling_mode) touchwin(text_win);
  ee_wrefresh(text_win);
  if (text_changes && (noverify == 0)) {
    ans = get_string(changes_made_prompt, 1);
    if (toupper((unsigned char)*ans) == toupper((unsigned char)*yes_char)) {
      text_changes = false;
    } else {
      return 0;
    }
    free(ans);
  }
  if (top_of_stack == nullptr) {
    if (info_window) {
      ee_wrefresh(info_win);
    }
    ee_wrefresh(com_win);
    resetty();
    if(!profiling_mode) endwin();
    putchar('\n');
    cleanup();
    exit(0);
  } else {
    delete_text();
    recv_file = true;
    input_file = true;
    check_fp();
  }
  return 0;
}

void cleanup() {
#ifdef HAS_ICU
  if (icu_bundle != nullptr) {
    ures_close(icu_bundle);
    icu_bundle = nullptr;
  }
#endif
#ifdef HAS_TREESITTER
  if (ts_tree != nullptr) {
    ts_tree_delete(ts_tree);
    ts_tree = nullptr;
  }
  if (ts_parser != nullptr) {
    ts_parser_delete(ts_parser);
    ts_parser = nullptr;
  }
#endif
#ifdef HAS_LSP
  if (lsp_pid != -1) {
    kill(lsp_pid, SIGTERM);
    lsp_pid = -1;
  }
#endif
#ifdef HAS_LIBEDIT
  if (el != nullptr) {
    el_end(el);
    el = nullptr;
  }
  if (hist != nullptr) {
    history_end(hist);
    hist = nullptr;
  }
#endif
}

[[noreturn]] void edit_abort(int arg) {
  (void)arg;
  ee_wrefresh(com_win);
  resetty();
  if(!profiling_mode) endwin();
  putchar('\n');
  cleanup();
  exit(1);
}




/* search for string in srch_str	*/

/* prompt and read search string (srch_str)	*/

/* set a mark for copying or cutting text */

/* copy or cut the region between the mark and the cursor */

/* paste text from the clipboard */

/* basic find and replace */

/* append the region between the mark and cursor to the existing clipboard */

/* Search backwards from the current cursor position */

/* delete current character	*/

/* undelete last deleted character	*/

/* delete word in front of cursor	*/

/* undelete last deleted word		*/

/* delete from cursor to end of line	*/

/* undelete last deleted line		*/

/* advance to next word		*/
void adv_word() {
  if (position < curr_line->line_length) {
    unsigned char *new_point = next_word(point);
    size_t moved = new_point - point;
    if (moved > 0) {
      point = new_point;
      position += moved;
      scanline(point);
    } else if (curr_line->next_line != nullptr) {
      right(1);
      adv_word();
    }
  } else if (curr_line->next_line != nullptr) {
    right(1);
    adv_word();
  }
}

/* move relative to current line	*/
void move_rel(int direction, int lines) {
  int i;
  unsigned char *tmp;

  if (direction == 'u') {
    scr_pos = 0;
    while (position > 1) {
      left(1);
    }
    for (i = 0; i < lines; i++) {
      up();
    }
    if ((last_line > 5) && (scr_vert < 4)) {
      tmp = point;
      tmp_line = curr_line;
      for (i = 0; (i < 5) && (curr_line->prev_line != nullptr); i++) {
        up();
      }
      scr_vert = scr_vert + i;
      curr_line = tmp_line;
      absolute_lin += i;
      point = tmp;
      scanline(point);
    }
  } else {
    if ((position != 1) && (curr_line->next_line != nullptr)) {
      nextline();
      scr_pos = scr_horz = 0;
      if (horiz_offset != 0) {
        horiz_offset = 0;
        midscreen(scr_vert, point);
      }
    } else {
      {
        adv_line();
      }
    }
    for (i = 1; i < lines; i++) {
      down();
    }
    if ((last_line > 10) && (scr_vert > (last_line - 5))) {
      tmp = point;
      tmp_line = curr_line;
      for (i = 0; (i < 5) && (curr_line->next_line != nullptr); i++) {
        down();
      }
      absolute_lin -= i;
      scr_vert = scr_vert - i;
      curr_line = tmp_line;
      point = tmp;
      scanline(point);
    }
  }
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* go to end of line			*/
void eol() {
  if (position < curr_line->line_length) {
    position = curr_line->line_length;
    point = curr_line->line + position - 1;
    scanline(point);
  } else if (curr_line->next_line != nullptr) {
    nextline();
    position = curr_line->line_length;
    point = curr_line->line + position - 1;
    scanline(point);
  }
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* move to beginning of line	*/
void bol() {
  if (point != curr_line->line) {
    point = curr_line->line;
    position = 1;
    scanline(point);
  } else if (curr_line->prev_line != nullptr) {
    scr_pos = 0;
    up();
  }
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* advance to beginning of next line	*/
void adv_line() {
  if ((point != curr_line->line) || (scr_pos > 0)) {
    while (position < curr_line->line_length) {
      right(1);
    }
    right(1);
  } else if (curr_line->next_line != nullptr) {
    scr_pos = 0;
    down();
  }
}


/* execute shell command			*/

/* set up the terminal for operating with ae	*/
void set_up_term() {
  if (!curses_initialized) {
    if(!profiling_mode) initscr();
    if(!profiling_mode) savetty();
    if(!profiling_mode) noecho();
    if(!profiling_mode) raw();
    if(!profiling_mode) nonl();

    if (has_colors()) {
      if(!profiling_mode) start_color();
      if(!profiling_mode) use_default_colors();
      if(!profiling_mode) init_pair(1, COLOR_GREEN, -1);   // comment
      if(!profiling_mode) init_pair(2, COLOR_YELLOW, -1);  // string
      if(!profiling_mode) init_pair(3, COLOR_CYAN, -1);    // number
      if(!profiling_mode) init_pair(4, COLOR_YELLOW, -1);  // type
      if(!profiling_mode) init_pair(5, COLOR_BLUE, -1);    // function
      if(!profiling_mode) init_pair(6, COLOR_WHITE, -1);   // variable
      if(!profiling_mode) init_pair(7, COLOR_MAGENTA, -1); // keyword
      if(!profiling_mode) init_pair(8, COLOR_RED, -1);     // error/diagnostic
    }

    curses_initialized = true;
  }

  ee_idlok(stdscr, true);
  com_win = profiling_mode ? nullptr : newwin(1, COLS, (LINES - 1), 0);
  ee_keypad(com_win, true);
  ee_idlok(com_win, true);
  ee_wrefresh(com_win);

  resize_info_win();

  last_col = COLS - 1;
  local_LINES = LINES;
  local_COLS = COLS;

#ifdef NCURSE
  if (ee_chinese)
    nc_setattrib(A_NC_BIG5);
#endif /* NCURSE */
}

void resize_check() {
  if ((LINES == local_LINES) && (COLS == local_COLS)) {
    return;
  }

  if (info_window) {
    delwin(info_win);
  }
  delwin(text_win);
  delwin(com_win);
  delwin(help_win);
  if (profiling_mode) {
    if (LINES == 0) LINES = 24;
    if (COLS == 0) COLS = 80;
  }
  set_up_term();
  redraw();
  ee_wrefresh(text_win);
}

char item_alpha[] = "abcdefghijklmnopqrstuvwxyz0123456789 ";

#ifdef HAS_MENU
#endif

#ifdef HAS_MENU
#endif
#ifdef HAS_HELP
#ifdef HAS_HELP
void help() {
  int counter;
  WINDOW *h_win;

  if (profiling_mode)
    return;

  h_win = newwin(LINES, COLS, 0, 0);
  if (h_win == nullptr)
    return;

  werase(h_win);
  clearok(h_win, true);
  for (counter = 0; counter < 22; counter++) {
    wmove(h_win, counter, 0);
    char *str = (emacs_keys_mode) ? emacs_help_text[counter] : help_text[counter];
    if (str != nullptr) {
      waddstr(h_win, str);
    }
  }

  wmove(h_win, min(23, LINES - 1), 0);
  waddstr(h_win, press_any_key_msg);
  wrefresh(h_win);
  wgetch(h_win);
  delwin(h_win);
  redraw();
}
#endif
#endif
static void buf_append(char *restrict buf, size_t *restrict pos,
                        size_t cap, const char *restrict s) {
  while (*s != '\0' && *pos < cap - 1) {
    buf[(*pos)++] = *s++;
  }
  buf[*pos] = '\0';
}

void generate_dynamic_info() {
  static char total_buf[4096];
  static char lines_buf[MAX_INFO_LINES][256];
  size_t buf_pos = 0;
  total_buf[0] = '\0';
  num_info_lines = 0;

  if (!info_window) return;

  control_handler *tbl = base_control_table;
  if (gold) tbl = gold_control_table;
  else if (emacs_keys_mode) tbl = emacs_control_table;

  // Add mandatory main menu hint if menu is enabled
#ifdef HAS_MENU
  buf_append(total_buf, &buf_pos, sizeof(total_buf), "Esc menu  ");
#endif

  for (int i = 0; commands_table[i].name != nullptr; i++) {
    const char *key = get_key_binding(commands_table[i].handler, tbl);
    if (key[0] != '\0') {
      char item[64];
      int n = snprintf(item, sizeof(item), "%s %s  ", key, commands_table[i].short_desc);
      size_t ilen = (size_t)n < sizeof(item) ? (size_t)n : sizeof(item) - 1;
      if (buf_pos + ilen < sizeof(total_buf) - 1) {
        buf_append(total_buf, &buf_pos, sizeof(total_buf), item);
      }
    }
  }

  // Word wrap
  int width = COLS;
  if (width <= 0) width = 80;
  
  char *p = total_buf;
  while (*p != '\0' && num_info_lines < MAX_INFO_LINES - 1) {
    int len = strlen(p);
    if (len > width - 1) {
      int split = width - 1;
      while (split > 0 && p[split] != ' ') split--;
      if (split == 0) split = width - 1;
      
      int cpy_len = min(split, 255);
      strncpy(lines_buf[num_info_lines], p, cpy_len);
      lines_buf[num_info_lines][cpy_len] = '\0';
      dynamic_info_lines[num_info_lines] = lines_buf[num_info_lines];
      num_info_lines++;
      p += split;
      while (*p == ' ') p++;
    } else {
      strncpy(lines_buf[num_info_lines], p, 255);
      lines_buf[num_info_lines][255] = '\0';
      dynamic_info_lines[num_info_lines] = lines_buf[num_info_lines];
      num_info_lines++;
      break;
    }
  }
}


int get_info_win_height() {
  if (!info_window)
    return 0;
  generate_dynamic_info();
  return max(1, num_info_lines + 1); 
}

void resize_info_win() {
  if (!curses_initialized) return;

  int new_height = get_info_win_height();
  
  if (info_win != nullptr) {
    delwin(info_win);
    info_win = nullptr;
  }
  if (text_win != nullptr) {
    delwin(text_win);
    text_win = nullptr;
  }

  if (new_height > 0) {
    info_win = profiling_mode ? nullptr : newwin(new_height, COLS, 0, 0);
    if (info_win != nullptr) {
      ee_idlok(info_win, true);
      ee_keypad(info_win, true);
    }
    text_win = profiling_mode ? nullptr : newwin(LINES - new_height - 1, COLS, new_height, 0);
  } else {
    text_win = profiling_mode ? nullptr : newwin(LINES - 1, COLS, 0, 0);
  }

  if (text_win != nullptr) {
    ee_keypad(text_win, true);
    ee_idlok(text_win, true);
    wtimeout(text_win, 5000);
    last_line = getmaxy(text_win) - 1;
  }

  if (info_win != nullptr) {
    paint_info_win();
  }
  draw_screen();
  doupdate();
}
void paint_info_win() {
  int counter;
  int height, width;

  if (!info_window) {
    return;
  }

  generate_dynamic_info();
  
  if (info_win == nullptr) return;
  getmaxyx(info_win, height, width);

  ee_werase(info_win);
  for (counter = 0; counter < num_info_lines && counter < height - 1; counter++) {
    ee_wmove(info_win, counter, 0);
    ee_wclrtoeol(info_win);
    if (dynamic_info_lines[counter] != nullptr) {
      ee_waddstr(info_win, dynamic_info_lines[counter]);
    }
  }

  // Construct status line
  ee_wmove(info_win, height - 1, 0);
  if (!nohighlight) {
    wstandout(info_win);
  }

  char status_buf[128];
  snprintf(status_buf, sizeof(status_buf), "%s line %d col %d top %d=", 
           (mark_line != nullptr ? "MARK" : ""), 
           curr_line->line_number, scr_pos, absolute_lin);
  int status_len = strlen(status_buf);

  char const *legend = "^ = Ctrl key ---- access HELP through menu ---";
  int legend_len = strlen(legend);

  // Draw legend
  for (int i = 0; i < width && i < legend_len; i++) {
    ee_waddch(info_win, legend[i]);
  }

  // Fill with '=' up to status info
  int current_x = 0;
  if (!profiling_mode) current_x = getcurx(info_win);
  int status_start_x = width - status_len;
  if (status_start_x < current_x) {
    status_start_x = current_x;
  }

  for (int i = current_x; i < status_start_x; i++) {
    ee_waddch(info_win, '=');
  }

  // Draw status info
  if (status_start_x < width) {
    ee_waddstr(info_win, status_buf);
  }

  // Final fill if needed
  current_x = 0;
  if (!profiling_mode) current_x = getcurx(info_win);
  for (int i = current_x; i < width; i++) {
    ee_waddch(info_win, '=');
  }

  if (!nohighlight) {
    wstandend(info_win);
  }
  ee_wrefresh(info_win);
}


int file_op(int arg) {
  char *string;
  static int flag;

  if (restrict_mode()) {
    return 0;
  }

  if (arg == READ_FILE) {
    string = get_string(file_read_prompt_str, 1);
    recv_file = true;
    tmp_file = resolve_name(string);
    check_fp();
    if (tmp_file != string) {
      free(tmp_file);
    }
    free(string);
  } else if (arg == WRITE_FILE) {
    string = get_string(file_write_prompt_str, 1);
    tmp_file = resolve_name(string);
    write_file(tmp_file, true);
    if (tmp_file != string) {
      free(tmp_file);
    }
    free(string);
  } else if (arg == SAVE_FILE) {
    /*
     |	changes made here should be reflected in finish()
     */

    flag = (int)(in_file_name != nullptr);

    string = in_file_name;
    if ((string == nullptr) || (*string == '\0')) {
      string = get_string(save_file_name_prompt, 1);
    }
    if ((string == nullptr) || (*string == '\0')) {
      ee_wmove(com_win, 0, 0);
      ee_wprintw(com_win, "%s", file_not_saved_msg);
      ee_wclrtoeol(com_win);
      ee_wrefresh(com_win);
      clear_com_win = true;
      return 0;
    }
    if (flag == 0) {
      tmp_file = resolve_name(string);
      if (tmp_file != string) {
        free(string);
        string = tmp_file;
      }
    }
    if (write_file(string, true) != 0) {
      in_file_name = string;
      text_changes = false;
    } else if (flag == 0) {
      {
        free(string);
      }
    }
  }
  return 0;
}

void shell_op() {
  char *string;

  if (((string = get_string(shell_prompt, 1)) != nullptr) &&
      (*string != '\0')) {
    sh_command(string);
    free(string);
  }
}

void leave_op() {
  if (text_changes) {
    menu_op(leave_menu);
  } else {
    {
      quit(1);
    }
  }
}

void redraw() {
  if (info_window) {
    if(!profiling_mode) clearok(info_win, true);
    paint_info_win();
  } else {
    {
      if(!profiling_mode) clearok(text_win, true);
    }
  }
  midscreen(scr_vert, point);
}

/*
 |	The following routines will "format" a paragraph (as defined by a
 |	block of text with blank lines before and after the block).
 */

/* test if line has any non-space characters	*/

/* format the paragraph according to set margins	*/

static char *init_name[3] = {"/usr/share/misc/init.ee", nullptr, ".init.ee"};

/* check for init file and read it if it exists	*/
void update_libedit_mode() {
#ifdef HAS_LIBEDIT
  if (el != nullptr) {
    el_set(el, EL_EDITOR, vi_keys_mode ? "vi" : "emacs");
  }
#endif
}

void ee_init() {
  FILE *init_file;
  char *string;
  char *str1;
  char *str2;
  char *home;
  int counter;
  int temp_int;

  string = getenv("HOME");
  if (string == nullptr) {
    string = "/tmp";
  }
  size_t home_len = strlen(string) + 10;
  str1 = home = malloc(home_len);
  snprintf(home, home_len, "%s/.init.ee", string);
  init_name[1] = home;
  string = malloc(512);

  for (counter = 0; counter < 3; counter++) {
    if ((access(init_name[counter], 4)) == 0) {
      init_file = fopen(init_name[counter], "r");
      while ((str2 = fgets(string, 512, init_file)) != nullptr) {
        str1 = str2 = string;
        while (*str2 != '\n') {
          str2++;
        }
        *str2 = '\0';

        if (unique_test(string, init_strings) != 1) {
          continue;
        }

        if (compare(str1, CASE, false)) {
          {
            case_sen = true;
          }
        } else if (compare(str1, NOCASE, false)) {
          {
            case_sen = false;
          }
        } else if (compare(str1, EXPAND, false)) {
          {
            expand_tabs = true;
          }
        } else if (compare(str1, NOEXPAND, false)) {
          {
            expand_tabs = false;
          }
        } else if (compare(str1, INFO, false)) {
          {
            info_window = true;
            resize_info_win();
          }
        } else if (compare(str1, NOINFO, false)) {
          {
            info_window = false;
            resize_info_win();
          }
        } else if (compare(str1, MARGINS, false)) {
          {
            observ_margins = true;
          }
        } else if (compare(str1, NOMARGINS, false)) {
          {
            observ_margins = false;
          }
        } else if (compare(str1, AUTOFORMAT, false)) {
          auto_format = true;
          observ_margins = true;
        } else if (compare(str1, NOAUTOFORMAT, false)) {
          {
            auto_format = false;
          }
        } else if (compare(str1, Echo, false)) {
          str1 = next_word(str1);
          if (*str1 != '\0') {
            echo_string(str1);
          }
        } else if (compare(str1, PRINTCOMMAND, false)) {
          str1 = next_word(str1);
          size_t cmd_len = strlen(str1) + 1;
          print_command = malloc(cmd_len);
          strscpy(print_command, str1, cmd_len);
        } else if (compare(str1, RIGHTMARGIN, false)) {
          str1 = next_word(str1);
          if ((*str1 >= '0') && (*str1 <= '9')) {
            temp_int = atoi(str1);
            if (temp_int > 0) {
              right_margin = temp_int;
            }
          }
        } else if (compare(str1, HIGHLIGHT, false)) {
          {
            nohighlight = false;
          }
        } else if (compare(str1, NOHIGHLIGHT, false)) {
          {
            nohighlight = true;
          }
        } else if (compare(str1, EIGHTBIT, false)) {
          {
            eightbit = true;
          }
        } else if (compare(str1, NOEIGHTBIT, false)) {
          eightbit = false;
          ee_chinese = false;
        } else if (compare(str1, EMACS_string, false)) {
          {
            emacs_keys_mode = true;
            update_libedit_mode();
          }
        } else if (compare(str1, NOEMACS_string, false)) {
          {
            emacs_keys_mode = false;
            update_libedit_mode();
          }
        } else if (compare(str1, chinese_cmd, false)) {
          ee_chinese = true;
          eightbit = true;
        } else if (compare(str1, nochinese_cmd, false)) {
          {
            ee_chinese = false;
          }
        } else if (compare(str1, BIND, false)) {
          char *key = next_word(str1);
          char *cmd = next_word(key);
          if (*key != '\0' && *cmd != '\0') {
            bind_key(key, cmd, 0);
          }
        } else if (compare(str1, GBIND, false)) {
          char *key = next_word(str1);
          char *cmd = next_word(key);
          if (*key != '\0' && *cmd != '\0') {
            bind_key(key, cmd, 1);
          }
        } else if (compare(str1, EBIND, false)) {
          char *key = next_word(str1);
          char *cmd = next_word(key);
          if (*key != '\0' && *cmd != '\0') {
            bind_key(key, cmd, 2);
          }
        }
      }
      fclose(init_file);
    }
  }
  free(string);
  free(home);

  {
    char yaml_path[512];
    const char *yh = getenv("HOME");
    if (!yh) yh = "/tmp";
    snprintf(yaml_path, sizeof(yaml_path), "%s/.config/ee/config.yaml", yh);
    FILE *yf = fopen(yaml_path, "r");
    if (yf) {
      char yline[512];
      while (fgets(yline, sizeof(yline), yf)) {
        char *ye = yline + strlen(yline);
        while (ye > yline && (ye[-1] == '\n' || ye[-1] == '\r' || ye[-1] == ' ' || ye[-1] == '\t')) ye--;
        *ye = '\0';
        if (yline[0] == '\0' || yline[0] == '#') continue;
        char *yc = strstr(yline, ": ");
        if (!yc) continue;
        *yc = '\0';
        char *yk = yline;
        char *yv = yc + 2;
        if (strcmp(yk, "case") == 0) case_sen = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "expand") == 0) expand_tabs = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "info") == 0) { info_window = strcmp(yv, "true") == 0; resize_info_win(); }
        else if (strcmp(yk, "margins") == 0) observ_margins = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "autoformat") == 0) { auto_format = strcmp(yv, "true") == 0; if (auto_format) observ_margins = true; }
        else if (strcmp(yk, "printcommand") == 0 && yv[0]) { size_t cl = strlen(yv) + 1; print_command = malloc(cl); snprintf(print_command, cl, "%s", yv); }
        else if (strcmp(yk, "rightmargin") == 0) { int ti = atoi(yv); if (ti > 0) right_margin = ti; }
        else if (strcmp(yk, "highlight") == 0) nohighlight = strcmp(yv, "false") == 0;
        else if (strcmp(yk, "eightbit") == 0) eightbit = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "emacs") == 0) { emacs_keys_mode = strcmp(yv, "true") == 0; update_libedit_mode(); }
        else if (strcmp(yk, "theme") == 0 && yv[0]) { snprintf(theme_name, sizeof(theme_name), "%s", yv); }
        else if (strncmp(yk, "bind(", 5) == 0) { char *cp = yk + 5; char *cl = strchr(cp, ')'); if (cl) { *cl = '\0'; bind_key(cp, yv, 0); } }
        else if (strncmp(yk, "gbind(", 6) == 0) { char *cp = yk + 6; char *cl = strchr(cp, ')'); if (cl) { *cl = '\0'; bind_key(cp, yv, 1); } }
        else if (strncmp(yk, "ebind(", 6) == 0) { char *cp = yk + 6; char *cl = strchr(cp, ')'); if (cl) { *cl = '\0'; bind_key(cp, yv, 2); } }
      }
      fclose(yf);
    }
  }

  string = getenv("LANG");
  if (string != nullptr) {
    if (strcmp(string, "zh_TW.big5") == 0) {
      ee_chinese = true;
      eightbit = true;
    } else if (strstr(string, "UTF-8") != nullptr ||
               strstr(string, "utf8") != nullptr) {
      eightbit = true;
      ee_chinese = true;
    }
  }

#ifdef HAS_LIBEDIT
  el = el_init("ee", stdin, stdout, stderr);
  el_set(el, EL_PROMPT, libedit_prompt);
  el_set(el, EL_EDITOR, emacs_keys_mode ? "emacs" : "vi");
  el_set(el, EL_GETCFN, libedit_getc);
#ifdef EL_WIDECHAR
  el_set(el, EL_WIDECHAR, 1);
#endif

  hist = history_init();
  HistEvent ev;
  history(hist, &ev, H_SETSIZE, 100);
  el_set(el, EL_HIST, history, hist);
#endif
}

/*
 |	Save current configuration to .init.ee file in the current directory.
 */

static void config_path(char *buf, size_t size) {
  const char *home = getenv("HOME");
  if (!home) home = "/tmp";
  snprintf(buf, size, "%s/.config/ee/config.yaml", home);
}

static void ensure_config_dir(void) {
  const char *home = getenv("HOME");
  if (!home) return;
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/.config", home);
  mkdir(dir, 0755);
  snprintf(dir, sizeof(dir), "%s/.config/ee", home);
  mkdir(dir, 0755);
}

void dump_ee_conf(void) {
  if (restrict_mode()) return;

  ensure_config_dir();

  char path[512];
  config_path(path, sizeof(path));

  FILE *f = fopen(path, "we");
  if (!f) {
    ee_werase(com_win);
    ee_wmove(com_win, 0, 0);
    ee_wprintw(com_win, "%s", conf_dump_err_msg);
    ee_wrefresh(com_win);
    return;
  }

  fprintf(f, "# ee configuration\n");
  fprintf(f, "case: %s\n", case_sen ? "true" : "false");
  fprintf(f, "expand: %s\n", expand_tabs ? "true" : "false");
  fprintf(f, "info: %s\n", info_window ? "true" : "false");
  fprintf(f, "margins: %s\n", observ_margins ? "true" : "false");
  fprintf(f, "autoformat: %s\n", auto_format ? "true" : "false");
  fprintf(f, "printcommand: %s\n", print_command ? print_command : "lpr");
  fprintf(f, "rightmargin: %d\n", right_margin);
  fprintf(f, "highlight: %s\n", nohighlight ? "false" : "true");
  fprintf(f, "eightbit: %s\n", eightbit ? "true" : "false");
  fprintf(f, "emacs: %s\n", emacs_keys_mode ? "true" : "false");
  fprintf(f, "theme: %s\n", theme_name[0] ? theme_name : "default");

  for (int t = 0; t < 3; t++) {
    control_handler *tbl = t == 0 ? base_control_table : (t == 1 ? gold_control_table : emacs_control_table);
    const char *prefix = t == 0 ? "bind" : (t == 1 ? "gbind" : "ebind");
    for (int i = 0; i < 1024; i++) {
      if (tbl[i] == no_op) continue;
      const char *cmd_name = nullptr;
      for (int j = 0; commands_table[j].name; j++) {
        if (commands_table[j].handler == tbl[i]) { cmd_name = commands_table[j].name; break; }
      }
      if (cmd_name) fprintf(f, "%s(%s): %s\n", prefix, get_key_name(i), cmd_name);
    }
  }

  fclose(f);

  ee_werase(com_win);
  ee_wmove(com_win, 0, 0);
  ee_wprintw(com_win, conf_dump_success_msg, path);
  ee_wrefresh(com_win);
}

/* echo the given string	*/
void echo_string(char *string) {
  char *temp;
  int Counter;

  temp = string;
  while (*temp != '\0') {
    if (*temp == '\\') {
      temp++;
      if (*temp == 'n') {
        {
          putchar('\n');
        }
      } else if (*temp == 't') {
        {
          putchar('\t');
        }
      } else if (*temp == 'b') {
        {
          putchar('\b');
        }
      } else if (*temp == 'r') {
        {
          putchar('\r');
        }
      } else if (*temp == 'f') {
        {
          putchar('\f');
        }
      } else if ((*temp == 'e') || (*temp == 'E')) {
        {
          putchar('\033'); /* escape */
        }
      } else if (*temp == '\\') {
        {
          putchar('\\');
        }
      } else if (*temp == '\'') {
        {
          putchar('\'');
        }
      } else if ((*temp >= '0') && (*temp <= '9')) {
        Counter = 0;
        while ((*temp >= '0') && (*temp <= '9')) {
          Counter = (8 * Counter) + (*temp - '0');
          temp++;
        }
        putchar(Counter);
        temp--;
      }
      temp++;
    } else {
      putchar(*temp);
      temp++;
    }
  }

  fflush(stdout);
}

/* check spelling of words in the editor	*/
#ifdef HAS_SPELL
void spell_op() {
  if (restrict_mode()) {
    return;
  }
  top();          /* go to top of file		*/
  insert_line(0); /* create two blank lines	*/
  insert_line(0);
  top();
  command(shell_echo_msg);
  adv_line();
  ee_wmove(com_win, 0, 0);
  ee_wprintw(com_win, "%s", spell_in_prog_msg);
  ee_wrefresh(com_win);
  command("<>!spell"); /* send contents of buffer to command 'spell'
                          and read the results back into the editor */
}

void ispell_op() {
  char template[128];
  char *name;
  char string[256];
  int fd;

  if (restrict_mode()) {
    return;
  }
  (void)snprintf(template, sizeof(template), "/tmp/ee.XXXXXXXX");
  fd = mkstemp(template);
  name = template;
  if (fd < 0) {
    ee_wmove(com_win, 0, 0);
    ee_wprintw(com_win, create_file_fail_msg, name);
    ee_wrefresh(com_win);
    return;
  }
  close(fd);
  if (write_file(name, false) != 0) {
    snprintf(string, sizeof(string), "ispell %s", name);
    sh_command(string);
    delete_text();
    tmp_file = name;
    recv_file = true;
    check_fp();
    unlink(name);
  }
}
#endif

int from_top(struct text *test_line) {
  int counter = 0;
  unsigned char *pnt;

  if (test_line == nullptr) {
    return 0;
  }

  pnt = test_line->line;
  if ((pnt == nullptr) || (*pnt == '\0') || (*pnt == '.') || (*pnt == '>')) {
    return 0;
  }

  if ((*pnt == ' ') || (*pnt == '\t')) {
    pnt = next_word(pnt);
  }

  if (*pnt == '\0') {
    return 0;
  }

  while ((*pnt != '\0') && ((*pnt != ' ') && (*pnt != '\t'))) {
    pnt++;
    counter++;
  }
  while ((*pnt != '\0') && ((*pnt == ' ') || (*pnt == '\t'))) {
    pnt++;
    counter++;
  }
  return counter;
}

/* format the paragraph according to set margins	*/


/* a strchr() look-alike for systems without strchr() */
char *get_token(char *restrict string, char *restrict substring) {
  char *full;
  static char *sub;

  for (sub = substring; (sub != nullptr) && (*sub != '\0'); sub++) {
    for (full = string; (full != nullptr) && (*full != '\0'); full++) {
      if (*sub == *full) {
        return full;
      }
    }
  }
  return nullptr;
}

/*
 |	handle names of the form "~/file", "~user/file",
 |	"$HOME/foo", "~/$FOO", etc.
 */


bool restrict_mode(void) {
  if (!restricted) {
    return false;
  }

  ee_wmove(com_win, 0, 0);
  ee_wprintw(com_win, "%s", restricted_msg);
  ee_wclrtoeol(com_win);
  ee_wrefresh(com_win);
  clear_com_win = true;
  return true;
}

/*
 |	The following routine tests the input string against the list of
 |	strings, to determine if the string is a unique match with one of the
 |	valid values.
 */

int unique_test(char *string, char *list[]) {
  int counter;
  int num_match;
  int result;

  num_match = 0;
  counter = 0;
  while (list[counter] != nullptr) {
    result = (int)(compare(string, list[counter], false));
    if (result != 0) {
      num_match++;
    }
    counter++;
  }
  return num_match;
}

#ifdef HAS_ICU
[[nodiscard]] char *locale_string(const char *key, char *fallback) {
  if (icu_bundle == nullptr)
    return fallback;

  UErrorCode status = U_ZERO_ERROR;
  int32_t len;
  const UChar *u_str = ures_getStringByKey(icu_bundle, key, &len, &status);

  if (U_SUCCESS(status)) {
    int32_t utf8_len;
    u_strToUTF8(nullptr, 0, &utf8_len, u_str, len, &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) {
      status = U_ZERO_ERROR;
      char *utf8_buf = malloc(utf8_len + 1);
      u_strToUTF8(utf8_buf, utf8_len + 1, nullptr, u_str, len, &status);
      if (U_SUCCESS(status)) {
        return utf8_buf;
      }
      free(utf8_buf);
    }
  }
  return fallback;
}
#else
[[nodiscard]] char *locale_string(const char *key, char *fallback) { return fallback; }
#endif /* HAS_ICU */

/*
 |	ICU resource bundles provide localized strings. The root.res file
 |	(compiled from ee.txt via genrb) contains the default (English)
 |	translations. System locale detection picks up the appropriate
 |	bundle — ee.txt can be translated and compiled per-locale.
 */

const char *get_key_name(int i) {
  static char key[16];
  if (i == 0) return "^@";
  if (i < 27) {
    snprintf(key, sizeof(key), "^%c", i + '@');
    return key;
  }
  if (i == 27) return "^[";
  if (i == 28) return "^\\";
  if (i == 29) return "^]";
  if (i == 30) return "^^";
  if (i == 31) return "^_";
  if (i >= 512 && i < 768) {
    snprintf(key, sizeof(key), "M-%c", i - 512);
    return key;
  }
  if (i >= 768 && i < 1024) {
    snprintf(key, sizeof(key), "W-%c", i - 768);
    return key;
  }
  if (i >= KEY_F(1) && i <= KEY_F(12)) {
    snprintf(key, sizeof(key), "F%d", i - KEY_F(0));
    return key;
  }
  snprintf(key, sizeof(key), "code:%d", i);
  return (const char *)key;
}

static const char *get_key_binding(control_handler handler,
                                   control_handler *table) {
  for (int i = 0; i < 1024; i++) {
    if (table[i] == handler) {
      return get_key_name(i);
    }
  }
  return "";
}

char *format_shortcut(const char *cmd_name, control_handler *table) {
  static char buf[16][64];
  static int idx = 0;
  char *current_buf = buf[idx++ % 16];
  control_handler h = nullptr;
  const char *short_desc = "";
  for (int i = 0; commands_table[i].name != nullptr; i++) {
    if (strcmp(commands_table[i].name, cmd_name) == 0) {
      h = commands_table[i].handler;
      short_desc = commands_table[i].short_desc;
      break;
    }
  }
  if (h == nullptr) return (char *)"";
  const char *key = get_key_binding(h, table);
  if (key[0] == '\0') return (char *)"";
  snprintf(current_buf, 64, "%s %s", key, short_desc);
  return current_buf;
}

#ifndef RESDIR_PATH
#define RESDIR_PATH "/usr/local/share/ee/"
#endif

void strings_init() {
  int counter;

  setlocale(LC_ALL, "");
#ifdef HAS_ICU
  UErrorCode status = U_ZERO_ERROR;
  // Try opening bundle in current directory first, then in system path
  icu_bundle = ures_open(".", uloc_getDefault(), &status);
  if (U_FAILURE(status)) {
    status = U_ZERO_ERROR;
    icu_bundle = ures_open(RESDIR_PATH, uloc_getDefault(), &status);
  }
#endif

  modes_menu[0].item_string = locale_string("modes_menu", "modes menu");
  mode_strings[1] = locale_string("tabs_to_spaces", "tabs to spaces       ");
  mode_strings[2] = locale_string("case_sensitive_search", "case sensitive search");
  mode_strings[3] = locale_string("margins_observed", "margins observed     ");
  mode_strings[4] = locale_string("auto_paragraph_format", "auto-paragraph format");
  mode_strings[5] = locale_string("eightbit_characters", "eightbit characters  ");
  mode_strings[6] = locale_string("info_window_toggle", "info window          ");
  mode_strings[7] = locale_string("emacs_key_bindings", "emacs key bindings   ");
  mode_strings[8] = locale_string("vi_key_bindings", "vi key bindings      ");
  mode_strings[9] = locale_string("right_margin_toggle", "right margin         ");
  mode_strings[10] = locale_string("sixteen_bit_chars", "16 bit characters    ");
  mode_strings[11] = locale_string("save_editor_config", "save editor configuration");
  
  leave_menu[0].item_string = locale_string("leave_menu", "leave menu");
  leave_menu[1].item_string = locale_string("save_changes", "save changes");
  leave_menu[2].item_string = locale_string("no_save", "no save");
  file_menu[0].item_string = locale_string("file_menu", "file menu");
  file_menu[1].item_string = locale_string("read_file", "read a file");
  file_menu[2].item_string = locale_string("write_file", "write a file");
  file_menu[3].item_string = locale_string("save_file", "save file");
  file_menu[4].item_string = locale_string("print_contents", "print editor contents");
  search_menu[0].item_string = locale_string("search_menu", "search menu");
  search_menu[1].item_string = locale_string("search_for_prompt", "search for ...");
  search_menu[2].item_string = locale_string("search_cmd", "search");
  spell_menu[0].item_string = locale_string("spell_menu", "spell menu");
  spell_menu[1].item_string = locale_string("use_spell", "use 'spell'");
  spell_menu[2].item_string = locale_string("use_ispell", "use 'ispell'");
  misc_menu[0].item_string = locale_string("misc_menu", "miscellaneous menu");
  misc_menu[1].item_string = locale_string("format_paragraph", "format paragraph");
  misc_menu[2].item_string = locale_string("shell_command", "shell command");
  misc_menu[3].item_string = locale_string("check_spelling", "check spelling");
  misc_menu[4].item_string = locale_string("themes_menu", "themes");
  main_menu[0].item_string = locale_string("main_menu", "main menu");
  main_menu[1].item_string = locale_string("leave_editor", "leave editor");
  main_menu[2].item_string = locale_string("help_cmd", "help");
  main_menu[3].item_string = locale_string("file_operations", "file operations");
  main_menu[4].item_string = locale_string("redraw_screen", "redraw screen");
  main_menu[5].item_string = locale_string("settings", "settings");
  main_menu[6].item_string = locale_string("search", "search");
  main_menu[7].item_string = locale_string("miscellaneous", "miscellaneous");
  help_text[0] = locale_string("control_keys_header", "Control keys:                                "
                                 "                              ");
  help_text[1] = locale_string("help_text_1", "^a ascii code           ^i tab               "
                                 "   ^r right                   ");
  help_text[2] = locale_string("help_text_2", "^b bottom of text       ^j newline           "
                                 "   ^t top of text             ");
  help_text[3] = locale_string("help_text_3", "^c command              ^k delete char       "
                                 "   ^u up                      ");
  help_text[4] = locale_string("help_text_4", "^d down                 ^l left              "
                                 "   ^v undelete word           ");
  help_text[5] = locale_string("help_text_5", "^e search prompt        ^m newline           "
                                 "   ^w delete word             ");
  help_text[6] = locale_string("help_text_6", "^f undelete char        ^n next page         "
                                 "   ^x search                  ");
  help_text[7] = locale_string("help_text_7", "^g begin of line        ^o end of line       "
                                 "   ^y delete line             ");
  help_text[8] = locale_string("help_text_8", "^h backspace            ^p prev page         "
                                 "   ^z undelete line           ");
  help_text[9] = locale_string("help_text_9", "^[ (escape) menu        ESC-Enter: exit ee   "
                                 "                              ");
  help_text[10] = locale_string("help_text_blank", "                                            "
                                  "                              ");
  help_text[11] = locale_string("commands_header", "Commands:                                   "
                                  "                              ");
  help_text[12] = locale_string("commands_help_1", "help    : get this info                 "
                                  "file    : print file name          ");
  help_text[13] = locale_string("commands_help_2", "read    : read a file                   "
                                  "char    : ascii code of char       ");
  help_text[14] = locale_string("commands_help_3", "write   : write a file                  "
                                  "case    : case sensitive search    ");
  help_text[15] = locale_string("commands_help_4", "                                        "
                                  "nocase  : case insensitive search  ");
  help_text[16] = locale_string("commands_help_5", "                                        "
                                  "!cmd    : execute \"cmd\" in shell   ");
  help_text[17] = locale_string("commands_help_6", "line    : display line #                0-9 "
                                  "    : go to line \"#\"           ");
  help_text[18] = locale_string("commands_help_7", "expand  : expand tabs                   "
                                  "noexpand: do not expand tabs         ");
  help_text[19] = locale_string("commands_help_8", "                                            "
                                  "                                 ");
  help_text[20] = locale_string("usage_summary", "  ee [+#] [-i] [-e] [-h] [file(s)]          "
                                  "                                  ");
  help_text[21] = locale_string("usage_options", "+# :go to line #  -i :no info window  -e : "
                                  "don't expand tabs  -h :no highlight");

  command_strings[0] =
      locale_string("command_strings_1", "help : get help info  |file  : print file name         "
                      "|line : print line # ");
  command_strings[1] =
      locale_string("command_strings_2", "read : read a file    |char  : ascii code of char      "
                      "|0-9 : go to line \"#\"");
  command_strings[2] =
      locale_string("command_strings_3", "write: write a file   |case  : case sensitive search   "
                      "|exit : leave and save ");
  command_strings[3] =
      locale_string("command_strings_4", "!cmd : shell \"cmd\"    |nocase: ignore case in search  "
                      " |quit : leave, no save");
  command_strings[4] =
      locale_string("command_strings_5", "expand: expand tabs   |noexpand: do not expand tabs     "
                      "                      ");
  com_win_message = locale_string("press_esc_for_menu", "    press Escape (^[) for menu");
  no_file_string = locale_string("no_file", "no file");
  ascii_code_str = locale_string("ascii_code_prompt", "ascii code: ");
  printer_msg_str = locale_string("sending_to_printer", "sending contents of buffer to \"%s\" ");
  command_str = locale_string("command_prompt", "command: ");
  file_write_prompt_str = locale_string("file_write_prompt", "name of file to write: ");
  file_read_prompt_str = locale_string("file_read_prompt", "name of file to read: ");
  char_str = locale_string("character_info", "character = %d");
  unkn_cmd_str = locale_string("unknown_command", "unknown command \"%s\"");
  non_unique_cmd_msg = locale_string("command_not_unique", "entered command is not unique");
  line_num_str = locale_string("line_info", "line %d  ");
  line_len_str = locale_string("length_info", "length = %d");
  current_file_str = locale_string("current_file_info", "current file is \"%s\" ");
  usage0 =
      locale_string("usage_text", "usage: %s [-i] [-e] [-h] [+line_number] [file(s)]\n");
  usage1 = locale_string("usage_opt_i", "       -i   turn off info window\n");
  usage2 = locale_string("usage_opt_e", "       -e   do not convert tabs to spaces\n");
  usage3 = locale_string("usage_opt_h", "       -h   do not use highlighting\n");
  file_is_dir_msg = locale_string("file_is_dir", "file \"%s\" is a directory");
  new_file_msg = locale_string("new_file", "new file \"%s\"");
  cant_open_msg = locale_string("cant_open_file", "can't open \"%s\"");
  open_file_msg = locale_string("file_lines_info", "file \"%s\", %d lines");
  file_read_fin_msg = locale_string("finished_reading", "finished reading file \"%s\"");
  reading_file_msg = locale_string("reading_file", "reading file \"%s\"");
  read_only_msg = locale_string("read_only", ", read only");
  file_read_lines_msg = locale_string("file_lines_count", "file \"%s\", %d lines");
  save_file_name_prompt = locale_string("enter_filename", "enter name of file: ");
  file_not_saved_msg = locale_string("no_filename_saved", "no filename entered: file not saved");
  changes_made_prompt =
      locale_string("changes_made_sure", "changes have been made, are you sure? (y/n [n]) ");
  yes_char = locale_string("yes_char", "y");
  file_exists_prompt =
      locale_string("file_exists_overwrite", "file already exists, overwrite? (y/n) [n] ");
  create_file_fail_msg = locale_string("unable_to_create", "unable to create file \"%s\"");
  writing_file_msg = locale_string("writing_file", "writing file \"%s\"");
  file_written_msg = locale_string("file_written_info", "\"%s\" %d lines, %d characters");
  searching_msg = locale_string("searching", "           ...searching");
  str_not_found_msg = locale_string("string_not_found", "string \"%s\" not found");
  search_prompt_str = locale_string("search_for_prompt", "search for: ");
  exec_err_msg = locale_string("could_not_exec", "could not exec %s\n");
  continue_msg = locale_string("press_return", "press return to continue ");
  menu_cancel_msg = locale_string("press_esc_cancel", "press Esc to cancel");
  menu_size_err_msg = locale_string("menu_too_large", "menu too large for window");
  press_any_key_msg = locale_string("press_any_key", "press any key to continue ");
  shell_prompt = locale_string("shell_command_prompt", "shell command: ");
  formatting_msg = locale_string("formatting_paragraph", "...formatting paragraph...");
  shell_echo_msg =
      locale_string("spell_header", "<!echo 'list of unrecognized words'; echo -=-=-=-=-=-");
  spell_in_prog_msg =
      locale_string("sending_to_spell", "sending contents of edit buffer to 'spell'");
  margin_prompt = locale_string("right_margin_info", "right margin is: ");
  restricted_msg = locale_string("restricted_mode_error",
      "restricted mode: unable to perform requested operation");
  STATE_ON = locale_string("state_on", "ON");
  STATE_OFF = locale_string("state_off", "OFF");
  HELP = locale_string("cmd_help", "HELP");
  MARK_str = locale_string("cmd_mark", "MARK");
  WRITE = locale_string("cmd_write", "WRITE");
  READ = locale_string("cmd_read", "READ");
  LINE = locale_string("cmd_line", "LINE");
  FILE_str = locale_string("cmd_file", "FILE");
  CHARACTER = locale_string("cmd_character", "CHARACTER");
  REDRAW = locale_string("cmd_redraw", "REDRAW");
  RESEQUENCE = locale_string("cmd_resequence", "RESEQUENCE");
  AUTHOR = locale_string("cmd_author", "AUTHOR");
  VERSION = locale_string("cmd_version", "VERSION");
  CASE = locale_string("cmd_case", "CASE");
  NOCASE = locale_string("cmd_nocase", "NOCASE");
  EXPAND = locale_string("cmd_expand", "EXPAND");
  NOEXPAND = locale_string("cmd_noexpand", "NOEXPAND");
  Exit_string = locale_string("cmd_exit", "EXIT");
  QUIT_string = locale_string("cmd_quit", "QUIT");
  INFO = locale_string("cmd_info", "INFO");
  NOINFO = locale_string("cmd_noinfo", "NOINFO");
  MARGINS = locale_string("cmd_margins", "MARGINS");
  NOMARGINS = locale_string("cmd_nomargins", "NOMARGINS");
  AUTOFORMAT = locale_string("cmd_autoformat", "AUTOFORMAT");
  NOAUTOFORMAT = locale_string("cmd_noautoformat", "NOAUTOFORMAT");
  Echo = locale_string("cmd_echo", "ECHO");
  PRINTCOMMAND = locale_string("cmd_printcommand", "PRINTCOMMAND");
  RIGHTMARGIN = locale_string("cmd_rightmargin", "RIGHTMARGIN");
  HIGHLIGHT = locale_string("cmd_highlight", "HIGHLIGHT");
  NOHIGHLIGHT = locale_string("cmd_nohighlight", "NOHIGHLIGHT");
  EIGHTBIT = locale_string("cmd_eightbit", "EIGHTBIT");
  NOEIGHTBIT = locale_string("cmd_noeightbit", "NOEIGHTBIT");
  
  VI_string = locale_string("cmd_vi", "VI");
  NOVI_string = locale_string("cmd_novi", "NOVI");

  emacs_help_text[0] = help_text[0];
  emacs_help_text[1] =
      locale_string("emacs_help_1", "^a beginning of line    ^i tab                  ^r "
                       "restore word            ");
  emacs_help_text[2] =
      locale_string("emacs_help_2", "^b back 1 char          ^j undel char           ^t top "
                       "of text             ");
  emacs_help_text[3] =
      locale_string("emacs_help_3", "^c command              ^k delete line          ^u "
                       "bottom of text          ");
  emacs_help_text[4] =
      locale_string("emacs_help_4", "^d delete char          ^l undelete line        ^v "
                       "next page               ");
  emacs_help_text[5] =
      locale_string("emacs_help_5", "^e end of line          ^m newline              ^w "
                       "delete word             ");
  emacs_help_text[6] =
      locale_string("emacs_help_6", "^f forward 1 char       ^n next line            ^x "
                       "search                  ");
  emacs_help_text[7] =
      locale_string("emacs_help_7", "^g go back 1 page       ^o ascii char insert    ^y "
                       "search prompt           ");
  emacs_help_text[8] =
      locale_string("emacs_help_8", "^h backspace            ^p prev line            ^z "
                       "next word               ");
  emacs_help_text[9] = help_text[9];
  emacs_help_text[10] = help_text[10];
  emacs_help_text[11] = help_text[11];
  emacs_help_text[12] = help_text[12];
  emacs_help_text[13] = help_text[13];
  emacs_help_text[14] = help_text[14];
  emacs_help_text[15] = help_text[15];
  emacs_help_text[16] = help_text[16];
  emacs_help_text[17] = help_text[17];
  emacs_help_text[18] = help_text[18];
  emacs_help_text[19] = help_text[19];
  emacs_help_text[20] = help_text[20];
  emacs_help_text[21] = help_text[21];
  emacs_control_keys[0] =
      locale_string("emacs_control_1", "^[ (escape) menu ^y search prompt ^k delete line   ^p "
                       "prev li     ^g prev page");
  emacs_control_keys[1] =
      locale_string("emacs_control_2", "^o ascii code    ^x search        ^l undelete line ^n "
                       "next li     ^v next page");
  emacs_control_keys[2] =
      locale_string("emacs_control_3", "^u end of file    ^a begin of line  ^w delete word   ^b "
                       "back 1 char ^z next word");
  emacs_control_keys[3] =
      locale_string("emacs_control_4", "^t top of text    ^e end of line    ^r restore word  ^f "
                       "forward char            ");
  emacs_control_keys[4] =
      locale_string("emacs_control_5", "^c command        ^d delete char    ^j undelete char     "
                       "                         ");
  
  EMACS_string = locale_string("cmd_emacs", "EMACS");
  NOEMACS_string = locale_string("cmd_noemacs", "NOEMACS");
  BIND = locale_string("bind_cmd", "BIND");
  GBIND = locale_string("gbind_cmd", "GBIND");
  EBIND = locale_string("ebind_cmd", "EBIND");
  usage4 = locale_string("usage_line_num", "       +#   put cursor at line #\n");
  conf_dump_err_msg = locale_string(
      "config_err_msg", "unable to open .init.ee for writing, no configuration saved!");
  conf_dump_success_msg = locale_string("config_saved_msg", "ee configuration saved in file %s");
  modes_menu[11].item_string = mode_strings[11];
  config_dump_menu[0].item_string = locale_string("save_ee_config", "save ee configuration");
  config_dump_menu[1].item_string = locale_string("save_config", "save configuration");
  conf_not_saved_msg = locale_string("config_not_saved", "ee configuration not saved");
  ree_no_file_msg = locale_string("ree_no_file", "must specify a file when invoking ree");
  menu_too_lrg_msg = locale_string("menu_too_large_alt", "menu too large for window");
  more_above_str = locale_string("more_above", "^^more^^");
  more_below_str = locale_string("more_below", "VVmoreVV");

  commands[0] = HELP;
  commands[1] = WRITE;
  commands[2] = READ;
  commands[3] = LINE;
  commands[4] = FILE_str;
  commands[5] = REDRAW;
  commands[6] = RESEQUENCE;
  commands[7] = AUTHOR;
  commands[8] = VERSION;
  commands[9] = CASE;
  commands[10] = NOCASE;
  commands[11] = EXPAND;
  commands[12] = NOEXPAND;
  commands[13] = MARK_str;
  commands[14] = nullptr;
  commands[15] = "<";
  commands[16] = ">";
  commands[17] = "!";
  commands[18] = "0";
  commands[19] = "1";
  commands[20] = "2";
  commands[21] = "3";
  commands[22] = "4";
  commands[23] = "5";
  commands[24] = "6";
  commands[25] = "7";
  commands[26] = "8";
  commands[27] = "9";
  commands[28] = CHARACTER;
  commands[29] = chinese_cmd;
  commands[30] = nochinese_cmd;
  commands[31] = nullptr;
  init_strings[0] = CASE;
  init_strings[1] = NOCASE;
  init_strings[2] = EXPAND;
  init_strings[3] = NOEXPAND;
  init_strings[4] = INFO;
  init_strings[5] = NOINFO;
  init_strings[6] = MARGINS;
  init_strings[7] = NOMARGINS;
  init_strings[8] = AUTOFORMAT;
  init_strings[9] = NOAUTOFORMAT;
  init_strings[10] = Echo;
  init_strings[11] = PRINTCOMMAND;
  init_strings[12] = RIGHTMARGIN;
  init_strings[13] = HIGHLIGHT;
  init_strings[14] = NOHIGHLIGHT;
  init_strings[15] = EIGHTBIT;
  init_strings[16] = NOEIGHTBIT;
  init_strings[17] = EMACS_string;
  init_strings[18] = NOEMACS_string;
  init_strings[19] = chinese_cmd;
  init_strings[20] = nochinese_cmd;
  init_strings[21] = BIND;
  init_strings[22] = GBIND;
  init_strings[23] = EBIND;
  init_strings[24] = nullptr;
  

  /*
   |	allocate space for strings here for settings menu
   */

  for (counter = 1; counter < NUM_MODES_ITEMS; counter++) {
    modes_menu[counter].item_string = malloc(80);
  }
}

void control_undo(void) {
  undo_perform(&undo_state);
}

void control_redo(void) {
  undo_redo(&undo_state);
}
