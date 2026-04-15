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

char *ee_copyright_message = "Copyright (c) 1986, 1990, 1991, 1992, 1993, "
                             "1994, 1995, 1996, 2009 Hugh Mahon ";

static char *version = "@(#) ee, version " EE_VERSION " $Revision: 1.104 $";

// Correct prototypes for menu callbacks which expect int (*)(int) or int
// (*)(struct menu_entries *)
static int quit_wrapper(int arg) {
  quit(arg);
  return 0;
}
static int file_op_wrapper(int arg) {
  file_op(arg);
  return 0;
}
static int search_wrapper(int arg) {
  search(arg);
  return 0;
}
static int menu_op_wrapper(struct menu_entries *m) { return menu_op(m); }

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
static struct text *first_line; /* first line of current buffer		*/
static struct text *dlt_line;   /* structure for info on deleted line	*/
static struct text *curr_line;  /* current line cursor is on		*/
static struct text *tmp_line;   /* temporary line pointer		*/
static struct text *srch_line;  /* temporary pointer for search routine */

static struct files *top_of_stack = nullptr;

const TSLanguage *tree_sitter_c(void);

// Tree-Sitter Globals
TSParser *ts_parser = nullptr;
static TSTree *ts_tree = nullptr;

// LSP Globals
static int lsp_to_child[2];
static int lsp_from_child[2];
static pid_t lsp_pid = -1;

static struct diagnostic *diagnostics_list = nullptr;

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

static void lsp_send(const char *msg);

static void lsp_start() {
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

static void lsp_open_file(const char *filename) {
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

static void lsp_poll() {
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

#ifndef NO_CATGETS

static nl_catd catalog;
#endif /* NO_CATGETS */

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

enum { TAB = 9 };
#define max(a, b)                                                              \
  ({                                                                           \
    typeof(a) _a = (a);                                                        \
    typeof(b) _b = (b);                                                        \
    _a > _b ? _a : _b;                                                         \
  })
#define min(a, b)                                                              \
  ({                                                                           \
    typeof(a) _a = (a);                                                        \
    typeof(b) _b = (b);                                                        \
    _a < _b ? _a : _b;                                                         \
  })

/*
 |	defines for type of data to show in info window
 */

enum { CONTROL_KEYS = 1, COMMANDS = 2 };

static int d_wrd_len;    /* length of deleted word		*/
static int position;     /* offset in bytes from begin of line	*/
static int scr_pos;      /* horizontal position			*/
static int scr_vert;     /* vertical position on screen		*/
static int scr_horz;     /* horizontal position on screen	*/
static int absolute_lin; /* number of lines from top		*/
static int tmp_vert, tmp_horz;
static bool input_file;           /* indicate to read input file		*/
static bool recv_file;            /* indicate reading a file		*/
static bool edit;                 /* continue executing while true	*/
static bool gold;                 /* 'gold' function key pressed		*/
static int fildes;                /* file descriptor			*/
static bool case_sen;             /* case sensitive search flag		*/
static int last_line;             /* last line for text display		*/
static int last_col;              /* last column for text display		*/
static int horiz_offset = 0;      /* offset from left edge of text	*/
static bool clear_com_win;        /* flag to indicate com_win needs clearing */
static bool text_changes = false; /* indicate changes have been made to text */
static int get_fd;                /* file descriptor for reading a file	*/
static bool info_window = true;   /* flag to indicate if help window visible */
static int info_type =
    CONTROL_KEYS;               /* flag to indicate type of info to display */
static bool expand_tabs = true; /* flag for expanding tabs		*/
static int right_margin = 0;    /* the right margin 			*/
static bool observ_margins = true; /* flag for whether margins are observed */
static int shell_fork;
static int temp_stdin;           /* temporary storage for stdin		*/
static int temp_stdout;          /* temp storage for stdout descriptor	*/
static int temp_stderr;          /* temp storage for stderr descriptor	*/
static int pipe_out[2];          /* pipe file desc for output		*/
static int pipe_in[2];           /* pipe file descriptors for input	*/
static bool out_pipe;            /* flag that info is piped out		*/
static bool in_pipe;             /* flag that info is piped in		*/
static bool formatted = false;   /* flag indicating paragraph formatted	*/
static bool auto_format = false; /* flag for auto_format mode		*/
static bool restricted = false;  /* flag to indicate restricted mode	*/
static bool nohighlight = false; /* turns off highlighting		*/
static bool eightbit = true;     /* eight bit character flag		*/
static int local_LINES = 0;      /* copy of LINES, to detect when win resizes */
static int local_COLS = 0;       /* copy of COLS, to detect when win resizes  */
static bool curses_initialized =
    false; /* flag indicating if curses has been started*/
static bool emacs_keys_mode =
    false;                      /* mode for if emacs key binings are used    */
static bool ee_chinese = false; /* allows handling of multi-byte characters  */
                                /* by checking for high bit in a byte the    */
                                /* code recognizes a two-byte character      */
                                /* sequence				     */

static unsigned char *point;      /* points to current position in line	*/
static unsigned char *srch_str;   /* pointer for search string		*/
static unsigned char *u_srch_str; /* pointer to non-case sensitive search	*/
static unsigned char *srch_1;     /* pointer to start of suspect string	*/
static unsigned char *srch_2;     /* pointer to next character of string	*/
static unsigned char *srch_3;
static char *in_file_name = nullptr; /* name of input file		*/
static char *tmp_file;        /* temporary file name			*/
static unsigned char *d_char; /* deleted character			*/
static unsigned char *d_word; /* deleted word				*/
static unsigned char *d_line; /* deleted line				*/
static unsigned char
    in_string[MAX_IN_STRING]; /* buffer for reading a file		*/
static char *print_command = (char *)"lpr"; /* string to use for the print command 	*/
static char *start_at_line = nullptr; /* move to this line at start of session*/
static int in; /* input character			*/

static FILE *temp_fp;    /* temporary file pointer		*/
static FILE *bit_bucket; /* file pointer to /dev/null		*/

static char *table[] = {"^@", "^A", "^B", "^C", "^D",  "^E", "^F", "^G",
                        "^H", "\t", "^J", "^K", "^L",  "^M", "^N", "^O",
                        "^P", "^Q", "^R", "^S", "^T",  "^U", "^V", "^W",
                        "^X", "^Y", "^Z", "^[", "^\\", "^]", "^^", "^_"};

static WINDOW *com_win;
static WINDOW *text_win;
static WINDOW *help_win;
static WINDOW *info_win;

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
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 8. right margin	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 9. chinese text	*/
    {"", nullptr, nullptr, nullptr, dump_ee_conf,
     -1}, /* 10. save editor config */
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}
    /* terminator		*/
};

char *mode_strings[11];

enum { NUM_MODES_ITEMS = 10 };

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

enum { READ_FILE = 1, WRITE_FILE = 2, SAVE_FILE = 3 };

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
    {"", nullptr, nullptr, nullptr, spell_op, -1},
    {"", nullptr, nullptr, nullptr, ispell_op, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries misc_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, Format, -1},
    {"", nullptr, nullptr, nullptr, shell_op, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)spell_menu, nullptr, nullptr, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

struct menu_entries main_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, leave_op, -1},
    {"", nullptr, nullptr, nullptr, help, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)file_menu, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, redraw, -1},
    {"", nullptr, nullptr, nullptr, modes_op, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)search_menu, nullptr, nullptr, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)misc_menu, nullptr, nullptr, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};

char *help_text[23];
char *control_keys[5];

char *emacs_help_text[22];
char *emacs_control_keys[5];

char *command_strings[5];
char *commands[32];
char *init_strings[22];

enum { MENU_WARN = 1 };

enum { max_alpha_char = 36 };

/*
 |	Declarations for strings for localization
 */

char *com_win_message; /* to be shown in com_win if no info window */
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

