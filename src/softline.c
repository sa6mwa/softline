#include "softline_internal.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#define SL_BUF_INITIAL 1024
#define SL_ESCAPE_SEQ_MAX 8

#define SL_KEY_NONE 0
#define SL_KEY_UP 1000
#define SL_KEY_DOWN 1001
#define SL_KEY_LEFT 1002
#define SL_KEY_RIGHT 1003
#define SL_KEY_HOME 1004
#define SL_KEY_END 1005
#define SL_KEY_DELETE 1006
#define SL_KEY_ALT_B 1007
#define SL_KEY_ALT_F 1008
#define SL_KEY_ESCAPE 1009
#define SL_KEY_UNKNOWN 1010

static size_t sl_line_start(const char *buf, size_t pos) {
  while (pos > 0 && buf[pos - 1] != '\n')
    pos--;
  return pos;
}

static size_t sl_line_end(const char *buf, size_t len, size_t pos) {
  while (pos < len && buf[pos] != '\n')
    pos++;
  return pos;
}

static size_t sl_prev_line(const char *buf, size_t pos) {
  if (pos == 0)
    return 0;
  if (buf[pos - 1] == '\n')
    pos--;
  return sl_line_start(buf, pos);
}

static size_t sl_next_line(const char *buf, size_t len, size_t pos) {
  size_t eol = sl_line_end(buf, len, pos);
  if (eol >= len)
    return pos;
  return eol + 1;
}

static size_t sl_word_forward(const char *buf, size_t len, size_t pos) {
  while (pos < len && buf[pos] != ' ' && buf[pos] != '\t' && buf[pos] != '\n')
    pos++;
  while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n'))
    pos++;
  return pos;
}

static size_t sl_word_backward(const char *buf, size_t pos) {
  if (pos == 0)
    return 0;
  pos--;
  while (pos > 0 && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n'))
    pos--;
  while (pos > 0 && buf[pos - 1] != ' ' && buf[pos - 1] != '\t' &&
         buf[pos - 1] != '\n')
    pos--;
  return pos;
}

typedef struct sl_vline {
  size_t start;
  size_t end;
} sl_vline_t;

typedef struct sl_display {
  sl_vline_t lines[SL_MAX_LINES];
  int count;
  int cursor_row;
  int cursor_col;
} sl_display_t;

static void sl_compute_display(const char *buf, size_t len, size_t cursor,
                               int avail, int indent, sl_display_t *d) {
  size_t pos;
  int row, col, in_indent;

  d->count = 0;
  d->cursor_row = 0;
  d->cursor_col = indent;
  if (avail <= indent)
    avail = indent + 1;

  pos = 0;
  row = 0;
  col = indent;
  in_indent = 1;

  if (d->count < SL_MAX_LINES) {
    d->lines[d->count].start = 0;
  }

  while (pos <= len && d->count < SL_MAX_LINES) {
    int w;

    if (pos == cursor) {
      d->cursor_row = row;
      d->cursor_col = col;
    }

    if (pos == len || buf[pos] == '\n') {
      d->lines[d->count].end = pos;
      d->count++;
      if (pos < len && buf[pos] == '\n') {
        pos++;
      }
      if (pos < len && d->count < SL_MAX_LINES) {
        d->lines[d->count].start = pos;
        row++;
        col = indent;
        in_indent = 1;
        continue;
      }
      break;
    }

    w = (buf[pos] == '\t') ? (8 - (col % 8)) : 1;

    if (col + w > avail && !in_indent) {
      d->lines[d->count].end = pos;
      d->count++;
      row++;
      d->lines[d->count].start = pos;
      col = indent;
      in_indent = 1;
      continue;
    }

    col += w;
    in_indent = 0;
    pos++;
  }
}

static void sl_wstr(int fd, const char *s) {
  size_t n;
  if (!s)
    return;
  n = strlen(s);
  if (n)
    (void)!write(fd, s, n);
}

static void sl_mvcur(int fd, int r, int c) {
  char buf[32];
  int n;
  n = snprintf(buf, sizeof(buf), "\033[%d;%dH", r + 1, c + 1);
  (void)!write(fd, buf, (size_t)n);
}

static void sl_cleol(int fd) { sl_wstr(fd, "\033[0K"); }

static void sl_hcur(int fd) { sl_wstr(fd, "\033[?25l"); }

static void sl_scur(int fd) { sl_wstr(fd, "\033[?25h"); }

static void sl_cls(int fd) {
  sl_wstr(fd, "\033[2J");
  sl_mvcur(fd, 0, 0);
}

static int sl_get_winsize(int *cols, int *rows) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    *cols = 80;
    *rows = 24;
    return -1;
  }
  *cols = (ws.ws_col > 0) ? (int)ws.ws_col : 80;
  *rows = (ws.ws_row > 0) ? (int)ws.ws_row : 24;
  return 0;
}

