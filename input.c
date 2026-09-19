/*
 * Input handling for ee (easy editor)
 */

#include "delete.h"
#include "ee.h"
#include "fileio.h"
#include "format.h"
#include "lsp.h"
#include "menu.h"
#include "search.h"
#include "state.h"
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
    {"search_prompt", control_search_prompt, "prompt for search string",
     "srch prmpt"},
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

static const struct command_map *cmd_hash_table[64];
static bool cmd_hash_inited = false;

static unsigned int cmd_hash(const char *s) {
  unsigned int h = 5381;
  while (*s)
    h = ((h << 5) + h) + (unsigned char)*s++;
  return h;
}

static void init_cmd_hash(void) {
  if (cmd_hash_inited)
    return;
  for (int i = 0; commands_table[i].name != nullptr; i++) {
    unsigned int idx = cmd_hash(commands_table[i].name) & 63;
    while (cmd_hash_table[idx] != nullptr)
      idx = (idx + 1) & 63;
    cmd_hash_table[idx] = &commands_table[i];
  }
  cmd_hash_inited = true;
}

const struct command_map *find_command(const char *name) {
  if (!name)
    return nullptr;
  init_cmd_hash();
  unsigned int idx = cmd_hash(name) & 63;
  while (cmd_hash_table[idx] != nullptr) {
    if (strcmp(cmd_hash_table[idx]->name, name) == 0)
      return cmd_hash_table[idx];
    idx = (idx + 1) & 63;
  }
  return nullptr;
}

void bind_key(const char *key_str, const char *cmd_name, int table_type) {
  int key_idx = -1;
  if (key_str[0] == '^' && key_str[1] != '\0') {
    unsigned char uc = toupper((unsigned char)key_str[1]);
    if (uc >= '@' && uc <= '_') {
      key_idx = uc - '@';
    }
  } else if (strlen(key_str) >= 3 && key_str[1] == '-') {
    char mod = toupper((unsigned char)key_str[0]);
    int base_key = (unsigned char)key_str[2];
    static const int mod_offsets[256] = {
        ['M'] = 512,
        ['W'] = 768,
    };
    if (mod == 'M' || mod == 'W') {
      key_idx = mod_offsets[(unsigned char)mod] + base_key;
    } else if (mod == 'C') {
      unsigned char uc = toupper((unsigned char)base_key);
      if (uc >= '@' && uc <= '_')
        key_idx = uc - '@';
    } else if (mod == 'S') {
      key_idx = base_key;
    }
  } else if (strncmp(key_str, "code:", 5) == 0) {
    key_idx = atoi(key_str + 5);
  }

  if (key_idx < 0 || key_idx >= 1024)
    return;

  const struct command_map *cmd = find_command(cmd_name);
  control_handler handler = cmd ? (control_handler)cmd->handler : no_op;

  static control_handler *const table_ptrs[] = {
      [BASE_TABLE] = base_control_table,
      [GOLD_TABLE] = gold_control_table,
      [EMACS_TABLE] = emacs_control_table,
  };

  control_handler *target_table =
      (table_type >= 0 && table_type < 3) ? table_ptrs[table_type]
                                          : base_control_table;

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
                                            [27] = control_esc}; // ESC -> menu

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

/* use control for commands (branchless dispatch) */
void control() {
  bool was_gold = gold;
  control_handler const *table_ptr =
      gold ? gold_control_table : base_control_table;
  int index = (in >= 512 && in < 1024) ? in : (in & 0x1F);
  control_handler handler = table_ptr[index];
  handler = handler ? handler : no_op;

  gold = false;
  if (was_gold && info_window) {
    resize_info_win();
  }
  handler();
}

