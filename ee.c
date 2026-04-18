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

void help(void);
void Format(void);
static void shell_op(void);
void leave_op(void);
void spell_op(void);
void ispell_op(void);
void print_buffer(void);
void finish(void);
int quit(int noverify);
int file_op(int arg);
int search(int display_message);
void redraw(void);
void modes_op(void);
int unique_test(char *string, char *list[]);
void command(char *cmd_str);
void set_up_term(void);
void edit_abort(int arg);
void cleanup(void);
void Auto_Format(void);
void copy_region(bool cut);
void append_region(bool cut);
void paste_region(void);
void delete_char_at_cursor(int no_verify);
void insert_line(int no_verify);
void bol(void);
void eol(void);
void top(void);
void bottom(void);
void right(int no_verify);
void left(int no_verify);
void prev_word(void);
void adv_word(void);
void undel_char(void);
void undel_word(void);
void undel_line(void);
void search_prompt(void);
void replace_prompt(void);
void command_prompt(void);
void gold_toggle(void);
void gold_append(void);
void gold_search_reverse(void);
void resize_info_win(void);
void update_help_strings(void);
#ifdef HAS_ICU
static int u_char_width(UChar32 c, int column);
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
int menu_op_wrapper(struct menu_entries *m) { return menu_op(m); }

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
static ssize_t strscpy(char *dest, const char *src, size_t count) {
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
struct text *dlt_line;   /* structure for info on deleted line	*/
struct text *curr_line;  /* current line cursor is on		*/
struct text *tmp_line;   /* temporary line pointer		*/
struct text *srch_line;  /* temporary pointer for search routine */

struct files *top_of_stack = nullptr;

struct text *mark_line = nullptr;
int mark_position = 0;
char *clipboard_buf = nullptr;

int char_len_table[256] = {
    [0 ... 8] = 2,   [9] = -1,        [10 ... 31] = 2, [32 ... 126] = 1,
    [127] = 2,       [128 ... 255] = 1};

void update_line_numbers(struct text *line, int delta);
struct text *find_next_recursive(struct text *line, int count,
                                        int *actual_count);
struct text *find_prev_recursive(struct text *line, int count,
                                        int *actual_count);
void cleanup(void);
const char *get_key_name(int i);
static const char *get_key_binding(control_handler handler,
                                   control_handler *table);

#ifdef HAS_TREESITTER
const TSLanguage *tree_sitter_c(void);

// Tree-Sitter Globals
TSParser *ts_parser = nullptr;
TSTree *ts_tree = nullptr;
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
int lsp_to_child[2];
int lsp_from_child[2];
pid_t lsp_pid = -1;

struct diagnostic *diagnostics_list = nullptr;
#endif

#ifdef HAS_TREESITTER
const char *ts_read_buffer(void *payload, uint32_t byte_index, TSPoint position,
                           uint32_t *bytes_read) {
  struct text *line = (struct text *)payload;
  uint32_t current_index = 0;
  static const char newline = '\n';

  while (line != nullptr) {
    uint32_t text_len = (line->line_length > 0) ? (line->line_length - 1) : 0;
    uint32_t total_line_len = text_len + 1;

    if (byte_index >= current_index &&
        byte_index < current_index + total_line_len) {
      uint32_t offset = byte_index - current_index;
      if (offset < text_len) {
        *bytes_read = text_len - offset;
        return (const char *)(line->line + offset);
      } else {
        *bytes_read = 1;
        return &newline;
      }
    }
    current_index += total_line_len;
    line = line->next_line;
  }
  *bytes_read = 0;
  return nullptr;
}

static void reparse() {
  if (ts_parser == nullptr) {
    ts_parser = ts_parser_new();
    ts_parser_set_language(ts_parser, tree_sitter_c());
  }
  TSInput input = {
      .payload = first_line,
      .read = ts_read_buffer,
      .encoding = TSInputEncodingUTF8,
  };
  if (ts_tree != nullptr) {
    ts_tree_delete(ts_tree);
  }
  ts_tree = ts_parser_parse(ts_parser, nullptr, input);
}
#endif

#ifdef HAS_LSP
static void lsp_send(const char *msg);

void lsp_start() {
  pipe2(lsp_to_child, O_CLOEXEC);
  pipe2(lsp_from_child, O_CLOEXEC);
  lsp_pid = fork();
  if (lsp_pid == 0) {
    dup2(lsp_to_child[0], STDIN_FILENO);
    dup2(lsp_from_child[1], STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull != -1) {
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    close(lsp_to_child[0]);
    close(lsp_to_child[1]);
    close(lsp_from_child[0]);
    close(lsp_from_child[1]);
    execlp("clangd", "clangd", "--log=error", nullptr);
    exit(1);
  }
  close(lsp_to_child[0]);
  close(lsp_from_child[1]);
  fcntl(lsp_from_child[0], F_SETFL, O_NONBLOCK);

  // Initialize
  lsp_send("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":"
           "{\"processId\":0,\"rootUri\":null,\"capabilities\":{}}}");
}

void lsp_send(const char *msg) {
  char header[128]; /* HEADER_SIZE */
  sprintf(header, "Content-Length: %zu\r\n\r\n", strlen(msg));
  write(lsp_to_child[1], header, strlen(header));
  write(lsp_to_child[1], msg, strlen(msg));
}

void lsp_open_file(const char *filename) {

  if (filename == nullptr) {
    return;
  }
  // Read buffer into string
  size_t total_len = 0;
  struct text const *line = first_line;
  while (line != nullptr) {
    total_len += line->line_length;
    line = line->next_line;
  }
  char *buf = malloc(total_len + 1);
  char *ptr = buf;
  line = first_line;
  while (line != nullptr) {
    memcpy(ptr, line->line, line->line_length - 1);
    ptr += line->line_length - 1;
    *ptr = '\n';
    ptr++;
    line = line->next_line;
  }
  *ptr = '\0';

  // Escape JSON
  char *escaped = malloc((total_len * 2) + 1);
  char *e_ptr = escaped;
  for (char const *ptr_c = buf; (*ptr_c) != 0; ptr_c++) {
    if (*ptr_c == '\"' || *ptr_c == '\\' || *ptr_c == '\n' || *ptr_c == '\r' ||
        *ptr_c == '\t') {
      *e_ptr++ = '\\';
      if (*ptr_c == '\n') {
        *e_ptr++ = 'n';
      } else if (*ptr_c == '\r') {
        *e_ptr++ = 'r';
      } else if (*ptr_c == '\t') {
        *e_ptr++ = 't';
      } else {
        *e_ptr++ = *ptr_c;
      }
    } else {
      *e_ptr++ = *ptr_c;
    }
  }
  *e_ptr = '\0';

  char *msg = malloc((total_len * 2) + 1024);
  sprintf(msg,
          "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
          "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file://"
          "%s\",\"languageId\":\"c\",\"version\":1,\"text\":\"%s\"}}}",
          filename, escaped);
  lsp_send(msg);
  free(buf);
  free(escaped);
  free(msg);
}

void lsp_poll() {
  char buf[8192]; /* BUF_SIZE */
  ssize_t const num_read = read(lsp_from_child[0], buf, sizeof(buf) - 1);
  if (num_read > 0) {
    buf[num_read] = '\0';
    // PublishDiagnostics
    char *diag_ptr = strstr(buf, "\"publishDiagnostics\"");
    if (diag_ptr != nullptr) {
      // Very basic parse: find the first diagnostic's range and message
      // Clear existing
      while (diagnostics_list != nullptr) {
        struct diagnostic *next = diagnostics_list->next;
        free(diagnostics_list->message);
        free(diagnostics_list);
        diagnostics_list = next;
      }
      // Find "line":
      char *line_ptr = strstr(diag_ptr, "\"line\":");
      if (line_ptr != nullptr) {
        int const line = atoi(line_ptr + 7);
        char *char_ptr = strstr(line_ptr, "\"character\":");
        int col = 0;
        if (char_ptr != nullptr) {
          col = atoi(char_ptr + 12);
        }
        char *msg_ptr = strstr(char_ptr, "\"message\":\"");
        if (msg_ptr != nullptr) {
          char const *msg_end = strchr(msg_ptr + 11, '\"');
          if (msg_end != nullptr) {
            diagnostics_list = malloc(sizeof(struct diagnostic));
            diagnostics_list->line = line + 1;
            diagnostics_list->col = col;
            diagnostics_list->message =
                strndup(msg_ptr + 11, msg_end - (msg_ptr + 11));
            diagnostics_list->next = nullptr;
          }
        }
      }
    }
  }
}
#endif

#ifdef HAS_ICU
UResourceBundle *icu_bundle = nullptr;
#endif

int d_wrd_len;    /* length of deleted word		*/
int position;     /* offset in bytes from begin of line	*/
int scr_pos;      /* horizontal position			*/
int scr_vert;     /* vertical position on screen		*/
int scr_horz;     /* horizontal position on screen	*/
int absolute_lin; /* number of lines from top		*/
int tmp_vert, tmp_horz;
bool input_file;           /* indicate to read input file		*/
bool recv_file;            /* indicate reading a file		*/
bool edit;                 /* continue executing while true	*/
bool gold;                 /* 'gold' function key pressed		*/
int fildes;                /* file descriptor			*/
bool case_sen;             /* case sensitive search flag		*/
int last_line;             /* last line for text display		*/
int last_col;              /* last column for text display		*/
int horiz_offset = 0;      /* offset from left edge of text	*/
bool clear_com_win;        /* flag to indicate com_win needs clearing */
bool text_changes = false; /* indicate changes have been made to text */
int get_fd;                /* file descriptor for reading a file	*/
bool info_window = true;   /* flag to indicate if help window visible */
int info_type =
    CONTROL_KEYS;               /* flag to indicate type of info to display */
bool expand_tabs = true; /* flag for expanding tabs		*/
int right_margin = 0;    /* the right margin 			*/
bool observ_margins = true; /* flag for whether margins are observed */
int shell_fork;
int temp_stdin;           /* temporary storage for stdin		*/
int temp_stdout;          /* temp storage for stdout descriptor	*/
int temp_stderr;          /* temp storage for stderr descriptor	*/
int pipe_out[2];          /* pipe file desc for output		*/
int pipe_in[2];           /* pipe file descriptors for input	*/
bool out_pipe;            /* flag that info is piped out		*/
bool in_pipe;             /* flag that info is piped in		*/
bool formatted = false;
bool formatting_in_progress = false;
bool profiling_mode = false;   /* flag indicating paragraph formatted	*/
#ifdef HAS_AUTOFORMAT
bool auto_format = false; /* flag for auto_format mode		*/
#endif
bool restricted = false;  /* flag to indicate restricted mode	*/
bool nohighlight = false; /* turns off highlighting		*/
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
unsigned char *srch_str;   /* pointer for search string		*/
unsigned char *u_srch_str; /* pointer to non-case sensitive search	*/
unsigned char *srch_1;     /* pointer to start of suspect string	*/
unsigned char *srch_2;     /* pointer to next character of string	*/
unsigned char *srch_3;
char *in_file_name = nullptr; /* name of input file		*/
char *tmp_file;        /* temporary file name			*/
unsigned char *d_char; /* deleted character			*/
unsigned char *d_word; /* deleted word				*/
unsigned char *d_line; /* deleted line				*/
unsigned char
    in_string[MAX_IN_STRING]; /* buffer for reading a file		*/
char *print_command = (char *)"lpr"; /* string to use for the print command 	*/
char *start_at_line = nullptr; /* move to this line at start of session*/
int in; /* input character			*/

FILE *temp_fp;    /* temporary file pointer		*/
FILE *bit_bucket; /* file pointer to /dev/null		*/

char *table[] = {"^@", "^A", "^B", "^C", "^D",  "^E", "^F", "^G",
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

struct menu_entries modes_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, 0},  /* title		*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 1. tabs to spaces	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 2. case sensitive search*/
    {"", nullptr, nullptr, nullptr, nullptr,
     -1},                                         /* 3. margins observed	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 4. auto-paragraph	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 5. eightbit characters*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 6. info window	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 7. emacs key bindings*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 8. vi key bindings	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 9. right margin	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 10. chinese text	*/
    {"", nullptr, nullptr, nullptr, dump_ee_conf,
     -1}, /* 11. save editor config */
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}
    /* terminator		*/
};

char *mode_strings[12];

struct menu_entries config_dump_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, 0},
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries leave_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, finish, -1},
    {"", nullptr, nullptr, quit_wrapper, nullptr, 1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries file_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, READ_FILE},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, WRITE_FILE},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, SAVE_FILE},
    {"", nullptr, nullptr, nullptr, print_buffer, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

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

struct menu_entries misc_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
#ifdef HAS_AUTOFORMAT
    {"", nullptr, nullptr, nullptr, Format, -1},
#endif
    {"", nullptr, nullptr, nullptr, shell_op, -1},
#ifdef HAS_MENU
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)spell_menu, nullptr, nullptr, -1},
#endif
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries main_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, leave_op, -1},
#ifdef HAS_HELP
    {"", nullptr, nullptr, nullptr, help, -1},
#endif
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)file_menu, nullptr, nullptr, -1},
#endif
    {"", nullptr, nullptr, nullptr, redraw, -1},
    {"", nullptr, nullptr, nullptr, modes_op, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)search_menu, nullptr, nullptr, -1},
#endif
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)misc_menu, nullptr, nullptr, -1},
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
char *file_write_prompt_str;
char *file_read_prompt_str;
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
char *file_is_dir_msg;
char *new_file_msg;
char *cant_open_msg;
char *open_file_msg;
char *file_read_fin_msg;
char *reading_file_msg;
char *read_only_msg;
char *file_read_lines_msg;
char *save_file_name_prompt;
char *file_not_saved_msg;
char *changes_made_prompt;
char *yes_char;
char *file_exists_prompt;
char *create_file_fail_msg;
char *writing_file_msg;
char *file_written_msg;
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
static void control_right(void) { right(1); }
void control_copy(void) { copy_region(false); }
void control_search(void) { search(1); }
void control_backspace(void) { delete_char_at_cursor(1); }
void control_newline(void) { insert_line(1); }
void control_next_page(void) { move_rel('d', max(5, (last_line - 5))); }
void control_prev_page(void) { move_rel('u', max(5, (last_line - 5))); }
void control_cut(void) { copy_region(true); }
static void control_left(void) { left(1); }
void control_down(void) { down(); }
void control_up(void) { up(); }
void control_insert_ascii(void) {
  char *string = get_string(ascii_code_str, 1);
  if (*string != '\0') {
    in = atoi(string);
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    insert(in);
  }
  free(string);
}
void gold_search_reverse(void) { search_reverse(1); }
void gold_append(void) { append_region(false); }

