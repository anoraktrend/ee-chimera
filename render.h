/*
 * Rendering declarations for ee (easy editor)
 */

#ifndef RENDER_H
#define RENDER_H

#include "ee.h"

// Function declarations
int tabshift(int temp_int);
int out_char(WINDOW *restrict window, int character, int column);
int len_char(int character, int column);
int scanline_step(unsigned char *ptr, const unsigned char *pos, int temp);
void scanline(const unsigned char *pos);
void draw_line(int vertical, int horiz, struct text *restrict line, int t_pos);
void draw_screen(void);
void midscreen(int line, unsigned char *ptr);
void top_of_screen(void);
void paint_info_win(void);
void resize_info_win(void);

#ifdef HAS_ICU
[[maybe_unused]] static int u_char_width(UChar32 c, int column);
#endif

#ifdef HAS_TREESITTER
[[maybe_unused]] static int get_node_attribute(int line, int col);
#endif

#endif // RENDER_H