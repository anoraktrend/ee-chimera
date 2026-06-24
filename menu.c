#include "menu.h"
#include "ee.h"
#include "fileio.h"
#include "theme.h"
bool nohighlight = false; /* turns off highlighting		*/
struct menu_entries modes_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, 0},  /* title		*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 1. tabs to spaces	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 2. case sensitive search*/
    {"", nullptr, nullptr, nullptr, nullptr,
     -1},                                         /* 3. margins observed	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 4. auto-paragraph	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 5. eightbit characters*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 6. info window	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 7. emacs key bindings*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 8. vi key bindings	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 9. right margin	*/
    {"", nullptr, nullptr, nullptr, nullptr, -1}, /* 10. chinese text	*/
    {"", nullptr, nullptr, nullptr, dump_ee_conf,
     -1}, /* 11. save editor config */
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}
    /* terminator		*/
};
char *mode_strings[12];
struct menu_entries config_dump_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, 0},
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};
struct menu_entries leave_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, finish, -1},
    {"", nullptr, nullptr, quit_wrapper, nullptr, 1},
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};
struct menu_entries misc_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
#ifdef HAS_AUTOFORMAT
    {"", nullptr, nullptr, nullptr, Format, -1},
#endif
    {"", nullptr, nullptr, nullptr, shell_op, -1},
#ifdef HAS_MENU
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
     (struct menu_entries *)spell_menu, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, theme_select_op, -1},
#endif
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};
struct menu_entries main_menu[] = {
    {"", nullptr, nullptr, nullptr, nullptr, -1},
    {"", nullptr, nullptr, nullptr, leave_op, -1},
#ifdef HAS_HELP
    {"", nullptr, nullptr, nullptr, help, -1},
#endif
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)file_menu, nullptr, nullptr, -1},
#endif
    {"", nullptr, nullptr, nullptr, redraw, -1},
    {"", nullptr, nullptr, nullptr, modes_op, -1},
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)search_menu, nullptr, nullptr, -1},
#endif
    {"", (int (*)(struct menu_entries *))menu_op_wrapper,
#ifdef HAS_MENU
     (struct menu_entries *)misc_menu, nullptr, nullptr, -1},
#endif
    {nullptr, nullptr, nullptr, nullptr, nullptr, -1}};
