#include "search.h"
#include "ee.h"
struct text *srch_line;    /* temporary pointer for search routine */
bool case_sen;             /* case sensitive search flag		*/
unsigned char *srch_str;   /* pointer for search string		*/
unsigned char *u_srch_str; /* pointer to non-case sensitive search	*/
unsigned char *srch_1;     /* pointer to start of suspect string	*/
unsigned char *srch_2;     /* pointer to next character of string	*/
unsigned char *srch_3;

/* create an uppercase duplicate of src */
static unsigned char *dup_upper(unsigned char *src) {
  unsigned char *dst = malloc(strlen((char *)src) + 1);
  unsigned char *d = dst;
  for (unsigned char *s = src; *s != '\0'; s++) {
    *d++ = toupper(*s);
  }
  *d = '\0';
  return dst;
}
[[nodiscard]] bool compare(const char *string1, const char *string2,
                           bool sensitive) {
  const char *strng1 = string1;
  const char *strng2 = string2;
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
[[nodiscard]] int search(int display_message) {
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
    if (lines_moved < 30) {
      if (lines_moved != 0) {
        move_rel('d', lines_moved);
      }
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
void search_prompt() {
  if (srch_str != nullptr) {
    free(srch_str);
  }
  if ((u_srch_str != nullptr) && (*u_srch_str != '\0')) {
    free(u_srch_str);
  }
  srch_str = (unsigned char *)get_string(search_prompt_str, 0);
  gold = false;
  u_srch_str = dup_upper(srch_str);
  srch_1 = u_srch_str + strlen((char *)u_srch_str);
  (void)search(1);
}
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
  u_srch_str = dup_upper(srch_str);
  srch_1 = u_srch_str + strlen((char *)u_srch_str);
  int found = search(1);
  if (found) {
    int len = strlen((char *)search_term);
    for (int i = 0; i < len; i++) {
      in = 8;
      delete_char_at_cursor(1);
    }
    if (replace_term) {
      size_t rlen = strlen((char *)replace_term);
      for (size_t i = 0; i < rlen; i++) {
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
[[nodiscard]] int search_reverse(int display_message) {
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
