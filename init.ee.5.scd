init.ee(5)

# NAME

init.ee - configuration file for the ee text editor

# DESCRIPTION

The *.init.ee* file is used to set default modes and key bindings for the *ee*(1)
text editor. The editor searches for this file in the following locations, in
order of precedence:

1. */usr/share/misc/init.ee*
2. *$HOME/.init.ee*
3. *.init.ee* in the current working directory

The file consists of one command per line. Commands are case-insensitive.

# COMMANDS

*CASE*, *NOCASE*
	Enable or disable case-sensitive searching.

*EXPAND*, *NOEXPAND*
	Enable or disable the expansion of tabs to spaces.

*INFO*, *NOINFO*
	Enable or disable the information window at the top of the screen.

*MARGINS*, *NOMARGINS*
	Enable or disable the observation of margins.

*AUTOFORMAT*, *NOAUTOFORMAT*
	Enable or disable automatic paragraph formatting.

*EIGHTBIT*, *NOEIGHTBIT*
	Enable or disable support for eight-bit characters.

*HIGHLIGHT*, *NOHIGHLIGHT*
	Enable or disable border and menu highlighting.

*EMACS*, *NOEMACS*
	Enable or disable Emacs-style control key bindings.

*16BIT*, *NO16BIT*
	Enable or disable support for 16-bit characters (e.g., Big-5).

*RIGHTMARGIN* **number**
	Set the right margin to the specified **number**.

*PRINTCOMMAND* **command**
	Set the command used for printing (default is *lpr*).

*ECHO* **string**
	Print **string** to the message line during initialization.

*BIND* **key** **command**
	Bind a base control key. **key** can be a control character (e.g., *^A*),
	a modified key (e.g., *M-a* for Alt-a, *W-x* for Super-x, *C-c* for Ctrl-c,
	*S-u* for Shift-u), or a literal code (e.g., *code:513*).

*GBIND* **key** **command**
	Bind a GOLD mode key (triggered after *^G*). Supports the same formats
	as *BIND*.

*EBIND* **key** **command**
	Bind a key in Emacs mode. Supports the same formats as *BIND*.

# BINDABLE COMMANDS

The following internal commands can be used with *BIND*, *GBIND*, and *EBIND*:

*right*, *left*, *up*, *down*, *bol*, *eol*, *next_page*, *prev_page*,
*top_of_txt*, *bottom_of_txt*, *del_char*, *del_word*, *del_line*, *und_char*,
*und_word*, *und_line*, *copy*, *cut*, *paste*, *append*, *mark*, *search*,
*search_reverse*, *search_prompt*, *replace_prompt*, *command_prompt*,
*gold_toggle*, *redraw*, *help*, *menu*, *format*, *adv_word*, *prev_word*,
*newline*, *backspace*.

# EXAMPLES

```
INFO
NOCASE
RIGHTMARGIN 78
BIND ^A top_of_txt
BIND M-f next_page
BIND W-q menu
BIND code:262 bol  # Bind Home key if it sends code 262
GBIND ^X format
```

# SEE ALSO

*ee*(1)

# AUTHOR

Developed by Hugh Mahon.

Modifications Designed and Assisted by Lucy Randall <lucybrown@vivaldi.net>,
Modifications Developed using Gemini and other LLMs.