static void control_esc(void);
static void control_gold_esc(void);
void gold_toggle(void);

struct command_map commands_table[] = {
    {"right", (void (*)(void))right, "move right one character", "right"},
    {"left", (void (*)(void))left, "move left one character", "left"},
    {"up", (void (*)(void))up, "move up one line", "up"},
    {"down", (void (*)(void))down, "move down one line", "down"},
    {"bol", bol, "move to beginning of line", "beg of lin"},
    {"eol", eol, "move to end of line", "end of lin"},
    {"next_page", (void (*)(void))nextline, "move to next page", "next page"},
    {"prev_page", (void (*)(void))prevline, "move to previous page", "prev page"},
    {"top_of_txt", top, "move to top of text", "top of txt"},
    {"bottom_of_txt", bottom, "move to bottom of text", "end of txt"},
    {"del_char", (void (*)(void))delete_char_at_cursor, "delete character at cursor", "del char"},
    {"del_word", del_word, "delete word at cursor", "del word"},
    {"del_line", (void (*)(void))del_line, "delete current line", "del line"},
    {"und_char", undel_char, "undelete last character", "und char"},
    {"und_word", undel_word, "undelete last word", "und word"},
    {"und_line", undel_line, "undelete last line", "und line"},
    {"copy", (void (*)(void))control_copy, "copy region to clipboard", "copy"},
    {"cut", (void (*)(void))control_cut, "cut region to clipboard", "cut"},
    {"paste", paste_region, "paste clipboard at cursor", "paste"},
    {"append", gold_append, "append region to clipboard", "append"},
    {"mark", set_mark, "set mark for region", "mark"},
    {"search", (void (*)(void))control_search, "search for string", "search"},
    {"search_reverse", gold_search_reverse, "search reverse", "reverse"},
    {"search_prompt", search_prompt, "prompt for search string", "srch prmpt"},
    {"replace_prompt", replace_prompt, "prompt for replace string",
     "repl prmpt"},
    {"command_prompt", command_prompt, "enter command mode", "command"},
    {"gold_toggle", (void (*)(void))gold_toggle, "toggle GOLD mode", "GOLD"},
    {"redraw", redraw, "redraw the screen", "redraw"},
#ifdef HAS_HELP
    {"help", help, "display help information", "help"},
#endif
    {"menu", (void (*)(void))control_esc, "open main menu", "menu"},
#ifdef HAS_AUTOFORMAT
    {"format", Format, "format paragraph", "fmt parag"},
#endif
    {"adv_word", adv_word, "advance to next word", "adv word"},
    {"prev_word", prev_word, "move to previous word", "prev word"},
    {"newline", (void (*)(void))control_newline, "insert newline", "newline"},
    {"backspace", (void (*)(void))control_backspace, "delete previous character", "backspace"},
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

static void control_esc(void) {
#ifdef HAS_MENU
  menu_op(main_menu);
#endif
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
  ee_init();
  if (argc > 0) {
    get_options(argc, argv);
  }
  if (profiling_mode) {
    if (LINES == 0) LINES = 24;
    if (COLS == 0) COLS = 80;
  }
  set_up_term();
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
    if (info_window) {
#ifdef HAS_INFO_WIN
      paint_info_win();
#endif
    }

    ee_wrefresh(text_win);
#ifdef HAS_NCURSESW
    wint_t wch;
    int res = wget_wch(text_win, &wch);
    if (res == ERR) {
      if (errno == EINTR)
        continue;
      time_t now = time(nullptr);
      if (now - last_redraw_time >= 5) {
        redraw();
        last_redraw_time = now;
      }
      continue;
    }
    if (res == KEY_CODE_YES) {
      in = wch;
    } else {
      in = wch;
    }
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
static unsigned char *resiz_line(int factor, struct text *rline, int rpos) {
  rline->max_length += factor;
  rline->line = realloc(rline->line, rline->max_length);
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
    if (auto_format && !formatting_in_progress) {
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
  if (auto_format && (character == ' ') && (!formatted) && !formatting_in_progress) {
    formatting_in_progress = true;
    Auto_Format();
    formatting_in_progress = false;
  } else 
#endif
  if ((character != ' ') && (character != '\t')) {
    formatted = false;
  }

  draw_line(scr_vert, scr_horz, curr_line, position);
}

void update_line_numbers(struct text *line, int delta) {
  if (line == nullptr)
    return;
  line->line_number += delta;
  update_line_numbers(line->next_line, delta);
}

/* delete character		*/
void delete_char_at_cursor(int disp) {
  unsigned char *tp;
  unsigned char *temp2;
  struct text *temp_buff;
  int temp_vert;
  int temp_pos;
  int del_width = 1;

  if (point != curr_line->line) /* if not at beginning of line	*/
  {
    text_changes = true;
    temp2 = tp = point;
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = (int32_t)(point - curr_line->line);
      U8_BACK_1(curr_line->line, 0, i);
      unsigned char *new_p = curr_line->line + i;
      del_width = (int)(point - new_p);
    }
#else
    if (ee_chinese && (position >= 2) && (*(point - 2) > 127)) {
      del_width = 2;
    }
#endif
    tp -= del_width;
    point -= del_width;
    position -= del_width;
    temp_pos = position;
    curr_line->line_length -= del_width;
    if ((*tp < ' ') || (*tp >= 127)) { /* check for TAB */
      scanline(tp);
    } else {
      scr_horz -= del_width;
    }
    scr_pos = scr_horz;
    if (in == 8) {
      if (del_width == 1) {
        {
          *d_char = *point; /* save deleted character  */
        }
      } else {
        memcpy(d_char, point, del_width);
      }
      d_char[del_width] = '\0';
    }
    size_t shift_len = curr_line->line_length - position + 1;
    memmove(tp, temp2, shift_len);
    if ((scr_horz < horiz_offset) && (horiz_offset > 0)) {
      horiz_offset -= 8;
      midscreen(scr_vert, point);
    }
  } else if (curr_line->prev_line != nullptr) {
    text_changes = true;
    left(disp); /* go to previous line	*/
    temp_buff = curr_line->next_line;
    point = resiz_line(temp_buff->line_length, curr_line, position);
    if (temp_buff->next_line != nullptr) {
      temp_buff->next_line->prev_line = curr_line;
    }
    curr_line->next_line = temp_buff->next_line;
    update_line_numbers(curr_line->next_line, -1);
    temp2 = temp_buff->line;
    if (in == 8) {
      d_char[0] = '\n';
      d_char[1] = '\0';
    }
    size_t join_len = temp_buff->line_length;
    memcpy(point, temp2, join_len);
    curr_line->line_length += join_len - 1;
    free(temp_buff->line);
    free(temp_buff);
    temp_buff = curr_line;
    temp_vert = scr_vert;
    scr_pos = scr_horz;
    if (scr_vert < last_line) {
      ee_wmove(text_win, scr_vert + 1, 0);
      wdeleteln(text_win);
    }
    int lines_to_find = last_line - temp_vert;
    temp_buff = find_next_recursive(temp_buff, lines_to_find, &temp_vert);

    if ((temp_vert == last_line) && (temp_buff != nullptr)) {
      tp = temp_buff->line;
      ee_wmove(text_win, last_line, 0);
      wclrtobot(text_win);
      draw_line(last_line, 0, temp_buff, 1);
      ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    }
  }
  draw_line(scr_vert, scr_horz, curr_line, position);
  formatted = false;
}

#ifdef HAS_ICU
static int u_char_width(UChar32 c, int column) {
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

static int scanline_step(unsigned char *ptr, const unsigned char *pos,
                         int temp) {
  if (ptr >= pos)
    return temp;
  if (ee_chinese) {
    int32_t i = 0;
    UChar32 c;
    U8_NEXT(ptr, i, (int32_t)(pos - ptr), c);
    if (c < 0) { // Invalid UTF-8
      return scanline_step(ptr + 1, pos, temp + 1);
    }
    return scanline_step(ptr + i, pos, temp + u_char_width(c, temp));
  } else {
    return scanline_step(ptr + 1, pos, temp + len_char(*ptr, temp));
  }
}
#else
static int scanline_step(unsigned char *ptr, const unsigned char *pos, int temp) {
  if (ptr >= pos)
    return temp;
  return scanline_step(ptr + 1, pos, temp + len_char(*ptr, temp));
}
#endif

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

int out_char(WINDOW *window, int character, int column) {
  int i1;
  static int i2;
  static char *string;
  static char string2[16];

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
      sprintf(string2, "<%d>", (character < 0) ? (character + 256) : character);
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
static int get_node_attribute(int line, int col) {
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
void lsp_change_file(const char *filename) {
  if (filename == nullptr) {
    return;
  }
  size_t total_len = 0;
  struct text const *line = first_line;
  while (line != nullptr) {
    total_len += line->line_length;
    line = line->next_line;
  }
  char *buf = (char *)malloc(total_len + 1);
  char *ptr = buf;
  line = first_line;
  while (line != nullptr) {
    memcpy(ptr, line->line, line->line_length - 1);
    ptr += line->line_length - 1;
    *ptr = '\n';
    ptr++;
    line = line->next_line;
  }
  *ptr = '\0';

  char *escaped = (char *)malloc((total_len * 2) + 1);
  char *e_ptr = escaped;
  for (char const *ptr_c = buf; (*ptr_c) != 0; ptr_c++) {
    if (*ptr_c == '\"' || *ptr_c == '\\' || *ptr_c == '\n' || *ptr_c == '\r' ||
        *ptr_c == '\t') {
      *e_ptr++ = '\\';
      if (*ptr_c == '\n') {
        *e_ptr++ = 'n';
      } else if (*ptr_c == '\r') {
        *e_ptr++ = 'r';
      } else if (*ptr_c == '\t') {
        *e_ptr++ = 't';
      } else {
        *e_ptr++ = *ptr_c;
      }
    } else {
      *e_ptr++ = *ptr_c;
    }
  }
  *e_ptr = '\0';

  char *msg = (char *)malloc((total_len * 2) + 1024);
  sprintf(msg,
          "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
          "didChange\",\"params\":{\"textDocument\":{\"uri\":\"file://"
          "%s\",\"version\":2},\"contentChanges\":[{\"text\":\"%s\"}]}}",
          filename, escaped);
  lsp_send(msg);
  free(buf);
  free(escaped);
  free(msg);
}
#endif

/* redraw line from current position */
void draw_line(int vertical, int horiz, struct text *line, int t_pos) {
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

    wattron(text_win, attr);
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
    wattroff(text_win, attr);
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
    if (split_len > temp_nod->max_length) {
      temp_nod->max_length = split_len + 10;
      temp_nod->line = realloc(temp_nod->line, temp_nod->max_length);
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
      winsertln(text_win);
    } else {
      ee_wmove(text_win, 0, 0);
      wdeleteln(text_win);
      ee_wmove(text_win, last_line, 0);
      wclrtobot(text_win);
    }
    scr_pos = scr_horz = 0;
    if (horiz_offset != 0) {
      horiz_offset = 0;
      midscreen(scr_vert, point);
    }
    draw_line(scr_vert, scr_horz, curr_line, position);
  }
}

/* allocate space for line structure	*/
struct text *txtalloc(void) {
  return ((struct text *)malloc(sizeof(struct text)));
}

/* allocate space for file name list node */
struct files *name_alloc(void) {
  return ((struct files *)malloc(sizeof(struct files)));
}

/* return the length of the first word in the line */
static int first_word_len(struct text *line) {
  char *ptr = (char *)line->line;
  ptr += strspn(ptr, " \t");  /* Skip leading whitespace */
  return strcspn(ptr, " \t"); /* Count length of the word */
}

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
  if (ptr <= start || (*(ptr - 1) != ' ' && *(ptr - 1) != '\t'))
    return ptr;
  return skip_spaces_back(start, ptr - 1);
}

static unsigned char *skip_word_back(unsigned char *start, unsigned char *ptr) {
  if (ptr <= start || (*(ptr - 1) == ' ' || *(ptr - 1) == '\t'))
    return ptr;
  return skip_word_back(start, ptr - 1);
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
  if (curr_line->next_line != nullptr) {
    curr_line = curr_line->next_line;
    absolute_lin++;
    bottom();
    return;
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
  if (curr_line->prev_line != nullptr) {
    curr_line = curr_line->prev_line;
    absolute_lin--;
    top();
    return;
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
    wdeleteln(text_win);
    ee_wmove(text_win, last_line, 0);
    wclrtobot(text_win);
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
    winsertln(text_win);
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

  sprintf(buffer, ">!%s", print_command);
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
bool compare(char *string1, char *string2, bool sensitive) {
  char *strng1 = string1;
  char *strng2 = string2;
  bool equal = true;

  if ((strng1 == nullptr) || (strng2 == nullptr) || (*strng1 == '\0') ||
      (*strng2 == '\0')) {
    return false;
  }
  while (equal) {
    if (sensitive) {
      if (*strng1 != *strng2) {
        equal = false;
      }
    } else {
      if (toupper((unsigned char)*strng1) != toupper((unsigned char)*strng2)) {
        equal = false;
      }
    }
    strng1++;
    strng2++;
    if ((*strng1 == '\0') || (*strng2 == '\0') || (*strng1 == ' ') ||
        (*strng2 == ' ')) {
      break;
    }
  }
  return equal;
}

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
  if (count <= 0 || line == nullptr)
    return line;
  (*actual_count)++;
  return find_next_recursive(line->next_line, count - 1, actual_count);
}

struct text *find_prev_recursive(struct text *line, int count,
                                        int *actual_count) {
  if (count <= 0 || line->prev_line == nullptr)
    return line;
  (*actual_count)++;
  return find_prev_recursive(line->prev_line, count - 1, actual_count);
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
void check_fp() {
  int line_num;
  int temp;
  struct stat buf;

  clear_com_win = true;
  tmp_vert = scr_vert;
  tmp_horz = scr_horz;
  tmp_line = curr_line;
  if (input_file) {
    in_file_name = tmp_file = (char *)top_of_stack->name;
    top_of_stack = top_of_stack->next_name;
  }
  temp = stat(tmp_file, &buf);
  buf.st_mode &= ~07777;
  if ((temp != -1) && (buf.st_mode != 0100000) && (buf.st_mode != 0)) {
    ee_wprintw(com_win, file_is_dir_msg, tmp_file);
    ee_wrefresh(com_win);
    if (input_file) {
      quit(0);
      return;
    }
    return;
  }
  if ((get_fd = open(tmp_file, O_RDONLY | O_CLOEXEC)) == -1) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    if (input_file) {
      ee_wprintw(com_win, new_file_msg, tmp_file);
    } else {
      ee_wprintw(com_win, cant_open_msg, tmp_file);
    }
    ee_wrefresh(com_win);
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    ee_wrefresh(text_win);
    recv_file = false;
    input_file = false;
    return;
  }
  get_file(tmp_file);

  recv_file = false;
  line_num = curr_line->line_number;
  scr_vert = tmp_vert;
  scr_horz = tmp_horz;
  if (input_file) {
    curr_line = first_line;
  } else {
    curr_line = tmp_line;
  }
  point = curr_line->line;
#ifdef HAS_TREESITTER
  reparse();
#endif
  draw_screen();
  if (input_file) {
    input_file = false;
    if (start_at_line != nullptr) {
      line_num = atoi(start_at_line) - 1;
      move_rel('d', line_num);
      line_num = 0;
      start_at_line = nullptr;
    }
  } else {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    text_changes = true;
    if ((tmp_file != nullptr) && (*tmp_file != '\0')) {
      ee_wprintw(com_win, file_read_fin_msg, tmp_file);
    }
  }
  ee_wrefresh(com_win);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  ee_wrefresh(text_win);
}

/* read specified file into current buffer	*/
void get_file(const char *file_name) {
  int can_read; /* file has at least one character	*/
  int length;   /* length of line read by read		*/
  int append;   /* should text be appended to current line */
  struct text *temp_line;
  char ro_flag = 0;

  if (recv_file) /* if reading a file			*/
  {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, reading_file_msg, file_name);
    if (access(file_name, 2) != 0) /* check permission to write */
    {
      if ((errno == ENOTDIR) || (errno == EACCES) || (errno == EROFS) ||
          (errno == ETXTBSY) || (errno == EFAULT)) {
        ee_wprintw(com_win, "%s", read_only_msg);
        ro_flag = 1;
      }
    }
    ee_wrefresh(com_win);
  }
  if (curr_line->line_length > 1) /* if current line is not blank	*/
  {
    insert_line(0);
    left(0);
    append = 0;
  } else {
    {
      append = 1;
    }
  }
  can_read = 0; /* test if file has any characters  */
  while (((length = read(get_fd, in_string, 512)) != 0) && (length != -1)) {
    can_read = 1; /* if set file has at least 1 character   */
    get_line(length, in_string, &append);
  }
  if ((can_read != 0) && (curr_line->line_length == 1)) {
    temp_line = curr_line->prev_line;
    temp_line->next_line = curr_line->next_line;
    if (temp_line->next_line != nullptr) {
      temp_line->next_line->prev_line = temp_line;
    }
    if (curr_line->line != nullptr) {
      free(curr_line->line);
    }
    free(curr_line);
    curr_line = temp_line;
  }
  if (input_file) /* if this is the file to be edited display number of lines
                   */
  {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, file_read_lines_msg, in_file_name, curr_line->line_number);
    if (ro_flag != 0) {
      ee_wprintw(com_win, "%s", read_only_msg);
    }
    ee_wrefresh(com_win);
  } else if (can_read != 0) {
    { /* not input_file and file is non-zero size */
      text_changes = true;
    }
  }

  if (recv_file) /* if reading a file			*/
  {
    in = EOF;
  }
}

/* read string and split into lines */
void get_line(int length, unsigned char *in_string, int *append) {
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

static void draw_screen_step(struct text *line, int vertical) {
  if (line == nullptr || vertical > last_line)
    return;
  draw_line(vertical, 0, line, 1);
  draw_screen_step(line->next_line, vertical + 1);
}

void draw_screen() /* redraw the screen from current postion	*/
{
  wclrtobot(text_win);
  draw_screen_step(curr_line, scr_vert);
  ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* prepare to exit edit session	*/
void finish() {
  char *file_name = in_file_name;

  /*
   |	changes made here should be reflected in the 'save'
   |	portion of file_op()
   */

  if ((file_name == nullptr) || (*file_name == '\0')) {
    file_name = get_string(save_file_name_prompt, 1);
  }

  if ((file_name == nullptr) || (*file_name == '\0')) {
    ee_wmove(com_win, 0, 0);
    ee_wprintw(com_win, "%s", file_not_saved_msg);
    ee_wclrtoeol(com_win);
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }

  tmp_file = resolve_name(file_name);
  if (tmp_file != file_name) {
    free(file_name);
    file_name = tmp_file;
  }

  if (write_file(file_name, true) != 0) {
    text_changes = false;
    quit(0);
  }
}

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

void edit_abort(int arg) {
  (void)arg;
  ee_wrefresh(com_win);
  resetty();
  if(!profiling_mode) endwin();
  putchar('\n');
  cleanup();
  exit(1);
}

static void free_text_lines(struct text *line) {
  if (line == nullptr)
    return;
  free_text_lines(line->next_line);
  free(line->line);
  free(line);
}

void delete_text() {
  free_text_lines(first_line->next_line);
  first_line->next_line = nullptr;
  *first_line->line = '\0';
  first_line->line_length = 1;
  first_line->line_number = 1;
  curr_line = first_line;
  point = curr_line->line;
  scr_pos = scr_vert = scr_horz = 0;
  position = 1;
}

int write_file(char *file_name, bool warn_if_exists) {
  char cr;
  char *tmp_point;
  struct text *out_line;
  int lines;
  int charac;
  int temp_pos;
  int write_flag = 1;

  charac = lines = 0;
  if (warn_if_exists && ((in_file_name == nullptr) ||
                         (strcmp((char *)in_file_name, file_name) != 0))) {
    if ((temp_fp = fopen(file_name, "r")) != nullptr) {
      tmp_point = get_string(file_exists_prompt, 1);
      write_flag = (int)(toupper((unsigned char)*tmp_point) ==
                         toupper((unsigned char)*yes_char));
      fclose(temp_fp);
      free(tmp_point);
    }
  }

  clear_com_win = true;

  if (write_flag != 0) {
    if ((temp_fp = fopen(file_name, "w")) == nullptr) {
      clear_com_win = true;
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wprintw(com_win, create_file_fail_msg, file_name);
      ee_wrefresh(com_win);
      return 0;
    }

    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, writing_file_msg, file_name);
    ee_wrefresh(com_win);
    cr = '\n';
    out_line = first_line;
    while (out_line != nullptr) {
      temp_pos = 1;
      tmp_point = (char *)out_line->line;
      while (temp_pos < out_line->line_length) {
        putc(*tmp_point, temp_fp);
        tmp_point++;
        temp_pos++;
      }
      charac += out_line->line_length;
      out_line = out_line->next_line;
      putc(cr, temp_fp);
      lines++;
    }
    fclose(temp_fp);
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, file_written_msg, file_name, lines, charac);
    ee_wrefresh(com_win);
    return 1;
  }
  return 0;
}

/* search for string in srch_str	*/
int search(int display_message) {
  int lines_moved;
  int iter;
  int found;

  if ((srch_str == nullptr) || (*srch_str == '\0')) {
    return 0;
  }
  if (display_message != 0) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "%s", searching_msg);
    ee_wrefresh(com_win);
    clear_com_win = true;
  }
  lines_moved = 0;
  found = 0;
  srch_line = curr_line;
  srch_1 = point;
  if (position < curr_line->line_length) {
    srch_1++;
  }
  iter = position + 1;
  while ((found == 0) && (srch_line != nullptr)) {
    while ((iter < srch_line->line_length) && (found == 0)) {
      srch_2 = srch_1;
      if (case_sen) /* if case sensitive		*/
      {
        srch_3 = srch_str;
        while ((*srch_2 == *srch_3) && (*srch_3 != '\0')) {
          found = 1;
          srch_2++;
          srch_3++;
        } /* end while	*/
      } else /* if not case sensitive	*/
      {
        srch_3 = u_srch_str;
        while ((toupper(*srch_2) == *srch_3) && (*srch_3 != '\0')) {
          found = 1;
          srch_2++;
          srch_3++;
        }
      } /* end else	*/
      if ((*srch_3 != '\0') || !(found != 0)) {
        found = 0;
        if (iter < srch_line->line_length) {
          srch_1++;
        }
        iter++;
      }
    }
    if (found == 0) {
      srch_line = srch_line->next_line;
      if (srch_line != nullptr) {
        srch_1 = srch_line->line;
      }
      iter = 1;
      lines_moved++;
    }
  }
  if (found != 0) {
    if (display_message != 0) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wrefresh(com_win);
    }
    if (lines_moved == 0) {
      while (position < iter) {
        right(1);
      }
    } else {
      if (lines_moved < 30) {
        move_rel('d', lines_moved);
        while (position < iter) {
          right(1);
        }
      } else {
        absolute_lin += lines_moved;
        curr_line = srch_line;
        point = srch_1;
        position = iter;
        scanline(point);
        scr_pos = scr_horz;
        midscreen((last_line / 2), point);
      }
    }
  } else {
    if (display_message != 0) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wprintw(com_win, str_not_found_msg, srch_str);
      ee_wrefresh(com_win);
    }
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
  return found;
}

