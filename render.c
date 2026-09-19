/*
 * Rendering logic for ee (easy editor)
 */

#include "render.h"
#include "ee.h"
#include "state.h"
#include "theme.h"

#ifdef HAS_ICU
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>
#endif

#ifdef HAS_TREESITTER
#include <tree_sitter/api.h>
extern TSParser *ts_parser;
extern TSTree *ts_tree;
#endif

#ifdef HAS_LSP
extern struct diagnostic *diagnostics_list;
#endif

/* give the number of spaces to shift */
int tabshift(int temp_int) { return 8 - (temp_int & 7); }

static constexpr int char_len_table[256] = {[0 ... 8] = 2,   [9] = -1,
                                            [10 ... 31] = 2, [32 ... 126] = 1,
                                            [127] = 2,       [128 ... 255] = 1};

int out_char(WINDOW *restrict window, int character, int column) {
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
#ifdef HAS_ICU
    uint8_t utf8_buf[4];
    int32_t utf8_len = 0;
    UErrorCode status = U_ZERO_ERROR;
    U8_APPEND(utf8_buf, utf8_len, 4, character, status);
    if (U_SUCCESS(status)) {
      waddnstr(window, (const char *)utf8_buf, (int)utf8_len);
      int adv = u_char_width(character, column);
      return (adv > 0) ? adv : 1;
    }
#endif
    if (!eightbit) {
      snprintf(string2, sizeof(string2), "<%d>",
               (character < 0) ? (character + 256) : character);
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

/* return the length of the character */
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

int scanline_step(unsigned char *ptr, const unsigned char *pos, int temp) {
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
    int base_off = scr_horz & ~7;
    int new_off_high = base_off - (COLS - 8);
    int new_off_low = max(0, base_off);

    horiz_offset = (beyond_last ? new_off_high : new_off_low);

    // Call draw_screen instead of midscreen to avoid recursion,
    // as midscreen calls scanline.
    ee_wmove(text_win, 0, 0);
    draw_screen();
  }
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

/* redraw line from current position */
void draw_line(int vertical, int horiz, struct text *restrict line, int t_pos) {
  int d;               /* partial length of special or tab char to display  */
  unsigned char *temp; /* temporary pointer to position in line          */
  int abs_column;      /* offset in screen units from begin of line      */
  int column;          /* horizontal position on screen              */
  int row;             /* vertical position on screen                */
  int posit;           /* temporary position indicator within line        */

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
  int max_column = last_col;
  while ((posit < line->line_length) && (column <= max_column)) {
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

    if (text_win != nullptr)
      wattron(text_win, attr);
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(temp, i, (int32_t)(line->line_length - posit + 1), c);
      if (c < 0) {
        // Invalid UTF-8: fallback to single-byte
        ee_waddch(text_win, *temp);
        abs_column++;
        column++;
        posit++;
        temp++;
      } else {
        /* out_char emits the codepoint's UTF-8 bytes and returns its
           display width -- single source of truth for both */
        int adv = out_char(text_win, (int)c, abs_column);
        abs_column += adv;
        column += adv;
        posit += i;
        temp += i;
      }
    } else {
      int char_len = len_char(*temp, abs_column);
      if (char_len == 1) {
        ee_waddch(text_win, *temp);
        column += 1;
        abs_column += 1;
      } else {
        int adv = out_char(text_win, *temp, abs_column);
        column += adv;
        abs_column += adv;
      }
      posit++;
      temp++;
    }
#else
      int char_len = len_char(*temp, abs_column);
      if (char_len == 1) {
        ee_waddch(text_win, *temp);
        column += 1;
        abs_column += 1;
      } else {
        int adv = out_char(text_win, *temp, abs_column);
        column += adv;
        abs_column += adv;
      }
      posit++;
      temp++;
    }
#endif

    if (text_win != nullptr)
      wattroff(text_win, attr);
  }
  if (column < last_col) {
    ee_wclrtoeol(text_win);
  }
  ee_wmove(text_win, vertical, (horiz - horiz_offset));
}

/* redraw the screen */
void draw_screen(void) {
  struct text *line = curr_line;
  int i = scr_vert;
  int j = absolute_lin;

  while ((i > 0) && (line->prev_line != nullptr)) {
    line = line->prev_line;
    j--;
    i--;
  }

  i = 0;
  while ((i <= last_line) && (line != nullptr)) {
    draw_line(i, 0, line, 1);
    line = line->next_line;
    i++;
  }
  if (i <= last_line) {
    ee_wmove(text_win, i, 0);
    ee_wclrtobot(text_win);
  }
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* center the screen on the cursor */
void midscreen(int line, unsigned char *ptr) {
  int counter;

  if ((line < 5) || (last_line < 5)) {
    top_of_screen();
    return;
  }

  counter = 0;
  while ((counter < (last_line / 2)) && (curr_line->prev_line != nullptr)) {
    curr_line = curr_line->prev_line;
    counter++;
  }

  absolute_lin -= counter;
  scr_vert -= counter;
  if (scr_vert < 0)
    scr_vert = 0;

  scanline(ptr);
  draw_screen();
}

/* move to top of screen */
void top_of_screen(void) {
  int counter;

  counter = 0;
  while ((counter < scr_vert) && (curr_line->prev_line != nullptr)) {
    curr_line = curr_line->prev_line;
    counter++;
  }

  absolute_lin -= counter;
  scr_vert = 0;
  scanline(point);
  draw_screen();
}

void paint_info_win(void) {
  int counter;
  int height, width;

  if (!info_window) {
    return;
  }

  generate_dynamic_info();

  if (info_win == nullptr)
    return;
  getmaxyx(info_win, height, width);

  ee_werase(info_win);
  for (counter = 0; counter < num_info_lines && counter < height - 1;
       counter++) {
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
  snprintf(status_buf, sizeof(status_buf),
           "%s line %d col %d top %d=", (mark_line != nullptr ? "MARK" : ""),
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
  if (!profiling_mode)
    current_x = getcurx(info_win);
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
  if (!profiling_mode)
    current_x = getcurx(info_win);
  for (int i = current_x; i < width; i++) {
    ee_waddch(info_win, '=');
  }

  if (!nohighlight) {
    wstandend(info_win);
  }
  ee_wrefresh(info_win);
}

void resize_info_win(void) {
  if (!curses_initialized)
    return;

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
    text_win = profiling_mode
                   ? nullptr
                   : newwin(LINES - new_height - 1, COLS, new_height, 0);
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