/* beginning of main program          */
int main(int argc, char *argv[]) {
  int counter;

  for (counter = 1; counter < 24; counter++) {

    signal(counter, SIG_IGN);
  }

  /* Always read from (and write to) a terminal. */
  if ((isatty(STDIN_FILENO) == 0) || (isatty(STDOUT_FILENO) == 0)) {
    fprintf(stderr, "ee's standard input and output must be a terminal\n");
    exit(1);
  }

  signal(SIGCHLD, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGINT, edit_abort);
  d_char =
      (unsigned char *)malloc(3); /* provide a buffer for multi-byte chars */
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
  set_up_term();
  if (right_margin == 0) {
    right_margin = COLS - 1;
  }
  if (top_of_stack == nullptr) {
    if (restrict_mode()) {
      wmove(com_win, 0, 0);
      werase(com_win);
      wprintw(com_win, "%s", ree_no_file_msg);
      wrefresh(com_win);
      edit_abort(0);
    }
    wprintw(com_win, "%s", no_file_string);
    wrefresh(com_win);
  } else {
    {
      check_fp();
    }
  }

  clear_com_win = true;

  counter = 0;

  lsp_start();
  if (in_file_name != nullptr) {
    lsp_open_file((const char *)in_file_name);
  }

  while (edit) {
    lsp_poll();
    /*
     |  display line and column information
     */
    if (info_window) {
      paint_info_win();
    }

    wrefresh(text_win);
    in = wgetch(text_win);
    if (in == -1) {
      exit(0);
    } /* without this exit ee will go into an
                                          infinite loop if the network
                                          session detaches */

    resize_check();

    if (clear_com_win) {
      clear_com_win = false;
      wmove(com_win, 0, 0);
      werase(com_win);
      if (!info_window) {
        wprintw(com_win, "%s", com_win_message);
      }
      wrefresh(com_win);
    }

    if (in > 255) {
      {
        function_key();
      }
    } else if ((in == '\10') || (in == ASCII_DEL)) {
      in = ASCII_BACKSPACE; /* make sure key is set to backspace */
      delete_char_at_cursor(1);
    } else if ((in > 31) || (in == 9)) {
      {
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
      reparse();
      if (in_file_name != nullptr) {
        lsp_change_file((const char *)in_file_name);
      }
      text_changes = false;
    }
  }
  return 0;
}

/* resize the line to length + factor*/
static unsigned char *resiz_line(int factor, struct text *rline, int rpos) {
  unsigned char *rpoint;
  int resiz_var;

  rline->max_length += factor;
  rline->line = realloc(rline->line, rline->max_length);
  rpoint = rline->line;
  for (resiz_var = 1; (resiz_var < rpos); resiz_var++) {
    rpoint++;
  }
  return rpoint;
}

/* insert character into line		*/
static void insert(int character) {
  int counter;
  int value;
  unsigned char *temp;  /* temporary pointer			*/
  unsigned char *temp2; /* temporary pointer			*/

  if ((character == '\011') && expand_tabs) {
    counter = len_char('\011', scr_horz);
    for (; counter > 0; counter--) {
      insert(' ');
    }
    if (auto_format) {
      Auto_Format();
    }
    return;
  }
  text_changes = true;
  if ((curr_line->max_length - curr_line->line_length) < 5) {
    point = resiz_line(10, curr_line, position);
  }
  curr_line->line_length++;
  temp = point;
  counter = position;
  while (counter < curr_line->line_length) /* find end of line */
  {
    counter++;
    temp++;
  }
  temp++; /* increase length of line by one	*/
  while (point < temp) {
    temp2 = temp - 1;
    *temp = *temp2; /* shift characters over by one		*/
    temp--;
  }
  *point = character; /* insert new character			*/
  wclrtoeol(text_win);
  if (isprint((unsigned char)character) == 0) /* check for TAB character*/
  {
    scr_pos = scr_horz += out_char(text_win, character, scr_horz);
    point++;
    position++;
  } else {
    waddch(text_win, (unsigned char)character);
    scr_pos = ++scr_horz;
    point++;
    position++;
  }

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

  if (auto_format && (character == ' ') && (!formatted)) {
    Auto_Format();
  } else if ((character != ' ') && (character != '\t')) {
    formatted = false;
  }

  draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
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
    if (ee_chinese && (position >= 2) && (*(point - 2) > 127)) {
      del_width = 2;
    }
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
        d_char[0] = *point;
        d_char[1] = *(point + 1);
      }
      d_char[del_width] = '\0';
    }
    while (temp_pos <= curr_line->line_length) {
      temp_pos++;
      *tp = *temp2;
      tp++;
      temp2++;
    }
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
    temp2 = temp_buff->line;
    if (in == 8) {
      d_char[0] = '\n';
      d_char[1] = '\0';
    }
    tp = point;
    temp_pos = 1;
    while (temp_pos < temp_buff->line_length) {
      curr_line->line_length++;
      temp_pos++;
      *tp = *temp2;
      tp++;
      temp2++;
    }
    *tp = '\0';
    free(temp_buff->line);
    free(temp_buff);
    temp_buff = curr_line;
    temp_vert = scr_vert;
    scr_pos = scr_horz;
    if (scr_vert < last_line) {
      wmove(text_win, scr_vert + 1, 0);
      wdeleteln(text_win);
    }
    while ((temp_buff != nullptr) && (temp_vert < last_line)) {
      temp_buff = temp_buff->next_line;
      temp_vert++;
    }
    if ((temp_vert == last_line) && (temp_buff != nullptr)) {
      tp = temp_buff->line;
      wmove(text_win, last_line, 0);
      wclrtobot(text_win);
      draw_line(last_line, 0, tp, 1, temp_buff->line_length);
      wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    }
  }
  draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
  formatted = false;
}

/* find the proper horizontal position for the pointer	*/
void scanline(const unsigned char *pos) {
  int temp;
  unsigned char *ptr;

  ptr = curr_line->line;
  temp = 0;
  while (ptr < pos) {
    if (*ptr <= 8) {
      temp += 2;
    } else if (*ptr == 9) {
      temp += tabshift(temp);
    } else if ((*ptr >= 10) && (*ptr <= 31)) {
      temp += 2;
    } else if ((*ptr >= 32) && (*ptr < 127)) {
      temp++;
    } else if (*ptr == 127) {
      temp += 2;
    } else if (!eightbit) {
      temp += 5;
    } else {
      temp++;
    }
    ptr++;
  }
  scr_horz = temp;
  if ((scr_horz - horiz_offset) > last_col) {
    horiz_offset = (scr_horz - (scr_horz % 8)) - (COLS - 8);
    midscreen(scr_vert, point);
  } else if (scr_horz < horiz_offset) {
    horiz_offset = max(0, (scr_horz - (scr_horz % 8)));
    midscreen(scr_vert, point);
  }
}

/* give the number of spaces to shift	*/
int tabshift(int temp_int) {
  int leftover = ((temp_int + 1) % 8);
  if (leftover == 0) {
    return 1;
  }
  return (9 - leftover);
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
      waddch(window, ' ');
    }
    return i1;
  }
  if ((character >= '\0') && (character < ' ')) {
    string = table[character];
  } else if ((character < 0) || (character >= 127)) {
    if (character == 127) {
      {
        string = "^?";
      }
    } else if (!eightbit) {
      sprintf(string2, "<%d>", (character < 0) ? (character + 256) : character);
      string = string2;
    } else {
      waddch(window, (unsigned char)character);
      return 1;
    }
  } else {
    waddch(window, (unsigned char)character);
    return 1;
  }
  for (i2 = 0;
       (string[i2] != '\0') && (((column + i2 + 1) - horiz_offset) < last_col);
       i2++) {
    waddch(window, (unsigned char)string[i2]);
  }
  return (strlen(string));
}

/* return the length of the character   */
int len_char(int character, int column) {
  int length;

  if (character == '\t') {
    length = tabshift(column);
  } else if ((character >= 0) && (character < 32)) {
    length = 2;
  } else if ((character >= 32) && (character <= 126)) {
    length = 1;
  } else if (character == 127) {
    length = 2;
  } else if (((character > 126) || (character < 0)) && (!eightbit)) {
    length = 5;
  } else {
    length = 1;
  }

  return length;
}

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

/* redraw line from current position */
static void draw_line(int vertical, int horiz, unsigned char *ptr, int t_pos,
                      int length) {
  int d;               /* partial length of special or tab char to display  */
  unsigned char *temp; /* temporary pointer to position in line	     */
  int abs_column;      /* offset in screen units from begin of line	     */
  int column;          /* horizontal position on screen		     */
  int row;             /* vertical position on screen			     */
  int posit;           /* temporary position indicator within line	     */

  abs_column = horiz;
  column = horiz - horiz_offset;
  row = vertical;
  temp = ptr;
  d = 0;
  posit = t_pos;

  // Find line number
  int line_no = 1;
  struct text const *l_ptr = first_line;
  while (l_ptr != nullptr && l_ptr->line != ptr - (t_pos - 1)) {
    l_ptr = l_ptr->next_line;
    line_no++;
  }

  if (column < 0) {
    wmove(text_win, row, 0);
    wclrtoeol(text_win);
  }
  while (column < 0) {
    d = len_char(*temp, abs_column);
    abs_column += d;
    column += d;
    posit++;
    temp++;
  }
  wmove(text_win, row, column);
  wclrtoeol(text_win);
  while ((posit < length) && (column <= last_col)) {
    int attr = get_node_attribute(line_no, posit - 1);

    // Check diagnostics
    struct diagnostic const *diag = diagnostics_list;
    while (diag != nullptr) {
      if (diag->line == line_no && diag->col == posit - 1) {
        attr |= A_REVERSE | COLOR_PAIR(8); // Highlight error
        break;
      }
      diag = diag->next;
    }

    wattron(text_win, attr);
    if (isprint(*temp) == 0) {
      column += len_char(*temp, abs_column);
      abs_column += out_char(text_win, *temp, abs_column);
    } else {
      abs_column++;
      column++;
      waddch(text_win, *temp);
    }
    wattroff(text_win, attr);
    posit++;
    temp++;
  }
  if (column < last_col) {
    wclrtoeol(text_win);
  }
  wmove(text_win, vertical, (horiz - horiz_offset));
}