/* prompt and read search string (srch_str)	*/
void search_prompt() {
  if (srch_str != nullptr) {
    free(srch_str);
  }
  if ((u_srch_str != nullptr) && (*u_srch_str != '\0')) {
    free(u_srch_str);
  }
  srch_str = (unsigned char *)get_string(search_prompt_str, 0);
  gold = false;
  srch_3 = srch_str;
  srch_1 = u_srch_str = malloc(strlen((char *)srch_str) + 1);
  while (*srch_3 != '\0') {
    *srch_1 = toupper(*srch_3);
    srch_1++;
    srch_3++;
  }
  *srch_1 = '\0';
  search(1);
}

/* set a mark for copying or cutting text */
void set_mark() {
  mark_line = curr_line;
  mark_position = position;
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "Mark set.");
  ee_wrefresh(com_win);
  clear_com_win = true;
}

/* copy or cut the region between the mark and the cursor */
void copy_region(bool cut) {
  if (!mark_line) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "No mark set.");
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }
  /* Verify the mark is still valid (hasn't been deleted) */
  bool valid = false;
  struct text *chk = first_line;
  while (chk) {
    if (chk == mark_line) {
      valid = true;
      break;
    }
    chk = chk->next_line;
  }
  if (!valid) {
    mark_line = nullptr;
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Mark invalid (line deleted).");
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }
  struct text *start_line = mark_line;
  int start_pos = mark_position;
  struct text *end_line = curr_line;
  int end_pos = position;
  /* Ensure start comes before end */
  bool swap = false;
  if (start_line->line_number > end_line->line_number) {
    swap = true;
  } else if (start_line->line_number == end_line->line_number &&
             start_pos > end_pos) {
    swap = true;
  }
  if (swap) {
    start_line = curr_line;
    start_pos = position;
    end_line = mark_line;
    end_pos = mark_position;
  }
  /* Calculate buffer size */
  int est_size = 0;
  struct text *tl = start_line;
  while (tl && tl != end_line) {
    est_size += tl->line_length;
    tl = tl->next_line;
  }
  est_size += end_line->line_length;
  if (clipboard_buf)
    free(clipboard_buf);
  clipboard_buf = malloc(est_size + 1);
  char *cb_ptr = clipboard_buf;
  /* Copy into clipboard buffer */
  tl = start_line;
  if (start_line == end_line) {
    memcpy(cb_ptr, start_line->line + start_pos - 1, end_pos - start_pos);
    cb_ptr += (end_pos - start_pos);
  } else {
    memcpy(cb_ptr, start_line->line + start_pos - 1,
           start_line->line_length - start_pos);
    cb_ptr += (start_line->line_length - start_pos);
    *cb_ptr++ = '\n';
    tl = tl->next_line;
    while (tl && tl != end_line) {
      memcpy(cb_ptr, tl->line, tl->line_length - 1);
      cb_ptr += (tl->line_length - 1);
      *cb_ptr++ = '\n';
      tl = tl->next_line;
    }
    memcpy(cb_ptr, end_line->line, end_pos - 1);
    cb_ptr += (end_pos - 1);
  }
  *cb_ptr = '\0';
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, cut ? "Region cut." : "Region copied.");
  ee_wrefresh(com_win);
  clear_com_win = true;
  /* If cutting, simulate backspacing to delete the region */
  if (cut) {
    /* Move cursor to end of the region if it isn't already */
    while (curr_line != end_line || position != end_pos) {
      if (curr_line->line_number < end_line->line_number ||
          (curr_line == end_line && position < end_pos)) {
        right(1);
      } else {
        left(1);
      }
    }
    int del_len = cb_ptr - clipboard_buf;
    for (int i = 0; i < del_len; i++) {
      in = 8; /* ASCII backspace */
      delete_char_at_cursor(1);
    }
  }
  mark_line = nullptr;
}

/* paste text from the clipboard */
void paste_region() {
  if (!clipboard_buf) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Clipboard empty.");
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }
  char *ptr = clipboard_buf;
  while (*ptr) {
    if (*ptr == '\n') {
      insert_line(1);
    } else {
      insert(*ptr);
    }
    ptr++;
  }
}

