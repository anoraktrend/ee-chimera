CFLAGS += -std=c23 -DNO_CATGETS -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600   
LDFLAGS +=  -lncursesw -ltree-sitter -ltree-sitter-c -licuio -licuuc
