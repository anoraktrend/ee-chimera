#ifndef EE_LSP_H
#define EE_LSP_H

#include "ee.h"

#ifdef HAS_LSP
extern int lsp_to_child[2];
extern int lsp_from_child[2];
extern pid_t lsp_pid;
extern struct diagnostic *diagnostics_list;

void lsp_send(const char *msg);
void lsp_start(void);
void lsp_open_file(const char *filename);
void lsp_poll(void);
void lsp_change_file(const char *filename);
#endif

#ifdef HAS_TREESITTER
extern TSParser *ts_parser;
extern TSTree *ts_tree;
const char *ts_read_buffer(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read);
void reparse(void);
#endif

#endif