/* basic find and replace */
void replace_prompt() {
  char *search_term = get_string("Replace: ", 0);
  if (!search_term || *search_term == '\0')
    return;
  char *replace_term = get_string("With: ", 0);
  if (srch_str != nullptr)
    free(srch_str);
  if (u_srch_str != nullptr)
    free(u_srch_str);
  srch_str = (unsigned char *)search_term;
  srch_3 = srch_str;
  srch_1 = u_srch_str = malloc(strlen((char *)srch_str) + 1);
  while (*srch_3 != '\0') {
    *srch_1 = toupper(*srch_3);
    srch_1++;
    srch_3++;
  }
  *srch_1 = '\0';
  int found = search(1);
  if (found) {
    int len = strlen((char *)search_term);
    for (int i = 0; i < len; i++) {
      in = 8;
      delete_char_at_cursor(1);
    }
    if (replace_term) {
      for (size_t i = 0; i < strlen((char *)replace_term); i++) {
        insert(replace_term[i]);
      }
    }
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Replaced 1 occurrence.");
    ee_wrefresh(com_win);
    clear_com_win = true;
  }
  if (replace_term)
    free(replace_term);
}

/* append the region between the mark and cursor to the existing clipboard */
void append_region(bool cut) {
  if (!mark_line) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "No mark set.");
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }

  if (!clipboard_buf) {
    /* If clipboard is empty, append is just a normal copy */
    copy_region(cut);
    return;
  }

  /* Branchless validation and swap logic */
  bool valid = false;
  struct text *chk = first_line;
  while (chk) {
    valid |= (chk == mark_line);
    chk = chk->next_line;
  }
  mark_line = valid ? mark_line : nullptr;
  if (!valid)
    return;

  bool swap = (mark_line->line_number > curr_line->line_number) ||
              ((mark_line->line_number == curr_line->line_number) &&
               (mark_position > position));

  struct text *start_line = swap ? curr_line : mark_line;
  int start_pos = swap ? position : mark_position;
  struct text *end_line = swap ? mark_line : curr_line;
  int end_pos = swap ? mark_position : position;

  /* Calculate new region size */
  int est_size = end_line->line_length;
  struct text *tl = start_line;
  while (tl && tl != end_line) {
    est_size += tl->line_length;
    tl = tl->next_line;
  }

  /* Reallocate existing clipboard to hold the appended data */
  int current_cb_len = strlen(clipboard_buf);
  char *new_cb = realloc(clipboard_buf, current_cb_len + est_size + 1);
  if (!new_cb)
    return;
  clipboard_buf = new_cb;

  char *cb_ptr = clipboard_buf + current_cb_len;

  /* Copy into clipboard buffer (reusing optimized copy logic) */
  bool single_line = (start_line == end_line);
  int copy_len =
      single_line ? (end_pos - start_pos) : (start_line->line_length - start_pos);

  memcpy(cb_ptr, start_line->line + start_pos - 1, copy_len);
  cb_ptr += copy_len;
  *cb_ptr = '\n';
  cb_ptr += !single_line;

  tl = start_line->next_line;
  while (!single_line && tl && tl != end_line) {
    memcpy(cb_ptr, tl->line, tl->line_length - 1);
    cb_ptr += (tl->line_length - 1);
    *cb_ptr++ = '\n';
    tl = tl->next_line;
  }

  if (!single_line) {
    memcpy(cb_ptr, end_line->line, end_pos - 1);
    cb_ptr += (end_pos - 1);
  }
  *cb_ptr = '\0';

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, cut ? "Region cut & appended." : "Region appended.");
  ee_wrefresh(com_win);
  clear_com_win = true;

  /* Simulate backspacing for cuts */
  if (cut) {
    while (curr_line != end_line || position != end_pos) {
      bool go_right = (curr_line->line_number < end_line->line_number) ||
                      (curr_line == end_line && position < end_pos);
      go_right ? right(1) : left(1);
    }
    int del_len = cb_ptr - (clipboard_buf + current_cb_len);
    in = 8;
    for (int i = 0; i < del_len; i++)
      delete_char_at_cursor(1);
  }
  mark_line = nullptr;
}

/* Search backwards from the current cursor position */
int search_reverse(int display_message) {
  if (!srch_str || *srch_str == '\0')
    return 0;

  if (display_message) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "           ...searching reverse");
    ee_wrefresh(com_win);
    clear_com_win = true;
  }

  int lines_moved = 0;
  int found = 0;
  srch_line = curr_line;

  /* Start searching immediately before the cursor */
  int iter = position - 1;
  int search_len = strlen((char *)srch_str);

  while (!found && srch_line != nullptr) {
    while (iter >= search_len && !found) {
      unsigned char *chk_ptr = srch_line->line + iter - search_len;
      unsigned char *ref_ptr = case_sen ? srch_str : u_srch_str;

      bool match = true;
      for (int i = 0; i < search_len; i++) {
        unsigned char c = chk_ptr[i];
        c = case_sen ? c : toupper(c);
        match &= (c == ref_ptr[i]);
      }

      if (match) {
        found = 1;
        srch_1 = chk_ptr;
      } else {
        iter--;
      }
    }

    if (!found) {
      srch_line = srch_line->prev_line;
      lines_moved--;
      if (srch_line)
        iter = srch_line->line_length;
    }
  }

  if (found) {
    if (display_message) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wrefresh(com_win);
    }
    /* Move cursor to the found location */
    int new_pos = (srch_1 - srch_line->line) + 1;
    while (lines_moved < 0) {
      up();
      lines_moved++;
    }
    while (position > new_pos)
      left(1);
    while (position < new_pos)
      right(1);
  } else {
    if (display_message) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wprintw(com_win, str_not_found_msg, srch_str);
      ee_wrefresh(com_win);
    }
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
  return found;
}

