#include "search.h"
#include "ee.h"
#ifdef __x86_64__
#include <immintrin.h>
#endif

#ifdef __x86_64__
// AVX2 kernel: requires target feature so intrinsics inline correctly
__attribute__((target("avx2"))) static unsigned char *
memmem_avx2(const unsigned char *haystack, size_t hlen,
            const unsigned char *needle, size_t nlen) {
  __m256i needle_vec = _mm256_loadu_si256((const __m256i *)needle);
  for (size_t i = 0; i <= hlen - 32; i += 32) {
    __m256i haystack_vec = _mm256_loadu_si256((const __m256i *)(haystack + i));
    __m256i cmp = _mm256_cmpeq_epi8(needle_vec, haystack_vec);
    int mask = _mm256_movemask_epi8(cmp);
    if (mask == 0xFFFFFFFF) {
      // Full match: verify remaining bytes
      if (memcmp(haystack + i, needle, nlen) == 0) {
        return (unsigned char *)(haystack + i);
      }
    }
  }
  return nullptr;
}
#endif

// AVX2-accelerated memmem with runtime dispatch
static unsigned char *memmem_simd(const unsigned char *haystack, size_t hlen,
                                  const unsigned char *needle, size_t nlen) {
  if (nlen == 0)
    return (unsigned char *)haystack;
  if (hlen < nlen)
    return nullptr;

#ifdef __x86_64__
  // Use AVX2 if the CPU supports it and needle is long enough
  static int have_avx2 = -1;
  if (have_avx2 < 0)
    have_avx2 = __builtin_cpu_supports("avx2");
  if (have_avx2 && nlen >= 32) {
    unsigned char *hit = memmem_avx2(haystack, hlen, needle, nlen);
    if (hit != nullptr)
      return hit;
  }
#endif

  // Fallback to memmem for short needles or non-x86_64 targets
  return (unsigned char *)memmem(haystack, hlen, needle, nlen);
}
struct text *srch_line;    /* temporary pointer for search routine */
bool case_sen;             /* case sensitive search flag		*/
unsigned char *srch_str;   /* pointer for search string		*/
unsigned char *u_srch_str; /* pointer to non-case sensitive search	*/
unsigned char *srch_1;     /* pointer to start of suspect string	*/
unsigned char *srch_2;     /* pointer to next character of string	*/
unsigned char *srch_3;

/* Lookup table: upper[c] == toupper(c) for all c in [0,255]. */
static const unsigned char upper_table[256] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,
    14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,
    28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,
    42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,
    56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
    70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,
    84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,
    /* a-z -> A-Z */
    65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,
    78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
    123, 124, 125, 126, 127,
    /* 128-255: identity */
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141,
    142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
    156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169,
    170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197,
    198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211,
    212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253,
    254, 255};

/* create an uppercase duplicate of src using the table above */
static unsigned char *dup_upper(unsigned char *src) {
  if (!src) {
    return nullptr;
  }
  size_t len = strlen((char *)src);
  unsigned char *dst = malloc(len + 1);
  if (!dst) {
    return nullptr;
  }
  /* Table-driven loop: no branch per byte; compiler can auto-vectorise. */
  for (size_t i = 0; i < len; i++) {
    dst[i] = upper_table[src[i]];
  }
  dst[len] = '\0';
  return dst;
}

static const unsigned char ident_table[256] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,
    14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,
    28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,
    42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,
    56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
    70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,
    84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,
    98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
    126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
    140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153,
    154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167,
    168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181,
    182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195,
    196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237,
    238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
    252, 253, 254, 255};

[[nodiscard]] bool compare(const char *string1, const char *string2,
                           bool sensitive) {
  if (!string1 || !string2 || *string1 == '\0' || *string2 == '\0') {
    return false;
  }
  const unsigned char *lut = sensitive ? ident_table : upper_table;
  const unsigned char *s1 = (const unsigned char *)string1;
  const unsigned char *s2 = (const unsigned char *)string2;

  while (*s1 != '\0' && *s2 != '\0' && *s1 != ' ' && *s2 != ' ') {
    if (lut[*s1] != lut[*s2]) {
      return false;
    }
    s1++;
    s2++;
  }
  return true;
}