/* insert new line		*/
void insert_line(int disp) {
  int temp_pos;
  int temp_pos2;
  unsigned char *temp;
  unsigned char *extra;
  struct text *temp_nod;

  text_changes = true;
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  wclrtoeol(text_win);
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
  temp_pos2 = position;
  temp = point;
  if (temp_pos2 < curr_line->line_length) {
    temp_pos = 1;
    while (temp_pos2 < curr_line->line_length) {
      if ((temp_nod->max_length - temp_nod->line_length) < 5) {
        extra = resiz_line(10, temp_nod, temp_pos);
      }
      temp_nod->line_length++;
      temp_pos++;
      temp_pos2++;
      *extra = *temp;
      extra++;
      temp++;
    }
    temp = point;
    *temp = '\0';
    temp = resiz_line((1 - temp_nod->line_length), curr_line, position);
    curr_line->line_length = 1 + temp - curr_line->line;
  }
  curr_line->line_length = position;
  absolute_lin++;
  curr_line = temp_nod;
  *extra = '\0';
  position = 1;
  point = curr_line->line;
  if (disp != 0) {
    if (scr_vert < last_line) {
      scr_vert++;
      wclrtoeol(text_win);
      wmove(text_win, scr_vert, 0);
      winsertln(text_win);
    } else {
      wmove(text_win, 0, 0);
      wdeleteln(text_win);
      wmove(text_win, last_line, 0);
      wclrtobot(text_win);
    }
    scr_pos = scr_horz = 0;
    if (horiz_offset != 0) {
      horiz_offset = 0;
      midscreen(scr_vert, point);
    }
    draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
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
  int len = 0;
  unsigned char *ptr = line->line;
  while ((*ptr != '\0') && ((*ptr == ' ') || (*ptr == '\t'))) {
    ptr++;
  }
  while ((*ptr != '\0') && (*ptr != ' ') && (*ptr != '\t')) {
    len++;
    ptr++;
  }
  return len;
}

/* move to next word in string		*/
void *next_word(void *s) {
  unsigned char *string = (unsigned char *)s;
  while ((*string != '\0') && ((*string != 32) && (*string != 9))) {
    string++;
  }
  while ((*string != '\0') && ((*string == 32) || (*string == 9))) {
    string++;
  }
  return string;
}

/* move to start of previous word in text	*/
static void prev_word() {
  if (position != 1) {
    if ((position != 1) &&
        ((point[-1] == ' ') ||
         (point[-1] == '\t'))) { /* if at the start of a word	*/
      while ((position != 1) && ((*point != ' ') && (*point != '\t'))) {
        left(1);
      }
    }
    while ((position != 1) && ((*point == ' ') || (*point == '\t'))) {
      left(1);
    }
    while ((position != 1) && ((*point != ' ') && (*point != '\t'))) {
      left(1);
    }
    if ((position != 1) && ((*point == ' ') || (*point == '\t'))) {
      right(1);
    }
  } else {
    {
      left(1);
    }
  }
}

/* use control for commands		*/
void control() {
  char *string;

  if (gold) {
    gold = false;
    if (in == 16) { /* control p - prev buff */
      /* not implemented yet */
    } else if (in == 22) { /* control v - forward */
      /* not implemented yet */
    } else if (in == 11) { /* control k - und char */
      undel_char();
    } else if (in == 21) { /* control u - mark */
      /* not implemented yet */
    } else if (in == 26) { /* control z - repl prmpt */
      search_prompt();
    } else if (in == 24) { /* control x - fmt parag */
      Format();
    } else if (in == 18) { /* control r - reverse */
      /* not implemented yet */
    } else if (in == 12) { /* control l - und line */
      undel_line();
    } else if (in == 6) { /* control f - srch prmpt */
      search_prompt();
    } else if (in == 2) { /* control b - append */
      /* not implemented yet */
    } else if (in == 23) { /* control w - und word */
      undel_word();
    } else if (in == 3) { /* control c - clear line */
      bol();
      del_line();
    } else if (in == 4) { /* control d - prefix */
      /* not implemented yet */
    } else if (in == 14) { /* control n - next buff */
      /* not implemented yet */
    } else if (in == 25) { /* control y - prev word */
      prev_word();
    }
    if (info_window) {
      paint_info_win();
    }
    return;
  }

  if (in == 7) { /* control g - GOLD */
    gold = true;
    if (info_window) {
      paint_info_win();
    }
    return;
  }

  if (in == 1) { /* control a - adv char */
    right(1);
  } else if (in == 2) { /* control b - end of txt */
    bottom();
  } else if (in == 3) { /* control c - copy */
    /* not implemented yet */
  } else if (in == 4) { /* control d - beg of lin */
    bol();
  } else if (in == 5) { /* control e - command */
    command_prompt();
  } else if (in == 6) { /* control f - search */
    search(1);
  } else if (in == 8) { /* control h - backspace */
    delete_char_at_cursor(1);
  } else if (in == 10 || in == 13) { /* control j/m - carrg rtrn */
    insert_line(1);
  } else if (in == 11) { /* control k - del char */
    del_char();
  } else if (in == 12) { /* control l - del line */
    del_line();
  } else if (in == 14) { /* control n - next page */
    move_rel('d', max(5, (last_line - 5)));
  } else if (in == 15) { /* control o - end of lin */
    eol();
  } else if (in == 16) { /* control p - prev page */
    move_rel('u', max(5, (last_line - 5)));
  } else if (in == 18) { /* control r - redraw */
    redraw();
  } else if (in == 20) { /* control t - top of txt */
    top();
  } else if (in == 21) { /* control u - mark */
    /* not implemented yet */
  } else if (in == 22) { /* control v - paste */
    /* not implemented yet */
  } else if (in == 23) { /* control w - del word */
    del_word();
  } else if (in == 24) { /* control x - cut */
    /* not implemented yet */
  } else if (in == 25) { /* control y - adv word */
    adv_word();
  } else if (in == 26) { /* control z - replace */
    /* not implemented yet */
  } else if (in == 27) { /* control [ (escape) */
    menu_op(main_menu);
  }
}

/*
 |	Emacs control-key bindings
 */

void emacs_control() {
  char *string;

  if (in == 1) {
    { /* control a	*/
      bol();
    }
  } else if (in == 2) {
    { /* control b	*/
      left(1);
    }
  } else if (in == 3) /* control c	*/
  {
    command_prompt();
  } else if (in == 4) {
    { /* control d	*/
      del_char();
    }
  } else if (in == 5) {
    { /* control e	*/
      eol();
    }
  } else if (in == 6) {
    { /* control f	*/
      right(1);
    }
  } else if (in == 7) {
    { /* control g	*/
      move_rel('u', max(5, (last_line - 5)));
    }
  } else if (in == 8) {
    { /* control h	*/
      delete_char_at_cursor(1);
    }
  } else if (in == 9) {
    { /* control i	*/
      ;
    }
  } else if (in == 10) {
    { /* control j	*/
      undel_char();
    }
  } else if (in == 11) {
    { /* control k	*/
      del_line();
    }
  } else if (in == 12) {
    { /* control l	*/
      undel_line();
    }
  } else if (in == 13) {
    { /* control m	*/
      insert_line(1);
    }
  } else if (in == 14) {
    { /* control n	*/
      down();
    }
  } else if (in == 15) /* control o	*/
  {
    string = get_string(ascii_code_str, 1);
    if (*string != '\0') {
      in = atoi(string);
      wmove(text_win, scr_vert, (scr_horz - horiz_offset));
      insert(in);
    }
    free(string);
  } else if (in == 16) {
    { /* control p	*/
      up();
    }
  } else if (in == 17) {
    { /* control q	*/
      ;
    }
  } else if (in == 18) {
    { /* control r	*/
      undel_word();
    }
  } else if (in == 19) {
    { /* control s	*/
      ;
    }
  } else if (in == 20) {
    { /* control t	*/
      top();
    }
  } else if (in == 21) {
    { /* control u	*/
      bottom();
    }
  } else if (in == 22) {
    { /* control v	*/
      move_rel('d', max(5, (last_line - 5)));
    }
  } else if (in == 23) {
    { /* control w	*/
      del_word();
    }
  } else if (in == 24) {
    { /* control x	*/
      search(1);
    }
  } else if (in == 25) {
    { /* control y	*/
      search_prompt();
    }
  } else if (in == 26) {
    { /* control z	*/
      adv_word();
    }
  } else if (in == 27) /* control [ (escape)	*/
  {
    menu_op(main_menu);
  }
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
    wmove(text_win, 0, 0);
    wdeleteln(text_win);
    wmove(text_win, last_line, 0);
    wclrtobot(text_win);
    draw_line(last_line, 0, point, 1, curr_line->line_length);
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
  point = curr_line->line;
  position = 1;
  if (scr_vert == 0) {
    winsertln(text_win);
    draw_line(0, 0, point, 1, curr_line->line_length);
  } else {
    {
      scr_vert--;
    }
  }
  while (position < curr_line->line_length) {
    position++;
    point++;
  }
}

/* move left one character	*/
void left(int disp) {
  if (point != curr_line->line) /* if not at begin of line	*/
  {
    if (ee_chinese && (position >= 2) && (*(point - 2) > 127)) {
      point--;
      position--;
    }
    point--;
    position--;
    scanline(point);
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
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
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
}

/* move right one character	*/
void right(int disp) {
  if (position < curr_line->line_length) {
    if (ee_chinese && (*point > 127) &&
        ((curr_line->line_length - position) >= 2)) {
      point++;
      position++;
    }
    point++;
    position++;
    scanline(point);
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
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
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    position = 1;
  }
}

/* move to the same column as on other line	*/
void find_pos() {
  scr_horz = 0;
  position = 1;
  while ((scr_horz < scr_pos) && (position < curr_line->line_length)) {
    if (*point == 9) {
      {
        scr_horz += tabshift(scr_horz);
      }
    } else if (*point < ' ') {
      {
        scr_horz += 2;
      }
    } else if (ee_chinese && (*point > 127) &&
               ((curr_line->line_length - position) >= 2)) {
      scr_horz += 2;
      point++;
      position++;
    } else {
      {
        scr_horz++;
      }
    }
    position++;
    point++;
  }
  if ((scr_horz - horiz_offset) > last_col) {
    horiz_offset = (scr_horz - (scr_horz % 8)) - (COLS - 8);
    midscreen(scr_vert, point);
  } else if (scr_horz < horiz_offset) {
    horiz_offset = max(0, (scr_horz - (scr_horz % 8)));
    midscreen(scr_vert, point);
  }
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
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
      paint_info_win();
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
  wmove(com_win, 0, 0);
  wclrtoeol(com_win);
  wprintw(com_win, printer_msg_str, print_command);
  wrefresh(com_win);
  command(buffer);
}

void command_prompt() {
  char *cmd_str;
  int result;

  info_type = COMMANDS;
  paint_info_win();
  cmd_str = get_string(command_str, 1);
  if ((result = unique_test(cmd_str, commands)) != 1) {
    werase(com_win);
    wmove(com_win, 0, 0);
    if (result == 0) {
      wprintw(com_win, unkn_cmd_str, cmd_str);
    } else {
      wprintw(com_win, "%s", non_unique_cmd_msg);
    }

    wrefresh(com_win);

    info_type = CONTROL_KEYS;
    paint_info_win();

    if (cmd_str != nullptr) {
      free(cmd_str);
    }
    return;
  }
  command(cmd_str);
  wrefresh(com_win);
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  info_type = CONTROL_KEYS;
  paint_info_win();
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, line_num_str, curr_line->line_number);
    wprintw(com_win, line_len_str, curr_line->line_length);
  } else if (compare(cmd_str, FILE_str, false)) {
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    if (in_file_name == nullptr) {
      wprintw(com_win, "%s", no_file_string);
    } else {
      wprintw(com_win, current_file_str, in_file_name);
    }
  } else if ((*cmd_str >= '0') && (*cmd_str <= '9')) {
    {
      goto_line(cmd_str);
    }
  } else if (compare(cmd_str, CHARACTER, false)) {
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, char_str, *point);
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, "written by Hugh Mahon");
  } else if (compare(cmd_str, VERSION, false)) {
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, "%s", version);
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
  } else if (compare(cmd_str, Exit_string, false)) {
    {
      finish();
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
  } else if (compare(cmd_str, QUIT_string, false)) {
    {
      quit(0);
    }
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, unkn_cmd_str, cmd_str);
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
  char *tmp_string;
  char *nam_str;
  char *g_point;
  int tmp_int;
  int g_horz;
  int g_position;
  int g_pos;
  int esc_flag;

  g_point = tmp_string = malloc(512);
  wmove(com_win, 0, 0);
  wclrtoeol(com_win);

  waddstr(com_win, prompt);
  wrefresh(com_win);
  nam_str = tmp_string;
  clear_com_win = true;
  g_horz = g_position = get_string_len(prompt, strlen(prompt), 0);
  g_pos = 0;
  do {
    esc_flag = 0;
    in = wgetch(com_win);
    if (in == -1) {
      exit(0);
    }
    if (((in == 8) || (in == 127) || (in == KEY_BACKSPACE)) && (g_pos > 0)) {
      tmp_int = g_horz;
      g_pos--;
      g_horz = get_string_len(g_point, g_pos, g_position);
      tmp_int = tmp_int - g_horz;
      for (; 0 < tmp_int; tmp_int--) {
        if ((g_horz + tmp_int) < (last_col - 1)) {
          waddch(com_win, '\010');
          waddch(com_win, ' ');
          waddch(com_win, '\010');
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
          exit(0);
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
          waddch(com_win, (unsigned char)in);
        }
      }
      nam_str++;
    }
    wrefresh(com_win);
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
  wrefresh(com_win);
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

static void goto_line(char *cmd_str) {
  int number;
  int i;
  unsigned char *ptr;
  char direction = '\0';
  struct text *t_line;

  ptr = (unsigned char *)cmd_str;
  i = 0;
  while ((*ptr >= '0') && (*ptr <= '9')) {
    i = (i * 10) + (*ptr - '0');
    ptr++;
  }
  number = i;
  i = 0;
  t_line = curr_line;
  while ((t_line->line_number > number) && (t_line->prev_line != nullptr)) {
    i++;
    t_line = t_line->prev_line;
    direction = 'u';
  }
  while ((t_line->line_number < number) && (t_line->next_line != nullptr)) {
    i++;
    direction = 'd';
    t_line = t_line->next_line;
  }
  if ((i < 30) && (i > 0)) {
    move_rel(direction, i);
  } else {
    if (direction != 'd') {
      absolute_lin += i;
    } else {
      absolute_lin -= i;
    }
    curr_line = t_line;
    point = curr_line->line;
    position = 1;
    midscreen((last_line / 2), point);
    scr_pos = scr_horz;
  }
  wmove(com_win, 0, 0);
  wclrtoeol(com_win);
  wprintw(com_win, line_num_str, curr_line->line_number);
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* put current line in middle of screen	*/
void midscreen(int line, unsigned char *pnt) {
  struct text *mid_line;
  int i;

  line = min(line, last_line);
  mid_line = curr_line;
  for (i = 0; ((i < line) && (curr_line->prev_line != nullptr)); i++) {
    curr_line = curr_line->prev_line;
  }
  scr_vert = scr_horz = 0;
  wmove(text_win, 0, 0);
  draw_screen();
  scr_vert = i;
  curr_line = mid_line;
  scanline(pnt);
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
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
    wprintw(com_win, file_is_dir_msg, tmp_file);
    wrefresh(com_win);
    if (input_file) {
      quit(0);
      return;
    }
    return;
  }
  if ((get_fd = open(tmp_file, O_RDONLY | O_CLOEXEC)) == -1) {
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    if (input_file) {
      wprintw(com_win, new_file_msg, tmp_file);
    } else {
      wprintw(com_win, cant_open_msg, tmp_file);
    }
    wrefresh(com_win);
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    wrefresh(text_win);
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
  reparse();
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    text_changes = true;
    if ((tmp_file != nullptr) && (*tmp_file != '\0')) {
      wprintw(com_win, file_read_fin_msg, tmp_file);
    }
  }
  wrefresh(com_win);
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  wrefresh(text_win);
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, reading_file_msg, file_name);
    if (access(file_name, 2) != 0) /* check permission to write */
    {
      if ((errno == ENOTDIR) || (errno == EACCES) || (errno == EROFS) ||
          (errno == ETXTBSY) || (errno == EFAULT)) {
        wprintw(com_win, "%s", read_only_msg);
        ro_flag = 1;
      }
    }
    wrefresh(com_win);
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, file_read_lines_msg, in_file_name, curr_line->line_number);
    if (ro_flag != 0) {
      wprintw(com_win, "%s", read_only_msg);
    }
    wrefresh(com_win);
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
    for (temp_counter = 1; temp_counter < char_count; temp_counter++) {
      *point = *str1;
      point++;
      str1++;
    }
    *point = '\0';
    *append = 0;
    if ((num == length) && (*str2 != '\n')) {
      *append = 1;
    }
  }
}

