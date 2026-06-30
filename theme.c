#include "theme.h"
#include "ee.h"
#include "menu.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_THEME_PATH 512

static char theme_names[MAX_THEMES][128];
static char theme_paths[MAX_THEMES][MAX_THEME_PATH];
static int theme_count = 0;
static struct menu_entries theme_menu_entries[MAX_THEMES + 2];
static bool themes_loaded = false;

static const struct theme_pair {
  const char *fish_var;
  int pair;
} fish_to_pair[] = {
  {"fish_color_comment",  1},
  {"fish_color_quote",    2},
  {"fish_color_param",    3},
  {"fish_color_operator", 4},
  {"fish_color_command",  5},
  {"fish_color_normal",   6},
  {"fish_color_error",    7},
  {nullptr, 0}
};

static int lookup_named_color(const char *name) {
  if (strcmp(name, "black") == 0)    return 0;
  if (strcmp(name, "red") == 0)      return 1;
  if (strcmp(name, "green") == 0)    return 2;
  if (strcmp(name, "yellow") == 0)   return 3;
  if (strcmp(name, "blue") == 0)     return 4;
  if (strcmp(name, "magenta") == 0)  return 5;
  if (strcmp(name, "cyan") == 0)     return 6;
  if (strcmp(name, "white") == 0)    return 7;
  if (strcmp(name, "brblack") == 0)  return 8;
  if (strcmp(name, "brred") == 0)    return 9;
  if (strcmp(name, "brgreen") == 0)  return 10;
  if (strcmp(name, "bryellow") == 0) return 11;
  if (strcmp(name, "brblue") == 0)   return 12;
  if (strcmp(name, "brmagenta") == 0) return 13;
  if (strcmp(name, "brcyan") == 0)   return 14;
  if (strcmp(name, "brwhite") == 0)  return 15;
  if (strcmp(name, "normal") == 0)   return -1;
  return -2;
}

static int is_hex_str(const char *s) {
  if (!s || strlen(s) != 6) return 0;
  for (int i = 0; i < 6; i++)
    if (!isxdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

static int custom_color_base = 16;

static int next_custom_color(void) {
  if (!can_change_color() || custom_color_base >= 256)
    return -1;
  return custom_color_base++;
}

static void hex_to_rgb(const char *hex, int *r, int *g, int *b) {
  unsigned int hr, hg, hb;
  sscanf(hex, "%02x%02x%02x", &hr, &hg, &hb);
  *r = hr * 1000 / 255;
  *g = hg * 1000 / 255;
  *b = hb * 1000 / 255;
}

static int parse_color_value(const char *value, int *attr) {
  char buf[256];
  buf[0] = '\0';
  strncat(buf, value, sizeof(buf) - 1);

  *attr = A_NORMAL;
  int color = -2;

  char *token = strtok(buf, " \t");
  while (token) {
    if (strcmp(token, "--bold") == 0 || strcmp(token, "-r") == 0) {
      *attr |= A_BOLD;
    } else if (strcmp(token, "--reverse") == 0 || strcmp(token, "-rv") == 0) {
      *attr |= A_REVERSE;
    } else if (strcmp(token, "--underline") == 0) {
      *attr |= A_UNDERLINE;
    } else if (strcmp(token, "--reset") == 0) {
      *attr = A_NORMAL;
      color = -1;
    } else if (strncmp(token, "--background=", 13) == 0) {
    } else if (strncmp(token, "--italics", 9) == 0) {
    } else if (token[0] == '$') {
    } else {
      int nc = lookup_named_color(token);
      if (nc != -2) {
        color = nc;
      } else if (is_hex_str(token)) {
        int cidx = next_custom_color();
        if (cidx >= 0) {
          int r, g, b;
          hex_to_rgb(token, &r, &g, &b);
          init_color(cidx, r, g, b);
          color = cidx;
        } else {
          color = COLOR_WHITE;
        }
      }
    }
    token = strtok(nullptr, " \t");
  }

  if (color == -2) color = COLOR_WHITE;
  return color;
}

static void scan_theme_dir(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) return;

  struct dirent *entry;
  while ((entry = readdir(d)) != nullptr) {
    const char *ext = strrchr(entry->d_name, '.');
    if (!ext || strcmp(ext, ".theme") != 0) continue;
    if (theme_count >= MAX_THEMES) break;

    char path[MAX_THEME_PATH];
    int path_len = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    if (path_len < 0 || path_len >= (int)sizeof(path)) continue;

    FILE *f = fopen(path, "r");
    if (!f) continue;

    char line[256];
    char display_name[128] = "";
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "# name:", 7) == 0) {
        char *n = line + 7;
        while (*n == ' ' || *n == '\t') n++;
        char *end = strchr(n, '\n');
        if (end) *end = '\0';
        snprintf(display_name, sizeof(display_name), "%s", n);
        break;
      }
    }
    fclose(f);

    if (display_name[0] == '\0') {
      snprintf(display_name, sizeof(display_name), "%s", entry->d_name);
      char *dot = strrchr(display_name, '.');
      if (dot) *dot = '\0';
    }

    snprintf(theme_names[theme_count], sizeof(theme_names[0]), "%s", display_name);
    snprintf(theme_paths[theme_count], sizeof(theme_paths[0]), "%s", path);
    theme_count++;
  }
  closedir(d);
}