static int sl_raw_init(sl_t *sl) {
  if (sl->initialized)
    return 0;
  if (linenoiseEditStart(&sl->ls, STDIN_FILENO, STDOUT_FILENO, sl->buf,
                         (int)sl->buflen_max, "") == -1) {
    return -1;
  }
  sl_hcur(STDOUT_FILENO);
  sl->initialized = 1;
  return 0;
}

static void sl_raw_stop(sl_t *sl) {
  if (!sl->initialized)
    return;
  sl_scur(STDOUT_FILENO);
  linenoiseEditStop(&sl->ls);
  sl->initialized = 0;
}

static int sl_read_key(sl_t *sl) {
  char ch;
  ssize_t n;

  n = read(sl->ls.ifd, &ch, 1);
  if (n <= 0)
    return SL_KEY_NONE;

  if (ch != '\033')
    return (int)(unsigned char)ch;

  n = read(sl->ls.ifd, &ch, 1);
  if (n <= 0)
    return SL_KEY_ESCAPE;

  if (ch == 'b')
    return SL_KEY_ALT_B;
  if (ch == 'f')
    return SL_KEY_ALT_F;

  if (ch != '[' && ch != 'O')
    return SL_KEY_ESCAPE;

  n = read(sl->ls.ifd, &ch, 1);
  if (n <= 0)
    return SL_KEY_ESCAPE;

  switch (ch) {
  case 'A':
    return SL_KEY_UP;
  case 'B':
    return SL_KEY_DOWN;
  case 'C':
    return SL_KEY_RIGHT;
  case 'D':
    return SL_KEY_LEFT;
  case 'H':
    return SL_KEY_HOME;
  case 'F':
    return SL_KEY_END;
  }

  if (ch >= '0' && ch <= '9') {
    char buf[8];
    int i;
    buf[0] = ch;
    for (i = 1; i < 7; i++) {
      n = read(sl->ls.ifd, &buf[i], 1);
      if (n <= 0)
        return SL_KEY_UNKNOWN;
      if (buf[i] == '~')
        break;
      if (buf[i] < '0' || buf[i] > '9')
        break;
    }
    if (buf[i] == '~') {
      if (buf[0] == '3')
        return SL_KEY_DELETE;
      if (buf[0] == '1' || buf[0] == '7')
        return SL_KEY_HOME;
      if (buf[0] == '4' || buf[0] == '8')
        return SL_KEY_END;
    }
  }

  return SL_KEY_UNKNOWN;
}

static void sl_buf_grow(sl_t *sl, size_t need) {
  size_t nc;
  char *nb;
  if (need <= sl->buflen_max)
    return;
  nc = sl->buflen_max * 2;
  if (nc < need)
    nc = need + SL_BUF_INITIAL;
  nb = (char *)realloc(sl->buf, nc);
  if (!nb)
    return;
  sl->buf = nb;
  sl->buflen_max = nc;
}

static void sl_buf_ins(sl_t *sl, size_t at, const char *s, size_t sn) {
  size_t mv;
  if (sn == 0)
    return;
  sl_buf_grow(sl, sl->buflen + sn + 1);
  mv = sl->buflen - at;
  if (mv > 0)
    memmove(sl->buf + at + sn, sl->buf + at, mv);
  memcpy(sl->buf + at, s, sn);
  sl->buflen += sn;
  sl->buf[sl->buflen] = '\0';
}

static void sl_buf_del(sl_t *sl, size_t at, size_t dn) {
  size_t mv;
  if (dn == 0 || at >= sl->buflen)
    return;
  if (at + dn > sl->buflen)
    dn = sl->buflen - at;
  mv = sl->buflen - (at + dn);
  if (mv > 0)
    memmove(sl->buf + at, sl->buf + at + dn, mv);
  sl->buflen -= dn;
  sl->buf[sl->buflen] = '\0';
}

