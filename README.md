# ee (easy editor)

This is a fork and modification of the version of ee available in FreeBSD/chimerautils, which utilizes various techniques to improve performance, security, and usability while retaining the original's simplicity.

The editor 'ee' (easy editor) is intended to be a simple, easy to use terminal-based screen oriented editor that requires no instruction to use. Its primary use would be for people who are new to computers, or who use computers only for things like e-mail.

A primary technical goal of this project is the implementation of **branchless and loopless programming** techniques to optimize performance and reduce processor pipeline stalls.

## Refactoring Progress

✅ **Core**: Split `ee.c` into modular files:
- `input.c`: Input handling (`control()`, `emacs_control()`, `function_key()`).
- `render.c`: Rendering logic (`draw_line()`, `draw_screen()`, `paint_info_win()`).
- `state.c`: Editor state management (`main()` event loop, `undo_state`, `curr_line`).

✅ **Branchless Dispatch**:
- Replaced `vi_command()` `switch-case` with **function pointer table**.
- Optimized `control()` and `emacs_control()` with **branchless dispatch** (`in & 0x1F`).

## Next Steps

- **Memory**: Audit `undo.c` and `search.c` for leaks/inefficiencies.
- **Performance**: Optimize hot paths (`draw_line()`, `insert()`) with branchless/SIMD.

## Recent Changes

* **LSP Integration Fix:** Redirected `stderr` to `/dev/null` for the `clangd` child process. This prevents a "Transport error: Input/output error" from being printed to the terminal every time `ee` is closed, resulting in a cleaner exit.
* **Transparent Background:** Changed the background color to be transparent, so it uses the terminal's default background color.
* **Syntax Highlighting Fix:** Fixed an issue where the first page of a file was not properly highlighted on startup. Added an initial reparse call when a file is loaded.
* **LSP File Path Fix:** Corrected the file path passed to the LSP server when opening or changing files.
* **AEE-style Hint Window:** Updated the information window at the top of the screen to match the layout and content of `aee`.
* **AEE Key Bindings:** Remapped control keys to match `aee` conventions, including support for the `GOLD` key (`^G`).
* **Dynamic Info Window:** The information window now automatically expands or shrinks based on the terminal height, providing more space for text on smaller terminals.
* **New Editing Operations:** Implemented Mark (`^U`), Copy (`^C`), Cut (`^X`), Paste (`^V`), and Replace (`^Z`) features.
* **Performance Optimizations:** Replaced manual byte-shifting loops with optimized `memmove` and `memcpy` calls for core buffer operations (`insert`, `delete_char_at_cursor`, `del_line`).

## Usage

See `ee.1` for the manual page and `ee.i18n.guide` for internationalization information.

## Themes

Syntax themes use Fish's `fish_color_*` variables. The editor scans themes in
`./themes`, `~/.config/ee/themes`, the installed `share/ee/themes` directory,
and Fish's theme directories. Run `make install-local` to install the bundled
themes for the current user.

The renderer uses these variables as follows: `fish_color_comment` colors
comments, `fish_color_quote` strings, `fish_color_param` numbers and literals,
`fish_color_operator` operators, `fish_color_command` functions and commands,
`fish_color_normal` the default foreground, and `fish_color_error` diagnostics.