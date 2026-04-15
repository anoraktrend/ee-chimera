# ee (easy editor)

The editor 'ee' (easy editor) is intended to be a simple, easy to use terminal-based screen oriented editor that requires no instruction to use. Its primary use would be for people who are new to computers, or who use computers only for things like e-mail.

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
