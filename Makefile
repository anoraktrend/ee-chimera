CC = clang
CFLAGS = -std=c23
LDFLAGS = -lcurses -ltree-sitter -ltree-sitter-c

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

clean:
	rm -f ee