void draw_screen() /* redraw the screen from current postion	*/
{
  struct text *temp_line;
  unsigned char *line_out;
  int temp_vert;

  temp_line = curr_line;
  temp_vert = scr_vert;
  wclrtobot(text_win);
  while ((temp_line != nullptr) && (temp_vert <= last_line)) {
    line_out = temp_line->line;
    draw_line(temp_vert, 0, line_out, 1, temp_line->line_length);
    temp_vert++;
    temp_line = temp_line->next_line;
  }
  wmove(text_win, temp_vert, 0);
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
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
    wmove(com_win, 0, 0);
    wprintw(com_win, "%s", file_not_saved_msg);
    wclrtoeol(com_win);
    wrefresh(com_win);
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

  touchwin(text_win);
  wrefresh(text_win);
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
      wrefresh(info_win);
    }
    wrefresh(com_win);
    resetty();
    endwin();
    putchar('\n');
    exit(0);
  } else {
    delete_text();
    recv_file = true;
    input_file = true;
    check_fp();
  }
  return 0;
}

static void edit_abort(int arg) {
  (void)arg;
  wrefresh(com_win);
  resetty();
  endwin();
  putchar('\n');
  exit(1);
}

void delete_text() {
  while (curr_line->next_line != nullptr) {
    curr_line = curr_line->next_line;
  }
  while (curr_line != first_line) {
    free(curr_line->line);
    curr_line = curr_line->prev_line;
    absolute_lin--;
    free(curr_line->next_line);
  }
  curr_line->next_line = nullptr;
  *curr_line->line = '\0';
  curr_line->line_length = 1;
  curr_line->line_number = 1;
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
      wmove(com_win, 0, 0);
      wclrtoeol(com_win);
      wprintw(com_win, create_file_fail_msg, file_name);
      wrefresh(com_win);
      return 0;
    }

    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, writing_file_msg, file_name);
    wrefresh(com_win);
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, file_written_msg, file_name, lines, charac);
    wrefresh(com_win);
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
    wmove(com_win, 0, 0);
    wclrtoeol(com_win);
    wprintw(com_win, "%s", searching_msg);
    wrefresh(com_win);
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
      wmove(com_win, 0, 0);
      wclrtoeol(com_win);
      wrefresh(com_win);
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
      wmove(com_win, 0, 0);
      wclrtoeol(com_win);
      wprintw(com_win, str_not_found_msg, srch_str);
      wrefresh(com_win);
    }
    wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
  return found;
}

