#!/bin/sh

echo "Probing dependencies..."

# Default settings
ENABLE_LSP=${ENABLE_LSP:-1}
ENABLE_TREESITTER=${ENABLE_TREESITTER:-1}
ENABLE_ICU=${ENABLE_ICU:-1}
ENABLE_LOCALIZATION=${ENABLE_LOCALIZATION:-1}
ENABLE_HELP=${ENABLE_HELP:-1}
ENABLE_MENU=${ENABLE_MENU:-1}
ENABLE_SPELL=${ENABLE_SPELL:-1}
ENABLE_AUTOFORMAT=${ENABLE_AUTOFORMAT:-1}
ENABLE_INFO_WIN=${ENABLE_INFO_WIN:-1}

CFLAGS="-std=c23"
LIBS=""

# Probe ncursesw/ncurses/curses
if pkg-config --exists ncursesw; then
    CFLAGS="$CFLAGS $(pkg-config --cflags ncursesw) -DHAS_NCURSESW"
    LIBS="$LIBS $(pkg-config --libs ncursesw)"
    echo "Found ncursesw"
elif pkg-config --exists ncurses; then
    CFLAGS="$CFLAGS $(pkg-config --cflags ncurses) -DHAS_NCURSES"
    LIBS="$LIBS $(pkg-config --libs ncurses)"
    echo "Found ncurses"
elif pkg-config --exists curses; then
    CFLAGS="$CFLAGS $(pkg-config --cflags curses)"
    LIBS="$LIBS $(pkg-config --libs curses)"
    echo "Found curses (possibly NetBSD curses)"
else
    echo "Warning: ncurses/curses not found via pkg-config, using defaults"
    LIBS="$LIBS -lcurses"
fi

# Feature checks
if [ "$ENABLE_TREESITTER" = "1" ]; then
    if pkg-config --exists tree-sitter; then
        CFLAGS="$CFLAGS $(pkg-config --cflags tree-sitter) -DHAS_TREESITTER"
        LIBS="$LIBS $(pkg-config --libs tree-sitter)"
        if pkg-config --exists tree-sitter-c; then
            CFLAGS="$CFLAGS $(pkg-config --cflags tree-sitter-c)"
            LIBS="$LIBS $(pkg-config --libs tree-sitter-c)"
            echo "Enabled Tree-Sitter with C support"
        else
             echo "Tree-Sitter-C not found, disabling Tree-Sitter"
             ENABLE_TREESITTER=0
        fi
    else
        echo "Tree-Sitter not found, disabling"
        ENABLE_TREESITTER=0
    fi
fi

if [ "$ENABLE_LSP" = "1" ]; then
    if which clangd > /dev/null 2>&1; then
        CFLAGS="$CFLAGS -DHAS_LSP"
        echo "Enabled LSP"
    else
        echo "clangd not found, disabling LSP"
        ENABLE_LSP=0
    fi
fi

if [ "$ENABLE_ICU" = "1" ]; then
    if pkg-config --exists icu-uc icu-io; then
        CFLAGS="$CFLAGS $(pkg-config --cflags icu-uc icu-io) -DHAS_ICU"
        LIBS="$LIBS $(pkg-config --libs icu-uc icu-io)"
        echo "Enabled ICU"
    else
        echo "ICU not found, disabling"
        ENABLE_ICU=0
    fi
fi

[ "$ENABLE_LOCALIZATION" = "0" ] && CFLAGS="$CFLAGS -DNO_CATGETS"
[ "$ENABLE_HELP" = "1" ] && CFLAGS="$CFLAGS -DHAS_HELP"
[ "$ENABLE_MENU" = "1" ] && CFLAGS="$CFLAGS -DHAS_MENU"
[ "$ENABLE_SPELL" = "1" ] && CFLAGS="$CFLAGS -DHAS_SPELL"
[ "$ENABLE_AUTOFORMAT" = "1" ] && CFLAGS="$CFLAGS -DHAS_AUTOFORMAT"
[ "$ENABLE_INFO_WIN" = "1" ] && CFLAGS="$CFLAGS -DHAS_INFO_WIN"

# Generate config.mk
echo "Generating config.mk..."
cat << EOF > config.mk
CFLAGS = $CFLAGS
LDFLAGS = $LIBS
EOF

echo "Done. Now you can run 'make'."