[[nodiscard]] int search(int display_message) {
  int lines_moved;
  int iter = 0; /* 1-based column of match; set when found */
  int found;

  if ((srch_str == nullptr) || (*srch_str == '\0')) {
    return 0;
  }
  if (display_message != 0) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "%s", searching_msg);
    ee_wrefresh(com_win);
    clear_com_win = true;
  }
  lines_moved = 0;
  found = 0;
  srch_line = curr_line;

  /* Start one position past the cursor so we don't re-match the current hit. */
  srch_1 = point;
  if (position < curr_line->line_length) {
    srch_1++;
  }

  const unsigned char *needle =
      case_sen ? srch_str : u_srch_str;
  size_t srch_len = strlen((char *)needle);

  while (!found && srch_line != nullptr) {
    /* Remaining bytes in this line from srch_1 onward. */
    size_t avail =
        (size_t)(srch_line->line_length - (int)(srch_1 - srch_line->line));

    if (avail >= srch_len) {
      /* Single memmem_simd call covers the whole remaining line. */
      unsigned char *hit = memmem_simd(srch_1, avail, needle, srch_len);
      if (hit != nullptr) {
        found = 1;
        srch_1 = hit;
        /* iter is 1-based byte offset within the line. */
        iter = (int)(srch_1 - srch_line->line) + 1;
      }
    }

    if (!found) {
      srch_line = srch_line->next_line;
      if (srch_line != nullptr) {
        srch_1 = srch_line->line;
      }
      lines_moved++;
    }
  }

  if (found != 0) {
    if (display_message != 0) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wrefresh(com_win);
    }
    if (lines_moved < 30) {
      if (lines_moved != 0) {
        move_rel('d', lines_moved);
      }
      while (position < iter) {
        right(1);
      }
    } else {
      absolute_lin += lines_moved;
      curr_line = srch_line;
      point = srch_1;
      position = iter;
      scanline(point);
      scr_pos = scr_horz;
      midscreen((last_line / 2), point);
    }
  } else {
    if (display_message != 0) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wprintw(com_win, str_not_found_msg, srch_str);
      ee_wrefresh(com_win);
    }
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
  return found;
}

void search_prompt() {
  char *new_srch_str = get_string(search_prompt_str, 0);
  if (!new_srch_str) {
    return; // get_string failed; retain old srch_str
  }
  if (srch_str != nullptr) {
    free(srch_str);
  }
  if (u_srch_str != nullptr) {
    free(u_srch_str);
  }
  srch_str = (unsigned char *)new_srch_str;
  gold = false;
  u_srch_str = dup_upper(srch_str);
  srch_1 = u_srch_str ? u_srch_str + strlen((char *)u_srch_str) : nullptr;
  (void)search(1);
}
void replace_prompt() {
  char *search_term = get_string("Replace: ", 0);
  if (!search_term || *search_term == '\0') {
    return;
  }
  char *replace_term = get_string("With: ", 0);
  if (srch_str != nullptr) {
    free(srch_str);
  }
  if (u_srch_str != nullptr) {
    free(u_srch_str);
  }
  srch_str = (unsigned char *)search_term;
  u_srch_str = dup_upper(srch_str);
  srch_1 = u_srch_str + strlen((char *)u_srch_str);
  int found = search(1);
  if (found) {
    int len = strlen((char *)search_term);
    // Bulk delete
    for (int i = 0; i < len; i++) {
      delete_char_at_cursor(1);
    }
    // Bulk insert
    if (replace_term) {
      size_t rlen = strlen(replace_term);
      for (size_t i = 0; i < rlen; i++) {
        insert(replace_term[i]);
      }
    }
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "Replaced 1 occurrence.");
    ee_wrefresh(com_win);
    clear_com_win = true;
  }
  if (replace_term) {
    free(replace_term);
  }
}
[[nodiscard]] int search_reverse(int display_message) {
  if (!srch_str || *srch_str == '\0')
    return 0;

  if (display_message) {
    ee_wmove(com_win, 0, 0);
    ee_wclrtoeol(com_win);
    ee_wprintw(com_win, "           ...searching reverse");
    ee_wrefresh(com_win);
    clear_com_win = true;
  }

  int lines_moved = 0;
  int found = 0;
  srch_line = curr_line;

  /* Start searching immediately before the cursor */
  int iter = position - 1;
  const unsigned char *needle = case_sen ? srch_str : u_srch_str;
  size_t search_len = strlen((char *)needle);

  while (!found && srch_line != nullptr) {
    if ((size_t)iter >= search_len) {
      /* Search forward in srch_line->line up to iter for the last match */
      unsigned char *curr = srch_line->line;
      size_t remaining = (size_t)iter;
      unsigned char *last_hit = nullptr;
      while (remaining >= search_len) {
        unsigned char *hit = memmem_simd(curr, remaining, needle, search_len);
        if (!hit)
          break;
        last_hit = hit;
        size_t advance = (size_t)(hit - curr) + 1;
        curr = hit + 1;
        remaining -= advance;
      }
      if (last_hit != nullptr) {
        found = 1;
        srch_1 = last_hit;
      }
    }

    if (!found) {
      srch_line = srch_line->prev_line;
      lines_moved--;
      if (srch_line)
        iter = srch_line->line_length;
    }
  }


  if (found) {
    if (display_message) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wrefresh(com_win);
    }
    /* Move cursor to the found location */
    int new_pos = (srch_1 - srch_line->line) + 1;
    while (lines_moved < 0) {
      up();
      lines_moved++;
    }
    while (position > new_pos)
      left(1);
    while (position < new_pos)
      right(1);
  } else {
    if (display_message) {
      ee_wmove(com_win, 0, 0);
      ee_wclrtoeol(com_win);
      ee_wprintw(com_win, str_not_found_msg, srch_str);
      ee_wrefresh(com_win);
    }
    ee_wmove(text_win, scr_vert, (scr_horz - horiz_offset));
  }
  return found;
}
