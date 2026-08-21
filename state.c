/*
 * Editor state management for ee (easy editor)
 */

#include "ee.h"
#include "delete.h"
#include "fileio.h"
#include "format.h"
#include "input.h"
#include "lsp.h"
#include "menu.h"
#include "render.h"
#include "search.h"
#include "theme.h"
#include "undo.h"

// Global state
struct text *first_line; /* first line of current buffer        */
struct text *curr_line;  /* current line cursor is on        */
struct text *tmp_line;   /* temporary line pointer        */
struct files *top_of_stack = nullptr;
undo_buffer undo_state;

int position;     /* offset in bytes from begin of line    */
int scr_pos;      /* horizontal position            */
int scr_vert;     /* vertical position on screen        */
int scr_horz;     /* horizontal position on screen        */
int absolute_lin; /* number of lines from top        */
int tmp_vert, tmp_horz;
bool edit;                    /* continue executing while true    */
bool gold;                    /* 'gold' function key pressed        */
int last_line;                /* last line for text display        */
int last_col;                 /* last column for text display        */
int horiz_offset = 0;         /* offset from left edge of text    */
bool clear_com_win;           /* flag to indicate com_win needs clearing */
bool text_changes = false;    /* indicate changes have been made to text */
bool info_window = true;      /* flag to indicate if help window visible */
int info_type = CONTROL_KEYS; /* flag to indicate type of info to display */
bool expand_tabs = true;      /* flag for expanding tabs        */
bool formatted = false;
bool pasting_mode = false;
bool formatting_in_progress = false;
bool profiling_mode = false; /* flag indicating paragraph formatted    */
#ifdef HAS_AUTOFORMAT
bool auto_format = false; /* flag for auto_format mode        */
#endif
bool restricted = false; /* flag to indicate restricted mode    */
bool undo_enabled = true;
char theme_name[128] = "";
bool eightbit = true;            /* eight bit character flag        */
int local_LINES = 0;             /* copy of LINES, to detect when win resizes */
int local_COLS = 0;              /* copy of COLS, to detect when win resizes  */
bool curses_initialized = false; /* flag indicating if curses has been started*/
bool emacs_keys_mode = false;    /* mode for if emacs key binings are used    */
bool vi_keys_mode = false;
bool vi_insert_mode = false;
bool ee_chinese = false; /* allows handling of multi-byte characters  */
                          /* by checking for high bit in a byte the    */
                          /* code recognizes a two-byte character      */
                          /* sequence                    */

unsigned char *point; /* points to current position in line    */
char *print_command = (char *) "lpr"; /* string to use for the print command    */
char *start_at_line = nullptr; /* move to this line at start of session*/
int in;                        /* input character            */

static char *const table[] = {"^@", "^A", "^B", "^C", "^D",  "^E", "^F", "^G",
                               "^H", "\t", "^J", "^K", "^L",  "^M", "^N", "^O",
                               "^P", "^Q", "^R", "^S", "^T",  "^U", "^V", "^W",
                               "^X", "^Y", "^Z", "^[", "^\\", "^]", "^^", "^_"};

WINDOW *com_win;
WINDOW *text_win;
WINDOW *help_win;
WINDOW *info_win;

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
  if (!profiling_mode &&
      ((isatty(STDIN_FILENO) == 0) || (isatty(STDOUT_FILENO) == 0))) {
    fprintf(stderr, "ee's standard input and output must be a terminal\n");
    exit(1);
  }

  signal(SIGCHLD, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGINT, edit_abort);
  d_char = (unsigned char *)malloc(8); /* provide a buffer for multi-byte chars */
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
    if (LINES == 0)
      LINES = 24;
    if (COLS == 0)
      COLS = 80;
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
    check_fp();
  }

  clear_com_win = true;

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
          for (int i = 0; buf[i]; i++)
            insert(buf[i]);
          insert('\n');
        }
      } else {
        if (strcmp(buf, "q") == 0 || strcmp(buf, "quit") == 0 ||
            strcmp(buf, ":quit") == 0) {
          edit = false;
        } else if (strcmp(buf, "a") == 0 || strcmp(buf, "i") == 0 ||
                   strcmp(buf, "c") == 0) {
          if (buf[0] == 'c')
            delete_char_at_cursor(1); // very basic change
          ed_insert_mode = 1;
        } else if (strcmp(buf, "d") == 0) {
          del_line();
        } else if (strcmp(buf, "w") == 0) {
          if (in_file_name)
            write_file(in_file_name, false);
        } else if (buf[0] == 'w' && buf[1] == ' ') {
          write_file(buf + 2, false);
        } else if (buf[0] == ':') {
          command(buf + 1);
        } else {
          command(buf); // Fallback
        }
      }
      if (!edit)
        break;
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
    if (!profiling_mode)
      wtimeout(text_win, 10);
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
    if (!profiling_mode)
      wtimeout(text_win, -1); // Restore blocking

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

/* travel to the top or bottom edge of the file    */
static void goto_buffer_edge(bool const to_bottom) {
  while (to_bottom ? curr_line->next_line != nullptr
                   : curr_line->prev_line != nullptr) {
    curr_line = to_bottom ? curr_line->next_line : curr_line->prev_line;
    absolute_lin += to_bottom ? 1 : -1;
  }
  point = curr_line->line;
  horiz_offset = 0;
  position = 1;
  midscreen(to_bottom ? last_line : 0, point);
  scr_pos = scr_horz;
}

/* go to bottom of file            */
void bottom() { goto_buffer_edge(true); }

/* go to top of file            */
void top() { goto_buffer_edge(false); }

/* move pointers to start of next line    */
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
    scr_vert++;
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

/* move left one character    */
void left(int disp) {
  if (point != curr_line->line) /* if not at begin of line    */
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

/* move right one character    */
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

/* move to the same column as on other line    */
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
}

void bol(void) {
  point = curr_line->line;
  position = 1;
  scr_horz = 0;
  ee_wmove(text_win, scr_vert, 0);
}

void eol(void) {
  point = curr_line->line + curr_line->line_length - 1;
  position = curr_line->line_length;
  scanline(point);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

void resize_check(void) {
  if (LINES != local_LINES || COLS != local_COLS) {
    local_LINES = LINES;
    local_COLS = COLS;
    last_line = LINES - 2;
    last_col = COLS - 1;
    if (right_margin == 0) {
      right_margin = COLS - 1;
    }
    wresize(text_win, LINES - 1, COLS);
    wresize(com_win, 1, COLS);
    mvwin(com_win, LINES - 1, 0);
    if (info_window) {
      resize_info_win();
    }
    redraw();
  }
}