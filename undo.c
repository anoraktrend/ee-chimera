#include "undo.h"
#include "ee.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void undo_entry_init(undo_entry *entry, undo_action_type action,
                            int line_number, int column, int length,
                            unsigned char *data);
static void undo_entry_cleanup(undo_entry *entry);
static void undo_buffer_add(undo_buffer *buffer, undo_entry *entry);
static void undo_free_entries(undo_buffer *buffer);
static struct text *undo_find_line(int line_number);
static undo_entry *undo_advance(undo_buffer *buffer);
static void undo_apply_splice(undo_entry *entry);
static void undo_apply_remove(undo_entry *entry);
static void undo_perform_move(undo_entry *entry);
static void undo_perform_replace(undo_entry *entry);

/* dispatch table indexed by undo_action_type */
typedef void (*undo_apply_fn)(undo_entry *entry);
static const undo_apply_fn undo_apply_table[] = {
    [UNDO_INSERT] = undo_apply_splice, [UNDO_DELETE] = undo_apply_remove,
    [UNDO_MOVE] = undo_perform_move,   [UNDO_REPLACE] = undo_perform_replace,
    [UNDO_CUT] = undo_apply_remove,    [UNDO_PASTE] = undo_apply_splice,
};

void undo_init(undo_buffer *buffer) {
  if (!buffer)
    return;

  memset(buffer, 0, sizeof(undo_buffer));
  buffer->capacity = MAX_UNDO_STEPS;
  buffer->head = nullptr;
  buffer->tail = nullptr;
  buffer->current = nullptr;
  buffer->size = 0;
  buffer->position = 0;
  buffer->in_transaction = false;
}

static void undo_free_entries(undo_buffer *buffer) {
  undo_entry *entry = buffer->head;
  while (entry) {
    undo_entry *next = entry->next;
    undo_entry_cleanup(entry);
    free(entry);
    entry = next;
  }
}

void undo_cleanup(undo_buffer *buffer) {
  if (!buffer)
    return;

  undo_free_entries(buffer);
  memset(buffer, 0, sizeof(undo_buffer));
}

[[nodiscard]] bool undo_can_undo(undo_buffer *buffer) {
  if (!buffer)
    return false;
  return buffer->current != nullptr && buffer->position > 0;
}

[[nodiscard]] bool undo_can_redo(undo_buffer *buffer) {
  if (!buffer)
    return false;
  return buffer->current != nullptr && buffer->position < buffer->size - 1;
}

/* advance the current pointer one entry forward, returning the entry passed */
static undo_entry *undo_advance(undo_buffer *buffer) {
  undo_entry *entry = buffer->current;
  buffer->current = entry->next;
  return entry;
}

void undo_perform(undo_buffer *buffer) {
  if (!buffer || !undo_can_undo(buffer))
    return;

  undo_entry *entry = undo_advance(buffer);
  buffer->position--;

  undo_apply_table[entry->action](entry);

  undo_entry_cleanup(entry);
  free(entry);
}

void undo_redo(undo_buffer *buffer) {
  if (!buffer || !undo_can_redo(buffer))
    return;

  undo_entry *entry = undo_advance(buffer);
  buffer->position++;

  undo_apply_table[entry->action](entry);
}

void undo_record(undo_buffer *buffer, undo_action_type action, int line_number,
                 int column, int length, unsigned char *data) {
  if (!buffer)
    return;

  undo_entry *entry = malloc(sizeof(undo_entry));
  if (!entry)
    return;

  undo_entry_init(entry, action, line_number, column, length, data);
  undo_buffer_add(buffer, entry);
}

void undo_record_move(undo_buffer *buffer, int from_line, int from_col,
                      int to_line, int to_col) {
  if (!buffer)
    return;

  undo_entry *entry = malloc(sizeof(undo_entry));
  if (!entry)
    return;

  undo_entry_init(entry, UNDO_MOVE, from_line, from_col, 0, nullptr);
  entry->line_after = malloc(sizeof(struct text));
  if (entry->line_after) {
    entry->line_after->line_number = to_line;
    entry->line_after->line = nullptr;
  }
  undo_buffer_add(buffer, entry);
}

void undo_record_replace(undo_buffer *buffer, int line_number, int column,
                         int old_length, unsigned char *old_data) {
  if (!buffer)
    return;

  undo_entry *entry = malloc(sizeof(undo_entry));
  if (!entry)
    return;

  /* entries own their data: keep an internal copy of the pre-replace text so
   * undo_entry_cleanup can safely free it and undo_perform_replace can
   * restore it; the entry struct has a single data slot */
  undo_entry_init(entry, UNDO_REPLACE, line_number, column, old_length,
                  old_data);
  undo_buffer_add(buffer, entry);
}

void undo_clear(undo_buffer *buffer) {
  if (!buffer)
    return;

  undo_free_entries(buffer);
  buffer->head = nullptr;
  buffer->tail = nullptr;
  buffer->current = nullptr;
  buffer->size = 0;
  buffer->position = 0;
}