static void sl_render(sl_t *sl, size_t cur, const char *prompt, int start_row) {
  int tcols, trows;
  int avail, indent;
  size_t plen;
  sl_display_t d;
  int row, col;

  sl_get_winsize(&tcols, &trows);

  plen = prompt ? strlen(prompt) : 0;
  indent = (int)plen;
  if (sl->indent_mode == SL_INDENT_SYMBOL && sl->prompt_symbol)
    indent = (int)strlen(sl->prompt_symbol);

  avail = sl->screen_width > 0 ? sl->screen_width : tcols;
  if (sl->screen_x > 0)
    indent += sl->screen_x;
  if (avail > tcols)
    avail = tcols;

  sl_compute_display(sl->buf, sl->buflen, cur, avail, indent, &d);

  for (row = 0; row < d.count; row++) {
    size_t s = d.lines[row].start;
    size_t e = d.lines[row].end;
    col = 0;

    sl_mvcur(STDOUT_FILENO, start_row + row, 0);
    sl_cleol(STDOUT_FILENO);

    if (s == 0 || (s > 0 && sl->buf[s - 1] == '\n')) {
      if (row == 0 && prompt) {
        sl_wstr(STDOUT_FILENO, prompt);
      } else {
        if (sl->indent_mode == SL_INDENT_SYMBOL && sl->prompt_symbol) {
          sl_wstr(STDOUT_FILENO, sl->prompt_symbol);
        } else {
          size_t k;
          for (k = 0; k < plen; k++)
            (void)!write(STDOUT_FILENO, " ", 1);
        }
      }
      col = indent;
    } else {
      size_t k;
      for (k = 0; k < (size_t)indent; k++)
        (void)!write(STDOUT_FILENO, " ", 1);
      col = indent;
    }

    while (s < e) {
      if (sl->buf[s] == '\t') {
        int sp = 8 - (col % 8);
        size_t j;
        for (j = 0; j < (size_t)sp; j++)
          (void)!write(STDOUT_FILENO, " ", 1);
        col += sp;
      } else {
        (void)!write(STDOUT_FILENO, &sl->buf[s], 1);
        col++;
      }
      s++;
    }
  }

  if (d.count > 0) {
    sl_cleol(STDOUT_FILENO);
    sl_mvcur(STDOUT_FILENO, start_row + d.cursor_row, d.cursor_col);
  }

  sl->ls.oldrows = (size_t)d.count;
  sl->ls.oldrpos = d.cursor_row;
}

static void sl_sigwinch_handler(int sig) { (void)sig; }

void sl_config_init(sl_config_t *config) {
  if (!config)
    return;
  config->screen_x = 0;
  config->screen_y = 0;
  config->screen_width = 0;
  config->screen_height = 0;
  config->prompt_symbol = NULL;
  config->indent_mode = SL_INDENT_SPACES;
}

sl_t *sl_create(void) {
  sl_config_t cfg;
  sl_config_init(&cfg);
  return sl_create_with_config(&cfg);
}

sl_t *sl_create_with_config(const sl_config_t *config) {
  sl_t *sl;
  sl = (sl_t *)calloc(1, sizeof(*sl));
  if (!sl)
    return NULL;
  if (config) {
    sl->screen_x = config->screen_x;
    sl->screen_y = config->screen_y;
    sl->screen_width = config->screen_width;
    sl->screen_height = config->screen_height;
    sl->indent_mode = config->indent_mode;
    if (config->prompt_symbol) {
      sl->prompt_symbol = strdup(config->prompt_symbol);
      if (!sl->prompt_symbol) {
        free(sl);
        return NULL;
      }
    }
  }
  sl->buflen_max = SL_BUF_INITIAL;
  sl->buf = (char *)malloc(sl->buflen_max);
  if (!sl->buf) {
    free(sl->prompt_symbol);
    free(sl);
    return NULL;
  }
  sl->buf[0] = '\0';
  sl->buflen = 0;
  sl->initialized = 0;
  return sl;
}

void sl_destroy(sl_t *sl) {
  if (!sl)
    return;
  sl_raw_stop(sl);
  free(sl->prompt_symbol);
  free(sl->buf);
  free(sl);
}

void sl_free(void *ptr) { free(ptr); }

int sl_history_add(sl_t *sl, const char *line) {
  (void)sl;
  return linenoiseHistoryAdd(line);
}

int sl_history_set_max_len(sl_t *sl, int max_len) {
  (void)sl;
  return linenoiseHistorySetMaxLen(max_len);
}

int sl_history_save(sl_t *sl, const char *filename) {
  (void)sl;
  return linenoiseHistorySave(filename);
}

int sl_history_load(sl_t *sl, const char *filename) {
  (void)sl;
  return linenoiseHistoryLoad(filename);
}

void sl_clear_screen(sl_t *sl) {
  (void)sl;
  sl_cls(STDOUT_FILENO);
}

int sl_set_bounds(sl_t *sl, int x, int y, int width, int height) {
  if (!sl)
    return -1;
  sl->screen_x = x;
  sl->screen_y = y;
  sl->screen_width = width;
  sl->screen_height = height;
  return 0;
}

int sl_set_prompt_symbol(sl_t *sl, const char *symbol) {
  char *dup;
  if (!sl)
    return -1;
  dup = symbol ? strdup(symbol) : NULL;
  free(sl->prompt_symbol);
  sl->prompt_symbol = dup;
  return 0;
}

int sl_set_indent_mode(sl_t *sl, int mode) {
  if (!sl)
    return -1;
  sl->indent_mode = mode;
  return 0;
}