/* prompt and read search string (srch_str)	*/
static void search_prompt() {
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

/* delete current character	*/
void del_char() {
  in = 8;                                /* backspace */
  if (position < curr_line->line_length) /* if not end of line	*/
  {
    if (ee_chinese && (*point > 127) &&
        ((curr_line->line_length - position) >= 2)) {
      point++;
      position++;
    }
    position++;
    point++;
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
  draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
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
  draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
}

/* delete from cursor to end of line	*/
void del_line() {
  unsigned char *dl1;
  unsigned char *dl2;
  int tposit;

  if (d_line != nullptr) {
    free(d_line);
  }
  d_line = malloc(curr_line->line_length);
  dl1 = d_line;
  dl2 = point;
  tposit = position;
  while (tposit < curr_line->line_length) {
    *dl1 = *dl2;
    dl1++;
    dl2++;
    tposit++;
  }
  dlt_line->line_length = 1 + tposit - position;
  *dl1 = '\0';
  *point = '\0';
  curr_line->line_length = position;
  wclrtoeol(text_win);
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
  draw_line(scr_vert, scr_horz, point, position, curr_line->line_length);
}

/* advance to next word		*/
void adv_word() {
  while ((position < curr_line->line_length) &&
         ((*point != 32) && (*point != 9))) {
    right(1);
  }
  while ((position < curr_line->line_length) &&
         ((*point == 32) || (*point == 9))) {
    right(1);
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
  wmove(text_win, scr_vert, (scr_horz - horiz_offset));
}

/* go to end of line			*/
void eol() {
  if (position < curr_line->line_length) {
    while (position < curr_line->line_length) {
      right(1);
    }
  } else if (curr_line->next_line != nullptr) {
    right(1);
    while (position < curr_line->line_length) {
      right(1);
    }
  }
}

/* move to beginning of line	*/
void bol() {
  if (point != curr_line->line) {
    while (point != curr_line->line) {
      left(1);
    }
  } else if (curr_line->prev_line != nullptr) {
    scr_pos = 0;
    up();
  }
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
    keypad(com_win, false);
    keypad(text_win, false);
    echo();
    nl();
    noraw();
    resetty();

#ifndef NCURSE
    endwin();
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
    noecho();
    nonl();
    raw();
    keypad(text_win, true);
    keypad(com_win, true);
    if (info_window) {
      clearok(info_win, true);
    }
  }

  redraw();
}

/* set up the terminal for operating with ae	*/
void set_up_term() {
  if (!curses_initialized) {
    initscr();
    savetty();
    noecho();
    raw();
    nonl();

    if (has_colors()) {
      start_color();
      use_default_colors();
      init_pair(1, COLOR_GREEN, -1);   // comment
      init_pair(2, COLOR_YELLOW, -1);  // string
      init_pair(3, COLOR_CYAN, -1);    // number
      init_pair(4, COLOR_YELLOW, -1);  // type
      init_pair(5, COLOR_BLUE, -1);    // function
      init_pair(6, COLOR_WHITE, -1);   // variable
      init_pair(7, COLOR_MAGENTA, -1); // keyword
      init_pair(8, COLOR_RED, -1);     // error/diagnostic
    }

    curses_initialized = true;
  }

  int info_win_height = 0;
  if (info_window) {
    if (LINES < 10) {
      info_win_height = 2;
    } else if (LINES < 15) {
      info_win_height = 4;
    } else {
      info_win_height = 6;
    }
    last_line = LINES - (info_win_height + 2);
  } else {
    info_window = false;
    last_line = LINES - 2;
  }

  idlok(stdscr, true);
  com_win = newwin(1, COLS, (LINES - 1), 0);
  keypad(com_win, true);
  idlok(com_win, true);
  wrefresh(com_win);
  if (!info_window) {
    text_win = newwin((LINES - 1), COLS, 0, 0);
  } else {
    text_win = newwin((LINES - (info_win_height + 1)), COLS, info_win_height, 0);
  }
  keypad(text_win, true);
  idlok(text_win, true);
  wrefresh(text_win);
  help_win = newwin((LINES - 1), COLS, 0, 0);
  keypad(help_win, true);
  idlok(help_win, true);
  if (info_window) {
    info_type = CONTROL_KEYS;
    info_win = newwin(info_win_height, COLS, 0, 0);
    werase(info_win);
    paint_info_win();
  }

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
  set_up_term();
  redraw();
  wrefresh(text_win);
}

static char item_alpha[] = "abcdefghijklmnopqrstuvwxyz0123456789 ";

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
    wmove(com_win, 0, 0);
    werase(com_win);
    wprintw(com_win, "%s", menu_too_lrg_msg);
    wrefresh(com_win);
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
    if (menu_list[0].argument != MENU_WARN) {
      max_height = vert_size + 8;
    } else {
      max_height = vert_size + 7;
    }
    top_offset = 4;
  }
  x_off = (COLS - max_width) / 2;
  y_off = (LINES - max_height - 1) / 2;
  temp_win = newwin(max_height, max_width, y_off, x_off);
  keypad(temp_win, true);

  paint_menu(menu_list, max_width, max_height, list_size, top_offset, temp_win,
             off_start, vert_size);

  counter = 1;

  do {
    if (off_start > 2) {
      wmove(temp_win, (1 + counter + top_offset - off_start), 3);
    } else {
      wmove(temp_win, (counter + top_offset - off_start), 3);
    }

    wrefresh(temp_win);
    in = wgetch(temp_win);
    input = in;
    if (input == -1) {
      exit(0);
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
        if (menu_list[0].argument != MENU_WARN) {
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

  werase(temp_win);
  wrefresh(temp_win);
  delwin(temp_win);

  if ((menu_list[counter].procedure != nullptr) ||
      (menu_list[counter].iprocedure != nullptr) ||
      (menu_list[counter].nprocedure != nullptr)) {
    if (menu_list[counter].argument != -1) {
      (*menu_list[counter].iprocedure)(menu_list[counter].argument);
    } else if (menu_list[counter].ptr_argument != nullptr) {
      (*menu_list[counter].procedure)(menu_list[counter].ptr_argument);
    } else {
      (*menu_list[counter].nprocedure)();
    }
  }

  if (info_window) {
    paint_info_win();
  }
  redraw();

  return counter;
}

static void paint_menu(struct menu_entries menu_list[], int max_width,
                       int max_height, int list_size, int top_offset,
                       WINDOW *menu_win, int off_start, int vert_size) {
  int counter;
  int temp_int;

  werase(menu_win);

  /*
   |	output top and bottom portions of menu box only if window
   |	large enough
   */

  if (max_height > vert_size) {
    wmove(menu_win, 1, 1);
    if (!nohighlight) {
      wstandout(menu_win);
    }
    waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      waddch(menu_win, '-');
    }
    waddch(menu_win, '+');

    wmove(menu_win, (max_height - 2), 1);
    waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      waddch(menu_win, '-');
    }
    waddch(menu_win, '+');
    wstandend(menu_win);
    wmove(menu_win, 2, 3);
    waddstr(menu_win, menu_list[0].item_string);
    wmove(menu_win, (max_height - 3), 3);
    if (menu_list[0].argument != MENU_WARN) {
      waddstr(menu_win, menu_cancel_msg);
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

    wmove(menu_win, temp_int, 1);
    waddch(menu_win, '|');
    wmove(menu_win, temp_int, (max_width - 2));
    waddch(menu_win, '|');
  }
  wstandend(menu_win);

  if (list_size > vert_size) {
    if (off_start >= 3) {
      temp_int = 1;
      wmove(menu_win, top_offset, 3);
      waddstr(menu_win, more_above_str);
    } else {
      {
        temp_int = 0;
      }
    }

    for (counter = off_start;
         ((temp_int + counter - off_start) < (vert_size - 1)); counter++) {
      wmove(menu_win, (top_offset + temp_int + (counter - off_start)), 3);
      if (list_size > 1) {
        wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), max_alpha_char)]);
      }
      waddstr(menu_win, menu_list[counter].item_string);
    }

    wmove(menu_win, (top_offset + (vert_size - 1)), 3);

    if (counter == list_size) {
      if (list_size > 1) {
        wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), max_alpha_char)]);
      }
      wprintw(menu_win, "%s", menu_list[counter].item_string);
    } else {
      {
        wprintw(menu_win, "%s", more_below_str);
      }
    }
  } else {
    for (counter = 1; counter <= list_size; counter++) {
      wmove(menu_win, (top_offset + counter - 1), 3);
      if (list_size > 1) {
        wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), max_alpha_char)]);
      }
      waddstr(menu_win, menu_list[counter].item_string);
    }
  }
}