/* Emacs control-key bindings (branchless dispatch) */
void emacs_control() {
  int index = (in >= 512 && in < 1024) ? in : (in & 0x1F);
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

// Vi command handler table
typedef void (*vi_command_handler)(void);

static void vi_h(void) { left(1); }
static void vi_j(void) { down(); }
static void vi_k(void) { up(); }
static void vi_l(void) { right(1); }
static void vi_i(void) { vi_insert_mode = true; }
static void vi_I(void) {
  bol();
  vi_insert_mode = true;
}
static void vi_a(void) {
  right(1);
  vi_insert_mode = true;
}
static void vi_A(void) {
  eol();
  vi_insert_mode = true;
}
static void vi_o(void) {
  eol();
  control_newline();
  vi_insert_mode = true;
}
static void vi_O(void) {
  bol();
  control_newline();
  up();
  vi_insert_mode = true;
}
static void vi_x(void) { delete_char_at_cursor(1); }
static void vi_X(void) {
  left(1);
  delete_char_at_cursor(1);
}
static void vi_0(void) { bol(); }
static void vi_dollar(void) { eol(); }
static void vi_g(void) { top(); }
static void vi_G(void) { bottom(); }
static void vi_w(void) { adv_word(); }
static void vi_b(void) { prev_word(); }
static void vi_u(void) { undel_char(); }
static void vi_colon(void) { command_prompt(); }
static void vi_slash(void) { search_prompt(); }

static vi_command_handler vi_command_table[256] = {
    ['h'] = vi_h,    ['j'] = vi_j,      ['k'] = vi_k, ['l'] = vi_l,
    ['i'] = vi_i,    ['I'] = vi_I,      ['a'] = vi_a, ['A'] = vi_A,
    ['o'] = vi_o,    ['O'] = vi_O,      ['x'] = vi_x, ['X'] = vi_X,
    ['0'] = vi_0,    ['$'] = vi_dollar, ['g'] = vi_g, ['G'] = vi_G,
    ['w'] = vi_w,    ['b'] = vi_b,      ['u'] = vi_u, [':'] = vi_colon,
    ['/'] = vi_slash};

void vi_command(int c) {
  if (c >= 0 && c < 256 && vi_command_table[c] != nullptr) {
    vi_command_table[c]();
  }
}

static void fn_npage(void) { move_rel('d', max(5, (last_line - 5))); }
static void fn_ppage(void) { move_rel('u', max(5, (last_line - 5))); }
static void fn_il(void) { insert_line(1); left(1); }
static void fn_gold_toggle(void) { gold = !gold; }
static void fn_gold_undel_line(void) { gold = false; undel_line(); }
static void fn_gold_undel_word(void) { gold = false; undel_word(); }
static void fn_gold_resize_midscreen(void) {
  gold = false;
  resize_info_win();
  midscreen(scr_vert, point);
}
static void fn_gold_search_prompt(void) { gold = false; search_prompt(); }
static void fn_gold_bottom(void) { gold = false; bottom(); }
static void fn_gold_eol(void) { gold = false; eol(); }
static void fn_gold_command_prompt(void) { gold = false; command_prompt(); }
static void fn_search_1(void) { search(1); }

static control_handler base_fn_key_table[512] = {
    [KEY_LEFT] = control_left,
    [KEY_RIGHT] = control_right,
    [KEY_HOME] = control_bol,
    [KEY_END] = control_eol,
    [KEY_UP] = control_up,
    [KEY_DOWN] = control_down,
    [KEY_NPAGE] = fn_npage,
    [KEY_PPAGE] = fn_ppage,
    [KEY_DL] = control_del_line,
    [KEY_DC] = del_char,
    [KEY_BACKSPACE] = control_backspace,
    [KEY_IL] = fn_il,
    [KEY_F(1)] = fn_gold_toggle,
    [KEY_F(2)] = control_und_char,
    [KEY_F(3)] = control_del_word,
    [KEY_F(4)] = control_adv_word,
    [KEY_F(5)] = fn_search_1,
    [KEY_F(6)] = control_top,
    [KEY_F(7)] = control_bol,
    [KEY_F(8)] = adv_line,
};

static control_handler gold_fn_key_table[512] = {
    [KEY_LEFT] = control_left,
    [KEY_RIGHT] = control_right,
    [KEY_HOME] = control_bol,
    [KEY_END] = control_eol,
    [KEY_UP] = control_up,
    [KEY_DOWN] = control_down,
    [KEY_NPAGE] = fn_npage,
    [KEY_PPAGE] = fn_ppage,
    [KEY_DL] = control_del_line,
    [KEY_DC] = del_char,
    [KEY_BACKSPACE] = control_backspace,
    [KEY_IL] = fn_il,
    [KEY_F(1)] = fn_gold_toggle,
    [KEY_F(2)] = fn_gold_undel_line,
    [KEY_F(3)] = fn_gold_undel_word,
    [KEY_F(4)] = fn_gold_resize_midscreen,
    [KEY_F(5)] = fn_gold_search_prompt,
    [KEY_F(6)] = fn_gold_bottom,
    [KEY_F(7)] = fn_gold_eol,
    [KEY_F(8)] = fn_gold_command_prompt,
};

/* handle function keys via branchless table dispatch */
void function_key() {
  if (in >= 0 && in < 512) {
    control_handler const *tbl = gold ? gold_fn_key_table : base_fn_key_table;
    control_handler handler = tbl[in];
    if (handler != nullptr) {
      handler();
    }
  }
}