/* delete current character	*/
void del_char() {
  in = 8;                                /* backspace */
  if (position < curr_line->line_length) /* if not end of line	*/
  {
#ifdef HAS_ICU
    if (ee_chinese) {
      int32_t i = 0;
      UChar32 c;
      U8_NEXT(point, i, curr_line->line_length - position + 1, c);
      point += i;
      position += i;
    } else {
      position++;
      point++;
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
    scanline(point);
    delete_char_at_cursor(1);
  } else {
    right(1);
    delete_char_at_cursor(1);
  }
}

/* undelete last deleted character	*/
void undel_char() {
  if (d_char[0] == '\n') {
    { /* insert line if last del_char deleted eol */
      insert_line(1);
    }
  } else {
    in = d_char[0];
    insert(in);
    if (d_char[1] != '\0') {
      in = d_char[1];
      insert(in);
    }
  }
}

/* delete word in front of cursor	*/
void del_word() {
  int tposit;
  int difference;
  unsigned char *d_word2;
  unsigned char *d_word3;
  unsigned char tmp_char[3];

  if (d_word != nullptr) {
    free(d_word);
  }
  d_word = malloc(curr_line->line_length);
  tmp_char[0] = d_char[0];
  tmp_char[1] = d_char[1];
  tmp_char[2] = d_char[2];
  d_word3 = point;
  d_word2 = d_word;
  tposit = position;
  while ((tposit < curr_line->line_length) &&
         ((*d_word3 != ' ') && (*d_word3 != '\t'))) {
    tposit++;
    *d_word2 = *d_word3;
    d_word2++;
    d_word3++;
  }
  while ((tposit < curr_line->line_length) &&
         ((*d_word3 == ' ') || (*d_word3 == '\t'))) {
    tposit++;
    *d_word2 = *d_word3;
    d_word2++;
    d_word3++;
  }
  *d_word2 = '\0';
  d_wrd_len = difference = d_word2 - d_word;
  d_word2 = point;
  while (tposit < curr_line->line_length) {
    tposit++;
    *d_word2 = *d_word3;
    d_word2++;
    d_word3++;
  }
  curr_line->line_length -= difference;
  *d_word2 = '\0';
  draw_line(scr_vert, scr_horz, curr_line, position);
  d_char[0] = tmp_char[0];
  d_char[1] = tmp_char[1];
  d_char[2] = tmp_char[2];
  text_changes = true;
  formatted = false;
}

/* undelete last deleted word		*/
void undel_word() {
  int temp;
  int tposit;
  unsigned char *tmp_old_ptr;
  unsigned char *tmp_space;
  unsigned char *tmp_ptr;
  unsigned char *d_word_ptr;

  /*
   |	resize line to handle undeleted word
   */
  if ((curr_line->max_length - (curr_line->line_length + d_wrd_len)) < 5) {
    point = resiz_line(d_wrd_len, curr_line, position);
  }
  tmp_ptr = tmp_space = malloc(curr_line->line_length + d_wrd_len);
  d_word_ptr = d_word;
  temp = 1;
  /*
   |	copy d_word contents into temp space
   */
  while (temp <= d_wrd_len) {
    temp++;
    *tmp_ptr = *d_word_ptr;
    tmp_ptr++;
    d_word_ptr++;
  }
  tmp_old_ptr = point;
  tposit = position;
  /*
   |	copy contents of line from curent position to eol into
   |	temp space
   */
  while (tposit < curr_line->line_length) {
    temp++;
    tposit++;
    *tmp_ptr = *tmp_old_ptr;
    tmp_ptr++;
    tmp_old_ptr++;
  }
  curr_line->line_length += d_wrd_len;
  tmp_old_ptr = point;
  *tmp_ptr = '\0';
  tmp_ptr = tmp_space;
  tposit = 1;
  /*
   |	now copy contents from temp space back to original line
   */
  while (tposit < temp) {
    tposit++;
    *tmp_old_ptr = *tmp_ptr;
    tmp_ptr++;
    tmp_old_ptr++;
  }
  *tmp_old_ptr = '\0';
  free(tmp_space);
  draw_line(scr_vert, scr_horz, curr_line, position);
}

/* delete from cursor to end of line	*/
void del_line() {
  if (d_line != nullptr) {
    free(d_line);
  }
  d_line = malloc(curr_line->line_length);
  size_t copy_len = curr_line->line_length - position;
  memcpy(d_line, point, copy_len);
  d_line[copy_len] = '\0';
  dlt_line->line_length = 1 + copy_len;
  *point = '\0';
  curr_line->line_length = position;
  ee_wclrtoeol(text_win);
  if (curr_line->next_line != nullptr) {
    right(0);
    delete_char_at_cursor(0);
  }
  text_changes = true;
}

/* undelete last deleted line		*/
void undel_line() {
  unsigned char *ud1;
  unsigned char *ud2;
  int tposit;

  if (dlt_line->line_length == 0) {
    return;
  }

  insert_line(1);
  left(1);
  point = resiz_line(dlt_line->line_length, curr_line, position);
  curr_line->line_length += dlt_line->line_length - 1;
  ud1 = point;
  ud2 = d_line;
  tposit = 1;
  while (tposit < dlt_line->line_length) {
    tposit++;
    *ud1 = *ud2;
    ud1++;
    ud2++;
  }
  *ud1 = '\0';
  draw_line(scr_vert, scr_horz, curr_line, position);
}

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

static void calc_abs_line() {
  struct text const *tmpline = first_line;
  int x = 1;

  while ((tmpline != nullptr) && (tmpline != curr_line)) {
    x++;
    tmpline = tmpline->next_line;
  }
  absolute_lin = x;
}

/* execute shell command			*/
void sh_command(const char *string) {
  char *temp_point;
  char *last_slash;
  char *path; /* directory path to executable		*/
  int parent; /* zero if child, child's pid if parent	*/
  int value;
  int return_val;
  struct text *line_holder;

  if (restrict_mode()) {
    return;
  }

  if ((path = getenv("SHELL")) == nullptr) {
    path = "/bin/sh";
  }
  last_slash = temp_point = path;
  while (*temp_point != '\0') {
    if (*temp_point == '/') {
      last_slash = ++temp_point;
    } else {
      temp_point++;
    }
  }

  /*
   |	if in_pipe is true, then output of the shell operation will be
   |	read by the editor, and curses doesn't need to be turned off
   */

  if (!in_pipe) {
    ee_keypad(com_win, false);
    ee_keypad(text_win, false);
    echo();
    nl();
    noraw();
    resetty();

#ifndef NCURSE
    if(!profiling_mode) endwin();
#endif
  }

  if (in_pipe) {
    pipe2(pipe_in, O_CLOEXEC); /* create a pipe	*/
    parent = fork();
    if (parent == 0) /* if the child		*/
    {
      /*
       |  child process which will fork and exec shell command (if shell output
       is |  to be read by editor)
       */
      in_pipe = false;
      /*
       |  redirect stdout to pipe
       */
      temp_stdout = fcntl(1, F_DUPFD_CLOEXEC);
      close(1);
      fcntl(pipe_in[1], F_DUPFD_CLOEXEC);
      /*
       |  redirect stderr to pipe
       */
      temp_stderr = fcntl(2, F_DUPFD_CLOEXEC);
      close(2);
      fcntl(pipe_in[1], F_DUPFD_CLOEXEC);
      close(pipe_in[1]);
      /*
       |	child will now continue down 'if (!in_pipe)'
       |	path below
       */
    } else /* if the parent	*/
    {
      /*
       |  prepare editor to read from the pipe
       */
      signal(SIGCHLD, SIG_IGN);
      line_holder = curr_line;
      tmp_vert = scr_vert;
      close(pipe_in[1]);
      get_fd = pipe_in[0];
      get_file("");
      close(pipe_in[0]);
      scr_vert = tmp_vert;
      scr_horz = scr_pos = 0;
      position = 1;
      curr_line = line_holder;
      calc_abs_line();
      point = curr_line->line;
      out_pipe = false;
      signal(SIGCHLD, SIG_DFL);
      /*
       |  since flag "in_pipe" is still true, the path which waits for the child
       |  process to die will be avoided.
       |  (the pipe is closed, no more output can be expected)
       */
    }
  }
  if (!in_pipe) {
    signal(SIGINT, SIG_IGN);
    if (out_pipe) {
      pipe2(pipe_out, O_CLOEXEC);
    }
    /*
     |  fork process which will exec command
     */
    parent = fork();
    if (parent == 0) /* if the child	*/
    {
      if (shell_fork != 0) {
        putchar('\n');
      }
      if (out_pipe) {
        /*
         |  prepare the child process (soon to exec a shell command) to read
         from the |  pipe (which will be output from the editor's buffer)
         */
        close(0);
        fcntl(pipe_out[0], F_DUPFD_CLOEXEC);
        close(pipe_out[0]);
        close(pipe_out[1]);
      }
      for (value = 1; value < 24; value++) {
        signal(value, SIG_DFL);
      }
      execl(path, last_slash, "-c", string, nullptr);
      fprintf(stderr, exec_err_msg, path);
      exit(-1);
    } else /* if the parent	*/
    {
      if (out_pipe) {
        /*
         |  output the contents of the buffer to the pipe (to be read by the
         |  process forked and exec'd above as stdin)
         */
        close(pipe_out[0]);
        line_holder = first_line;
        while (line_holder != nullptr) {
          write(pipe_out[1], line_holder->line, (line_holder->line_length - 1));
          write(pipe_out[1], "\n", 1);
          line_holder = line_holder->next_line;
        }
        close(pipe_out[1]);
        out_pipe = false;
      }
      do {
        return_val = wait((int *)nullptr);
      } while ((return_val != parent) && (return_val != -1));
      /*
       |  if this process is actually the child of the editor, exit.  Here's how
       it |  works: |  The editor forks a process.  If output must be sent to
       the command to be |  exec'd another process is forked, and that process
       (the child's child) |  will exec the command.  In this case, "shell_fork"
       will be false.  If no |  output is to be performed to the shell command,
       "shell_fork" will be true. |  If this is the editor process, shell_fork
       will be true, otherwise this is |  the child of the edit process.
       */
      if (shell_fork == 0) {
        exit(0);
      }
    }
    signal(SIGINT, edit_abort);
  }
  if (shell_fork != 0) {
    fputs(continue_msg, stdout);
    fflush(stdout);
    while ((in = getchar()) != '\n') {
      ;
    }
  }

  if (!in_pipe) {
    fixterm();
    if(!profiling_mode) noecho();
    if(!profiling_mode) nonl();
    if(!profiling_mode) raw();
    ee_keypad(text_win, true);
    ee_keypad(com_win, true);
    if (info_window) {
      if(!profiling_mode) clearok(info_win, true);
    }
  }

  redraw();
}

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
int menu_op(struct menu_entries menu_list[]) {
  WINDOW *temp_win;
  int max_width;
  int max_height;
  int x_off;
  int y_off;
  int counter;
  int length;
  int input;
  int temp;
  int list_size;
  int top_offset;    /* offset from top where menu items start */
  int vert_size;     /* vertical size for menu list item display */
  int off_start = 1; /* offset from start of menu items to start display */

  /*
   |      determine number and width of menu items
   */

  list_size = 1;
  while (menu_list[list_size + 1].item_string != nullptr) {
    list_size++;
  }
  max_width = 0;
  for (counter = 0; counter <= list_size; counter++) {
    if ((length = strlen(menu_list[counter].item_string)) > max_width) {
      max_width = length;
    }
  }
  max_width += 3;
  max_width = max(max_width, (int)strlen(menu_cancel_msg));
  max_width = max(
      max_width, max((int)strlen(more_above_str), (int)strlen(more_below_str)));
  max_width += 6;

  /*
   |      make sure that window is large enough to handle menu
   |      if not, print error message and return to calling function
   */

  if (max_width > COLS) {
    ee_wmove(com_win, 0, 0);
    ee_werase(com_win);
    ee_wprintw(com_win, "%s", menu_too_lrg_msg);
    ee_wrefresh(com_win);
    clear_com_win = true;
    return 0;
  }

  top_offset = 0;

  if (list_size > LINES) {
    max_height = LINES;
    if (max_height > 11) {
      vert_size = max_height - 8;
    } else {
      vert_size = max_height;
    }
  } else {
    vert_size = list_size;
    max_height = list_size;
  }

  if (LINES >= (vert_size + 8)) {
    if (menu_list[0].value != MENU_WARN) {
      max_height = vert_size + 8;
    } else {
      max_height = vert_size + 7;
    }
    top_offset = 4;
  }
  x_off = (COLS - max_width) / 2;
  y_off = (LINES - max_height - 1) / 2;
  temp_win = profiling_mode ? nullptr : newwin(max_height, max_width, y_off, x_off);
  ee_keypad(temp_win, true);

  paint_menu(menu_list, max_width, max_height, list_size, top_offset, temp_win,
             off_start, vert_size);

  counter = 1;

  do {
    if (off_start > 2) {
      ee_wmove(temp_win, (1 + counter + top_offset - off_start), 3);
    } else {
      ee_wmove(temp_win, (counter + top_offset - off_start), 3);
    }

    ee_wrefresh(temp_win);
    in = wgetch(temp_win);
    input = in;
    if (input == -1) {
      edit_abort(0);
    }

    if ((isascii(input) != 0) && (isalnum(input) != 0)) {
      if (isalpha(input) != 0) {
        temp = 1 + tolower(input) - 'a';
      } else if (isdigit(input) != 0) {
        temp = (2 + 'z' - 'a') + (input - '0');
      }

      if (temp <= list_size) {
        input = '\n';
        counter = temp;
      }
    } else {
      switch (input) {
      case ' ':    /* space	*/
      case '\004': /* ^d, down	*/
      case KEY_RIGHT:
      case KEY_DOWN:
        counter++;
        if (counter > list_size) {
          counter = 1;
        }
        break;
      case '\010': /* ^h, backspace*/
      case '\025': /* ^u, up	*/
      case 127:    /* ^?, delete	*/
      case KEY_BACKSPACE:
      case KEY_LEFT:
      case KEY_UP:
        counter--;
        if (counter == 0) {
          counter = list_size;
        }
        break;
      case '\033': /* escape key	*/
        if (menu_list[0].value != MENU_WARN) {
          counter = 0;
        }
        break;
      case '\014': /* ^l       	*/
      case '\022': /* ^r, redraw	*/
        paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                   temp_win, off_start, vert_size);
        break;
      default:
        break;
      }
    }

    if (((list_size - off_start) >= (vert_size - 1)) &&
        (counter > (off_start + vert_size - 3)) && (off_start > 1)) {
      if (counter == list_size) {
        off_start = (list_size - vert_size) + 2;
      } else {
        off_start++;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    } else if ((list_size != vert_size) &&
               (counter > (off_start + vert_size - 2))) {
      if (counter == list_size) {
        off_start = 2 + (list_size - vert_size);
      } else if (off_start == 1) {
        off_start = 3;
      } else {
        off_start++;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    } else if (counter < off_start) {
      if (counter <= 2) {
        off_start = 1;
      } else {
        off_start = counter;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    }
  } while ((input != '\r') && (input != '\n') && (counter != 0));

  ee_werase(temp_win);
  ee_wrefresh(temp_win);
  delwin(temp_win);

  if (counter > 0 && ((menu_list[counter].procedure != nullptr) ||
      (menu_list[counter].procedure2 != nullptr) ||
      (menu_list[counter].procedure3 != nullptr))) {
    if (menu_list[counter].value != -1) {
      (*menu_list[counter].procedure2)(menu_list[counter].value);
    } else if (menu_list[counter].ptr_menu != nullptr) {
      (*menu_list[counter].procedure)(menu_list[counter].ptr_menu);
    } else {
      (*menu_list[counter].procedure3)();
    }
  }

  if (info_window) {
    paint_info_win();
  }
  redraw();

  return counter;
}
#endif

#ifdef HAS_MENU
void paint_menu(struct menu_entries menu_list[], int max_width,
                       int max_height, int list_size, int top_offset,
                       WINDOW *menu_win, int off_start, int vert_size) {
  int counter;
  int temp_int;

  ee_werase(menu_win);

  /*
   |	output top and bottom portions of menu box only if window
   |	large enough
   */

  if (max_height > vert_size) {
    ee_wmove(menu_win, 1, 1);
    if (!nohighlight) {
      wstandout(menu_win);
    }
    ee_waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      ee_waddch(menu_win, '-');
    }
    ee_waddch(menu_win, '+');

    ee_wmove(menu_win, (max_height - 2), 1);
    ee_waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      ee_waddch(menu_win, '-');
    }
    ee_waddch(menu_win, '+');
    wstandend(menu_win);
    ee_wmove(menu_win, 2, 3);
    ee_waddstr(menu_win, menu_list[0].item_string);
    ee_wmove(menu_win, (max_height - 3), 3);
    if (menu_list[0].value != MENU_WARN) {
      ee_waddstr(menu_win, menu_cancel_msg);
    }
  }
  if (!nohighlight) {
    wstandout(menu_win);
  }

  for (counter = 0; counter < (vert_size + top_offset); counter++) {
    if (top_offset == 4) {
      temp_int = counter + 2;
    } else {
      {
        temp_int = counter;
      }
    }

    ee_wmove(menu_win, temp_int, 1);
    ee_waddch(menu_win, '|');
    ee_wmove(menu_win, temp_int, (max_width - 2));
    ee_waddch(menu_win, '|');
  }
  wstandend(menu_win);

  if (list_size > vert_size) {
    for (counter = off_start; counter < (off_start + vert_size); counter++) {
      ee_wmove(menu_win, (top_offset + counter - off_start), 3);
      if (list_size > 1) {
        ee_wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), MAX_ALPHA_CHAR)]);
      }
      ee_waddstr(menu_win, menu_list[counter].item_string);
      if (off_start > 1) {
        ee_wmove(menu_win, top_offset, (max_width - 12));
        ee_wprintw(menu_win, "%s", more_above_str);
      }
      if ((off_start + vert_size - 1) < list_size) {
        ee_wmove(menu_win, (top_offset + vert_size - 1), (max_width - 12));
        ee_wprintw(menu_win, "%s", more_below_str);
      }
    }
  } else {
    for (counter = 1; counter <= list_size; counter++) {
      ee_wmove(menu_win, (top_offset + counter - 1), 3);
      if (list_size > 1) {
        ee_wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), MAX_ALPHA_CHAR)]);
      }
      ee_waddstr(menu_win, menu_list[counter].item_string);
    }
  }
}
#endif
#ifdef HAS_HELP
void help() {
  int counter;

  ee_werase(help_win);
  if(!profiling_mode) clearok(help_win, true);
  for (counter = 0; counter < 22; counter++) {
    ee_wmove(help_win, counter, 0);
    ee_waddstr(help_win,
            (emacs_keys_mode) ? emacs_help_text[counter] : help_text[counter]);
  }
  ee_wrefresh(help_win);
  ee_werase(com_win);
  ee_wmove(com_win, 0, 0);
  ee_wprintw(com_win, "%s", press_any_key_msg);
  ee_wrefresh(com_win);
  counter = wgetch(com_win);
  if (counter == -1) {
    edit_abort(0);
  }
  ee_werase(com_win);
  ee_wmove(com_win, 0, 0);
  ee_werase(help_win);
  ee_wrefresh(help_win);
  ee_wrefresh(com_win);
  redraw();
}
#endif

int get_info_win_height() {
  if (!info_window)
    return 0;
  int count = 0;
  if (info_type == CONTROL_KEYS) {
    char **keys = (gold) ? gold_control_keys : (emacs_keys_mode ? emacs_control_keys : control_keys);
    while (count < 5 && keys[count] != nullptr && keys[count][0] != '\0') {
      count++;
    }
  } else if (info_type == COMMANDS) {
    while (count < 5 && command_strings[count] != nullptr && command_strings[count][0] != '\0') {
      count++;
    }
  }
  return max(1, count + 1); // At least 1 for status line
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

  getmaxyx(info_win, height, width);

  ee_werase(info_win);
  for (counter = 0; counter < height - 1; counter++) {
    ee_wmove(info_win, counter, 0);
    ee_wclrtoeol(info_win);
    if (info_type == CONTROL_KEYS) {
      if (counter < 5) {
        char *str = (emacs_keys_mode) ? emacs_control_keys[counter]
                                      : control_keys[counter];
        if (gold) {
          str = gold_control_keys[counter];
        }
        if (str != nullptr && *str != '\0') {
          ee_waddstr(info_win, str);
        } else {
          // If we encounter a null/empty string, we might want to skip the line
          // but we already did werase, so we just don't move or anything.
        }
      }
    } else if (info_type == COMMANDS) {
      if (counter < 5) {
        ee_waddstr(info_win, command_strings[counter]);
      }
    }
  }

  // Construct status line
  ee_wmove(info_win, height - 1, 0);
  if (!nohighlight) {
    wstandout(info_win);
  }

  char status_buf[128];
  snprintf(status_buf, sizeof(status_buf), " line %d col %d top %d=",
           curr_line->line_number, scr_horz, absolute_lin);
  int status_len = strlen(status_buf);

  char const *legend = "^ = Ctrl key ---- access HELP through menu ---";
  int legend_len = strlen(legend);

  // Draw legend
  for (int i = 0; i < width && i < legend_len; i++) {
    ee_waddch(info_win, legend[i]);
  }

  // Fill with '=' up to status info
  int current_x = getcurx(info_win);
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
  current_x = getcurx(info_win);
  for (int i = current_x; i < width; i++) {
    ee_waddch(info_win, '=');
  }

  wstandend(info_win);
  wnoutrefresh(info_win);
}

void no_info_window() {
  if (!info_window) {
    return;
  }
  delwin(info_win);
  delwin(text_win);
  info_window = false;
  last_line = LINES - 2;
  text_win = profiling_mode ? nullptr : newwin((LINES - 1), COLS, 0, 0);
  ee_keypad(text_win, true);
  ee_idlok(text_win, true);
  if(!profiling_mode) clearok(text_win, true);
  midscreen(scr_vert, point);
  ee_wrefresh(text_win);
  clear_com_win = true;
}