void help() {
  int counter;

  werase(help_win);
  clearok(help_win, true);
  for (counter = 0; counter < 22; counter++) {
    wmove(help_win, counter, 0);
    waddstr(help_win,
            (emacs_keys_mode) ? emacs_help_text[counter] : help_text[counter]);
  }
  wrefresh(help_win);
  werase(com_win);
  wmove(com_win, 0, 0);
  wprintw(com_win, "%s", press_any_key_msg);
  wrefresh(com_win);
  counter = wgetch(com_win);
  if (counter == -1) {
    exit(0);
  }
  werase(com_win);
  wmove(com_win, 0, 0);
  werase(help_win);
  wrefresh(help_win);
  wrefresh(com_win);
  redraw();
}

void paint_info_win() {
  int counter;
  int height, width;

  if (!info_window) {
    return;
  }

  getmaxyx(info_win, height, width);

  werase(info_win);
  for (counter = 0; counter < height - 1; counter++) {
    wmove(info_win, counter, 0);
    wclrtoeol(info_win);
    if (info_type == CONTROL_KEYS) {
      if (counter < 5) {
        waddstr(info_win, (emacs_keys_mode) ? emacs_control_keys[counter]
                                           : control_keys[counter]);
      }
    } else if (info_type == COMMANDS) {
      if (counter < 5) {
        waddstr(info_win, command_strings[counter]);
      }
    }
  }

  // Construct status line
  wmove(info_win, height - 1, 0);
  if (!nohighlight) {
    wstandout(info_win);
  }

  char status_buf[128];
  snprintf(status_buf, sizeof(status_buf), " %sline %d col %d top %d ",
           gold ? "GOLD " : "", curr_line->line_number, scr_horz, absolute_lin);
  int status_len = strlen(status_buf);

  char const *legend = " ^ = Ctrl key  ---- access HELP through menu ---";
  int legend_len = strlen(legend);

  // Draw legend
  for (int i = 0; i < width && i < legend_len; i++) {
    waddch(info_win, legend[i]);
  }

  // Fill with '=' up to status info
  int current_x = getcurx(info_win);
  int status_start_x = width - status_len;
  if (status_start_x < current_x) {
    status_start_x = current_x;
  }

  for (int i = current_x; i < status_start_x; i++) {
    waddch(info_win, '=');
  }

  // Draw status info
  if (status_start_x < width) {
    waddstr(info_win, status_buf);
  }

  // Final fill if needed
  current_x = getcurx(info_win);
  for (int i = current_x; i < width; i++) {
    waddch(info_win, '=');
  }

  wstandend(info_win);
  wrefresh(info_win);
}

