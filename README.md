# ee (easy editor)

The editor 'ee' (easy editor) is intended to be a simple, easy to use terminal-based screen oriented editor that requires no instruction to use. Its primary use would be for people who are new to computers, or who use computers only for things like e-mail.

## Recent Changes

* **LSP Integration Fix:** Redirected `stderr` to `/dev/null` for the `clangd` child process. This prevents a "Transport error: Input/output error" from being printed to the terminal every time `ee` is closed, resulting in a cleaner exit.
* **Transparent Background:** Changed the background color to be transparent, so it uses the terminal's default background color.

## Usage

See `ee.1` for the manual page and `ee.i18n.guide` for internationalization information.