void create_info_window() {
  int info_win_height = 0;
  if (info_window) {
    return;
  }
  if (LINES < 10) {
    info_win_height = 2;
  } else if (LINES < 15) {
    info_win_height = 4;
  } else {
    info_win_height = 6;
  }
  last_line = LINES - (info_win_height + 2);
  delwin(text_win);
  text_win = profiling_mode ? nullptr : newwin((LINES - (info_win_height + 1)), COLS, info_win_height, 0);
  ee_keypad(text_win, true);
  ee_idlok(text_win, true);
  ee_werase(text_win);
  info_window = true;
  info_win = profiling_mode ? nullptr : newwin(info_win_height, COLS, 0, 0);
  ee_werase(info_win);
  info_type = CONTROL_KEYS;
  midscreen(min(scr_vert, last_line), point);
  if(!profiling_mode) clearok(info_win, true);
  paint_info_win();
  ee_wrefresh(text_win);
  clear_com_win = true;
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

static void shell_op() {
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
bool Blank_Line(struct text *test_line) {
  if (test_line == nullptr) {
    return true;
  }

  unsigned char c = *test_line->line;
  bool is_special = (c == '.') | (c == '>');
  size_t skip = strspn((char *)test_line->line, " \t");
  bool is_all_whitespace = (skip + 1) >= test_line->line_length;

  return is_special | is_all_whitespace;
}

/* format the paragraph according to set margins	*/
void Format() {
  int string_count;
  int offset;
  int temp_case;
  int status;
  int tmp_af;
  int counter;
  unsigned char *line;
  unsigned char *tmp_srchstr;
  unsigned char *temp1;
  unsigned char *temp2;
  unsigned char *temp_dword;
  unsigned char temp_d_char[3];

  temp_d_char[0] = d_char[0];
  temp_d_char[1] = d_char[1];
  temp_d_char[2] = d_char[2];

  /*
   |	if observ_margins is not set, or the current line is blank,
   |	do not format the current paragraph
   */

  if ((!observ_margins) || (Blank_Line(curr_line))) {
    return;
  }

  /*
   |	save the currently set flags, and clear them
   */

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

  /*
   |	get current position in paragraph, so after formatting, the cursor
   |	will be in the same relative position
   */

  tmp_af = (int)auto_format;
  auto_format = false;
  offset = position;
  if (position != 1) {
    prev_word();
  }
  temp_dword = d_word;
  d_word = nullptr;
  temp_case = (int)case_sen;
  case_sen = true;
  tmp_srchstr = srch_str;
  temp2 = srch_str =
      (unsigned char *)malloc(1 + curr_line->line_length - position);
  if ((*point == ' ') || (*point == '\t')) {
    adv_word();
  }
  offset -= position;
  counter = position;
  line = temp1 = point;
  while ((*temp1 != '\0') && (*temp1 != ' ') && (*temp1 != '\t') &&
         (counter < curr_line->line_length)) {
    *temp2 = *temp1;
    temp2++;
    temp1++;
    counter++;
  }
  *temp2 = '\0';
  if (position != 1) {
    bol();
  }
  while (!Blank_Line(curr_line->prev_line)) {
    bol();
  }
  string_count = 0;
  status = 1;
  while ((line != point) && (status != 0)) {
    status = search(0);
    string_count++;
  }

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

  /*
   |	now get back to the start of the paragraph to start formatting
   */

  if (position != 1) {
    bol();
  }
  while (!Blank_Line(curr_line->prev_line)) {
    bol();
  }

  observ_margins = false;

  /*
   |	Start going through lines, putting spaces at end of lines if they do
   |	not already exist.  Append lines together to get one long line, and
   |	eliminate spacing at begin of lines.
   */

  while (!Blank_Line(curr_line->next_line)) {
    eol();
    left(1);
    if (*point != ' ') {
      right(1);
      insert(' ');
    } else {
      {
        right(1);
      }
    }
    del_char();
    if ((*point == ' ') || (*point == '\t')) {
      del_word();
    }
  }

  /*
   |	Now there is one long line.  Eliminate extra spaces within the line
   |	after the first word (so as not to blow away any indenting the user
   |	may have put in).
   */

  bol();
  adv_word();
  while (position < curr_line->line_length) {
    if ((*point == ' ') && (*(point + 1) == ' ')) {
      del_char();
    } else {
      right(1);
    }
  }

  /*
   |	Now make sure there are two spaces after a '.'.
   */

  bol();
  while (position < curr_line->line_length) {
    if ((*point == '.') && (*(point + 1) == ' ')) {
      right(1);
      insert(' ');
      insert(' ');
      while (*point == ' ') {
        del_char();
      }
    }
    right(1);
  }

  observ_margins = true;
  bol();

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

  /*
   |	create lines between margins
   */

  while (position < curr_line->line_length) {
    while ((scr_pos < right_margin) && (position < curr_line->line_length)) {
      right(1);
    }
    if (position < curr_line->line_length) {
      prev_word();
      if (position == 1) {
        adv_word();
      }
      insert_line(1);
    }
  }

  /*
   |	go back to begin of paragraph, put cursor back to original position
   */

  bol();
  while (!Blank_Line(curr_line->prev_line)) {
    bol();
  }

  /*
   |	find word cursor was in
   */

  while ((status != 0) && (string_count > 0)) {
    search(0);
    string_count--;
  }

  /*
   |	offset the cursor to where it was before from the start of the word
   */

  while (offset > 0) {
    offset--;
    right(1);
  }

  /*
   |	reset flags and strings to what they were before formatting
   */

  if (d_word != nullptr) {
    free(d_word);
  }
  d_word = temp_dword;
  case_sen = (temp_case != 0);
  free(srch_str);
  srch_str = tmp_srchstr;
  d_char[0] = temp_d_char[0];
  d_char[1] = temp_d_char[1];
  d_char[2] = temp_d_char[2];
  auto_format = (tmp_af != 0);

  midscreen(scr_vert, point);
  ee_werase(com_win);
  ee_wrefresh(com_win);
}

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

void dump_ee_conf() {
  FILE *init_file;
  FILE *old_init_file = nullptr;
  char *file_name = ".init.ee";
  char const *home_dir = "~/.init.ee";
  char buffer[512];
  struct stat buf;
  char *string;
  int length;
  int option = 0;

  if (restrict_mode()) {
    return;
  }

  option = menu_op(config_dump_menu);

  ee_werase(com_win);
  ee_wmove(com_win, 0, 0);

  if (option == 0) {
    ee_wprintw(com_win, "%s", conf_not_saved_msg);
    ee_wrefresh(com_win);
    return;
  }
  if (option == 2) {
    file_name = resolve_name(home_dir);
  }

  /*
   |	If a .init.ee file exists, move it to .init.ee.old.
   */

  if (stat(file_name, &buf) != -1) {
    sprintf(buffer, "%s.old", file_name);
    unlink(buffer);
    link(file_name, buffer);
    unlink(file_name);
    old_init_file = fopen(buffer, "re");
  }

  init_file = fopen(file_name, "we");
  if (init_file == nullptr) {
    ee_wprintw(com_win, "%s", conf_dump_err_msg);
    ee_wrefresh(com_win);
    return;
  }

  if (old_init_file != nullptr) {
    /*
     |	Copy non-configuration info into new .init.ee file.
     */
    while ((string = fgets(buffer, 512, old_init_file)) != nullptr) {
      length = strlen(string);
      string[length - 1] = '\0';

      if (unique_test(string, init_strings) == 1) {
        if (compare(string, Echo, false)) {
          fprintf(init_file, "%s\n", string);
        }
      } else {
        {
          fprintf(init_file, "%s\n", string);
        }
      }
    }

    fclose(old_init_file);
  }

  fprintf(init_file, "%s\n", (((int)case_sen) != 0) ? CASE : NOCASE);
  fprintf(init_file, "%s\n", (((int)expand_tabs) != 0) ? EXPAND : NOEXPAND);
  fprintf(init_file, "%s\n", (((int)info_window) != 0) ? INFO : NOINFO);
  fprintf(init_file, "%s\n",
          (((int)observ_margins) != 0) ? MARGINS : NOMARGINS);
  fprintf(init_file, "%s\n",
          (((int)auto_format) != 0) ? AUTOFORMAT : NOAUTOFORMAT);
  fprintf(init_file, "%s %s\n", PRINTCOMMAND, print_command);
  fprintf(init_file, "%s %d\n", RIGHTMARGIN, right_margin);
  fprintf(init_file, "%s\n",
          (((int)nohighlight) != 0) ? NOHIGHLIGHT : HIGHLIGHT);
  fprintf(init_file, "%s\n", (((int)eightbit) != 0) ? EIGHTBIT : NOEIGHTBIT);
  fprintf(init_file, "%s\n",
          (((int)emacs_keys_mode) != 0) ? EMACS_string : NOEMACS_string);
  fprintf(init_file, "%s\n",
          (((int)ee_chinese) != 0) ? chinese_cmd : nochinese_cmd);

  // Dump key bindings
  for (int t = 0; t < 3; t++) {
    control_handler *tbl = (t == 0) ? base_control_table : (t == 1 ? gold_control_table : emacs_control_table);
    const char *cmd = (t == 0) ? BIND : (t == 1 ? GBIND : EBIND);
    for (int i = 0; i < 1024; i++) {
      if (tbl[i] == no_op) continue;
      const char *cmd_name = nullptr;
      for (int j = 0; commands_table[j].name != nullptr; j++) {
        if (commands_table[j].handler == tbl[i]) {
          cmd_name = commands_table[j].name;
          break;
        }
      }
      if (cmd_name != nullptr) {
        fprintf(init_file, "%s %s %s\n", cmd, get_key_name(i), cmd_name);
      }
    }
  }

  fclose(init_file);

  ee_wprintw(com_win, conf_dump_success_msg, file_name);
  ee_wrefresh(com_win);

  if ((option == 2) && (file_name != home_dir)) {
    free(file_name);
  }
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
  (void)sprintf(template, "/tmp/ee.XXXXXXXX");
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
    sprintf(string, "ispell %s", name);
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
void Auto_Format() {
  int string_count;
  int offset;
  int temp_case;
  int word_len;
  int temp_dwl;
  int tmp_d_line_length;
  int leave_loop = 0;
  int status;
  int counter;
  char not_blank;
  unsigned char *line;
  unsigned char *tmp_srchstr;
  unsigned char *temp1;
  unsigned char *temp2;
  unsigned char *temp_dword;
  unsigned char temp_d_char[3];
  unsigned char *tmp_d_line;

  temp_d_char[0] = d_char[0];
  temp_d_char[1] = d_char[1];
  temp_d_char[2] = d_char[2];

  /*
   |	if observ_margins is not set, or the current line is blank,
   |	do not format the current paragraph
   */

  if ((!observ_margins) || (Blank_Line(curr_line))) {
    return;
  }

  /*
   |	get current position in paragraph, so after formatting, the cursor
   |	will be in the same relative position
   */

  tmp_d_line = d_line;
  tmp_d_line_length = dlt_line->line_length;
  d_line = nullptr;
  auto_format = false;
  offset = position;
  if ((position != 1) &&
      ((*point == ' ') || (*point == '\t') ||
       (position == curr_line->line_length) || (*point == '\0'))) {
    prev_word();
  }
  temp_dword = d_word;
  temp_dwl = d_wrd_len;
  d_wrd_len = 0;
  d_word = nullptr;
  temp_case = (int)case_sen;
  case_sen = true;
  tmp_srchstr = srch_str;
  temp2 = srch_str =
      (unsigned char *)malloc(1 + curr_line->line_length - position);
  if ((*point == ' ') || (*point == '\t')) {
    adv_word();
  }
  offset -= position;
  counter = position;
  line = temp1 = point;
  while ((*temp1 != '\0') && (*temp1 != ' ') && (*temp1 != '\t') &&
         (counter < curr_line->line_length)) {
    *temp2 = *temp1;
    temp2++;
    temp1++;
    counter++;
  }
  *temp2 = '\0';
  if (position != 1) {
    bol();
  }
  while (!Blank_Line(curr_line->prev_line)) {
    bol();
  }
  string_count = 0;
  status = 1;
  while ((line != point) && (status != 0)) {
    status = search(0);
    string_count++;
  }

  /*
   |	now get back to the start of the paragraph to start checking
   */

  if (position != 1) {
    bol();
  }
  while (!Blank_Line(curr_line->prev_line)) {
    bol();
  }

  /*
   |	Start going through lines, putting spaces at end of lines if they do
   |	not already exist.  Check line length, and move words to the next line
   |	if they cross the margin.  Then get words from the next line if they
   |	will fit in before the margin.
   */

  counter = 0;

  while (leave_loop == 0) {
    if (position != curr_line->line_length) {
      eol();
    }
    left(1);
    if (*point != ' ') {
      right(1);
      insert(' ');
    } else {
      {
        right(1);
      }
    }

    not_blank = 0;

    /*
     |	fill line if first word on next line will fit
     |	in the line without crossing the margin
     */

    while ((curr_line->next_line != nullptr) &&
           ((word_len = first_word_len(curr_line->next_line)) > 0) &&
           ((scr_pos + word_len) < right_margin)) {
      adv_line();
      if ((*point == ' ') || (*point == '\t')) {
        adv_word();
      }
      del_word();
      if (position != 1) {
        bol();
      }

      /*
       |	We know this line was not blank before, so
       |	make sure that it doesn't have one of the
       |	leading characters that indicate the line
       |	should not be modified.
       |
       |	We also know that this character should not
       |	be left as the first character of this line.
       */

      if ((Blank_Line(curr_line)) && (curr_line->line[0] != '.') &&
          (curr_line->line[0] != '>')) {
        del_line();
        not_blank = 0;
      } else {
        {
          not_blank = 1;
        }
      }

      /*
       |   go to end of previous line
       */
      left(1);
      undel_word();
      eol();
      /*
       |   make sure there's a space at the end of the line
       */
      left(1);
      if (*point != ' ') {
        right(1);
        insert(' ');
      } else {
        {
          right(1);
        }
      }
    }

    /*
     |	make sure line does not cross right margin
     */

    while (right_margin <= scr_pos) {
      prev_word();
      if (position != 1) {
        del_word();
        if (Blank_Line(curr_line->next_line)) {
          insert_line(1);
        } else {
          adv_line();
        }
        if ((*point == ' ') || (*point == '\t')) {
          adv_word();
        }
        undel_word();
        not_blank = 1;
        if (position != 1) {
          bol();
        }
        left(1);
      }
    }

    if ((!Blank_Line(curr_line->next_line)) || (not_blank != 0)) {
      adv_line();
      counter++;
    } else {
      {
        leave_loop = 1;
      }
    }
  }

  /*
   |	go back to begin of paragraph, put cursor back to original position
   */

  if (position != 1) {
    bol();
  }
  while ((counter-- > 0) || (!Blank_Line(curr_line->prev_line))) {
    bol();
  }

  /*
   |	find word cursor was in
   */

  status = 1;
  while ((status != 0) && (string_count > 0)) {
    status = search(0);
    string_count--;
  }

  /*
   |	offset the cursor to where it was before from the start of the word
   */

  while (offset > 0) {
    offset--;
    right(1);
  }

  if ((string_count > 0) && (offset < 0)) {
    while (offset < 0) {
      offset++;
      left(1);
    }
  }

  /*
   |	reset flags and strings to what they were before formatting
   */

  if (d_word != nullptr) {
    free(d_word);
  }
  d_word = temp_dword;
  d_wrd_len = temp_dwl;
  case_sen = (temp_case != 0);
  free(srch_str);
  srch_str = tmp_srchstr;
  d_char[0] = temp_d_char[0];
  d_char[1] = temp_d_char[1];
  d_char[2] = temp_d_char[2];
  auto_format = true;
  dlt_line->line_length = tmp_d_line_length;
  d_line = tmp_d_line;

  formatted = true;
  midscreen(scr_vert, point);
}

void modes_op() {
  int ret_value;
  int counter;
  char *string;

  do {
    sprintf(modes_menu[1].item_string, "%s %s", mode_strings[1],
            (expand_tabs ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[2].item_string, "%s %s", mode_strings[2],
            (case_sen ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[3].item_string, "%s %s", mode_strings[3],
            (observ_margins ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[4].item_string, "%s %s", mode_strings[4],
            (auto_format ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[5].item_string, "%s %s", mode_strings[5],
            (eightbit ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[6].item_string, "%s %s", mode_strings[6],
            (info_window ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[7].item_string, "%s %s", mode_strings[7],
            (emacs_keys_mode ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[8].item_string, "%s %s", mode_strings[8],
            (vi_keys_mode ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[9].item_string, "%s %d", mode_strings[9], right_margin);
    sprintf(modes_menu[10].item_string, "%s %s", mode_strings[10],
            (ee_chinese ? STATE_ON : STATE_OFF));

    ret_value = menu_op(modes_menu);

    switch (ret_value) {
    case 1:
      expand_tabs = !expand_tabs;
      break;
    case 2:
      case_sen = !case_sen;
      break;
    case 3:
      observ_margins = !observ_margins;
      break;
    case 4:
      auto_format = !auto_format;
      if (auto_format) {
        observ_margins = true;
      }
      break;
    case 5:
      eightbit = !eightbit;
      if (!eightbit) {
        ee_chinese = false;
      }
#ifdef NCURSE
      if (ee_chinese)
        nc_setattrib(A_NC_BIG5);
      else
        nc_clearattrib(A_NC_BIG5);
#endif /* NCURSE */

      redraw();
      wnoutrefresh(text_win);
      break;
    case 6:
      info_window = !info_window;
      resize_info_win();
      break;
    case 7:
      emacs_keys_mode = !emacs_keys_mode;
      if (emacs_keys_mode) vi_keys_mode = false;
      update_libedit_mode();
      resize_info_win();
      break;
    case 8:
      vi_keys_mode = !vi_keys_mode;
      if (vi_keys_mode) emacs_keys_mode = false;
      update_libedit_mode();
      resize_info_win();
      break;
    case 9:
      string = get_string(margin_prompt, 1);
      if (string != nullptr) {
        counter = atoi(string);
        if (counter > 0) {
          right_margin = counter;
        }
        free(string);
      }
      break;
    case 10:
      ee_chinese = !ee_chinese;
      if (ee_chinese) {
        eightbit = true;
      }
#ifdef NCURSE
      if (ee_chinese)
        nc_setattrib(A_NC_BIG5);
      else
        nc_clearattrib(A_NC_BIG5);
#endif /* NCURSE */
      redraw();
      break;
    case 11:
      // Handled by menu struct call to dump_ee_conf
      break;
    default:
      break;
    }
  } while (ret_value != 0);
}

/* a strchr() look-alike for systems without strchr() */
char *get_token(char *string, char *substring) {
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

char *resolve_name(const char *name) {
  char long_buffer[1024];
  static char short_buffer[128];
  static char *buffer;
  static char *slash;
  static char *tmp;
  static char *start_of_var;
  static int offset;
  static int index;
  static int counter;
  static struct passwd *user;

  if (name[0] == '~') {
    if (name[1] == '/') {
      index = getuid();
      user = getpwuid(index);
      slash = (char *)name + 1;
    } else {
      slash = (char *)strchr(name, '/');
      if (slash == nullptr) {
        return (char *)name;
      }
      *slash = '\0';
      user = getpwnam((name + 1));
      *slash = '/';
    }
    if (user == nullptr) {
      return (char *)name;
    }
    size_t buf_len = strlen(user->pw_dir) + strlen(slash) + 1;
    buffer = malloc(buf_len);
    snprintf(buffer, buf_len, "%s%s", user->pw_dir, slash);
  } else {
    {
      buffer = (char *)name;
    }
  }

  if (strstr(buffer, "$") != nullptr) {
    tmp = buffer;
    index = 0;

    while ((*tmp != '\0') && (index < 1024)) {

      while ((*tmp != '\0') && (*tmp != '$') && (index < 1024)) {
        long_buffer[index] = *tmp;
        tmp++;
        index++;
      }

      if ((*tmp == '$') && (index < 1024)) {
        counter = 0;
        start_of_var = tmp;
        tmp++;
        if (*tmp == '{') /* } */ /* bracketed variable name */
        {
          tmp++; /* { */
          while ((*tmp != '\0') && (*tmp != '}') && (counter < 128)) {
            short_buffer[counter] = *tmp;
            counter++;
            tmp++;
          } /* { */
          if (*tmp == '}') {
            tmp++;
          }
        } else {
          while ((*tmp != '\0') && (*tmp != '/') && (*tmp != '$') &&
                 (counter < 128)) {
            short_buffer[counter] = *tmp;
            counter++;
            tmp++;
          }
        }
        short_buffer[counter] = '\0';
        if ((slash = getenv(short_buffer)) != nullptr) {
          offset = strlen(slash);
          if ((offset + index) < 1024) {
            strscpy(&long_buffer[index], slash, 1024 - index);
          }
          index += offset;
        } else {
          while ((start_of_var != tmp) && (index < 1024)) {
            long_buffer[index] = *start_of_var;
            start_of_var++;
            index++;
          }
        }
      }
    }

    if (index == 1024) {
      return buffer;
    }
    long_buffer[index] = '\0';

    if (name != buffer) {
      free(buffer);
    }
    size_t buffer_len = index + 1;
    buffer = malloc(buffer_len);
    strscpy(buffer, long_buffer, buffer_len);
  }

  return buffer;
}

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
char *catgetlocal(const char *key, char *string) {
  if (icu_bundle == nullptr)
    return string;

  UErrorCode status = U_ZERO_ERROR;
  int32_t len;
  const UChar *u_str = ures_getStringByKey(icu_bundle, key, &len, &status);

  if (U_SUCCESS(status)) {
    // Convert UChar* to UTF-8 char*
    int32_t utf8_len;
    u_strToUTF8(nullptr, 0, &utf8_len, u_str, len, &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) {
      status = U_ZERO_ERROR;
      char *utf8_buf = malloc(utf8_len + 1);
      u_strToUTF8(utf8_buf, utf8_len + 1, nullptr, u_str, len, &status);
      if (U_SUCCESS(status)) {
        return utf8_buf; // Note: might leak if not careful, but consistent with existing logic
      }
      free(utf8_buf);
    }
  }
  return string;
}
#else
char *catgetlocal(const char *key, char *string) { return string; }
#endif /* HAS_ICU */

/*
 |	The following is to allow for using message catalogs which allow
 |	the software to be 'localized', that is, to use different languages
 |	all with the same binary.  For more information, see your system
 |	documentation, or the X/Open Internationalization Guide.
 */

const char *get_key_name(int i) {
  static char key[16];
  if (i == 0) return "^@";
  if (i < 27) {
    sprintf(key, "^%c", i + '@');
    return key;
  }
  if (i == 27) return "^[";
  if (i == 28) return "^\\";
  if (i == 29) return "^]";
  if (i == 30) return "^^";
  if (i == 31) return "^_";
  if (i >= 512 && i < 768) {
    sprintf(key, "M-%c", i - 512);
    return key;
  }
  if (i >= 768 && i < 1024) {
    sprintf(key, "W-%c", i - 768);
    return key;
  }
  if (i >= KEY_F(1) && i <= KEY_F(12)) {
    sprintf(key, "F%d", i - KEY_F(0));
    return key;
  }
  sprintf(key, "code:%d", i);
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
  sprintf(current_buf, "%s %s", key, short_desc);
  return current_buf;
}

void update_help_strings() {
  static char lines[5][128];
  static char glines[5][128];
  control_handler *tbl = emacs_keys_mode ? emacs_control_table : base_control_table;

#ifdef HAS_MENU
  snprintf(lines[0], 128, "Esc menu  %s  %s  %s  %s",
#else
  snprintf(lines[0], 128, "%s  %s  %s  %s  %s",
#endif
           format_shortcut("prev_page", tbl), format_shortcut("del_char", tbl),
           format_shortcut("eol", tbl), format_shortcut("adv_word", tbl));
  control_keys[0] = lines[0];

  snprintf(lines[1], 128, "%s  %s  %s  %s  %s",
           format_shortcut("command_prompt", tbl), format_shortcut("del_line", tbl),
           format_shortcut("mark", tbl), format_shortcut("replace_prompt", tbl),
           format_shortcut("top_of_txt", tbl));
  control_keys[1] = lines[1];

  snprintf(lines[2], 128, "%s  %s  %s  %s  %s",
           format_shortcut("search", tbl), format_shortcut("cut", tbl),
           format_shortcut("gold_toggle", tbl), format_shortcut("bottom_of_txt", tbl),
           format_shortcut("del_word", tbl));
  control_keys[2] = lines[2];

  snprintf(lines[3], 128, "%s  %s  %s  %s  %s",
           format_shortcut("copy", tbl), format_shortcut("adv_char", tbl),
           format_shortcut("next_page", tbl), format_shortcut("bol", tbl),
           format_shortcut("paste", tbl));
  control_keys[3] = lines[3];

  control_keys[4] = nullptr;

  // GOLD help strings
#ifdef HAS_MENU
  snprintf(glines[0], 128, "Esc menu  %s  %s  %s  %s",
#else
  snprintf(glines[0], 128, "Esc exit  %s  %s  %s  %s",
#endif
           format_shortcut("und_char", gold_control_table),
           format_shortcut("und_word", gold_control_table),
           format_shortcut("und_line", gold_control_table),
           format_shortcut("search_reverse", gold_control_table));
  gold_control_keys[0] = glines[0];

  snprintf(glines[1], 128, "%s  %s  %s  %s",
           format_shortcut("format", gold_control_table),
           format_shortcut("search_prompt", gold_control_table),
           format_shortcut("replace_prompt", gold_control_table),
           format_shortcut("append", gold_control_table));
  gold_control_keys[1] = glines[1];
  gold_control_keys[2] = nullptr;
  gold_control_keys[3] = nullptr;
  gold_control_keys[4] = nullptr;
}

void strings_init() {
  int counter;

  setlocale(LC_ALL, "");
#ifdef HAS_ICU
  UErrorCode status = U_ZERO_ERROR;
  icu_bundle = ures_open(".", uloc_getDefault(), &status);
#endif

  modes_menu[0].item_string = catgetlocal("modes_menu", "modes menu");
  mode_strings[1] = catgetlocal("tabs_to_spaces", "tabs to spaces       ");
  mode_strings[2] = catgetlocal("case_sensitive_search", "case sensitive search");
  mode_strings[3] = catgetlocal("margins_observed", "margins observed     ");
  mode_strings[4] = catgetlocal("auto_paragraph_format", "auto-paragraph format");
  mode_strings[5] = catgetlocal("eightbit_characters", "eightbit characters  ");
  mode_strings[6] = catgetlocal("info_window_toggle", "info window          ");
  mode_strings[7] = catgetlocal("emacs_key_bindings", "emacs key bindings   ");
  mode_strings[8] = catgetlocal("vi_key_bindings", "vi key bindings      ");
  mode_strings[9] = catgetlocal("right_margin_toggle", "right margin         ");
  mode_strings[10] = catgetlocal("sixteen_bit_chars", "16 bit characters    ");
  mode_strings[11] = catgetlocal("save_editor_config", "save editor configuration");
  
  leave_menu[0].item_string = catgetlocal("leave_menu", "leave menu");
  leave_menu[1].item_string = catgetlocal("save_changes", "save changes");
  leave_menu[2].item_string = catgetlocal("no_save", "no save");
  file_menu[0].item_string = catgetlocal("file_menu", "file menu");
  file_menu[1].item_string = catgetlocal("read_file", "read a file");
  file_menu[2].item_string = catgetlocal("write_file", "write a file");
  file_menu[3].item_string = catgetlocal("save_file", "save file");
  file_menu[4].item_string = catgetlocal("print_contents", "print editor contents");
  search_menu[0].item_string = catgetlocal("search_menu", "search menu");
  search_menu[1].item_string = catgetlocal("search_for_prompt", "search for ...");
  search_menu[2].item_string = catgetlocal("search_cmd", "search");
  spell_menu[0].item_string = catgetlocal("spell_menu", "spell menu");
  spell_menu[1].item_string = catgetlocal("use_spell", "use 'spell'");
  spell_menu[2].item_string = catgetlocal("use_ispell", "use 'ispell'");
  misc_menu[0].item_string = catgetlocal("misc_menu", "miscellaneous menu");
  misc_menu[1].item_string = catgetlocal("format_paragraph", "format paragraph");
  misc_menu[2].item_string = catgetlocal("shell_command", "shell command");
  misc_menu[3].item_string = catgetlocal("check_spelling", "check spelling");
  main_menu[0].item_string = catgetlocal("main_menu", "main menu");
  main_menu[1].item_string = catgetlocal("leave_editor", "leave editor");
  main_menu[2].item_string = catgetlocal("help_cmd", "help");
  main_menu[3].item_string = catgetlocal("file_operations", "file operations");
  main_menu[4].item_string = catgetlocal("redraw_screen", "redraw screen");
  main_menu[5].item_string = catgetlocal("settings", "settings");
  main_menu[6].item_string = catgetlocal("search", "search");
  main_menu[7].item_string = catgetlocal("miscellaneous", "miscellaneous");
  help_text[0] = catgetlocal("control_keys_header", "Control keys:                                "
                                 "                              ");
  help_text[1] = catgetlocal("help_text_1", "^a ascii code           ^i tab               "
                                 "   ^r right                   ");
  help_text[2] = catgetlocal("help_text_2", "^b bottom of text       ^j newline           "
                                 "   ^t top of text             ");
  help_text[3] = catgetlocal("help_text_3", "^c command              ^k delete char       "
                                 "   ^u up                      ");
  help_text[4] = catgetlocal("help_text_4", "^d down                 ^l left              "
                                 "   ^v undelete word           ");
  help_text[5] = catgetlocal("help_text_5", "^e search prompt        ^m newline           "
                                 "   ^w delete word             ");
  help_text[6] = catgetlocal("help_text_6", "^f undelete char        ^n next page         "
                                 "   ^x search                  ");
  help_text[7] = catgetlocal("help_text_7", "^g begin of line        ^o end of line       "
                                 "   ^y delete line             ");
  help_text[8] = catgetlocal("help_text_8", "^h backspace            ^p prev page         "
                                 "   ^z undelete line           ");
  help_text[9] = catgetlocal("help_text_9", "^[ (escape) menu        ESC-Enter: exit ee   "
                                 "                              ");
  help_text[10] = catgetlocal("help_text_blank", "                                            "
                                  "                              ");
  help_text[11] = catgetlocal("commands_header", "Commands:                                   "
                                  "                              ");
  help_text[12] = catgetlocal("commands_help_1", "help    : get this info                 "
                                  "file    : print file name          ");
  help_text[13] = catgetlocal("commands_help_2", "read    : read a file                   "
                                  "char    : ascii code of char       ");
  help_text[14] = catgetlocal("commands_help_3", "write   : write a file                  "
                                  "case    : case sensitive search    ");
  help_text[15] = catgetlocal("commands_help_4", "                                        "
                                  "nocase  : case insensitive search  ");
  help_text[16] = catgetlocal("commands_help_5", "                                        "
                                  "!cmd    : execute \"cmd\" in shell   ");
  help_text[17] = catgetlocal("commands_help_6", "line    : display line #                0-9 "
                                  "    : go to line \"#\"           ");
  help_text[18] = catgetlocal("commands_help_7", "expand  : expand tabs                   "
                                  "noexpand: do not expand tabs         ");
  help_text[19] = catgetlocal("commands_help_8", "                                            "
                                  "                                 ");
  help_text[20] = catgetlocal("usage_summary", "  ee [+#] [-i] [-e] [-h] [file(s)]          "
                                  "                                  ");
  help_text[21] = catgetlocal("usage_options", "+# :go to line #  -i :no info window  -e : "
                                  "don't expand tabs  -h :no highlight");

  command_strings[0] =
      catgetlocal("command_strings_1", "help : get help info  |file  : print file name         "
                      "|line : print line # ");
  command_strings[1] =
      catgetlocal("command_strings_2", "read : read a file    |char  : ascii code of char      "
                      "|0-9 : go to line \"#\"");
  command_strings[2] =
      catgetlocal("command_strings_3", "write: write a file   |case  : case sensitive search   "
                      "|exit : leave and save ");
  command_strings[3] =
      catgetlocal("command_strings_4", "!cmd : shell \"cmd\"    |nocase: ignore case in search  "
                      " |quit : leave, no save");
  command_strings[4] =
      catgetlocal("command_strings_5", "expand: expand tabs   |noexpand: do not expand tabs     "
                      "                      ");
  com_win_message = catgetlocal("press_esc_for_menu", "    press Escape (^[) for menu");
  no_file_string = catgetlocal("no_file", "no file");
  ascii_code_str = catgetlocal("ascii_code_prompt", "ascii code: ");
  printer_msg_str = catgetlocal("sending_to_printer", "sending contents of buffer to \"%s\" ");
  command_str = catgetlocal("command_prompt", "command: ");
  file_write_prompt_str = catgetlocal("file_write_prompt", "name of file to write: ");
  file_read_prompt_str = catgetlocal("file_read_prompt", "name of file to read: ");
  char_str = catgetlocal("character_info", "character = %d");
  unkn_cmd_str = catgetlocal("unknown_command", "unknown command \"%s\"");
  non_unique_cmd_msg = catgetlocal("command_not_unique", "entered command is not unique");
  line_num_str = catgetlocal("line_info", "line %d  ");
  line_len_str = catgetlocal("length_info", "length = %d");
  current_file_str = catgetlocal("current_file_info", "current file is \"%s\" ");
  usage0 =
      catgetlocal("usage_text", "usage: %s [-i] [-e] [-h] [+line_number] [file(s)]\n");
  usage1 = catgetlocal("usage_opt_i", "       -i   turn off info window\n");
  usage2 = catgetlocal("usage_opt_e", "       -e   do not convert tabs to spaces\n");
  usage3 = catgetlocal("usage_opt_h", "       -h   do not use highlighting\n");
  file_is_dir_msg = catgetlocal("file_is_dir", "file \"%s\" is a directory");
  new_file_msg = catgetlocal("new_file", "new file \"%s\"");
  cant_open_msg = catgetlocal("cant_open_file", "can't open \"%s\"");
  open_file_msg = catgetlocal("file_lines_info", "file \"%s\", %d lines");
  file_read_fin_msg = catgetlocal("finished_reading", "finished reading file \"%s\"");
  reading_file_msg = catgetlocal("reading_file", "reading file \"%s\"");
  read_only_msg = catgetlocal("read_only", ", read only");
  file_read_lines_msg = catgetlocal("file_lines_count", "file \"%s\", %d lines");
  save_file_name_prompt = catgetlocal("enter_filename", "enter name of file: ");
  file_not_saved_msg = catgetlocal("no_filename_saved", "no filename entered: file not saved");
  changes_made_prompt =
      catgetlocal("changes_made_sure", "changes have been made, are you sure? (y/n [n]) ");
  yes_char = catgetlocal("yes_char", "y");
  file_exists_prompt =
      catgetlocal("file_exists_overwrite", "file already exists, overwrite? (y/n) [n] ");
  create_file_fail_msg = catgetlocal("unable_to_create", "unable to create file \"%s\"");
  writing_file_msg = catgetlocal("writing_file", "writing file \"%s\"");
  file_written_msg = catgetlocal("file_written_info", "\"%s\" %d lines, %d characters");
  searching_msg = catgetlocal("searching", "           ...searching");
  str_not_found_msg = catgetlocal("string_not_found", "string \"%s\" not found");
  search_prompt_str = catgetlocal("search_for_prompt", "search for: ");
  exec_err_msg = catgetlocal("could_not_exec", "could not exec %s\n");
  continue_msg = catgetlocal("press_return", "press return to continue ");
  menu_cancel_msg = catgetlocal("press_esc_cancel", "press Esc to cancel");
  menu_size_err_msg = catgetlocal("menu_too_large", "menu too large for window");
  press_any_key_msg = catgetlocal("press_any_key", "press any key to continue ");
  shell_prompt = catgetlocal("shell_command_prompt", "shell command: ");
  formatting_msg = catgetlocal("formatting_paragraph", "...formatting paragraph...");
  shell_echo_msg =
      catgetlocal("spell_header", "<!echo 'list of unrecognized words'; echo -=-=-=-=-=-");
  spell_in_prog_msg =
      catgetlocal("sending_to_spell", "sending contents of edit buffer to 'spell'");
  margin_prompt = catgetlocal("right_margin_info", "right margin is: ");
  restricted_msg = catgetlocal("restricted_mode_error",
      "restricted mode: unable to perform requested operation");
  STATE_ON = catgetlocal("state_on", "ON");
  STATE_OFF = catgetlocal("state_off", "OFF");
  HELP = catgetlocal("cmd_help", "HELP");
  WRITE = catgetlocal("cmd_write", "WRITE");
  READ = catgetlocal("cmd_read", "READ");
  LINE = catgetlocal("cmd_line", "LINE");
  FILE_str = catgetlocal("cmd_file", "FILE");
  CHARACTER = catgetlocal("cmd_character", "CHARACTER");
  REDRAW = catgetlocal("cmd_redraw", "REDRAW");
  RESEQUENCE = catgetlocal("cmd_resequence", "RESEQUENCE");
  AUTHOR = catgetlocal("cmd_author", "AUTHOR");
  VERSION = catgetlocal("cmd_version", "VERSION");
  CASE = catgetlocal("cmd_case", "CASE");
  NOCASE = catgetlocal("cmd_nocase", "NOCASE");
  EXPAND = catgetlocal("cmd_expand", "EXPAND");
  NOEXPAND = catgetlocal("cmd_noexpand", "NOEXPAND");
  Exit_string = catgetlocal("cmd_exit", "EXIT");
  QUIT_string = catgetlocal("cmd_quit", "QUIT");
  INFO = catgetlocal("cmd_info", "INFO");
  NOINFO = catgetlocal("cmd_noinfo", "NOINFO");
  MARGINS = catgetlocal("cmd_margins", "MARGINS");
  NOMARGINS = catgetlocal("cmd_nomargins", "NOMARGINS");
  AUTOFORMAT = catgetlocal("cmd_autoformat", "AUTOFORMAT");
  NOAUTOFORMAT = catgetlocal("cmd_noautoformat", "NOAUTOFORMAT");
  Echo = catgetlocal("cmd_echo", "ECHO");
  PRINTCOMMAND = catgetlocal("cmd_printcommand", "PRINTCOMMAND");
  RIGHTMARGIN = catgetlocal("cmd_rightmargin", "RIGHTMARGIN");
  HIGHLIGHT = catgetlocal("cmd_highlight", "HIGHLIGHT");
  NOHIGHLIGHT = catgetlocal("cmd_nohighlight", "NOHIGHLIGHT");
  EIGHTBIT = catgetlocal("cmd_eightbit", "EIGHTBIT");
  NOEIGHTBIT = catgetlocal("cmd_noeightbit", "NOEIGHTBIT");
  
  VI_string = catgetlocal("cmd_vi", "VI");
  NOVI_string = catgetlocal("cmd_novi", "NOVI");

  emacs_help_text[0] = help_text[0];
  emacs_help_text[1] =
      catgetlocal("emacs_help_1", "^a beginning of line    ^i tab                  ^r "
                       "restore word            ");
  emacs_help_text[2] =
      catgetlocal("emacs_help_2", "^b back 1 char          ^j undel char           ^t top "
                       "of text             ");
  emacs_help_text[3] =
      catgetlocal("emacs_help_3", "^c command              ^k delete line          ^u "
                       "bottom of text          ");
  emacs_help_text[4] =
      catgetlocal("emacs_help_4", "^d delete char          ^l undelete line        ^v "
                       "next page               ");
  emacs_help_text[5] =
      catgetlocal("emacs_help_5", "^e end of line          ^m newline              ^w "
                       "delete word             ");
  emacs_help_text[6] =
      catgetlocal("emacs_help_6", "^f forward 1 char       ^n next line            ^x "
                       "search                  ");
  emacs_help_text[7] =
      catgetlocal("emacs_help_7", "^g go back 1 page       ^o ascii char insert    ^y "
                       "search prompt           ");
  emacs_help_text[8] =
      catgetlocal("emacs_help_8", "^h backspace            ^p prev line            ^z "
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
      catgetlocal("emacs_control_1", "^[ (escape) menu ^y search prompt ^k delete line   ^p "
                       "prev li     ^g prev page");
  emacs_control_keys[1] =
      catgetlocal("emacs_control_2", "^o ascii code    ^x search        ^l undelete line ^n "
                       "next li     ^v next page");
  emacs_control_keys[2] =
      catgetlocal("emacs_control_3", "^u end of file    ^a begin of line  ^w delete word   ^b "
                       "back 1 char ^z next word");
  emacs_control_keys[3] =
      catgetlocal("emacs_control_4", "^t top of text    ^e end of line    ^r restore word  ^f "
                       "forward char            ");
  emacs_control_keys[4] =
      catgetlocal("emacs_control_5", "^c command        ^d delete char    ^j undelete char     "
                       "                         ");
  
  EMACS_string = catgetlocal("cmd_emacs", "EMACS");
  NOEMACS_string = catgetlocal("cmd_noemacs", "NOEMACS");
  BIND = catgetlocal("bind_cmd", "BIND");
  GBIND = catgetlocal("gbind_cmd", "GBIND");
  EBIND = catgetlocal("ebind_cmd", "EBIND");
  usage4 = catgetlocal("usage_line_num", "       +#   put cursor at line #\n");
  conf_dump_err_msg = catgetlocal(
      "config_err_msg", "unable to open .init.ee for writing, no configuration saved!");
  conf_dump_success_msg = catgetlocal("config_saved_msg", "ee configuration saved in file %s");
  modes_menu[11].item_string = mode_strings[11];
  config_dump_menu[0].item_string = catgetlocal("save_ee_config", "save ee configuration");
  config_dump_menu[1].item_string =
      catgetlocal("save_current_dir", "save in current directory");
  config_dump_menu[2].item_string = catgetlocal("save_home_dir", "save in home directory");
  conf_not_saved_msg = catgetlocal("config_not_saved", "ee configuration not saved");
  ree_no_file_msg = catgetlocal("ree_no_file", "must specify a file when invoking ree");
  menu_too_lrg_msg = catgetlocal("menu_too_large_alt", "menu too large for window");
  more_above_str = catgetlocal("more_above", "^^more^^");
  more_below_str = catgetlocal("more_below", "VVmoreVV");

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
  commands[13] = nullptr;
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
  
  update_help_strings();

  /*
   |	allocate space for strings here for settings menu
   */

  for (counter = 1; counter < NUM_MODES_ITEMS; counter++) {
    modes_menu[counter].item_string = malloc(80);
  }
}
