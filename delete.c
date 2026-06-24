#include "delete.h"
#include "ee.h"
#include "undo.h"
struct text *dlt_line;   /* structure for info on deleted line	*/
struct text *mark_line = nullptr;
int mark_position = 0;
char *clipboard_buf = nullptr;
int d_wrd_len;    /* length of deleted word		*/
unsigned char *d_char; /* deleted character			*/
unsigned char *d_word; /* deleted word				*/
unsigned char *d_line; /* deleted line				*/
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
      undo_record_delete(&undo_state, curr_line->line_number, position, del_width, d_char);
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
  if (info_window) paint_info_win();
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
      if (info_window) paint_info_win();
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
    
    if (undo_enabled) {
      undo_record_cut(&undo_state, start_line->line_number, start_pos, del_len, clipboard_buf);
    }
  }
  mark_line = nullptr;
  if (info_window) paint_info_win();
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
    undo_record_paste(&undo_state, curr_line->line_number, position, paste_len, (unsigned char *)clipboard_buf);
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

  /* Branchless validation and swap logic */
  bool valid = false;
  struct text *chk = first_line;
  while (chk) {
    valid |= (chk == mark_line);
    chk = chk->next_line;
  }
  if (info_window) paint_info_win();
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
  if (info_window) paint_info_win();
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
  
  if (undo_enabled) {
    undo_record_delete(&undo_state, curr_line->line_number, position, d_wrd_len, d_word);
  }
}
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
    undo_record_delete(&undo_state, curr_line->line_number, position, copy_len, d_line);
  }
}
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
