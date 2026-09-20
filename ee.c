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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif


#include "ee.h"
#include "delete.h"
#include "fileio.h"
#include "format.h"
#include "input.h"
#include "lsp.h"
#include "menu.h"
#include "render.h"
#include "search.h"
#include "state.h"
#include "theme.h"
#include "undo.h"

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
void command_prompt(void);
void gold_toggle(void);
void gold_append(void);
void gold_search_reverse(void);
void resize_info_win(void);
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

// Global state (declared in state.h)
extern struct text *first_line;
extern struct text *curr_line;
extern struct text *tmp_line;
extern struct files *top_of_stack;
extern undo_buffer undo_state;

static constexpr int char_len_table[256] = {[0 ... 8] = 2,   [9] = -1,
                                            [10 ... 31] = 2, [32 ... 126] = 1,
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

/* number of lines from top		*/

/* flag indicating paragraph formatted	*/
#ifdef HAS_AUTOFORMAT
/* flag for auto_format mode		*/
#endif

/* allows handling of multi-byte characters  */
/* by checking for high bit in a byte the    */
/* code recognizes a two-byte character      */
/* sequence				     */

/* points to current position in line	*/

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

#define MAX_INFO_LINES 64
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
char *com_win_message; /* to be shown in com_win if no info window */
char *menu_too_lrg_msg;
char *more_above_str;
char *more_below_str;
char const *separator =
    " ^ = Ctrl key  ---- access HELP through menu ---"
    "============================================================"
    "===================";

char *chinese_cmd;
char *nochinese_cmd;

void gold_toggle(void);

/* resize the line to length + factor*/
unsigned char *resiz_line(int factor, struct text *restrict rline, int rpos) {
  int new_max = rline->max_length + factor;
  if (ckd_add(&new_max, rline->max_length, factor))
    return nullptr;
  unsigned char *new_line = realloc(rline->line, new_max);
  if (!new_line)
    return nullptr;
  rline->line = new_line;
  rline->max_length = new_max;
  return rline->line + rpos - 1;
}

/* insert character into line		*/
void insert(int character) {
  int counter;
  int value;

  if (curr_line == nullptr || curr_line->line == nullptr ||
      position < 1 || position > curr_line->line_length ||
      curr_line->line_length > curr_line->max_length) {
    return;
  }

#ifdef HAS_ICU
  if (character > 127 &&
      (character > 0x10FFFF ||
       (character >= 0xD800 && character <= 0xDFFF))) {
    return;
  }
#endif

  if ((character == '\t') && expand_tabs) {
    int spaces = len_char('\t', scr_horz);
    while (spaces--) {
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

  // Ensure space is available (branchless allocation)
  if (curr_line->max_length - curr_line->line_length < utf8_len + 1) {
    point = resiz_line(utf8_len + 10, curr_line, position);
    if (!point)
      return; // Allocation failed
  }

  text_changes = true;
  lsp_change_pending = true;
  size_t move_len = curr_line->line_length - position;
  /* Bulk move + copy (safe: utf8_buf is local, no overlap) */
  memmove(point + utf8_len, point, move_len);
  memcpy(point, utf8_buf, utf8_len); // Safe for non-overlapping local buffers
  curr_line->line_length += utf8_len;

  // Update screen once for the whole character
#ifdef HAS_ICU
  if (ee_chinese) {
    if (character == '\t' || character < 32 || character == 127) {
      scr_horz += u_char_width(character, scr_horz);
      out_char(text_win, character, scr_horz);
    } else {
      waddnstr(text_win, (const char *)utf8_buf, (int)utf8_len);
      scr_horz += u_char_width(character, scr_horz);
    }
  } else {
    // Branchless: Use len_char table
    int char_len = len_char(character, scr_horz);
    scr_horz += char_len;
    if (char_len == 1) {
      ee_waddch(text_win, character);
    } else {
      out_char(text_win, character, scr_horz);
    }
  }
#else
  // Branchless: Use len_char table
  int char_len = len_char(character, scr_horz);
  scr_horz += char_len;
  if (char_len == 1) {
    ee_waddch(text_win, character);
  } else {
    out_char(text_win, character, scr_horz);
  }
#endif

  scr_pos = scr_horz;
  point += utf8_len;
  position += utf8_len;

  ee_wclrtoeol(text_win);

  if (observ_margins && (right_margin < scr_pos)) {
    counter = position;
    while (scr_pos > right_margin) {
      if (ee_chinese && position > 1)
        left(1);
      else
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
  if (auto_format && (character == ' ') && (!formatted) &&
      !formatting_in_progress && !pasting_mode) {
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
    undo_record(&undo_state, UNDO_INSERT, curr_line->line_number, position,
                utf8_len, (unsigned char *)utf8_buf);
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
  return 1;
}
#endif

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

/* insert new line		*/
void insert_line(int disp) {
  int temp_pos;
  int temp_pos2;
  unsigned char *temp;
  unsigned char *extra;
  struct text *temp_nod;

  text_changes = true;
  lsp_change_pending = true;
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
      if (ckd_add(&new_max, (int)split_len, 10))
        return;
      unsigned char *new_line = realloc(temp_nod->line, new_max);
      if (!new_line)
        return;
      temp_nod->line = new_line;
      temp_nod->max_length = new_max;
    }
    memcpy(temp_nod->line, temp, split_len);
    temp_nod->line_length = split_len;
    curr_line->line_length = temp_pos2;
    *temp = '\0';
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

/*
 |	Emacs control-key bindings
 */

/* go to bottom of file			*/

/* move up one line		*/
void up() {
  if (curr_line->prev_line != nullptr) {
    prevline();
    point = curr_line->line;
    find_pos();
    scr_pos = scr_horz;
  }
}

/* move down one line		*/
void down() {
  if (curr_line->next_line != nullptr) {
    nextline();
    find_pos();
    scr_pos = scr_horz;
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

/* read string from input in a menu-like popup input field */
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
    if (!profiling_mode)
      def_prog_mode();
    if (!profiling_mode)
      endwin();

    printf("\r%s", prompt);
    fflush(stdout);

    line = el_gets(el, &count);

    if (!profiling_mode)
      reset_prog_mode();
    if (!profiling_mode)
      refresh();
    if (!profiling_mode)
      touchwin(text_win);
    ee_wrefresh(text_win);

    if (line != nullptr && count > 0) {
      string = malloc(count + 1);
      strscpy(string, line, count + 1);
      char *nl = strchr(string, '\n');
      if (nl)
        *nl = '\0';
      nl = strchr(string, '\r');
      if (nl)
        *nl = '\0';

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

  const int field_w = max(24, min(COLS - 8, 60));
  const int field_h = 6;
  const int win_y = max(1, (LINES - field_h) / 2);
  const int win_x = max(0, (COLS - field_w) / 2);
  WINDOW *input_win = newwin(field_h, field_w, win_y, win_x);
  if (input_win == nullptr) {
    return strdup("");
  }
  keypad(input_win, true);

  char buffer[512] = {0};
  char *tmp = nullptr;
  int cursor = 0;
  int ch;

  if (prompt != nullptr) {
    const char *existing = prompt;
    if (existing[0] == ' ' || existing[0] == '\t') {
      existing = next_word((char *)existing);
    }
    (void)existing;
  }

  while (true) {
    werase(input_win);
    box(input_win, 0, 0);
    mvwaddstr(input_win, 1, 2, prompt ? prompt : "Input:");
    mvwhline(input_win, 3, 2, ACS_HLINE, field_w - 4);
    if (buffer[0] != '\0') {
      mvwaddnstr(input_win, 3, 2, buffer, field_w - 6);
    }
    wmove(input_win, 3, min(field_w - 3, 2 + cursor));
    wrefresh(input_win);

    ch = wgetch(input_win);
    if (ch == -1) {
      edit_abort(0);
    }
    if (ch == 27) {
      buffer[0] = '\0';
      cursor = 0;
      break;
    }
    if (ch == '\n' || ch == '\r') {
      break;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (cursor > 0) {
        memmove(&buffer[cursor - 1], &buffer[cursor], strlen(buffer + cursor) + 1);
        cursor--;
      }
      continue;
    }
    if (ch == KEY_DC || ch == 4) {
      if (cursor < (int)strlen(buffer)) {
        memmove(&buffer[cursor], &buffer[cursor + 1], strlen(buffer + cursor + 1) + 1);
      }
      continue;
    }
    if (ch == 22) {
      ch = wgetch(input_win);
      if (ch == -1) {
        edit_abort(0);
      }
    }
    if (ch >= 32 && ch <= 126) {
      size_t len = strlen(buffer);
      if (len + 1 < sizeof(buffer)) {
        memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor + 1);
        buffer[cursor++] = (char)ch;
      }
    }
  }

  tmp = buffer;
  if (advance && ((tmp[0] == ' ') || (tmp[0] == '\t'))) {
    tmp = next_word(tmp);
  }

  string = strdup(tmp);
  delwin(input_win);
  if (string == nullptr) {
    string = strdup("");
  }
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

/* walk count lines in the given direction, reporting how far we went */
static struct text *walk_lines(struct text *line, int count, int *actual_count,
                               bool const forward) {
  struct text *curr = line;
  int i = 0;
  while (curr != nullptr && i < count &&
         (forward ? curr->next_line : curr->prev_line) != nullptr) {
    curr = forward ? curr->next_line : curr->prev_line;
    i++;
  }
  if (actual_count != nullptr)
    *actual_count += i;
  return curr;
}

struct text *find_next_recursive(struct text *line, int count,
                                 int *actual_count) {
  return walk_lines(line, count, actual_count, true);
}

struct text *find_prev_recursive(struct text *line, int count,
                                 int *actual_count) {
  return walk_lines(line, count, actual_count, false);
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
void get_line(int length, unsigned char *restrict in_string,
              int *restrict append) {
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

/* prepare to exit edit session	*/

/* exit editor			*/
int quit(int noverify) {
  char *ans;

  if (!profiling_mode)
    touchwin(text_win);
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
    if (!profiling_mode)
      endwin();
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
  if (!profiling_mode)
    endwin();
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

/* move to beginning of line	*/

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
    if (!profiling_mode)
      initscr();
    if (!profiling_mode)
      savetty();
    if (!profiling_mode)
      noecho();
    if (!profiling_mode)
      raw();
    if (!profiling_mode)
      nonl();

    if (has_colors()) {
      if (!profiling_mode)
        start_color();
      if (!profiling_mode)
        use_default_colors();
      if (!profiling_mode)
        init_pair(1, COLOR_GREEN, -1); // comment
      if (!profiling_mode)
        init_pair(2, COLOR_YELLOW, -1); // string
      if (!profiling_mode)
        init_pair(3, COLOR_CYAN, -1); // number
      if (!profiling_mode)
        init_pair(4, COLOR_YELLOW, -1); // type
      if (!profiling_mode)
        init_pair(5, COLOR_BLUE, -1); // function
      if (!profiling_mode)
        init_pair(6, COLOR_WHITE, -1); // variable
      if (!profiling_mode)
        init_pair(7, COLOR_MAGENTA, -1); // keyword
      if (!profiling_mode)
        init_pair(8, COLOR_RED, -1); // error/diagnostic
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
    char *str =
        (emacs_keys_mode) ? emacs_help_text[counter] : help_text[counter];
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
static void buf_append(char *restrict buf, size_t *restrict pos, size_t cap,
                       const char *restrict s) {
  while (*s != '\0' && *pos < cap - 1) {
    buf[(*pos)++] = *s++;
  }
  buf[*pos] = '\0';
}

static const char *shortcut_description(control_handler handler) {
  for (int i = 0; commands_table[i].name != nullptr; i++) {
    if (commands_table[i].handler == handler)
      return commands_table[i].short_desc;
  }
  if (handler == bottom)
    return "move to bottom of text";
  if (handler == top)
    return "move to top of text";
  if (handler == bol)
    return "move to beginning of line";
  if (handler == eol)
    return "move to end of line";
  if (handler == del_char)
    return "delete character at cursor";
  if (handler == del_line)
    return "delete current line";
  if (handler == del_word)
    return "delete word at cursor";
  if (handler == adv_word)
    return "advance to next word";
  if (handler == prev_word)
    return "move to previous word";
  if (handler == redraw)
    return "redraw the screen";
  if (handler == command_prompt)
    return "enter command mode";
  if (handler == set_mark)
    return "set mark for region";
  if (handler == paste_region)
    return "paste clipboard at cursor";
  if (handler == replace_prompt)
    return "prompt for replacement";
  if (handler == gold_toggle)
    return "toggle GOLD mode";
  return nullptr;
}

static void append_shortcuts(char *restrict buf, size_t *restrict pos,
                             size_t cap, control_handler *table, int count) {
  for (int i = 0; i < count; i++) {
    control_handler handler = table[i];
    if (handler == nullptr || handler == no_op)
      continue;

    const char *description = shortcut_description(handler);
    if (description == nullptr)
      description = function_key_description(handler);
    if (description == nullptr)
      description = "function key action";

    char item[128];
    int n = snprintf(item, sizeof(item), "%s %s  ", get_key_name(i),
                     description);
    size_t item_len = (n > 0 && (size_t)n < sizeof(item))
                          ? (size_t)n
                          : sizeof(item) - 1;
    if (*pos + item_len >= cap - 1)
      break;
    buf_append(buf, pos, cap, item);
    buf_append(buf, pos, cap, "\x1f");
  }
}

void generate_dynamic_info() {
  static char total_buf[16384];
  static char lines_buf[MAX_INFO_LINES][512];
  size_t buf_pos = 0;
  total_buf[0] = '\0';
  num_info_lines = 0;

  if (!info_window)
    return;

  control_handler *tbl = base_control_table;
  if (gold)
    tbl = gold_control_table;
  else if (emacs_keys_mode)
    tbl = emacs_control_table;

  // Add mandatory main menu hint if menu is enabled
#ifdef HAS_MENU
  buf_append(total_buf, &buf_pos, sizeof(total_buf), "Esc menu");
#endif

  buf_append(total_buf, &buf_pos, sizeof(total_buf), " ");
  append_shortcuts(total_buf, &buf_pos, sizeof(total_buf), tbl, 1024);

  control_handler *fn_tbl = gold ? gold_fn_key_table : base_fn_key_table;
  if (function_keys_visible) {
    buf_append(total_buf, &buf_pos, sizeof(total_buf), "\n");
    append_shortcuts(total_buf, &buf_pos, sizeof(total_buf), fn_tbl, 512);
  }

  // Word wrap
  int width = COLS;
  if (width <= 0)
    width = 80;

  char *p = total_buf;
  while (*p != '\0' && num_info_lines < MAX_INFO_LINES - 1) {
    char *newline = strchr(p, '\n');
    size_t line_len = newline == nullptr ? strlen(p) : (size_t)(newline - p);

    if (line_len == 0) {
      dynamic_info_lines[num_info_lines] = lines_buf[num_info_lines];
      lines_buf[num_info_lines++][0] = '\0';
      p += newline == nullptr ? 0 : 1;
      continue;
    }

    while (line_len > 0 && num_info_lines < MAX_INFO_LINES - 1) {
      size_t split = line_len;
      if (split > (size_t)(width - 1)) {
        split = (size_t)(width - 1);
        while (split > 0 && p[split] != '\x1f')
          split--;
        if (split == 0)
          split = (size_t)(width - 1);
      }

      size_t copy_len = min(split, sizeof(lines_buf[0]) - 1);
      int line_index = num_info_lines++;
      memcpy(lines_buf[line_index], p, copy_len);
      for (size_t i = 0; i < copy_len; i++) {
        if (lines_buf[line_index][i] == '\x1f')
          lines_buf[line_index][i] = ' ';
      }
      lines_buf[line_index][copy_len] = '\0';
      dynamic_info_lines[line_index] = lines_buf[line_index];
      p += split;
      line_len -= split;
      if (line_len > 0 && *p == '\x1f') {
        p++;
        line_len--;
      }
      while (line_len > 0 && *p == ' ') {
        p++;
        line_len--;
      }
    }

    if (*p == '\n')
      p++;
  }
}

int get_info_win_height() {
  if (!info_window)
    return 0;
  generate_dynamic_info();
  return max(1, num_info_lines + 1);
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
    if (!profiling_mode)
      clearok(info_win, true);
    paint_info_win();
  } else {
    {
      if (!profiling_mode)
        clearok(text_win, true);
    }
  }
  scanline(point);
  draw_screen();
  ee_wrefresh(text_win);
  ee_wrefresh(com_win);
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
    if (!yh)
      yh = "/tmp";
    snprintf(yaml_path, sizeof(yaml_path), "%s/.config/ee/config.yaml", yh);
    FILE *yf = fopen(yaml_path, "r");
    if (yf) {
      char yline[512];
      while (fgets(yline, sizeof(yline), yf)) {
        char *ye = yline + strlen(yline);
        while (ye > yline && (ye[-1] == '\n' || ye[-1] == '\r' ||
                              ye[-1] == ' ' || ye[-1] == '\t'))
          ye--;
        *ye = '\0';
        if (yline[0] == '\0' || yline[0] == '#')
          continue;
        char *yc = strstr(yline, ": ");
        if (!yc)
          continue;
        *yc = '\0';
        char *yk = yline;
        char *yv = yc + 2;
        if (strcmp(yk, "case") == 0)
          case_sen = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "expand") == 0)
          expand_tabs = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "info") == 0) {
          info_window = strcmp(yv, "true") == 0;
          resize_info_win();
        } else if (strcmp(yk, "function_keys") == 0) {
          function_keys_visible = strcmp(yv, "true") == 0;
          resize_info_win();
        } else if (strcmp(yk, "margins") == 0)
          observ_margins = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "autoformat") == 0) {
          auto_format = strcmp(yv, "true") == 0;
          if (auto_format)
            observ_margins = true;
        } else if (strcmp(yk, "printcommand") == 0 && yv[0]) {
          size_t cl = strlen(yv) + 1;
          print_command = malloc(cl);
          snprintf(print_command, cl, "%s", yv);
        } else if (strcmp(yk, "rightmargin") == 0) {
          int ti = atoi(yv);
          if (ti > 0)
            right_margin = ti;
        } else if (strcmp(yk, "highlight") == 0)
          nohighlight = strcmp(yv, "false") == 0;
        else if (strcmp(yk, "eightbit") == 0)
          eightbit = strcmp(yv, "true") == 0;
        else if (strcmp(yk, "emacs") == 0) {
          emacs_keys_mode = strcmp(yv, "true") == 0;
          update_libedit_mode();
        } else if (strcmp(yk, "theme") == 0 && yv[0]) {
          snprintf(theme_name, sizeof(theme_name), "%s", yv);
        } else if (strncmp(yk, "bind(", 5) == 0) {
          char *cp = yk + 5;
          char *cl = strchr(cp, ')');
          if (cl) {
            *cl = '\0';
            bind_key(cp, yv, 0);
          }
        } else if (strncmp(yk, "gbind(", 6) == 0) {
          char *cp = yk + 6;
          char *cl = strchr(cp, ')');
          if (cl) {
            *cl = '\0';
            bind_key(cp, yv, 1);
          }
        } else if (strncmp(yk, "ebind(", 6) == 0) {
          char *cp = yk + 6;
          char *cl = strchr(cp, ')');
          if (cl) {
            *cl = '\0';
            bind_key(cp, yv, 2);
          }
        }
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
  if (!home)
    home = "/tmp";
  snprintf(buf, size, "%s/.config/ee/config.yaml", home);
}

static void ensure_config_dir(void) {
  const char *home = getenv("HOME");
  if (!home)
    return;
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/.config", home);
  mkdir(dir, 0755);
  snprintf(dir, sizeof(dir), "%s/.config/ee", home);
  mkdir(dir, 0755);
}

void dump_ee_conf(void) {
  if (restrict_mode())
    return;

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
    fprintf(f, "function_keys: %s\n",
      function_keys_visible ? "true" : "false");
  fprintf(f, "margins: %s\n", observ_margins ? "true" : "false");
  fprintf(f, "autoformat: %s\n", auto_format ? "true" : "false");
  fprintf(f, "printcommand: %s\n", print_command ? print_command : "lpr");
  fprintf(f, "rightmargin: %d\n", right_margin);
  fprintf(f, "highlight: %s\n", nohighlight ? "false" : "true");
  fprintf(f, "eightbit: %s\n", eightbit ? "true" : "false");
  fprintf(f, "emacs: %s\n", emacs_keys_mode ? "true" : "false");
  fprintf(f, "theme: %s\n", theme_name[0] ? theme_name : "default");

  for (int t = 0; t < 3; t++) {
    control_handler *tbl =
        t == 0 ? base_control_table
               : (t == 1 ? gold_control_table : emacs_control_table);
    const char *prefix = t == 0 ? "bind" : (t == 1 ? "gbind" : "ebind");
    for (int i = 0; i < 1024; i++) {
      if (tbl[i] == no_op)
        continue;
      const char *cmd_name = nullptr;
      for (int j = 0; commands_table[j].name; j++) {
        if (commands_table[j].handler == tbl[i]) {
          cmd_name = commands_table[j].name;
          break;
        }
      }
      if (cmd_name)
        fprintf(f, "%s(%s): %s\n", prefix, get_key_name(i), cmd_name);
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
[[nodiscard]] char *locale_string(const char *key, char *fallback) {
  return fallback;
}
#endif /* HAS_ICU */

/*
 |	ICU resource bundles provide localized strings. The root.res file
 |	(compiled from ee.txt via genrb) contains the default (English)
 |	translations. System locale detection picks up the appropriate
 |	bundle — ee.txt can be translated and compiled per-locale.
 */

const char *get_key_name(int i) {
  static char key[16];
  if (i == KEY_LEFT)
    return "Left";
  if (i == KEY_RIGHT)
    return "Right";
  if (i == KEY_UP)
    return "Up";
  if (i == KEY_DOWN)
    return "Down";
  if (i == KEY_HOME)
    return "Home";
  if (i == KEY_END)
    return "End";
  if (i == KEY_NPAGE)
    return "PageDown";
  if (i == KEY_PPAGE)
    return "PageUp";
  if (i == KEY_DL)
    return "DeleteLine";
  if (i == KEY_DC)
    return "Delete";
  if (i == KEY_IL)
    return "InsertLine";
  if (i == KEY_BACKSPACE)
    return "Backspace";
  if (i >= 256 && i < 512) {
    const char *name = keyname(i);
    if (name != nullptr)
      return name;
  }
  if (i == 0)
    return "^@";
  if (i < 27) {
    snprintf(key, sizeof(key), "^%c", i + '@');
    return key;
  }
  if (i == 27)
    return "^[";
  if (i == 28)
    return "^\\";
  if (i == 29)
    return "^]";
  if (i == 30)
    return "^^";
  if (i == 31)
    return "^_";
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
  const struct command_map *cmd = find_command(cmd_name);
  if (cmd == nullptr || cmd->handler == nullptr)
    return (char *)"";
  control_handler h = cmd->handler;
  const char *short_desc = cmd->short_desc;

  const char *key = get_key_binding(h, table);
  if (key[0] == '\0')
    return (char *)"";
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
  mode_strings[2] =
      locale_string("case_sensitive_search", "case sensitive search");
  mode_strings[3] = locale_string("margins_observed", "margins observed     ");
  mode_strings[4] =
      locale_string("auto_paragraph_format", "auto-paragraph format");
  mode_strings[5] =
      locale_string("eightbit_characters", "eightbit characters  ");
  mode_strings[6] =
      locale_string("info_window_toggle", "info window          ");
  mode_strings[7] =
      locale_string("emacs_key_bindings", "emacs key bindings   ");
  mode_strings[8] = locale_string("vi_key_bindings", "vi key bindings      ");
  mode_strings[9] =
      locale_string("right_margin_toggle", "right margin         ");
  mode_strings[10] =
      locale_string("sixteen_bit_chars", "16 bit characters    ");
  mode_strings[11] =
      locale_string("function_keys_toggle", "function keys");
    mode_strings[12] =
      locale_string("save_editor_config", "save editor configuration");

  leave_menu[0].item_string = locale_string("leave_menu", "leave menu");
  leave_menu[1].item_string = locale_string("save_changes", "save changes");
  leave_menu[2].item_string = locale_string("no_save", "no save");
  file_menu[0].item_string = locale_string("file_menu", "file menu");
  file_menu[1].item_string = locale_string("read_file", "read a file");
  file_menu[2].item_string = locale_string("write_file", "write a file");
  file_menu[3].item_string = locale_string("save_file", "save file");
  file_menu[4].item_string =
      locale_string("print_contents", "print editor contents");
  search_menu[0].item_string = locale_string("search_menu", "search menu");
  search_menu[1].item_string =
      locale_string("search_for_prompt", "search for ...");
  search_menu[2].item_string = locale_string("search_cmd", "search");
  spell_menu[0].item_string = locale_string("spell_menu", "spell menu");
  spell_menu[1].item_string = locale_string("use_spell", "use 'spell'");
  spell_menu[2].item_string = locale_string("use_ispell", "use 'ispell'");
  misc_menu[0].item_string = locale_string("misc_menu", "miscellaneous menu");
  misc_menu[1].item_string =
      locale_string("format_paragraph", "format paragraph");
  misc_menu[2].item_string = locale_string("shell_command", "shell command");
  misc_menu[3].item_string = locale_string("check_spelling", "check spelling");
  misc_menu[4].item_string = locale_string("themes_menu", "themes");
  main_menu[0].item_string = locale_string("main_menu", "main menu");
  main_menu[1].item_string = locale_string("leave_editor", "leave editor");
  main_menu[2].item_string = locale_string("help_cmd", "help");
  main_menu[3].item_string =
      locale_string("file_operations", "file operations");
  main_menu[4].item_string = locale_string("redraw_screen", "redraw screen");
  main_menu[5].item_string = locale_string("settings", "settings");
  main_menu[6].item_string = locale_string("search", "search");
  main_menu[7].item_string = locale_string("miscellaneous", "miscellaneous");
  help_text[0] = locale_string("control_keys_header",
                               "Control keys:                                "
                               "                              ");
  help_text[1] = locale_string("help_text_1",
                               "^a ascii code           ^i tab               "
                               "   ^r right                   ");
  help_text[2] = locale_string("help_text_2",
                               "^b bottom of text       ^j newline           "
                               "   ^t top of text             ");
  help_text[3] = locale_string("help_text_3",
                               "^c command              ^k delete char       "
                               "   ^u up                      ");
  help_text[4] = locale_string("help_text_4",
                               "^d down                 ^l left              "
                               "   ^v undelete word           ");
  help_text[5] = locale_string("help_text_5",
                               "^e search prompt        ^m newline           "
                               "   ^w delete word             ");
  help_text[6] = locale_string("help_text_6",
                               "^f undelete char        ^n next page         "
                               "   ^x search                  ");
  help_text[7] = locale_string("help_text_7",
                               "^g begin of line        ^o end of line       "
                               "   ^y delete line             ");
  help_text[8] = locale_string("help_text_8",
                               "^h backspace            ^p prev page         "
                               "   ^z undelete line           ");
  help_text[9] = locale_string("help_text_9",
                               "^[ (escape) menu        ESC-Enter: exit ee   "
                               "                              ");
  help_text[10] = locale_string("help_text_blank",
                                "                                            "
                                "                              ");
  help_text[11] = locale_string("commands_header",
                                "Commands:                                   "
                                "                              ");
  help_text[12] = locale_string("commands_help_1",
                                "help    : get this info                 "
                                "file    : print file name          ");
  help_text[13] = locale_string("commands_help_2",
                                "read    : read a file                   "
                                "char    : ascii code of char       ");
  help_text[14] = locale_string("commands_help_3",
                                "write   : write a file                  "
                                "case    : case sensitive search    ");
  help_text[15] = locale_string("commands_help_4",
                                "                                        "
                                "nocase  : case insensitive search  ");
  help_text[16] = locale_string("commands_help_5",
                                "                                        "
                                "!cmd    : execute \"cmd\" in shell   ");
  help_text[17] = locale_string("commands_help_6",
                                "line    : display line #                0-9 "
                                "    : go to line \"#\"           ");
  help_text[18] = locale_string("commands_help_7",
                                "expand  : expand tabs                   "
                                "noexpand: do not expand tabs         ");
  help_text[19] = locale_string("commands_help_8",
                                "                                            "
                                "                                 ");
  help_text[20] = locale_string("usage_summary",
                                "  ee [+#] [-i] [-e] [-h] [file(s)]          "
                                "                                  ");
  help_text[21] = locale_string("usage_options",
                                "+# :go to line #  -i :no info window  -e : "
                                "don't expand tabs  -h :no highlight");

  command_strings[0] =
      locale_string("command_strings_1",
                    "help : get help info  |file  : print file name         "
                    "|line : print line # ");
  command_strings[1] =
      locale_string("command_strings_2",
                    "read : read a file    |char  : ascii code of char      "
                    "|0-9 : go to line \"#\"");
  command_strings[2] =
      locale_string("command_strings_3",
                    "write: write a file   |case  : case sensitive search   "
                    "|exit : leave and save ");
  command_strings[3] =
      locale_string("command_strings_4",
                    "!cmd : shell \"cmd\"    |nocase: ignore case in search  "
                    " |quit : leave, no save");
  command_strings[4] =
      locale_string("command_strings_5",
                    "expand: expand tabs   |noexpand: do not expand tabs     "
                    "                      ");
  com_win_message =
      locale_string("press_esc_for_menu", "    press Escape (^[) for menu");
  no_file_string = locale_string("no_file", "no file");
  ascii_code_str = locale_string("ascii_code_prompt", "ascii code: ");
  printer_msg_str = locale_string("sending_to_printer",
                                  "sending contents of buffer to \"%s\" ");
  command_str = locale_string("command_prompt", "command: ");
  file_write_prompt_str =
      locale_string("file_write_prompt", "name of file to write: ");
  file_read_prompt_str =
      locale_string("file_read_prompt", "name of file to read: ");
  char_str = locale_string("character_info", "character = %d");
  unkn_cmd_str = locale_string("unknown_command", "unknown command \"%s\"");
  non_unique_cmd_msg =
      locale_string("command_not_unique", "entered command is not unique");
  line_num_str = locale_string("line_info", "line %d  ");
  line_len_str = locale_string("length_info", "length = %d");
  current_file_str =
      locale_string("current_file_info", "current file is \"%s\" ");
  usage0 = locale_string("usage_text",
                         "usage: %s [-i] [-e] [-h] [+line_number] [file(s)]\n");
  usage1 = locale_string("usage_opt_i", "       -i   turn off info window\n");
  usage2 = locale_string("usage_opt_e",
                         "       -e   do not convert tabs to spaces\n");
  usage3 =
      locale_string("usage_opt_h", "       -h   do not use highlighting\n");
  file_is_dir_msg = locale_string("file_is_dir", "file \"%s\" is a directory");
  new_file_msg = locale_string("new_file", "new file \"%s\"");
  cant_open_msg = locale_string("cant_open_file", "can't open \"%s\"");
  open_file_msg = locale_string("file_lines_info", "file \"%s\", %d lines");
  file_read_fin_msg =
      locale_string("finished_reading", "finished reading file \"%s\"");
  reading_file_msg = locale_string("reading_file", "reading file \"%s\"");
  read_only_msg = locale_string("read_only", ", read only");
  file_read_lines_msg =
      locale_string("file_lines_count", "file \"%s\", %d lines");
  save_file_name_prompt =
      locale_string("enter_filename", "enter name of file: ");
  file_not_saved_msg =
      locale_string("no_filename_saved", "no filename entered: file not saved");
  changes_made_prompt = locale_string(
      "changes_made_sure", "changes have been made, are you sure? (y/n [n]) ");
  yes_char = locale_string("yes_char", "y");
  file_exists_prompt = locale_string(
      "file_exists_overwrite", "file already exists, overwrite? (y/n) [n] ");
  create_file_fail_msg =
      locale_string("unable_to_create", "unable to create file \"%s\"");
  writing_file_msg = locale_string("writing_file", "writing file \"%s\"");
  file_written_msg =
      locale_string("file_written_info", "\"%s\" %d lines, %d characters");
  searching_msg = locale_string("searching", "           ...searching");
  str_not_found_msg =
      locale_string("string_not_found", "string \"%s\" not found");
  search_prompt_str = locale_string("search_for_prompt", "search for: ");
  exec_err_msg = locale_string("could_not_exec", "could not exec %s\n");
  continue_msg = locale_string("press_return", "press return to continue ");
  menu_cancel_msg = locale_string("press_esc_cancel", "press Esc to cancel");
  menu_size_err_msg =
      locale_string("menu_too_large", "menu too large for window");
  press_any_key_msg =
      locale_string("press_any_key", "press any key to continue ");
  shell_prompt = locale_string("shell_command_prompt", "shell command: ");
  formatting_msg =
      locale_string("formatting_paragraph", "...formatting paragraph...");
  shell_echo_msg = locale_string(
      "spell_header", "<!echo 'list of unrecognized words'; echo -=-=-=-=-=-");
  spell_in_prog_msg = locale_string(
      "sending_to_spell", "sending contents of edit buffer to 'spell'");
  margin_prompt = locale_string("right_margin_info", "right margin is: ");
  restricted_msg =
      locale_string("restricted_mode_error",
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
  emacs_help_text[1] = locale_string(
      "emacs_help_1", "^a beginning of line    ^i tab                  ^r "
                      "restore word            ");
  emacs_help_text[2] = locale_string(
      "emacs_help_2", "^b back 1 char          ^j undel char           ^t top "
                      "of text             ");
  emacs_help_text[3] = locale_string(
      "emacs_help_3", "^c command              ^k delete line          ^u "
                      "bottom of text          ");
  emacs_help_text[4] = locale_string(
      "emacs_help_4", "^d delete char          ^l undelete line        ^v "
                      "next page               ");
  emacs_help_text[5] = locale_string(
      "emacs_help_5", "^e end of line          ^m newline              ^w "
                      "delete word             ");
  emacs_help_text[6] = locale_string(
      "emacs_help_6", "^f forward 1 char       ^n next line            ^x "
                      "search                  ");
  emacs_help_text[7] = locale_string(
      "emacs_help_7", "^g go back 1 page       ^o ascii char insert    ^y "
                      "search prompt           ");
  emacs_help_text[8] = locale_string(
      "emacs_help_8", "^h backspace            ^p prev line            ^z "
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
      locale_string("emacs_control_1",
                    "^[ (escape) menu ^y search prompt ^k delete line   ^p "
                    "prev li     ^g prev page");
  emacs_control_keys[1] =
      locale_string("emacs_control_2",
                    "^o ascii code    ^x search        ^l undelete line ^n "
                    "next li     ^v next page");
  emacs_control_keys[2] =
      locale_string("emacs_control_3",
                    "^u end of file    ^a begin of line  ^w delete word   ^b "
                    "back 1 char ^z next word");
  emacs_control_keys[3] =
      locale_string("emacs_control_4",
                    "^t top of text    ^e end of line    ^r restore word  ^f "
                    "forward char            ");
  emacs_control_keys[4] =
      locale_string("emacs_control_5",
                    "^c command        ^d delete char    ^j undelete char     "
                    "                         ");

  EMACS_string = locale_string("cmd_emacs", "EMACS");
  NOEMACS_string = locale_string("cmd_noemacs", "NOEMACS");
  BIND = locale_string("bind_cmd", "BIND");
  GBIND = locale_string("gbind_cmd", "GBIND");
  EBIND = locale_string("ebind_cmd", "EBIND");
  usage4 =
      locale_string("usage_line_num", "       +#   put cursor at line #\n");
  conf_dump_err_msg = locale_string(
      "config_err_msg",
      "unable to open .init.ee for writing, no configuration saved!");
  conf_dump_success_msg =
      locale_string("config_saved_msg", "ee configuration saved in file %s");
  modes_menu[12].item_string = mode_strings[12];
  config_dump_menu[0].item_string =
      locale_string("save_ee_config", "save ee configuration");
  config_dump_menu[1].item_string =
      locale_string("save_config", "save configuration");
  conf_not_saved_msg =
      locale_string("config_not_saved", "ee configuration not saved");
  ree_no_file_msg =
      locale_string("ree_no_file", "must specify a file when invoking ree");
  menu_too_lrg_msg =
      locale_string("menu_too_large_alt", "menu too large for window");
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

  for (counter = 1; counter < NUM_MODES_ITEMS - 1; counter++) {
    modes_menu[counter].item_string = malloc(128);
  }
}

void control_undo(void) { undo_perform(&undo_state); }

void control_redo(void) { undo_redo(&undo_state); }
