#include "delete.h"
#include "ee.h"
#include "undo.h"
struct text *dlt_line; /* structure for info on deleted line	*/
struct text *mark_line = nullptr;
int mark_position = 0;
char *clipboard_buf = nullptr;
int d_wrd_len;         /* length of deleted word		*/
unsigned char *d_char; /* deleted character			*/
unsigned char *d_word; /* deleted word				*/
unsigned char *d_line; /* deleted line				*/

/* total text length of the region from start through end */
static int region_size(struct text *start_line, struct text *end_line) {
  int size = end_line->line_length;
  for (struct text *tl = start_line; tl && tl != end_line; tl = tl->next_line) {
    size += tl->line_length;
  }
  return size;
}

/* copy the region's text into cb_ptr, returns pointer past last byte written */
static char *copy_region_text(char *cb_ptr, struct text *start_line,
                              int start_pos, struct text *end_line,
                              int end_pos) {
  if (start_line == end_line) {
    memcpy(cb_ptr, start_line->line + start_pos - 1, end_pos - start_pos);
    return cb_ptr + (end_pos - start_pos);
  }
  memcpy(cb_ptr, start_line->line + start_pos - 1,
         start_line->line_length - start_pos);
  cb_ptr += (start_line->line_length - start_pos);
  *cb_ptr++ = '\n';
  for (struct text *tl = start_line->next_line; tl && tl != end_line;
       tl = tl->next_line) {
    memcpy(cb_ptr, tl->line, tl->line_length - 1);
    cb_ptr += (tl->line_length - 1);
    *cb_ptr++ = '\n';
  }
  memcpy(cb_ptr, end_line->line, end_pos - 1);
  return cb_ptr + (end_pos - 1);
}

/* walk the cursor to the end of the region if it isn't already there */
static void goto_region_end(struct text *end_line, int end_pos) {
  while (curr_line != end_line || position != end_pos) {
    if (curr_line->line_number < end_line->line_number ||
        (curr_line == end_line && position < end_pos)) {
      right(1);
    } else {
      left(1);
    }
  }
}

/* backspace-delete del_len characters ending at the current cursor */
static void delete_region_chars(int del_len) {
  /* The cursor is already at the end of the region (goto_region_end was
   * called by the caller).  Walk backwards del_len positions and memmove
   * the tail forward.  We operate line-by-line to keep draw_line correct;
   * for the common single-line case this is one memmove. */
  for (int i = 0; i < del_len; i++) {
    in = 8; /* ASCII backspace */
    delete_char_at_cursor(1);
  }
}