int sl_on_resize(sl_t *sl, int cols, int rows) {
  int tw, th;
  if (!sl)
    return -1;
  sl_get_winsize(&tw, &th);
  if (cols > 0)
    sl->screen_width = cols;
  else
    sl->screen_width = tw;
  if (rows > 0)
    sl->screen_height = rows;
  else
    sl->screen_height = th;
  return 0;
}

char *sl_readline(sl_t *sl, const char *prompt) {
  size_t cur;
  char *r;
  int done;
  int srow;
  int tcols, trows;
  struct sigaction sa, osa;

  if (!sl)
    return NULL;
  if (sl_raw_init(sl) != 0)
    return NULL;

  sl->buflen = 0;
  sl->buf[0] = '\0';
  cur = 0;

  sl_get_winsize(&tcols, &trows);
  sl_mvcur(STDOUT_FILENO, trows - 1, 0);
  srow = 0;
  sl_render(sl, cur, prompt, srow);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sl_sigwinch_handler;
  sigaction(SIGWINCH, &sa, &osa);

  done = 0;
  while (!done) {
    int key;
    size_t ls, le;

    sl_get_winsize(&tcols, &trows);
    if (sl->screen_width <= 0)
      sl->screen_width = tcols;
    if (sl->screen_height <= 0)
      sl->screen_height = trows;

    key = sl_read_key(sl);

    ls = sl_line_start(sl->buf, cur);
    le = sl_line_end(sl->buf, sl->buflen, cur);

    switch (key) {
    case 3:
      sl->buflen = 0;
      sl->buf[0] = '\0';
      cur = 0;
      done = 1;
      break;

    case '\r':
    case '\n': {
      char nl = '\n';
      sl_buf_ins(sl, cur, &nl, 1);
      cur++;
      sl_render(sl, cur, prompt, srow);
      done = 1;
      break;
    }

    case SL_KEY_LEFT:
    case 2:
      if (cur > 0)
        cur--;
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_RIGHT:
    case 6:
      if (cur < sl->buflen)
        cur++;
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_UP:
    case 16:
      if (ls > 0) {
        cur = sl_prev_line(sl->buf, cur);
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_DOWN:
    case 14:
      if (le < sl->buflen) {
        cur = sl_next_line(sl->buf, sl->buflen, cur);
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_HOME:
    case 1:
      if (cur == ls) {
        cur = 0;
      } else {
        cur = ls;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_END:
    case 5:
      if (cur == le && le < sl->buflen) {
        cur = sl->buflen;
      } else {
        cur = le;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case 21:
      if (ls < cur) {
        sl_buf_del(sl, ls, cur - ls);
        cur = ls;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case 11:
      if (le > cur)
        sl_buf_del(sl, cur, le - cur);
      sl_render(sl, cur, prompt, srow);
      break;

    case 23:
      if (cur > 0) {
        size_t wp = sl_word_backward(sl->buf, cur);
        sl_buf_del(sl, wp, cur - wp);
        cur = wp;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case 127:
    case 8:
      if (cur > 0) {
        sl_buf_del(sl, cur - 1, 1);
        cur--;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_DELETE:
      if (cur < sl->buflen)
        sl_buf_del(sl, cur, 1);
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_ALT_B:
      if (cur > 0)
        cur = sl_word_backward(sl->buf, cur);
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_ALT_F:
      if (cur < sl->buflen) {
        cur = sl_word_forward(sl->buf, sl->buflen, cur);
        if (cur > sl->buflen)
          cur = sl->buflen;
      }
      sl_render(sl, cur, prompt, srow);
      break;

    case 4:
      if (sl->buflen == 0) {
        done = 1;
      } else if (cur < sl->buflen) {
        sl_buf_del(sl, cur, 1);
        sl_render(sl, cur, prompt, srow);
      }
      break;

    case 12:
      sl_cls(STDOUT_FILENO);
      sl_render(sl, cur, prompt, srow);
      break;

    case '\t':
      sl_buf_ins(sl, cur, "\t", 1);
      cur++;
      sl_render(sl, cur, prompt, srow);
      break;

    case SL_KEY_ESCAPE:
    case SL_KEY_NONE:
    case SL_KEY_UNKNOWN:
      break;

    default:
      if (key >= 32 && key < 127) {
        char c = (char)key;
        sl_buf_ins(sl, cur, &c, 1);
        cur++;
        sl_render(sl, cur, prompt, srow);
      }
      break;
    }
  }

  sigaction(SIGWINCH, &osa, NULL);

  sl_raw_stop(sl);

  if (sl->buflen > 0 && sl->buf[sl->buflen - 1] == '\n')
    sl->buflen--;

  r = strdup(sl->buf ? sl->buf : "");
  sl->buflen = 0;
  if (sl->buf)
    sl->buf[0] = '\0';
  return r;
}