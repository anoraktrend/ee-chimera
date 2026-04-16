ee(1)

# NAME

ee - easy editor
ree - restricted easy editor

# SYNOPSIS

*ee* [*-e*] [*-i*] [*-h*] [*+#*] [*file* ...]
*ree* [*-e*] [*-i*] [*-h*] [*+#*] [*file* ...]

# DESCRIPTION

The *ee* editor is a simple, screen-oriented text editor designed to be
intuitive and easy to use. It is modeless, meaning it is always in text
insertion mode unless a prompt or menu is active.

*ree* is a restricted version of *ee*. It prevents the user from saving files
with names other than those specified on the command line, and disables shell
escapes.

# OPTIONS

*-e*
	Disable expansion of tabs to spaces.

*-i*
	Disable the information window at the top of the screen.

*-h*
	Disable border highlighting.

*+#*
	Start the editor with the cursor at line number *#*.

# KEY BINDINGS

*ee* uses control keys and function keys for navigation and commands.

## Basic Navigation

*^P* (Prev Page), *^N* (Next Page)
	Move up or down one page.

*Up*, *Down*, *Left*, *Right* (Arrow keys)
	Move the cursor in the corresponding direction.

*^O*
	Move to end of line.

*^D*
	Move to beginning of line.

*^T*
	Move to top of text.

*^B*
	Move to bottom of text.

*^Y* (Adv Word)
	Move to the next word.

*^G ^Y* (Prev Word)
	Move to the previous word.

## Editing

*^K* (Del Char)
	Delete the character at the cursor.

*Backspace*
	Delete the character before the cursor.

*^L* (Del Line)
	Delete the current line.

*^W* (Del Word)
	Delete the current word.

*^U* (Mark)
	Set a mark at the current cursor position.

*^C* (Copy)
	Copy the region between the mark and cursor to the clipboard.

*^X* (Cut)
	Cut the region between the mark and cursor to the clipboard.

*^V* (Paste)
	Paste the clipboard content at the cursor.

*^J* (Return)
	Insert a newline.

## Command and Menu

*^E* (Command)
	Prompt for an editor command.

*Esc* (Menu)
	Open the main menu.

*^G* (GOLD)
	Enter GOLD mode for secondary commands.

# GOLD MODE COMMANDS

After pressing *^G*, the following secondary commands are available:

*Esc*
	Open the main menu (required if *Esc* alone is remapped).

*^P* (Prev Buff)
	Switch to the previous file buffer.

*^N* (Next Buff)
	Switch to the next file buffer.

*^V* (Forward)
	Search forward for the last searched string.

*^R* (Reverse)
	Search backward for the last searched string.

*^X* (Fmt Parag)
	Format the current paragraph.

*^K* (Und Char)
	Undelete the last deleted character.

*^L* (Und Line)
	Undelete the last deleted line.

*^W* (Und Word)
	Undelete the last deleted word.

*^Z* (Repl Prmpt)
	Prompt for string replacement.

*^B* (Append)
	Append the selected region to the clipboard.

# COMMANDS

Commands are entered at the *^E* prompt:

*help*
	Display help information.

*file* [**name**]
	Print the current file name or set a new one.

*read* **filename**
	Read a file into the buffer.

*write* [**filename**]
	Write the buffer to a file.

*line*
	Print the current line number.

*search* [**string**]
	Search for a string.

*case*, *nocase*
	Toggle case-sensitive search.

*expand*, *noexpand*
	Toggle tab expansion.

*!cmd*
	Execute **cmd** in a shell.

*><!cmd*
	Pipe the buffer to **cmd** and replace it with the output.

# CONFIGURATION

*ee* reads configuration from the following files in order:

1. */usr/share/misc/init.ee* (system-wide)
2. *$HOME/.init.ee* (user-specific)
3. *.init.ee* (directory-specific)

## Custom Key Bindings

User-definable bindings are supported via the following commands in the
configuration file:

*BIND* **key** **command**
	Bind a base control key (e.g., *BIND ^A top_of_txt*).

*GBIND* **key** **command**
	Bind a GOLD mode key.

*EBIND* **key** **command**
	Bind an Emacs mode key.

**command** must be one of the internal command names (e.g., *right*, *left*,
*up*, *down*, *bol*, *eol*, *next_page*, *prev_page*, *top_of_txt*,
*bottom_of_txt*, *del_char*, *del_word*, *del_line*, *und_char*, *und_word*,
*und_line*, *copy*, *cut*, *paste*, *append*, *mark*, *search*,
*search_reverse*, *search_prompt*, *replace_prompt*, *command_prompt*,
*gold_toggle*, *redraw*, *help*, *menu*, *format*, *adv_word*, *prev_word*,
*newline*, *backspace*).

# AUTHOR

Developed by Hugh Mahon.

Modifications Designed and Assisted by Lucy Randall <lucybrown@vivaldi.net>,
Modifications Developed using Gemini and other LLMs.
