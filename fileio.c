#define _GNU_SOURCE
#include "fileio.h"
#include <pwd.h>
#ifdef HAS_TREESITTER
#include <tree_sitter/api.h>
const TSLanguage *tree_sitter_c(void);
#endif

/* Forward declarations of functions defined in ee.c */
int file_op_wrapper(int arg);
void print_buffer(void);
ssize_t strscpy(char *dest, const char *src, size_t count);

bool input_file;           /* indicate to read input file		*/
bool recv_file;            /* indicate reading a file		*/
int fildes;                /* file descriptor			*/
int get_fd;                /* file descriptor for reading a file	*/
int shell_fork;
int temp_stdin;           /* temporary storage for stdin		*/
int temp_stdout;          /* temp storage for stdout descriptor	*/
int temp_stderr;          /* temp storage for stderr descriptor	*/
int pipe_out[2];          /* pipe file desc for output		*/
int pipe_in[2];           /* pipe file descriptors for input	*/
bool out_pipe;            /* flag that info is piped out		*/
bool in_pipe;             /* flag that info is piped in		*/
char *in_file_name = nullptr; /* name of input file		*/
char *tmp_file;        /* temporary file name			*/
unsigned char
    in_string[MAX_IN_STRING]; /* buffer for reading a file		*/
FILE *temp_fp;    /* temporary file pointer		*/
FILE *bit_bucket; /* file pointer to /dev/null		*/
struct menu_entries file_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, READ_FILE},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, WRITE_FILE},
    {"", nullptr, nullptr, file_op_wrapper, nullptr, SAVE_FILE},
    {"", nullptr, nullptr, nullptr, print_buffer, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};
char *file_write_prompt_str;
char *file_read_prompt_str;
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
void calc_abs_line() {
  struct text const *tmpline = first_line;
  int x = 1;

  while ((tmpline != nullptr) && (tmpline != curr_line)) {
    x++;
    tmpline = tmpline->next_line;
  }
  absolute_lin = x;
}
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
#ifdef HAS_TREESITTER

#endif