static void scan_themes(void) {
  if (themes_loaded) return;
  theme_count = 0;

  scan_theme_dir("/usr/share/fish/themes");

  const char *home = getenv("HOME");
  if (home) {
    char user_dir[MAX_THEME_PATH];
    int len = snprintf(user_dir, sizeof(user_dir), "%s/.config/fish/themes", home);
    if (len > 0 && len < (int)sizeof(user_dir))
      scan_theme_dir(user_dir);
  }

  themes_loaded = true;
}

void apply_theme(int index) {
  if (index < 0 || index >= theme_count) return;
  if (!has_colors()) return;

  FILE *f = fopen(theme_paths[index], "r");
  if (!f) return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '[' || line[0] == '\n') continue;

    char var[64] = {0};
    char val[192] = {0};
    if (sscanf(line, "%63s %191[^\n]", var, val) < 2) continue;

    for (int i = 0; fish_to_pair[i].fish_var; i++) {
      if (strcmp(var, fish_to_pair[i].fish_var) == 0) {
        int attr;
        int color = parse_color_value(val, &attr);
        if (color >= 0) {
          init_pair(fish_to_pair[i].pair, color, -1);
        }
        break;
      }
    }
  }
  fclose(f);
}

static void reset_default_theme(void) {
  if (!has_colors()) return;
  init_pair(1, COLOR_GREEN, -1);
  init_pair(2, COLOR_YELLOW, -1);
  init_pair(3, COLOR_CYAN, -1);
  init_pair(4, COLOR_YELLOW, -1);
  init_pair(5, COLOR_BLUE, -1);
  init_pair(6, COLOR_WHITE, -1);
  init_pair(7, COLOR_MAGENTA, -1);
  init_pair(8, COLOR_RED, -1);
}

void apply_startup_theme(void) {
  if (!theme_name[0] || strcmp(theme_name, "default") == 0) return;
  scan_themes();
  for (int i = 0; i < theme_count; i++) {
    if (strcmp(theme_names[i], theme_name) == 0) {
      apply_theme(i);
      return;
    }
  }
}

void theme_select_op(void) {
  int i;

  scan_themes();

  theme_menu_entries[0].item_string = "select theme";
  theme_menu_entries[0].procedure = nullptr;
  theme_menu_entries[0].ptr_menu = nullptr;
  theme_menu_entries[0].procedure2 = nullptr;
  theme_menu_entries[0].procedure3 = nullptr;
  theme_menu_entries[0].value = 0;

  theme_menu_entries[1].item_string = "(reset to defaults)";
  theme_menu_entries[1].procedure = nullptr;
  theme_menu_entries[1].ptr_menu = nullptr;
  theme_menu_entries[1].procedure2 = nullptr;
  theme_menu_entries[1].procedure3 = nullptr;
  theme_menu_entries[1].value = -1;

  for (i = 0; i < theme_count && i < MAX_THEMES; i++) {
    theme_menu_entries[i + 2].item_string = theme_names[i];
    theme_menu_entries[i + 2].procedure = nullptr;
    theme_menu_entries[i + 2].ptr_menu = nullptr;
    theme_menu_entries[i + 2].procedure2 = nullptr;
    theme_menu_entries[i + 2].procedure3 = nullptr;
    theme_menu_entries[i + 2].value = -1;
  }

  theme_menu_entries[i + 2].item_string = nullptr;

  int result = menu_op(theme_menu_entries);

  if (result == 1) {
    reset_default_theme();
    theme_name[0] = '\0';
    dump_ee_conf();
  } else if (result >= 2 && result - 2 < theme_count) {
    int idx = result - 2;
    apply_theme(idx);
    snprintf(theme_name, sizeof(theme_name), "%s", theme_names[idx]);
    dump_ee_conf();
  }
}
