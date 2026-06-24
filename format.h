#ifndef EE_FORMAT_H
#define EE_FORMAT_H

#include "ee.h"

extern int right_margin;
extern bool observ_margins;

bool Blank_Line(struct text *test_line);
void Format(void);
void Auto_Format(void);

#endif