void update_line_numbers(struct text *line, int delta) {
  struct text *curr = line;
  while (curr != nullptr) {
    curr->line_number += delta;
    curr = curr->next_line;
  }
}
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

    if (undo_enabled) {
      undo_record(&undo_state, UNDO_DELETE, curr_line->line_number, position,
                  del_width, d_char);
    }
  } else if (curr_line->prev_line != nullptr) {
    text_changes = true;
    left(disp); /* go to previous line	*/
    temp_buff = curr_line->next_line;
    if (mark_line == temp_buff) {
      mark_line = nullptr;
      mark_position = 0;
    }
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
      ee_wdeleteln(text_win);
    }
    int lines_to_find = last_line - temp_vert;
    temp_buff = find_next_recursive(temp_buff, lines_to_find, &temp_vert);

    if ((temp_vert == last_line) && (temp_buff != nullptr)) {
      tp = temp_buff->line;
      ee_wmove(text_win, last_line, 0);
      ee_wclrtobot(text_win);
      draw_line(last_line, 0, temp_buff, 1);
      ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
    }
  }
  draw_line(scr_vert, scr_horz, curr_line, position);
  formatted = false;
}
static void free_text_lines(struct text *line) {
  struct text *curr = line;
  struct text *next;
  while (curr != nullptr) {
    next = curr->next_line;
    if (curr->line != nullptr)
      free(curr->line);
    free(curr);
    curr = next;
  }
}
void delete_text() {
  mark_line = nullptr;
  mark_position = 0;
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

void set_mark() {
  if (mark_line != nullptr) {
    mark_line = nullptr;
    mark_position = 0;
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Mark cleared.");
  } else {
    mark_line = curr_line;
    mark_position = position;
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Mark set.");
  }
  ee_wrefresh(com_win);
  clear_com_win = true;
  if (info_window)
    paint_info_win();
}
void copy_region(bool cut) {
  if (!mark_line) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "No mark set.");
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
  int est_size = region_size(start_line, end_line);
  if (clipboard_buf)
    free(clipboard_buf);
  clipboard_buf = malloc(est_size + 1);
  /* Copy into clipboard buffer */
  char *cb_ptr =
      copy_region_text(clipboard_buf, start_line, start_pos, end_line, end_pos);
  *cb_ptr = '\0';
  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, cut ? "Region cut." : "Region copied.");
  ee_wrefresh(com_win);
  clear_com_win = true;
  /* If cutting, simulate backspacing to delete the region */
  if (cut) {
    /* Move cursor to end of the region if it isn't already */
    goto_region_end(end_line, end_pos);
    int del_len = cb_ptr - clipboard_buf;
    delete_region_chars(del_len);

    if (undo_enabled) {
      undo_record(&undo_state, UNDO_CUT, start_line->line_number, start_pos,
                   del_len, (unsigned char *)clipboard_buf);
    }
  }
  mark_line = nullptr;
  if (info_window)
    paint_info_win();
}
void paste_region() {
  if (!clipboard_buf) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Clipboard empty.");
    ee_wrefresh(com_win);
    clear_com_win = true;
    return;
  }

  int paste_len = strlen(clipboard_buf);

  char *ptr = clipboard_buf;
  while (*ptr) {
    if (*ptr == '\n') {
      insert_line(1);
    } else {
      insert(*ptr);
    }
    ptr++;
  }

  if (undo_enabled && paste_len > 0) {
    undo_record(&undo_state, UNDO_PASTE, curr_line->line_number, position,
                paste_len, (unsigned char *)clipboard_buf);
  }
}
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

  bool swap = (mark_line->line_number > curr_line->line_number) ||
              ((mark_line->line_number == curr_line->line_number) &&
               (mark_position > position));

  struct text *start_line = swap ? curr_line : mark_line;
  int start_pos = swap ? position : mark_position;
  struct text *end_line = swap ? mark_line : curr_line;
  int end_pos = swap ? mark_position : position;


  /* Calculate new region size */
  int est_size = region_size(start_line, end_line);

  /* Reallocate existing clipboard to hold the appended data */
  int current_cb_len = strlen(clipboard_buf);
  char *new_cb = realloc(clipboard_buf, current_cb_len + est_size + 1);
  if (!new_cb)
    return;
  clipboard_buf = new_cb;

  /* Copy into clipboard buffer (reusing optimized copy logic) */
  char *cb_ptr = copy_region_text(clipboard_buf + current_cb_len, start_line,
                                  start_pos, end_line, end_pos);
  *cb_ptr = '\0';

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, cut ? "Region cut & appended." : "Region appended.");
  ee_wrefresh(com_win);
  clear_com_win = true;

  /* Simulate backspacing for cuts */
  if (cut) {
    goto_region_end(end_line, end_pos);
    int del_len = cb_ptr - (clipboard_buf + current_cb_len);
    delete_region_chars(del_len);
  }
  mark_line = nullptr;
  if (info_window)
    paint_info_win();
}
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
void del_word() {
  unsigned char tmp_char[3];

  if (d_word != nullptr) {
    free(d_word);
  }
  d_word = malloc(curr_line->line_length);

  /* Save d_char before we clobber it via delete mechanics. */
  tmp_char[0] = d_char[0];
  tmp_char[1] = d_char[1];
  tmp_char[2] = d_char[2];

  /* Find end of the non-space run starting at point. */
  int remaining = curr_line->line_length - position;
  unsigned char *end = point;
  if (remaining > 0) {
    size_t word_span = strcspn((char *)end, " \t");
    if ((int)word_span > remaining) word_span = (size_t)remaining;
    end += word_span;
    remaining -= (int)word_span;
  }
  /* span of trailing whitespace */
  if (remaining > 0) {
    size_t ws_span = strspn((char *)end, " \t");
    if ((int)ws_span > remaining) ws_span = (size_t)remaining;
    end += ws_span;
  }

  int difference = (int)(end - point);

  /* Save deleted bytes into d_word. */
  memcpy(d_word, point, difference);
  d_word[difference] = '\0';
  d_wrd_len = difference;

  /* Slide the tail left over the deleted span (includes the '\0'). */
  int tail_len = curr_line->line_length - position - difference + 1;
  if (tail_len > 0)
    memmove(point, end, tail_len);

  curr_line->line_length -= difference;

  draw_line(scr_vert, scr_horz, curr_line, position);
  d_char[0] = tmp_char[0];
  d_char[1] = tmp_char[1];
  d_char[2] = tmp_char[2];
  text_changes = true;
  formatted = false;

  if (undo_enabled) {
    undo_record(&undo_state, UNDO_DELETE, curr_line->line_number, position,
                d_wrd_len, d_word);
  }
}

void undel_word() {
  /*
   |  resize line to handle undeleted word
   */
  if ((curr_line->max_length - (curr_line->line_length + d_wrd_len)) < 5) {
    point = resiz_line(d_wrd_len, curr_line, position);
  }
  int tmp_size;
  if (ckd_add(&tmp_size, curr_line->line_length, d_wrd_len))
    return;

  /*
   |  Build the new line contents in a scratch buffer:
   |    [line[0..position-1]] [d_word[0..d_wrd_len-1]] [line[position..end]]
   |  then copy it back over the original.  Uses three memcpy calls instead
   |  of three manual byte loops.
   */
  unsigned char *tmp_space = malloc(tmp_size + 1);
  if (!tmp_space)
    return;

  int tail_len = curr_line->line_length - position;

  /* prefix: bytes before cursor */
  memcpy(tmp_space, curr_line->line, position - 1);
  /* the restored word */
  memcpy(tmp_space + position - 1, d_word, d_wrd_len);
  /* tail: bytes from cursor to end (including '\0') */
  if (tail_len > 0)
    memcpy(tmp_space + position - 1 + d_wrd_len, point, tail_len);
  tmp_space[position - 1 + d_wrd_len + tail_len] = '\0';

  int new_len = curr_line->line_length + d_wrd_len;
  memcpy(curr_line->line, tmp_space, new_len + 1);
  curr_line->line_length = new_len;

  free(tmp_space);
  draw_line(scr_vert, scr_horz, curr_line, position);
}

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

  if (undo_enabled) {
    undo_record(&undo_state, UNDO_DELETE, curr_line->line_number, position,
                copy_len, d_line);
  }
}
void undel_line() {
  if (dlt_line->line_length == 0) {
    return;
  }

  insert_line(1);
  left(1);
  point = resiz_line(dlt_line->line_length, curr_line, position);
  size_t copy_len = dlt_line->line_length - 1;
  memcpy(point, d_line, copy_len);
  point[copy_len] = '\0';
  curr_line->line_length += (int)copy_len;
  draw_line(scr_vert, scr_horz, curr_line, position);
}

