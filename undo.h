#ifndef UNDO_H
#define UNDO_H

#include <stdbool.h>

struct text;

#define MAX_UNDO_STEPS 10000
#define UNDO_BUFFER_SIZE 4096

typedef enum {
    UNDO_INSERT,
    UNDO_DELETE,
    UNDO_MOVE,
    UNDO_REPLACE,
    UNDO_CUT,
    UNDO_PASTE
} undo_action_type;

typedef struct undo_entry {
    undo_action_type action;
    int timestamp;
    int line_number;
    int column;
    int length;
    unsigned char *data;
    struct text *line_before;
    struct text *line_after;
    struct undo_entry *next;
} undo_entry;

typedef struct {
    undo_entry *head;
    undo_entry *tail;
    undo_entry *current;
    int size;
    int capacity;
    int position;
    bool in_transaction;
} undo_buffer;

void undo_init(undo_buffer *buffer);
void undo_cleanup(undo_buffer *buffer);
void undo_begin_transaction(undo_buffer *buffer);
void undo_end_transaction(undo_buffer *buffer);
bool undo_can_undo(undo_buffer *buffer);
bool undo_can_redo(undo_buffer *buffer);
void undo_perform(undo_buffer *buffer);
void undo_redo(undo_buffer *buffer);
void undo_record_insert(undo_buffer *buffer, int line_number, int column, int length, unsigned char *data);
void undo_record_delete(undo_buffer *buffer, int line_number, int column, int length, unsigned char *data);
void undo_record_move(undo_buffer *buffer, int from_line, int from_col, int to_line, int to_col);
void undo_record_replace(undo_buffer *buffer, int line_number, int column, int old_length, int new_length, unsigned char *old_data, unsigned char *new_data);
void undo_record_cut(undo_buffer *buffer, int line_number, int column, int length, unsigned char *data);
void undo_record_paste(undo_buffer *buffer, int line_number, int column, int length, unsigned char *data);
void undo_clear(undo_buffer *buffer);
void undo_save_state(undo_buffer *buffer);

#endif /* UNDO_H */