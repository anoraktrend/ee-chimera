/*
 * Input handling for ee (easy editor)
 */

#include "ee.h"
#include "delete.h"
#include "fileio.h"
#include "format.h"
#include "lsp.h"
#include "menu.h"
#include "search.h"
#include "theme.h"

// Control handler wrappers
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
    {"replace_prompt", control_replace_prompt, "prompt for replace string", "repl prmpt"},
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
      key_idx = base_key;    // Standard key, but we can differentiate if needed
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

control_handler base_control_table[1024] = {[1] = control_right,
                                            [2] = bottom,
                                            [3] = control_copy,
                                            [4] = bol,
                                            [5] = command_prompt,
                                            [6] = control_search,
                                            [7] = gold_toggle,
                                            [8] = control_backspace,
                                            [10] = control_newline,
                                            [11] = del_char,
                                            [12] = del_line,
                                            [13] = control_newline,
                                            [14] = control_next_page,
                                            [15] = eol,
                                            [16] = control_prev_page,
                                            [18] = redraw,
                                            [20] = top,
                                            [21] = set_mark,
                                            [22] = paste_region,
                                            [23] = del_word,
                                            [24] = control_cut,
                                            [25] = adv_word,
                                            [26] = replace_prompt,
                                            [27] = control_esc};

control_handler gold_control_table[1024] = {
    [2] = gold_append,      [3] = del_line,        [6] = search_prompt,
    [11] = undel_char,      [12] = undel_line,     [18] = gold_search_reverse,
    [21] = set_mark,        [22] = control_search, [23] = undel_word,
    [24] = Format,          [25] = prev_word,      [26] = replace_prompt,
    [27] = control_gold_esc};

control_handler emacs_control_table[1024] = {[1] = bol,
                                             [2] = control_left,
                                             [3] = command_prompt,
                                             [4] = del_char,
                                             [5] = eol,
                                             [6] = control_right,
                                             [7] = control_prev_page,
                                             [8] = control_backspace,
                                             [10] = undel_char,
                                             [11] = del_line,
                                             [12] = undel_line,
                                             [13] = control_newline,
                                             [14] = control_down,
                                             [15] = control_insert_ascii,
                                             [16] = control_up,
                                             [18] = undel_word,
                                             [20] = top,
                                             [21] = bottom,
                                             [22] = control_next_page,
                                             [23] = del_word,
                                             [24] = control_search,
                                             [25] = search_prompt,
                                             [26] = adv_word,
                                             [27] = control_esc};

/* use control for commands */
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

/* Emacs control-key bindings */
void emacs_control() {
  int index = in * ((in >= 0) & (in <= 31));
  control_handler handler = emacs_control_table[index];
  handler = handler ? handler : no_op;

  handler();
}

/* move to start of previous word in text */
static unsigned char *skip_chars_back(unsigned char *start, unsigned char *ptr,
                                      bool const spaces) {
  unsigned char *current = ptr;
  while (current > start &&
         ((*(current - 1) == ' ' || *(current - 1) == '\t') == spaces)) {
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
    new_p = skip_chars_back(curr_line->line, new_p, true);
    new_p = skip_chars_back(curr_line->line, new_p, false);

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

void adv_word() {
  if (position < curr_line->line_length) {
    unsigned char *new_p = point;
    if ((*new_p != ' ') && (*new_p != '\t')) {
      new_p = skip_chars_back(curr_line->line, new_p, false);
    }
    new_p = skip_chars_back(curr_line->line, new_p, true);
    position += (new_p - point);
    point = new_p;
    scanline(point);
    scr_pos = scr_horz;
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  } else if (curr_line->next_line != nullptr) {
    right(1);
  }
}

void vi_command(int c) {
  switch (c) {
  case 'h':
    left(1);
    break;
  case 'j':
    down();
    break;
  case 'k':
    up();
    break;
  case 'l':
    right(1);
    break;
  case 'i':
    vi_insert_mode = true;
    break;
  case 'I':
    bol();
    vi_insert_mode = true;
    break;
  case 'a':
    right(1);
    vi_insert_mode = true;
    break;
  case 'A':
    eol();
    vi_insert_mode = true;
    break;
  case 'o':
    eol();
    control_newline();
    vi_insert_mode = true;
    break;
  case 'O':
    bol();
    control_newline();
    up();
    vi_insert_mode = true;
    break;
  case 'x':
    delete_char_at_cursor(1);
    break;
  case 'X':
    left(1);
    delete_char_at_cursor(1);
    break;
  case '0':
    bol();
    break;
  case '$':
    eol();
    break;
  case 'g':
    top();
    break;
  case 'G':
    bottom();
    break;
  case 'w':
    adv_word();
    break;
  case 'b':
    prev_word();
    break;
  case 'u':
    undel_char();
    break;
  case ':'
    command_prompt();
    break;
  case '/':
    search_prompt();
    break;
  }
}

/* handle function keys */
void function_key() {
  // Placeholder for function_key logic
  // Will be moved from ee.c in next step
}