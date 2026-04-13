#!/bin/sh

echo "Probing dependencies..."

CFLAGS="-std=c23 -DNO_CATGETS"
LIBS=""

# Probe ncursesw/ncurses/curses
if pkg-config --exists ncursesw; then
    CFLAGS="$CFLAGS $(pkg-config --cflags ncursesw)"
    LIBS="$LIBS $(pkg-config --libs ncursesw)"
    echo "Found ncursesw"
elif pkg-config --exists ncurses; then
    CFLAGS="$CFLAGS $(pkg-config --cflags ncurses)"
    LIBS="$LIBS $(pkg-config --libs ncurses)"
    echo "Found ncurses"
elif pkg-config --exists curses; then
    CFLAGS="$CFLAGS $(pkg-config --cflags curses)"
    LIBS="$LIBS $(pkg-config --libs curses)"
    echo "Found curses"
else
    echo "Warning: ncurses not found via pkg-config, using defaults"
    LIBS="$LIBS -lcurses"
fi

# Probe tree-sitter
if pkg-config --exists tree-sitter; then
    CFLAGS="$CFLAGS $(pkg-config --cflags tree-sitter)"
    LIBS="$LIBS $(pkg-config --libs tree-sitter)"
    echo "Found tree-sitter"
else
    echo "Warning: tree-sitter not found via pkg-config, using defaults"
    LIBS="$LIBS -ltree-sitter"
fi

if pkg-config --exists tree-sitter-c; then
    CFLAGS="$CFLAGS $(pkg-config --cflags tree-sitter-c)"
    LIBS="$LIBS $(pkg-config --libs tree-sitter-c)"
    echo "Found tree-sitter-c"
else
    echo "Warning: tree-sitter-c not found via pkg-config, using defaults"
    LIBS="$LIBS -ltree-sitter-c"
fi

# Probe ICU
if pkg-config --exists icu-uc icu-io; then
    CFLAGS="$CFLAGS $(pkg-config --cflags icu-uc icu-io)"
    LIBS="$LIBS $(pkg-config --libs icu-uc icu-io)"
    echo "Found ICU"
fi

# Generate config.mk
echo "Generating config.mk..."
cat << EOF > config.mk
CFLAGS += $CFLAGS
LDFLAGS += $LIBS
EOF

echo "Done. Now you can run 'make'."
