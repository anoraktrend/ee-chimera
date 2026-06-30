#ifndef EE_FILEIO_H
#define EE_FILEIO_H

#include "ee.h"

extern bool input_file;
extern bool recv_file;
extern int fildes;
extern int get_fd;
extern char *in_file_name;
extern char *tmp_file;
extern FILE *temp_fp;
extern FILE *bit_bucket;
extern unsigned char in_string[MAX_IN_STRING];
extern int shell_fork;
extern int temp_stdin;
extern int temp_stdout;
extern int temp_stderr;
extern int pipe_out[2];
extern int pipe_in[2];
extern bool out_pipe;
extern bool in_pipe;
extern char *file_write_prompt_str;
extern char *file_read_prompt_str;
extern char *file_is_dir_msg;
extern char *file_read_fin_msg;
extern char *reading_file_msg;
extern char *file_read_lines_msg;
extern char *file_not_saved_msg;
extern char *file_exists_prompt;
extern char *create_file_fail_msg;
extern char *writing_file_msg;
extern char *file_written_msg;
extern char *read_only_msg;
extern char *save_file_name_prompt;
extern char *changes_made_prompt;
extern char *yes_char;
extern char *new_file_msg;
extern char *cant_open_msg;
extern char *open_file_msg;
extern struct menu_entries file_menu[];

/* Variables defined in ee.c but needed by fileio.c functions */
extern int tmp_vert;
extern int tmp_horz;
extern struct text *tmp_line;
extern struct files *top_of_stack;
extern char *start_at_line;

int write_file(const char *file_name, bool warn_if_exists);
void check_fp(void);
void get_file(const char *file_name);
void sh_command(const char *string);
char *resolve_name(const char *name);
void finish(void);

#ifdef HAS_TREESITTER
void reparse(void);
#endif

#endif /* EE_FILEIO_H */