int menu_op(struct menu_entries menu_list[]) {
  WINDOW *temp_win;
  int max_width;
  int max_height;
  int x_off;
  int y_off;
  int counter;
  int length;
  int input;
  int temp;
  int list_size;
  int top_offset;    /* offset from top where menu items start */
  int vert_size;     /* vertical size for menu list item display */
  int off_start = 1; /* offset from start of menu items to start display */

  /*
   |      determine number and width of menu items
   */

  list_size = 1;
  while (menu_list[list_size + 1].item_string != nullptr) {
    list_size++;
  }
  max_width = 0;
  for (counter = 0; counter <= list_size; counter++) {
    if ((length = strlen(menu_list[counter].item_string)) > max_width) {
      max_width = length;
    }
  }
  max_width += 3;
  max_width = max(max_width, (int)strlen(menu_cancel_msg));
  max_width = max(
      max_width, max((int)strlen(more_above_str), (int)strlen(more_below_str)));
  max_width += 6;

  /*
   |      make sure that window is large enough to handle menu
   |      if not, print error message and return to calling function
   */

  if (max_width > COLS) {
    ee_wmove(com_win, 0, 0);
    ee_werase(com_win);
    ee_wprintw(com_win, "%s", menu_too_lrg_msg);
    ee_wrefresh(com_win);
    clear_com_win = true;
    return 0;
  }

  top_offset = 0;

  if (list_size > LINES) {
    max_height = LINES;
    if (max_height > 11) {
      vert_size = max_height - 8;
    } else {
      vert_size = max_height;
    }
  } else {
    vert_size = list_size;
    max_height = list_size;
  }

  if (LINES >= (vert_size + 8)) {
    if (menu_list[0].value != MENU_WARN) {
      max_height = vert_size + 8;
    } else {
      max_height = vert_size + 7;
    }
    top_offset = 4;
  }
  x_off = (COLS - max_width) / 2;
  y_off = (LINES - max_height - 1) / 2;
  temp_win = profiling_mode ? nullptr : newwin(max_height, max_width, y_off, x_off);
  ee_keypad(temp_win, true);

  paint_menu(menu_list, max_width, max_height, list_size, top_offset, temp_win,
             off_start, vert_size);

  counter = 1;

  do {
    if (off_start > 2) {
      ee_wmove(temp_win, (1 + counter + top_offset - off_start), 3);
    } else {
      ee_wmove(temp_win, (counter + top_offset - off_start), 3);
    }

    ee_wrefresh(temp_win);
    in = wgetch(temp_win);
    input = in;
    if (input == -1) {
      edit_abort(0);
    }

    if ((isascii(input) != 0) && (isalnum(input) != 0)) {
      if (isalpha(input) != 0) {
        temp = 1 + tolower(input) - 'a';
      } else if (isdigit(input) != 0) {
        temp = (2 + 'z' - 'a') + (input - '0');
      }

      if (temp <= list_size) {
        input = '\n';
        counter = temp;
      }
    } else {
      switch (input) {
      case ' ':    /* space	*/
      case '\004': /* ^d, down	*/
      case KEY_RIGHT:
      case KEY_DOWN:
        counter++;
        if (counter > list_size) {
          counter = 1;
        }
        break;
      case '\010': /* ^h, backspace*/
      case '\025': /* ^u, up	*/
      case 127:    /* ^?, delete	*/
      case KEY_BACKSPACE:
      case KEY_LEFT:
      case KEY_UP:
        counter--;
        if (counter == 0) {
          counter = list_size;
        }
        break;
      case '\033': /* escape key	*/
        if (menu_list[0].value != MENU_WARN) {
          counter = 0;
        }
        break;
      case '\014': /* ^l       	*/
      case '\022': /* ^r, redraw	*/
        paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                   temp_win, off_start, vert_size);
        break;
      default:
        break;
      }
    }

    if (((list_size - off_start) >= (vert_size - 1)) &&
        (counter > (off_start + vert_size - 3)) && (off_start > 1)) {
      if (counter == list_size) {
        off_start = (list_size - vert_size) + 2;
      } else {
        off_start++;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    } else if ((list_size != vert_size) &&
               (counter > (off_start + vert_size - 2))) {
      if (counter == list_size) {
        off_start = 2 + (list_size - vert_size);
      } else if (off_start == 1) {
        off_start = 3;
      } else {
        off_start++;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    } else if (counter < off_start) {
      if (counter <= 2) {
        off_start = 1;
      } else {
        off_start = counter;
      }

      paint_menu(menu_list, max_width, max_height, list_size, top_offset,
                 temp_win, off_start, vert_size);
    }
  } while ((input != '\r') && (input != '\n') && (counter != 0));

  ee_werase(temp_win);
  ee_wrefresh(temp_win);
  delwin(temp_win);

  if (counter > 0 && ((menu_list[counter].procedure != nullptr) ||
      (menu_list[counter].procedure2 != nullptr) ||
      (menu_list[counter].procedure3 != nullptr))) {
    if (menu_list[counter].value != -1) {
      (*menu_list[counter].procedure2)(menu_list[counter].value);
    } else if (menu_list[counter].ptr_menu != nullptr) {
      (*menu_list[counter].procedure)(menu_list[counter].ptr_menu);
    } else {
      (*menu_list[counter].procedure3)();
    }
  }

  if (info_window) {
    paint_info_win();
  }
  redraw();

  return counter;
}
void paint_menu(struct menu_entries menu_list[], int max_width,
                       int max_height, int list_size, int top_offset,
                       WINDOW *menu_win, int off_start, int vert_size) {
  int counter;
  int temp_int;

  ee_werase(menu_win);

  /*
   |	output top and bottom portions of menu box only if window
   |	large enough
   */

  if (max_height > vert_size) {
    ee_wmove(menu_win, 1, 1);
    if (!nohighlight) {
      wstandout(menu_win);
    }
    ee_waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      ee_waddch(menu_win, '-');
    }
    ee_waddch(menu_win, '+');

    ee_wmove(menu_win, (max_height - 2), 1);
    ee_waddch(menu_win, '+');
    for (counter = 0; counter < (max_width - 4); counter++) {
      ee_waddch(menu_win, '-');
    }
    ee_waddch(menu_win, '+');
    wstandend(menu_win);
    ee_wmove(menu_win, 2, 3);
    ee_waddstr(menu_win, menu_list[0].item_string);
    ee_wmove(menu_win, (max_height - 3), 3);
    if (menu_list[0].value != MENU_WARN) {
      ee_waddstr(menu_win, menu_cancel_msg);
    }
  }
  if (!nohighlight) {
    wstandout(menu_win);
  }

  for (counter = 0; counter < (vert_size + top_offset); counter++) {
    if (top_offset == 4) {
      temp_int = counter + 2;
    } else {
      {
        temp_int = counter;
      }
    }

    ee_wmove(menu_win, temp_int, 1);
    ee_waddch(menu_win, '|');
    ee_wmove(menu_win, temp_int, (max_width - 2));
    ee_waddch(menu_win, '|');
  }
  wstandend(menu_win);

  if (list_size > vert_size) {
    for (counter = off_start; counter < (off_start + vert_size); counter++) {
      ee_wmove(menu_win, (top_offset + counter - off_start), 3);
      if (list_size > 1) {
        ee_wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), MAX_ALPHA_CHAR)]);
      }
      ee_waddstr(menu_win, menu_list[counter].item_string);
      if (off_start > 1) {
        ee_wmove(menu_win, top_offset, (max_width - 12));
        ee_wprintw(menu_win, "%s", more_above_str);
      }
      if ((off_start + vert_size - 1) < list_size) {
        ee_wmove(menu_win, (top_offset + vert_size - 1), (max_width - 12));
        ee_wprintw(menu_win, "%s", more_below_str);
      }
    }
  } else {
    for (counter = 1; counter <= list_size; counter++) {
      ee_wmove(menu_win, (top_offset + counter - 1), 3);
      if (list_size > 1) {
        ee_wprintw(menu_win, "%c) ",
                item_alpha[min((counter - 1), MAX_ALPHA_CHAR)]);
      }
      ee_waddstr(menu_win, menu_list[counter].item_string);
    }
  }
}
void modes_op() {
  int ret_value;
  int counter;
  char *string;

  do {
    sprintf(modes_menu[1].item_string, "%s %s", mode_strings[1],
            (expand_tabs ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[2].item_string, "%s %s", mode_strings[2],
            (case_sen ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[3].item_string, "%s %s", mode_strings[3],
            (observ_margins ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[4].item_string, "%s %s", mode_strings[4],
            (auto_format ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[5].item_string, "%s %s", mode_strings[5],
            (eightbit ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[6].item_string, "%s %s", mode_strings[6],
            (info_window ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[7].item_string, "%s %s", mode_strings[7],
            (emacs_keys_mode ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[8].item_string, "%s %s", mode_strings[8],
            (vi_keys_mode ? STATE_ON : STATE_OFF));
    sprintf(modes_menu[9].item_string, "%s %d", mode_strings[9], right_margin);
    sprintf(modes_menu[10].item_string, "%s %s", mode_strings[10],
            (ee_chinese ? STATE_ON : STATE_OFF));

    ret_value = menu_op(modes_menu);

    switch (ret_value) {
    case 1:
      expand_tabs = !expand_tabs;
      break;
    case 2:
      case_sen = !case_sen;
      break;
    case 3:
      observ_margins = !observ_margins;
      break;
    case 4:
      auto_format = !auto_format;
      if (auto_format) {
        observ_margins = true;
      }
      break;
    case 5:
      eightbit = !eightbit;
      if (!eightbit) {
        ee_chinese = false;
      }
#ifdef NCURSE
      if (ee_chinese)
        nc_setattrib(A_NC_BIG5);
      else
        nc_clearattrib(A_NC_BIG5);
#endif /* NCURSE */

      redraw();
      wnoutrefresh(text_win);
      break;
    case 6:
      info_window = !info_window;
      resize_info_win();
      break;
    case 7:
      emacs_keys_mode = !emacs_keys_mode;
      if (emacs_keys_mode) vi_keys_mode = false;
      update_libedit_mode();
      resize_info_win();
      break;
    case 8:
      vi_keys_mode = !vi_keys_mode;
      if (vi_keys_mode) emacs_keys_mode = false;
      update_libedit_mode();
      resize_info_win();
      break;
    case 9:
      string = get_string(margin_prompt, 1);
      if (string != nullptr) {
        counter = atoi(string);
        if (counter > 0) {
          right_margin = counter;
        }
        free(string);
      }
      break;
    case 10:
      ee_chinese = !ee_chinese;
      if (ee_chinese) {
        eightbit = true;
      }
#ifdef NCURSE
      if (ee_chinese)
        nc_setattrib(A_NC_BIG5);
      else
        nc_clearattrib(A_NC_BIG5);
#endif /* NCURSE */
      redraw();
      break;
    case 11:
      // Handled by menu struct call to dump_ee_conf
      break;
    default:
      break;
    }
  } while (ret_value != 0);
}
