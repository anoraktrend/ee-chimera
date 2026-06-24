#define _GNU_SOURCE
#include "lsp.h"
#include "ee.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAS_TREESITTER
#include <tree_sitter/api.h>
const TSLanguage *tree_sitter_c(void);
#endif
TSParser *ts_parser = nullptr;
TSTree *ts_tree = nullptr;
int lsp_to_child[2];
int lsp_from_child[2];
pid_t lsp_pid = -1;
struct diagnostic *diagnostics_list = nullptr;
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
void reparse() {
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
