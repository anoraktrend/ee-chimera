#include "format.h"
#include "ee.h"
#include "delete.h"
int right_margin = 0;    /* the right margin 			*/
bool observ_margins = true; /* flag for whether margins are observed */
static int first_word_len(struct text *line) {
  char *ptr = (char *)line->line;
  ptr += strspn(ptr, " \t");  /* Skip leading whitespace */
  return strcspn(ptr, " \t"); /* Count length of the word */
}
bool Blank_Line(struct text *test_line) {
  if (test_line == nullptr) {
    return true;
  }

  unsigned char c = *test_line->line;
  bool is_special = (c == '.') | (c == '>');
  size_t skip = strspn((char *)test_line->line, " \t");
  bool is_all_whitespace = (skip + 1) >= test_line->line_length;

  return is_special | is_all_whitespace;
}
void Format() {
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
  unsigned char temp_d_char[8];

  memcpy(temp_d_char, d_char, 8);

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

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

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

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

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

  ee_wmove(com_win, 0, 0);
  ee_wclrtoeol(com_win);
  ee_wprintw(com_win, "%s", formatting_msg);
  ee_wrefresh(com_win);

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
  memcpy(d_char, temp_d_char, 8);
  auto_format = (tmp_af != 0);

  midscreen(scr_vert, point);
  ee_werase(com_win);
  ee_wrefresh(com_win);
}
void Auto_Format() {
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
  unsigned char temp_d_char[8];
  unsigned char *tmp_d_line;

  memcpy(temp_d_char, d_char, 8);

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
  memcpy(d_char, temp_d_char, 8);
  auto_format = true;
  dlt_line->line_length = tmp_d_line_length;
  d_line = tmp_d_line;

  formatted = true;
  midscreen(scr_vert, point);
}