void undo_save_state(undo_buffer *buffer) {
  if (!buffer)
    return;

  if (buffer->in_transaction) {
    return;
  }

  undo_entry *entry = malloc(sizeof(undo_entry));
  if (!entry)
    return;

  undo_entry_init(entry, UNDO_INSERT, curr_line ? curr_line->line_number : 0,
                  position, 0, nullptr);
  undo_buffer_add(buffer, entry);
}

static void undo_entry_init(undo_entry *entry, undo_action_type action,
                            int line_number, int column, int length,
                            unsigned char *data) {
  if (!entry)
    return;

  entry->action = action;
  entry->timestamp = time(nullptr);
  entry->line_number = line_number;
  entry->column = column;
  entry->length = length;
  entry->data = nullptr;
  entry->line_before = nullptr;
  entry->line_after = nullptr;

  if (data && length > 0) {
    entry->data = malloc(length + 1);
    if (!entry->data) {
      return; // Early return if malloc fails
    }
    memcpy(entry->data, data, length);
    entry->data[length] = '\0';
  }
}

static void undo_entry_cleanup(undo_entry *entry) {
  if (!entry)
    return;

  if (entry->data) {
    free(entry->data);
    entry->data = nullptr;
  }

  if (entry->line_before) {
    free(entry->line_before);
    entry->line_before = nullptr;
  }

  if (entry->line_after) {
    free(entry->line_after);
    entry->line_after = nullptr;
  }
}

static void undo_buffer_add(undo_buffer *buffer, undo_entry *entry) {
  if (!buffer || !entry)
    return;

  if (buffer->in_transaction) {
    undo_entry_cleanup(entry);
    free(entry);
    return;
  }

  if (buffer->size >= buffer->capacity) {
    undo_entry *old = buffer->head;
    buffer->head = old->next;
    if (!buffer->head) {
      buffer->tail = nullptr;
    }
    undo_entry_cleanup(old);
    free(old);
    buffer->size--;
    if (buffer->position > 0) {
      buffer->position--;
    }
  }

  if (!buffer->head) {
    buffer->head = entry;
    buffer->tail = entry;
    entry->next = nullptr;
  } else {
    buffer->tail->next = entry;
    buffer->tail = entry;
    entry->next = nullptr;
  }

  buffer->size++;
  buffer->current = entry;
  buffer->position = buffer->size - 1;
}

static struct text *undo_find_line(int line_number) {
  struct text *line = curr_line;
  for (int i = 0; i < line_number && line; i++) {
    line = line->next_line;
  }
  return line;
}

/* shared by UNDO_INSERT and UNDO_PASTE: splice entry->data into the line */
static void undo_apply_splice(undo_entry *entry) {
  if (!entry || entry->line_number < 0 || entry->line_number >= last_line)
    return;

  struct text *line = undo_find_line(entry->line_number);
  if (!line || (!entry->data && entry->length > 0))
    return;

  int new_len;
  if (ckd_add(&new_len, line->line_length, entry->length))
    return;
  unsigned char *new_line = malloc(new_len + 1);
  if (!new_line)
    return;

  memcpy(new_line, line->line, entry->column);
  memcpy(new_line + entry->column, entry->data, entry->length);
  memcpy(new_line + entry->column + entry->length, line->line + entry->column,
         line->line_length - entry->column);
  pos = entry->column + entry->length + (line->line_length - entry->column);

  new_line[pos] = '\0';

  free(line->line);
  line->line = new_line;
  line->line_length = pos;
}

/* shared by UNDO_DELETE and UNDO_CUT: excise entry->length bytes at column */
static void undo_apply_remove(undo_entry *entry) {
  if (!entry || entry->line_number < 0 || entry->line_number >= last_line)
    return;

  struct text *line = undo_find_line(entry->line_number);
  if (!line || !entry->data)
    return;

  int new_length = line->line_length - entry->length;
  if (new_length < 0)
    new_length = 0;

  unsigned char *new_line = malloc(new_length + 1);
  if (!new_line)
    return;

  memcpy(new_line, line->line, entry->column);
  memcpy(new_line + entry->column, line->line + entry->column + entry->length,
         line->line_length - entry->column - entry->length);
  pos = entry->column + (line->line_length - entry->column - entry->length);

  new_line[pos] = '\0';

  free(line->line);
  line->line = new_line;
  line->line_length = pos;
}

static void undo_perform_move(undo_entry *entry) {
  if (!entry || !entry->line_after)
    return;

  if (entry->line_number < 0 || entry->line_number >= last_line)
    return;

  struct text *line = undo_find_line(entry->line_number);
  if (!line)
    return;

  line->line_number = entry->line_after->line_number;
}

static void undo_perform_replace(undo_entry *entry) {
  if (!entry || entry->line_number < 0 || entry->line_number >= last_line)
    return;

  struct text *line = undo_find_line(entry->line_number);
  if (!line)
    return;

  int new_length = entry->length;
  unsigned char *new_line = malloc(new_length + 1);
  if (!new_line)
    return;

  memcpy(new_line, entry->data, new_length + 1);

  free(line->line);
  line->line = new_line;
  line->line_length = new_length;
}