void no_info_window() {
  if (!info_window) {
    return;
  }
  delwin(info_win);
  delwin(text_win);
  info_window = false;
  last_line = LINES - 2;
  text_win = newwin((LINES - 1), COLS, 0, 0);
  keypad(text_win, true);
  idlok(text_win, true);
  clearok(text_win, true);
  midscreen(scr_vert, point);
  wrefresh(text_win);
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
  text_win = newwin((LINES - (info_win_height + 1)), COLS, info_win_height, 0);
  keypad(text_win, true);
  idlok(text_win, true);
  werase(text_win);
  info_window = true;
  info_win = newwin(info_win_height, COLS, 0, 0);
  werase(info_win);
  info_type = CONTROL_KEYS;
  midscreen(min(scr_vert, last_line), point);
  clearok(info_win, true);
  paint_info_win();
  wrefresh(text_win);
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
      wmove(com_win, 0, 0);
      wprintw(com_win, "%s", file_not_saved_msg);
      wclrtoeol(com_win);
      wrefresh(com_win);
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
    clearok(info_win, true);
    paint_info_win();
  } else {
    {
      clearok(text_win, true);
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
  unsigned char *line;
  int length;

  if (test_line == nullptr) {
    return true;
  }

  length = 1;
  line = test_line->line;

  /*
   |	To handle troff/nroff documents, consider a line with a
   |	period ('.') in the first column to be blank.  To handle mail
   |	messages with included text, consider a line with a '>' blank.
   */

  if ((*line == '.') || (*line == '>')) {
    return true;
  }

  while (((*line == ' ') || (*line == '\t')) &&
         (length < test_line->line_length)) {
    length++;
    line++;
  }
  return length == test_line->line_length;
}

/* format the paragraph according to set margins	*/
static void Format() {
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

  wmove(com_win, 0, 0);
  wclrtoeol(com_win);
  wprintw(com_win, "%s", formatting_msg);
  wrefresh(com_win);

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

  wmove(com_win, 0, 0);
  wclrtoeol(com_win);
  wprintw(com_win, "%s", formatting_msg);
  wrefresh(com_win);

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

  wmove(com_win, 0, 0);
  wclrtoeol(com_win);
  wprintw(com_win, "%s", formatting_msg);
  wrefresh(com_win);

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
  werase(com_win);
  wrefresh(com_win);
}

static char *init_name[3] = {"/usr/share/misc/init.ee", nullptr, ".init.ee"};

/* check for init file and read it if it exists	*/
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
          }
        } else if (compare(str1, NOINFO, false)) {
          {
            info_window = false;
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
          }
        } else if (compare(str1, NOEMACS_string, false)) {
          {
            emacs_keys_mode = false;
          }
        } else if (compare(str1, chinese_cmd, false)) {
          ee_chinese = true;
          eightbit = true;
        } else if (compare(str1, nochinese_cmd, false)) {
          {
            ee_chinese = false;
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
    }
  }
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

  werase(com_win);
  wmove(com_win, 0, 0);

  if (option == 0) {
    wprintw(com_win, "%s", conf_not_saved_msg);
    wrefresh(com_win);
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
    wprintw(com_win, "%s", conf_dump_err_msg);
    wrefresh(com_win);
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

  fclose(init_file);

  wprintw(com_win, conf_dump_success_msg, file_name);
  wrefresh(com_win);

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
  wmove(com_win, 0, 0);
  wprintw(com_win, "%s", spell_in_prog_msg);
  wrefresh(com_win);
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
    wmove(com_win, 0, 0);
    wprintw(com_win, create_file_fail_msg, name);
    wrefresh(com_win);
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
static void Auto_Format() {
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
    sprintf(modes_menu[8].item_string, "%s %d", mode_strings[8], right_margin);
    sprintf(modes_menu[9].item_string, "%s %s", mode_strings[9],
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
      if (info_window) {
        no_info_window();
      } else {
        create_info_window();
      }
      break;
    case 7:
      emacs_keys_mode = !emacs_keys_mode;
      if (info_window) {
        paint_info_win();
      }
      break;
    case 8:
      string = get_string(margin_prompt, 1);
      if (string != nullptr) {
        counter = atoi(string);
        if (counter > 0) {
          right_margin = counter;
        }
        free(string);
      }
      break;
    case 9:
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

  wmove(com_win, 0, 0);
  wprintw(com_win, "%s", restricted_msg);
  wclrtoeol(com_win);
  wrefresh(com_win);
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

#ifndef NO_CATGETS
/*
 |	Get the catalog entry, and if it got it from the catalog,
 |	make a copy, since the buffer will be overwritten by the
 |	next call to catgets().
 */

char *catgetlocal(int number, char *string) {
  char *temp1;
  static char *temp2;

  temp1 = catgets(catalog, 1, number, string);
  if (temp1 != string) {
    size_t t2_len = strlen(temp1) + 1;
    temp2 = malloc(t2_len);
    strscpy(temp2, temp1, t2_len);
    temp1 = temp2;
  }
  return temp1;
}
#else
char *catgetlocal(int number, char *string) { return string; }
#endif /* NO_CATGETS */

/*
 |	The following is to allow for using message catalogs which allow
 |	the software to be 'localized', that is, to use different languages
 |	all with the same binary.  For more information, see your system
 |	documentation, or the X/Open Internationalization Guide.
 */

static void strings_init() {
  int counter;

  setlocale(LC_ALL, "");
#ifndef NO_CATGETS
  catalog = catopen("ee", NL_CAT_LOCALE);
#endif /* NO_CATGETS */

  modes_menu[0].item_string = catgetlocal(1, "modes menu");
  mode_strings[1] = catgetlocal(2, "tabs to spaces       ");
  mode_strings[2] = catgetlocal(3, "case sensitive search");
  mode_strings[3] = catgetlocal(4, "margins observed     ");
  mode_strings[4] = catgetlocal(5, "auto-paragraph format");
  mode_strings[5] = catgetlocal(6, "eightbit characters  ");
  mode_strings[6] = catgetlocal(7, "info window          ");
  mode_strings[8] = catgetlocal(8, "right margin         ");
  leave_menu[0].item_string = catgetlocal(9, "leave menu");
  leave_menu[1].item_string = catgetlocal(10, "save changes");
  leave_menu[2].item_string = catgetlocal(11, "no save");
  file_menu[0].item_string = catgetlocal(12, "file menu");
  file_menu[1].item_string = catgetlocal(13, "read a file");
  file_menu[2].item_string = catgetlocal(14, "write a file");
  file_menu[3].item_string = catgetlocal(15, "save file");
  file_menu[4].item_string = catgetlocal(16, "print editor contents");
  search_menu[0].item_string = catgetlocal(17, "search menu");
  search_menu[1].item_string = catgetlocal(18, "search for ...");
  search_menu[2].item_string = catgetlocal(19, "search");
  spell_menu[0].item_string = catgetlocal(20, "spell menu");
  spell_menu[1].item_string = catgetlocal(21, "use 'spell'");
  spell_menu[2].item_string = catgetlocal(22, "use 'ispell'");
  misc_menu[0].item_string = catgetlocal(23, "miscellaneous menu");
  misc_menu[1].item_string = catgetlocal(24, "format paragraph");
  misc_menu[2].item_string = catgetlocal(25, "shell command");
  misc_menu[3].item_string = catgetlocal(26, "check spelling");
  main_menu[0].item_string = catgetlocal(27, "main menu");
  main_menu[1].item_string = catgetlocal(28, "leave editor");
  main_menu[2].item_string = catgetlocal(29, "help");
  main_menu[3].item_string = catgetlocal(30, "file operations");
  main_menu[4].item_string = catgetlocal(31, "redraw screen");
  main_menu[5].item_string = catgetlocal(32, "settings");
  main_menu[6].item_string = catgetlocal(33, "search");
  main_menu[7].item_string = catgetlocal(34, "miscellaneous");
  help_text[0] = catgetlocal(35, "Control keys:                                "
                                 "                              ");
  help_text[1] = catgetlocal(36, "^a ascii code           ^i tab               "
                                 "   ^r right                   ");
  help_text[2] = catgetlocal(37, "^b bottom of text       ^j newline           "
                                 "   ^t top of text             ");
  help_text[3] = catgetlocal(38, "^c command              ^k delete char       "
                                 "   ^u up                      ");
  help_text[4] = catgetlocal(39, "^d down                 ^l left              "
                                 "   ^v undelete word           ");
  help_text[5] = catgetlocal(40, "^e search prompt        ^m newline           "
                                 "   ^w delete word             ");
  help_text[6] = catgetlocal(41, "^f undelete char        ^n next page         "
                                 "   ^x search                  ");
  help_text[7] = catgetlocal(42, "^g begin of line        ^o end of line       "
                                 "   ^y delete line             ");
  help_text[8] = catgetlocal(43, "^h backspace            ^p prev page         "
                                 "   ^z undelete line           ");
  help_text[9] = catgetlocal(44, "^[ (escape) menu        ESC-Enter: exit ee   "
                                 "                              ");
  help_text[10] = catgetlocal(45, "                                            "
                                  "                               ");
  help_text[11] = catgetlocal(46, "Commands:                                   "
                                  "                               ");
  help_text[12] = catgetlocal(47, "help    : get this info                 "
                                  "file    : print file name          ");
  help_text[13] = catgetlocal(48, "read    : read a file                   "
                                  "char    : ascii code of char       ");
  help_text[14] = catgetlocal(49, "write   : write a file                  "
                                  "case    : case sensitive search    ");
  help_text[15] = catgetlocal(50, "exit    : leave and save                "
                                  "nocase  : case insensitive search  ");
  help_text[16] = catgetlocal(51, "quit    : leave, no save                "
                                  "!cmd    : execute \"cmd\" in shell   ");
  help_text[17] = catgetlocal(52, "line    : display line #                0-9 "
                                  "    : go to line \"#\"           ");
  help_text[18] = catgetlocal(53, "expand  : expand tabs                   "
                                  "noexpand: do not expand tabs         ");
  help_text[19] = catgetlocal(54, "                                            "
                                  "                                 ");
  help_text[20] = catgetlocal(55, "  ee [+#] [-i] [-e] [-h] [file(s)]          "
                                  "                                  ");
  help_text[21] = catgetlocal(56, "+# :go to line #  -i :no info window  -e : "
                                  "don't expand tabs  -h :no highlight");
  control_keys[0] = catgetlocal(57, "Esc  menu       ^P   prev page  ^K   del char   ^O   end of lin ^Y   adv word   ^G^P prev buff  ^G^V forward    ^J   carrg rtrn");
  control_keys[1] = catgetlocal(58, "^E   command    ^L   del line   ^G^K und char   ^U   mark       ^Z   replace    ^G^X fmt parag  ^G^R reverse    ^H   backspace");
  control_keys[2] = catgetlocal(59, "^T   top of txt ^G^L und line   ^F   search     ^X   cut        ^G^Z repl prmpt ^G^X fmt parag  ^G^B append     ^G   GOLD");
  control_keys[3] = catgetlocal(60, "^B   end of txt ^W   del word   ^G^F srch prmpt ^C   copy       ^G^C clear line ^A   adv char   ^G^D prefix");
  control_keys[4] = catgetlocal(61, "^N   next page  ^G^W und word   ^D   beg of lin ^V   paste      ^G^N next buff  ^G^Y prev word  ^R   redraw");
  command_strings[0] =
      catgetlocal(62, "help : get help info  |file  : print file name         "
                      "|line : print line # ");
  command_strings[1] =
      catgetlocal(63, "read : read a file    |char  : ascii code of char      "
                      "|0-9 : go to line \"#\"");
  command_strings[2] =
      catgetlocal(64, "write: write a file   |case  : case sensitive search   "
                      "|exit : leave and save ");
  command_strings[3] =
      catgetlocal(65, "!cmd : shell \"cmd\"    |nocase: ignore case in search  "
                      " |quit : leave, no save");
  command_strings[4] =
      catgetlocal(66, "expand: expand tabs   |noexpand: do not expand tabs     "
                      "                      ");
  com_win_message = catgetlocal(67, "    press Escape (^[) for menu");
  no_file_string = catgetlocal(68, "no file");
  ascii_code_str = catgetlocal(69, "ascii code: ");
  printer_msg_str = catgetlocal(70, "sending contents of buffer to \"%s\" ");
  command_str = catgetlocal(71, "command: ");
  file_write_prompt_str = catgetlocal(72, "name of file to write: ");
  file_read_prompt_str = catgetlocal(73, "name of file to read: ");
  char_str = catgetlocal(74, "character = %d");
  unkn_cmd_str = catgetlocal(75, "unknown command \"%s\"");
  non_unique_cmd_msg = catgetlocal(76, "entered command is not unique");
  line_num_str = catgetlocal(77, "line %d  ");
  line_len_str = catgetlocal(78, "length = %d");
  current_file_str = catgetlocal(79, "current file is \"%s\" ");
  usage0 =
      catgetlocal(80, "usage: %s [-i] [-e] [-h] [+line_number] [file(s)]\n");
  usage1 = catgetlocal(81, "       -i   turn off info window\n");
  usage2 = catgetlocal(82, "       -e   do not convert tabs to spaces\n");
  usage3 = catgetlocal(83, "       -h   do not use highlighting\n");
  file_is_dir_msg = catgetlocal(84, "file \"%s\" is a directory");
  new_file_msg = catgetlocal(85, "new file \"%s\"");
  cant_open_msg = catgetlocal(86, "can't open \"%s\"");
  open_file_msg = catgetlocal(87, "file \"%s\", %d lines");
  file_read_fin_msg = catgetlocal(88, "finished reading file \"%s\"");
  reading_file_msg = catgetlocal(89, "reading file \"%s\"");
  read_only_msg = catgetlocal(90, ", read only");
  file_read_lines_msg = catgetlocal(91, "file \"%s\", %d lines");
  save_file_name_prompt = catgetlocal(92, "enter name of file: ");
  file_not_saved_msg = catgetlocal(93, "no filename entered: file not saved");
  changes_made_prompt =
      catgetlocal(94, "changes have been made, are you sure? (y/n [n]) ");
  yes_char = catgetlocal(95, "y");
  file_exists_prompt =
      catgetlocal(96, "file already exists, overwrite? (y/n) [n] ");
  create_file_fail_msg = catgetlocal(97, "unable to create file \"%s\"");
  writing_file_msg = catgetlocal(98, "writing file \"%s\"");
  file_written_msg = catgetlocal(99, "\"%s\" %d lines, %d characters");
  searching_msg = catgetlocal(100, "           ...searching");
  str_not_found_msg = catgetlocal(101, "string \"%s\" not found");
  search_prompt_str = catgetlocal(102, "search for: ");
  exec_err_msg = catgetlocal(103, "could not exec %s\n");
  continue_msg = catgetlocal(104, "press return to continue ");
  menu_cancel_msg = catgetlocal(105, "press Esc to cancel");
  menu_size_err_msg = catgetlocal(106, "menu too large for window");
  press_any_key_msg = catgetlocal(107, "press any key to continue ");
  shell_prompt = catgetlocal(108, "shell command: ");
  formatting_msg = catgetlocal(109, "...formatting paragraph...");
  shell_echo_msg =
      catgetlocal(110, "<!echo 'list of unrecognized words'; echo -=-=-=-=-=-");
  spell_in_prog_msg =
      catgetlocal(111, "sending contents of edit buffer to 'spell'");
  margin_prompt = catgetlocal(112, "right margin is: ");
  restricted_msg = catgetlocal(
      113, "restricted mode: unable to perform requested operation");
  STATE_ON = catgetlocal(114, "ON");
  STATE_OFF = catgetlocal(115, "OFF");
  HELP = catgetlocal(116, "HELP");
  WRITE = catgetlocal(117, "WRITE");
  READ = catgetlocal(118, "READ");
  LINE = catgetlocal(119, "LINE");
  FILE_str = catgetlocal(120, "FILE");
  CHARACTER = catgetlocal(121, "CHARACTER");
  REDRAW = catgetlocal(122, "REDRAW");
  RESEQUENCE = catgetlocal(123, "RESEQUENCE");
  AUTHOR = catgetlocal(124, "AUTHOR");
  VERSION = catgetlocal(125, "VERSION");
  CASE = catgetlocal(126, "CASE");
  NOCASE = catgetlocal(127, "NOCASE");
  EXPAND = catgetlocal(128, "EXPAND");
  NOEXPAND = catgetlocal(129, "NOEXPAND");
  Exit_string = catgetlocal(130, "EXIT");
  QUIT_string = catgetlocal(131, "QUIT");
  INFO = catgetlocal(132, "INFO");
  NOINFO = catgetlocal(133, "NOINFO");
  MARGINS = catgetlocal(134, "MARGINS");
  NOMARGINS = catgetlocal(135, "NOMARGINS");
  AUTOFORMAT = catgetlocal(136, "AUTOFORMAT");
  NOAUTOFORMAT = catgetlocal(137, "NOAUTOFORMAT");
  Echo = catgetlocal(138, "ECHO");
  PRINTCOMMAND = catgetlocal(139, "PRINTCOMMAND");
  RIGHTMARGIN = catgetlocal(140, "RIGHTMARGIN");
  HIGHLIGHT = catgetlocal(141, "HIGHLIGHT");
  NOHIGHLIGHT = catgetlocal(142, "NOHIGHLIGHT");
  EIGHTBIT = catgetlocal(143, "EIGHTBIT");
  NOEIGHTBIT = catgetlocal(144, "NOEIGHTBIT");
  /*
   |	additions
   */
  mode_strings[7] = catgetlocal(145, "emacs key bindings   ");
  emacs_help_text[0] = help_text[0];
  emacs_help_text[1] =
      catgetlocal(146, "^a beginning of line    ^i tab                  ^r "
                       "restore word            ");
  emacs_help_text[2] =
      catgetlocal(147, "^b back 1 char          ^j undel char           ^t top "
                       "of text             ");
  emacs_help_text[3] =
      catgetlocal(148, "^c command              ^k delete line          ^u "
                       "bottom of text          ");
  emacs_help_text[4] =
      catgetlocal(149, "^d delete char          ^l undelete line        ^v "
                       "next page               ");
  emacs_help_text[5] =
      catgetlocal(150, "^e end of line          ^m newline              ^w "
                       "delete word             ");
  emacs_help_text[6] =
      catgetlocal(151, "^f forward 1 char       ^n next line            ^x "
                       "search                  ");
  emacs_help_text[7] =
      catgetlocal(152, "^g go back 1 page       ^o ascii char insert    ^y "
                       "search prompt           ");
  emacs_help_text[8] =
      catgetlocal(153, "^h backspace            ^p prev line            ^z "
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
      catgetlocal(154, "^[ (escape) menu ^y search prompt ^k delete line   ^p "
                       "prev li     ^g prev page");
  emacs_control_keys[1] =
      catgetlocal(155, "^o ascii code    ^x search        ^l undelete line ^n "
                       "next li     ^v next page");
  emacs_control_keys[2] =
      catgetlocal(156, "^u end of file   ^a begin of line ^w delete word   ^b "
                       "back 1 char ^z next word");
  emacs_control_keys[3] =
      catgetlocal(157, "^t top of text   ^e end of line   ^r restore word  ^f "
                       "forward char            ");
  emacs_control_keys[4] =
      catgetlocal(158, "^c command       ^d delete char   ^j undelete char     "
                       "         ESC-Enter: exit");
  EMACS_string = catgetlocal(159, "EMACS");
  NOEMACS_string = catgetlocal(160, "NOEMACS");
  usage4 = catgetlocal(161, "       +#   put cursor at line #\n");
  conf_dump_err_msg = catgetlocal(
      162, "unable to open .init.ee for writing, no configuration saved!");
  conf_dump_success_msg = catgetlocal(163, "ee configuration saved in file %s");
  modes_menu[10].item_string = catgetlocal(164, "save editor configuration");
  config_dump_menu[0].item_string = catgetlocal(165, "save ee configuration");
  config_dump_menu[1].item_string =
      catgetlocal(166, "save in current directory");
  config_dump_menu[2].item_string = catgetlocal(167, "save in home directory");
  conf_not_saved_msg = catgetlocal(168, "ee configuration not saved");
  ree_no_file_msg = catgetlocal(169, "must specify a file when invoking ree");
  menu_too_lrg_msg = catgetlocal(180, "menu too large for window");
  more_above_str = catgetlocal(181, "^^more^^");
  more_below_str = catgetlocal(182, "VVmoreVV");
  mode_strings[9] = catgetlocal(183, "16 bit characters    ");
  chinese_cmd = catgetlocal(184, "16BIT");
  nochinese_cmd = catgetlocal(185, "NO16BIT");

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
  commands[13] = Exit_string;
  commands[14] = QUIT_string;
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
  init_strings[21] = nullptr;

  /*
   |	allocate space for strings here for settings menu
   */

  for (counter = 1; counter < NUM_MODES_ITEMS; counter++) {
    modes_menu[counter].item_string = malloc(80);
  }

#ifndef NO_CATGETS
  catclose(catalog);
#endif /* NO_CATGETS */
}
