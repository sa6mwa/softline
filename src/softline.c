#include "softline_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/stat.h>

typedef struct sl_row {
  char *text;
  size_t len;
  size_t start;
  size_t end;
  int base_col;
  int cols;
} sl_row_t;

typedef struct sl_render {
  sl_row_t *rows;
  int cap;
  int count;
  int cursor_row;
  int cursor_col;
} sl_render_t;

static size_t sl_utf8_clamp_cluster_boundary(const char *buf, size_t len,
                                             size_t pos);

static sl_impl_t *sl_impl(sl_t *self) {
  if (!self)
    return NULL;
  return (sl_impl_t *)self->impl;
}

static const sl_impl_t *sl_impl_const(const sl_t *self) {
  if (!self)
    return NULL;
  return (const sl_impl_t *)self->impl;
}

static void sl_set_error(sl_t *self, const char *message) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl)
    return;
  if (!message)
    message = "";
  strncpy(impl->error, message, sizeof(impl->error) - 1);
  impl->error[sizeof(impl->error) - 1] = '\0';
}

static void sl_set_readline_status(sl_t *self, sl_readline_status_t status) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (impl)
    impl->last_readline_status = status;
}

static char *sl_strdup(const char *s) {
  size_t n;
  char *copy;
  if (!s)
    s = "";
  n = strlen(s) + 1;
  copy = (char *)malloc(n);
  if (copy)
    memcpy(copy, s, n);
  return copy;
}

static int sl_write_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n;
    n = write(fd, buf, len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      return -1;
    buf += n;
    len -= (size_t)n;
  }
  return 0;
}

static int sl_wstr(int fd, const char *s) {
  if (!s)
    return 0;
  return sl_write_all(fd, s, strlen(s));
}

static int sl_wchar(int fd, char c) { return sl_write_all(fd, &c, 1); }

static int sl_write_cursor_up(int fd, int rows) {
  char seq[32];
  int n;
  if (rows <= 0)
    return 0;
  n = snprintf(seq, sizeof(seq), "\033[%dA", rows);
  if (n <= 0 || n >= (int)sizeof(seq))
    return -1;
  return sl_write_all(fd, seq, (size_t)n);
}

static int sl_write_cursor_down(int fd, int rows) {
  char seq[32];
  int n;
  if (rows <= 0)
    return 0;
  n = snprintf(seq, sizeof(seq), "\033[%dB", rows);
  if (n <= 0 || n >= (int)sizeof(seq))
    return -1;
  return sl_write_all(fd, seq, (size_t)n);
}

static int sl_write_cursor_forward(int fd, int cols) {
  char seq[32];
  int n;
  if (cols <= 0)
    return 0;
  n = snprintf(seq, sizeof(seq), "\033[%dC", cols);
  if (n <= 0 || n >= (int)sizeof(seq))
    return -1;
  return sl_write_all(fd, seq, (size_t)n);
}

static int sl_write_cursor_pos(int fd, int row, int col) {
  char seq[48];
  int n;
  if (row < 0)
    row = 0;
  if (col < 0)
    col = 0;
  n = snprintf(seq, sizeof(seq), "\033[%d;%dH", row + 1, col + 1);
  if (n <= 0 || n >= (int)sizeof(seq))
    return -1;
  return sl_write_all(fd, seq, (size_t)n);
}

static int sl_set_scroll_region(int fd, int top, int bottom) {
  char seq[48];
  int n;
  if (top < 0)
    top = 0;
  if (bottom < top)
    bottom = top;
  n = snprintf(seq, sizeof(seq), "\033[%d;%dr", top + 1, bottom + 1);
  if (n <= 0 || n >= (int)sizeof(seq))
    return -1;
  return sl_write_all(fd, seq, (size_t)n);
}

static int sl_reset_scroll_region(int fd) { return sl_wstr(fd, "\033[r"); }

static int sl_scroll_region_up(int fd, int top, int bottom, int rows) {
  int i;
  if (rows <= 0 || bottom < top)
    return 0;
  if (sl_set_scroll_region(fd, top, bottom) != 0 ||
      sl_write_cursor_pos(fd, bottom, 0) != 0)
    return -1;
  for (i = 0; i < rows; i++) {
    if (sl_wstr(fd, "\r\n") != 0) {
      (void)sl_reset_scroll_region(fd);
      return -1;
    }
  }
  return sl_reset_scroll_region(fd);
}

static int sl_write_spaces(int fd, int cols) {
  while (cols-- > 0) {
    if (sl_wchar(fd, ' ') != 0)
      return -1;
  }
  return 0;
}

static int sl_enable_bracketed_paste(sl_impl_t *impl) {
  return sl_wstr(impl->output_fd, "\033[?2004h");
}

static void sl_disable_bracketed_paste(sl_impl_t *impl) {
  (void)sl_wstr(impl->output_fd, "\033[?2004l");
}

static int sl_terminal_columns(sl_impl_t *impl) {
  struct winsize ws;
  if (ioctl(impl->output_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return (int)ws.ws_col;
  if (!impl->dynamic_width && impl->screen_width > 0)
    return impl->screen_x + impl->screen_width;
  return 80;
}

static int sl_terminal_width(sl_impl_t *impl) {
  int width;
  if (!impl->dynamic_width && impl->screen_width > 0)
    return impl->screen_width;
  width = sl_terminal_columns(impl);
  if (impl->bounded && impl->dynamic_width)
    width -= impl->screen_x;
  return width > 0 ? width : 1;
}

static int sl_terminal_height(sl_impl_t *impl) {
  struct winsize ws;
  int height;
  if (!impl->dynamic_height && impl->screen_height > 0)
    return impl->screen_height;
  if (ioctl(impl->output_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
    height = (int)ws.ws_row;
  else
    height = 24;
  if (impl->bounded && impl->dynamic_height)
    height -= impl->screen_y;
  return height > 0 ? height : 1;
}

static int sl_bounded_mode(sl_impl_t *impl) { return impl->bounded; }

static int sl_box_width(sl_impl_t *impl) {
  int width;
  width = sl_terminal_width(impl);
  return width > 0 ? width : 1;
}

static int sl_bounded_scroll_spans_full_width(sl_impl_t *impl) {
  if (!impl || impl->screen_x != 0)
    return 0;
  return sl_box_width(impl) >= sl_terminal_columns(impl);
}

static int sl_clear_box_tail(sl_impl_t *impl, int from_col) {
  int remaining;
  remaining = sl_box_width(impl) - from_col;
  if (remaining <= 0)
    return 0;
  return sl_write_spaces(impl->output_fd, remaining);
}

static int sl_box_bottom(sl_impl_t *impl) {
  return impl->screen_y + sl_terminal_height(impl) - 1;
}

static int sl_prompt_top(sl_impl_t *impl, int prompt_rows) {
  int top;
  int height;
  height = sl_terminal_height(impl);
  if (prompt_rows < 1)
    prompt_rows = 1;
  if (prompt_rows > height)
    prompt_rows = height;
  top = sl_box_bottom(impl) - prompt_rows + 1;
  if (top < impl->screen_y)
    top = impl->screen_y;
  return top;
}

static int sl_enable_raw(sl_t *self) {
  sl_impl_t *impl;
  struct termios raw;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  if (impl->raw_active)
    return 0;
  if (!isatty(impl->input_fd)) {
    sl_set_error(self, "input file descriptor is not a terminal");
    errno = ENOTTY;
    return -1;
  }
  if (tcgetattr(impl->input_fd, &impl->original_termios) != 0) {
    sl_set_error(self, "failed to read terminal attributes");
    return -1;
  }
  raw = impl->original_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(impl->input_fd, TCSAFLUSH, &raw) != 0) {
    sl_set_error(self, "failed to enable terminal raw mode");
    return -1;
  }
  impl->raw_active = 1;
  return 0;
}

static void sl_disable_raw(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || !impl->raw_active)
    return;
  (void)tcsetattr(impl->input_fd, TCSAFLUSH, &impl->original_termios);
  impl->raw_active = 0;
}

static int sl_buf_reserve(sl_t *self, size_t need) {
  sl_impl_t *impl;
  size_t cap;
  char *next;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  if (need > impl->line_max_len + 1) {
    sl_set_error(self, "line exceeds configured maximum length");
    return -1;
  }
  if (need <= impl->cap)
    return 0;
  cap = impl->cap ? impl->cap : SL_BUF_INITIAL;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      sl_set_error(self, "line buffer is too large");
      return -1;
    }
    cap *= 2;
  }
  next = (char *)realloc(impl->buf, cap);
  if (!next) {
    sl_set_error(self, "out of memory while growing line buffer");
    return -1;
  }
  impl->buf = next;
  impl->cap = cap;
  return 0;
}

static int sl_buf_set(sl_t *self, const char *s) {
  sl_impl_t *impl;
  size_t n;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  n = s ? strlen(s) : 0;
  if (sl_buf_reserve(self, n + 1) != 0)
    return -1;
  if (n > 0)
    memcpy(impl->buf, s, n);
  impl->buf[n] = '\0';
  impl->len = n;
  impl->cursor = n;
  return 0;
}

static int sl_buf_insert(sl_t *self, size_t at, const char *s, size_t n) {
  sl_impl_t *impl;
  size_t tail;
  impl = sl_impl(self);
  if (!impl || !s)
    return -1;
  if (at > impl->len)
    at = impl->len;
  if (n == 0)
    return 0;
  if (sl_buf_reserve(self, impl->len + n + 1) != 0)
    return -1;
  tail = impl->len - at;
  memmove(impl->buf + at + n, impl->buf + at, tail);
  memcpy(impl->buf + at, s, n);
  impl->len += n;
  impl->buf[impl->len] = '\0';
  return 0;
}

static void sl_buf_delete(sl_t *self, size_t at, size_t n) {
  sl_impl_t *impl;
  size_t tail;
  impl = sl_impl(self);
  if (!impl || at >= impl->len || n == 0)
    return;
  if (at + n > impl->len)
    n = impl->len - at;
  tail = impl->len - at - n;
  memmove(impl->buf + at, impl->buf + at + n, tail);
  impl->len -= n;
  impl->buf[impl->len] = '\0';
}

static int sl_buf_insert_cstr(sl_t *self, const char *text) {
  sl_impl_t *impl;
  size_t n;
  impl = sl_impl(self);
  if (!impl || !text)
    return SL_ERROR_INVALID;
  n = strlen(text);
  if (sl_buf_insert(self, impl->cursor, text, n) != 0)
    return SL_ERROR;
  impl->cursor += n;
  return SL_OK;
}

static int sl_buf_set_public(sl_t *self, const char *text) {
  if (!self || !text)
    return SL_ERROR_INVALID;
  if (sl_buf_set(self, text) != 0)
    return SL_ERROR;
  return SL_OK;
}

static const char *sl_buffer_method(const sl_t *self) {
  const sl_impl_t *impl;
  impl = sl_impl_const(self);
  if (!impl)
    return NULL;
  return impl->buf ? impl->buf : "";
}

static size_t sl_cursor_method(const sl_t *self) {
  const sl_impl_t *impl;
  impl = sl_impl_const(self);
  if (!impl)
    return 0;
  return impl->cursor;
}

static int sl_set_cursor_method(sl_t *self, size_t cursor) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl)
    return SL_ERROR_INVALID;
  if (cursor > impl->len)
    cursor = impl->len;
  impl->cursor = sl_utf8_clamp_cluster_boundary(impl->buf, impl->len, cursor);
  return SL_OK;
}

static int sl_submit_method(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || !impl->active_readline)
    return SL_ERROR_INVALID;
  impl->request_submit = 1;
  return SL_OK;
}

static int sl_cancel_method(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || !impl->active_readline)
    return SL_ERROR_INVALID;
  impl->request_cancel = 1;
  return SL_OK;
}

static size_t sl_word_forward(const char *buf, size_t len, size_t pos) {
  while (pos < len && !isspace((unsigned char)buf[pos]))
    pos++;
  while (pos < len && isspace((unsigned char)buf[pos]))
    pos++;
  return pos;
}

static size_t sl_word_backward(const char *buf, size_t pos) {
  if (pos == 0)
    return 0;
  pos--;
  while (pos > 0 && isspace((unsigned char)buf[pos]))
    pos--;
  while (pos > 0 && !isspace((unsigned char)buf[pos - 1]))
    pos--;
  return pos;
}

static size_t sl_utf8_prev_len(const char *buf, size_t pos) {
  size_t start;
  if (pos == 0)
    return 0;
  start = pos - 1;
  while (start > 0 && (((unsigned char)buf[start] & 0xc0) == 0x80))
    start--;
  return pos - start;
}

static size_t sl_utf8_decode(const char *buf, size_t len, size_t pos,
                             unsigned long *codepoint) {
  unsigned char c;
  size_t n;
  size_t i;
  unsigned long cp;
  if (codepoint)
    *codepoint = 0xfffdUL;
  if (pos >= len)
    return 0;
  c = (unsigned char)buf[pos];
  if ((c & 0x80) == 0) {
    if (codepoint)
      *codepoint = c;
    return 1;
  }
  if ((c & 0xe0) == 0xc0) {
    n = 2;
    cp = (unsigned long)(c & 0x1f);
  } else if ((c & 0xf0) == 0xe0) {
    n = 3;
    cp = (unsigned long)(c & 0x0f);
  } else if ((c & 0xf8) == 0xf0) {
    n = 4;
    cp = (unsigned long)(c & 0x07);
  } else {
    return 1;
  }
  if (pos + n > len)
    return 1;
  for (i = 1; i < n; i++) {
    c = (unsigned char)buf[pos + i];
    if ((c & 0xc0) != 0x80)
      return 1;
    cp = (cp << 6) | (unsigned long)(c & 0x3f);
  }
  if ((n == 2 && cp < 0x80UL) || (n == 3 && cp < 0x800UL) ||
      (n == 4 && cp < 0x10000UL) || cp > 0x10ffffUL ||
      (cp >= 0xd800UL && cp <= 0xdfffUL))
    return 1;
  if (codepoint)
    *codepoint = cp;
  return n;
}

static int sl_codepoint_is_combining(unsigned long cp) {
  return (cp >= 0x0300UL && cp <= 0x036fUL) ||
         (cp >= 0x0483UL && cp <= 0x0489UL) ||
         (cp >= 0x0591UL && cp <= 0x05bdUL) || cp == 0x05bfUL ||
         (cp >= 0x05c1UL && cp <= 0x05c2UL) ||
         (cp >= 0x05c4UL && cp <= 0x05c5UL) || cp == 0x05c7UL ||
         (cp >= 0x0610UL && cp <= 0x061aUL) ||
         (cp >= 0x064bUL && cp <= 0x065fUL) || cp == 0x0670UL ||
         (cp >= 0x06d6UL && cp <= 0x06dcUL) ||
         (cp >= 0x06dfUL && cp <= 0x06e4UL) ||
         (cp >= 0x06e7UL && cp <= 0x06e8UL) ||
         (cp >= 0x06eaUL && cp <= 0x06edUL) ||
         (cp >= 0x0711UL && cp <= 0x0711UL) ||
         (cp >= 0x0730UL && cp <= 0x074aUL) ||
         (cp >= 0x07a6UL && cp <= 0x07b0UL) ||
         (cp >= 0x07ebUL && cp <= 0x07f3UL) ||
         (cp >= 0x0816UL && cp <= 0x0819UL) ||
         (cp >= 0x081bUL && cp <= 0x0823UL) ||
         (cp >= 0x0825UL && cp <= 0x0827UL) ||
         (cp >= 0x0829UL && cp <= 0x082dUL) ||
         (cp >= 0x0859UL && cp <= 0x085bUL) ||
         (cp >= 0x08d3UL && cp <= 0x08e1UL) ||
         (cp >= 0x08e3UL && cp <= 0x0902UL) || cp == 0x093aUL ||
         cp == 0x093cUL || (cp >= 0x0941UL && cp <= 0x0948UL) ||
         cp == 0x094dUL || (cp >= 0x0951UL && cp <= 0x0957UL) ||
         (cp >= 0x0962UL && cp <= 0x0963UL) ||
         (cp >= 0x0981UL && cp <= 0x0981UL) || cp == 0x09bcUL ||
         (cp >= 0x09c1UL && cp <= 0x09c4UL) || cp == 0x09cdUL ||
         (cp >= 0x09e2UL && cp <= 0x09e3UL) ||
         (cp >= 0x0a01UL && cp <= 0x0a02UL) || cp == 0x0a3cUL ||
         (cp >= 0x0a41UL && cp <= 0x0a42UL) ||
         (cp >= 0x0a47UL && cp <= 0x0a48UL) ||
         (cp >= 0x0a4bUL && cp <= 0x0a4dUL) || cp == 0x0a51UL ||
         (cp >= 0x0a70UL && cp <= 0x0a71UL) || cp == 0x0a75UL ||
         (cp >= 0x0a81UL && cp <= 0x0a82UL) || cp == 0x0abcUL ||
         (cp >= 0x0ac1UL && cp <= 0x0ac5UL) ||
         (cp >= 0x0ac7UL && cp <= 0x0ac8UL) || cp == 0x0acdUL ||
         (cp >= 0x0ae2UL && cp <= 0x0ae3UL) || cp == 0x0b01UL ||
         cp == 0x0b3cUL || (cp >= 0x0b3fUL && cp <= 0x0b3fUL) ||
         (cp >= 0x0b41UL && cp <= 0x0b44UL) || cp == 0x0b4dUL ||
         (cp >= 0x0b56UL && cp <= 0x0b56UL) ||
         (cp >= 0x0b62UL && cp <= 0x0b63UL) || cp == 0x0b82UL ||
         cp == 0x0bc0UL || cp == 0x0bcdUL || cp == 0x0c00UL ||
         (cp >= 0x0c3eUL && cp <= 0x0c40UL) ||
         (cp >= 0x0c46UL && cp <= 0x0c48UL) ||
         (cp >= 0x0c4aUL && cp <= 0x0c4dUL) ||
         (cp >= 0x0c55UL && cp <= 0x0c56UL) ||
         (cp >= 0x0c62UL && cp <= 0x0c63UL) || cp == 0x0c81UL ||
         cp == 0x0cbcUL || (cp >= 0x0cbfUL && cp <= 0x0cbfUL) ||
         cp == 0x0cc6UL || (cp >= 0x0cccUL && cp <= 0x0ccdUL) ||
         (cp >= 0x0ce2UL && cp <= 0x0ce3UL) ||
         (cp >= 0x0d00UL && cp <= 0x0d01UL) ||
         (cp >= 0x0d41UL && cp <= 0x0d44UL) || cp == 0x0d4dUL ||
         (cp >= 0x0d62UL && cp <= 0x0d63UL) || cp == 0x0dcaUL ||
         (cp >= 0x0dd2UL && cp <= 0x0dd4UL) || cp == 0x0dd6UL ||
         cp == 0x0e31UL || (cp >= 0x0e34UL && cp <= 0x0e3aUL) ||
         (cp >= 0x0e47UL && cp <= 0x0e4eUL) || cp == 0x0eb1UL ||
         (cp >= 0x0eb4UL && cp <= 0x0eb9UL) ||
         (cp >= 0x0ebbUL && cp <= 0x0ebcUL) ||
         (cp >= 0x0ec8UL && cp <= 0x0ecdUL) ||
         (cp >= 0x0f18UL && cp <= 0x0f19UL) || cp == 0x0f35UL ||
         cp == 0x0f37UL || cp == 0x0f39UL ||
         (cp >= 0x0f71UL && cp <= 0x0f7eUL) ||
         (cp >= 0x0f80UL && cp <= 0x0f84UL) ||
         (cp >= 0x0f86UL && cp <= 0x0f87UL) ||
         (cp >= 0x0f8dUL && cp <= 0x0f97UL) ||
         (cp >= 0x0f99UL && cp <= 0x0fbcUL) || cp == 0x0fc6UL ||
         (cp >= 0x102dUL && cp <= 0x1030UL) ||
         (cp >= 0x1032UL && cp <= 0x1037UL) ||
         (cp >= 0x1039UL && cp <= 0x103aUL) ||
         (cp >= 0x103dUL && cp <= 0x103eUL) ||
         (cp >= 0x1058UL && cp <= 0x1059UL) ||
         (cp >= 0x105eUL && cp <= 0x1060UL) ||
         (cp >= 0x1071UL && cp <= 0x1074UL) || cp == 0x1082UL ||
         (cp >= 0x1085UL && cp <= 0x1086UL) || cp == 0x108dUL ||
         cp == 0x109dUL || (cp >= 0x135dUL && cp <= 0x135fUL) ||
         (cp >= 0x1712UL && cp <= 0x1714UL) ||
         (cp >= 0x1732UL && cp <= 0x1734UL) ||
         (cp >= 0x1752UL && cp <= 0x1753UL) ||
         (cp >= 0x1772UL && cp <= 0x1773UL) ||
         (cp >= 0x17b4UL && cp <= 0x17b5UL) ||
         (cp >= 0x17b7UL && cp <= 0x17bdUL) || cp == 0x17c6UL ||
         (cp >= 0x17c9UL && cp <= 0x17d3UL) || cp == 0x17ddUL ||
         (cp >= 0x180bUL && cp <= 0x180dUL) || cp == 0x1885UL ||
         cp == 0x1886UL || cp == 0x18a9UL ||
         (cp >= 0x1920UL && cp <= 0x1922UL) ||
         (cp >= 0x1927UL && cp <= 0x1928UL) || cp == 0x1932UL ||
         (cp >= 0x1939UL && cp <= 0x193bUL) ||
         (cp >= 0x1a17UL && cp <= 0x1a18UL) ||
         (cp >= 0x1a1bUL && cp <= 0x1a1bUL) || cp == 0x1a56UL ||
         (cp >= 0x1a58UL && cp <= 0x1a5eUL) || cp == 0x1a60UL ||
         cp == 0x1a62UL || (cp >= 0x1a65UL && cp <= 0x1a6cUL) ||
         (cp >= 0x1a73UL && cp <= 0x1a7cUL) || cp == 0x1a7fUL ||
         (cp >= 0x1ab0UL && cp <= 0x1affUL) ||
         (cp >= 0x1b00UL && cp <= 0x1b03UL) || cp == 0x1b34UL ||
         (cp >= 0x1b36UL && cp <= 0x1b3aUL) || cp == 0x1b3cUL ||
         cp == 0x1b42UL || (cp >= 0x1b6bUL && cp <= 0x1b73UL) ||
         (cp >= 0x1b80UL && cp <= 0x1b81UL) ||
         (cp >= 0x1ba2UL && cp <= 0x1ba5UL) ||
         (cp >= 0x1ba8UL && cp <= 0x1ba9UL) ||
         (cp >= 0x1babUL && cp <= 0x1badUL) || cp == 0x1be6UL ||
         (cp >= 0x1be8UL && cp <= 0x1be9UL) || cp == 0x1bedUL ||
         (cp >= 0x1befUL && cp <= 0x1bf1UL) ||
         (cp >= 0x1c2cUL && cp <= 0x1c33UL) ||
         (cp >= 0x1c36UL && cp <= 0x1c37UL) ||
         (cp >= 0x1cd0UL && cp <= 0x1cd2UL) ||
         (cp >= 0x1cd4UL && cp <= 0x1ce0UL) ||
         (cp >= 0x1ce2UL && cp <= 0x1ce8UL) || cp == 0x1cedUL ||
         cp == 0x1cf4UL || (cp >= 0x1cf8UL && cp <= 0x1cf9UL) ||
         (cp >= 0x1dc0UL && cp <= 0x1dffUL) ||
         (cp >= 0x20d0UL && cp <= 0x20ffUL) ||
         (cp >= 0x2cefUL && cp <= 0x2cf1UL) || cp == 0x2d7fUL ||
         (cp >= 0x2de0UL && cp <= 0x2dffUL) ||
         (cp >= 0x302aUL && cp <= 0x302fUL) ||
         (cp >= 0x3099UL && cp <= 0x309aUL) ||
         (cp >= 0xa66fUL && cp <= 0xa672UL) ||
         (cp >= 0xa674UL && cp <= 0xa67dUL) ||
         (cp >= 0xa69eUL && cp <= 0xa69fUL) ||
         (cp >= 0xa6f0UL && cp <= 0xa6f1UL) || cp == 0xa802UL ||
         cp == 0xa806UL || cp == 0xa80bUL ||
         (cp >= 0xa825UL && cp <= 0xa826UL) || cp == 0xa8c4UL ||
         (cp >= 0xa8e0UL && cp <= 0xa8f1UL) ||
         (cp >= 0xa926UL && cp <= 0xa92dUL) ||
         (cp >= 0xa947UL && cp <= 0xa951UL) ||
         (cp >= 0xa980UL && cp <= 0xa982UL) || cp == 0xa9b3UL ||
         (cp >= 0xa9b6UL && cp <= 0xa9b9UL) ||
         (cp >= 0xa9bcUL && cp <= 0xa9bdUL) ||
         (cp >= 0xa9e5UL && cp <= 0xa9e5UL) ||
         (cp >= 0xaa29UL && cp <= 0xaa2eUL) ||
         (cp >= 0xaa31UL && cp <= 0xaa32UL) ||
         (cp >= 0xaa35UL && cp <= 0xaa36UL) || cp == 0xaa43UL ||
         cp == 0xaa4cUL || cp == 0xaa7cUL || cp == 0xaab0UL ||
         (cp >= 0xaab2UL && cp <= 0xaab4UL) ||
         (cp >= 0xaab7UL && cp <= 0xaab8UL) ||
         (cp >= 0xaabeUL && cp <= 0xaabfUL) || cp == 0xaac1UL ||
         (cp >= 0xaaecUL && cp <= 0xaaedUL) || cp == 0xaaf6UL ||
         cp == 0xabe5UL || cp == 0xabe8UL || cp == 0xabedUL ||
         (cp >= 0xfb1eUL && cp <= 0xfb1eUL) ||
         (cp >= 0xfe00UL && cp <= 0xfe0fUL) ||
         (cp >= 0xfe20UL && cp <= 0xfe2fUL) ||
         (cp >= 0x101fdUL && cp <= 0x101fdUL) ||
         (cp >= 0x102e0UL && cp <= 0x102e0UL) ||
         (cp >= 0x10376UL && cp <= 0x1037aUL) ||
         (cp >= 0x10a01UL && cp <= 0x10a03UL) ||
         (cp >= 0x10a05UL && cp <= 0x10a06UL) ||
         (cp >= 0x10a0cUL && cp <= 0x10a0fUL) ||
         (cp >= 0x10a38UL && cp <= 0x10a3aUL) || cp == 0x10a3fUL ||
         (cp >= 0x10ae5UL && cp <= 0x10ae6UL) ||
         (cp >= 0x10d24UL && cp <= 0x10d27UL) ||
         (cp >= 0x10f46UL && cp <= 0x10f50UL) ||
         (cp >= 0x11001UL && cp <= 0x11001UL) ||
         (cp >= 0x11038UL && cp <= 0x11046UL) ||
         (cp >= 0x1107fUL && cp <= 0x11081UL) ||
         (cp >= 0x110b3UL && cp <= 0x110b6UL) ||
         (cp >= 0x110b9UL && cp <= 0x110baUL) ||
         (cp >= 0x11100UL && cp <= 0x11102UL) ||
         (cp >= 0x11127UL && cp <= 0x1112bUL) ||
         (cp >= 0x1112dUL && cp <= 0x11134UL) ||
         (cp >= 0x11173UL && cp <= 0x11173UL) ||
         (cp >= 0x11180UL && cp <= 0x11181UL) ||
         (cp >= 0x111b6UL && cp <= 0x111beUL) ||
         (cp >= 0x111c9UL && cp <= 0x111ccUL) || cp == 0x1122fUL ||
         (cp >= 0x11231UL && cp <= 0x11234UL) || cp == 0x11236UL ||
         cp == 0x1123eUL || cp == 0x112dfUL ||
         (cp >= 0x112e3UL && cp <= 0x112eaUL) ||
         (cp >= 0x11300UL && cp <= 0x11301UL) ||
         (cp >= 0x1133bUL && cp <= 0x1133cUL) || cp == 0x11340UL ||
         cp == 0x11366UL || cp == 0x11367UL ||
         (cp >= 0x11370UL && cp <= 0x11374UL) ||
         (cp >= 0x11438UL && cp <= 0x1143fUL) ||
         (cp >= 0x11442UL && cp <= 0x11444UL) || cp == 0x11446UL ||
         (cp >= 0x1145eUL && cp <= 0x1145eUL) ||
         (cp >= 0x114b3UL && cp <= 0x114b8UL) || cp == 0x114baUL ||
         (cp >= 0x114bfUL && cp <= 0x114c0UL) || cp == 0x114c2UL ||
         cp == 0x114c3UL || (cp >= 0x115b2UL && cp <= 0x115b5UL) ||
         (cp >= 0x115bcUL && cp <= 0x115bdUL) || cp == 0x115bfUL ||
         cp == 0x115c0UL || (cp >= 0x115dcUL && cp <= 0x115ddUL) ||
         (cp >= 0x11633UL && cp <= 0x1163aUL) || cp == 0x1163dUL ||
         (cp >= 0x1163fUL && cp <= 0x11640UL) ||
         (cp >= 0x116abUL && cp <= 0x116abUL) || cp == 0x116adUL ||
         (cp >= 0x116b0UL && cp <= 0x116b5UL) || cp == 0x116b7UL ||
         (cp >= 0x1171dUL && cp <= 0x1171fUL) ||
         (cp >= 0x11722UL && cp <= 0x11725UL) ||
         (cp >= 0x11727UL && cp <= 0x1172bUL) ||
         (cp >= 0x1182fUL && cp <= 0x11837UL) ||
         (cp >= 0x11839UL && cp <= 0x1183aUL) ||
         (cp >= 0x1193bUL && cp <= 0x1193cUL) || cp == 0x1193eUL ||
         cp == 0x11943UL || (cp >= 0x119d4UL && cp <= 0x119d7UL) ||
         cp == 0x119daUL || cp == 0x119dbUL || cp == 0x119e0UL ||
         (cp >= 0x11a01UL && cp <= 0x11a0aUL) ||
         (cp >= 0x11a33UL && cp <= 0x11a38UL) ||
         (cp >= 0x11a3bUL && cp <= 0x11a3eUL) || cp == 0x11a47UL ||
         (cp >= 0x11a51UL && cp <= 0x11a56UL) ||
         (cp >= 0x11a59UL && cp <= 0x11a5bUL) ||
         (cp >= 0x11a8aUL && cp <= 0x11a96UL) ||
         (cp >= 0x11a98UL && cp <= 0x11a99UL) ||
         (cp >= 0x11c30UL && cp <= 0x11c36UL) ||
         (cp >= 0x11c38UL && cp <= 0x11c3dUL) ||
         (cp >= 0x11c3fUL && cp <= 0x11c3fUL) ||
         (cp >= 0x11c92UL && cp <= 0x11ca7UL) ||
         (cp >= 0x11caaUL && cp <= 0x11cb0UL) ||
         (cp >= 0x11cb2UL && cp <= 0x11cb3UL) ||
         (cp >= 0x11cb5UL && cp <= 0x11cb6UL) ||
         (cp >= 0x11d31UL && cp <= 0x11d36UL) || cp == 0x11d3aUL ||
         (cp >= 0x11d3cUL && cp <= 0x11d3dUL) ||
         (cp >= 0x11d3fUL && cp <= 0x11d45UL) || cp == 0x11d47UL ||
         (cp >= 0x11d90UL && cp <= 0x11d91UL) || cp == 0x11d95UL ||
         cp == 0x11d97UL || (cp >= 0x11ef3UL && cp <= 0x11ef4UL) ||
         (cp >= 0x16af0UL && cp <= 0x16af4UL) ||
         (cp >= 0x16b30UL && cp <= 0x16b36UL) ||
         (cp >= 0x16f4fUL && cp <= 0x16f4fUL) ||
         (cp >= 0x16f8fUL && cp <= 0x16f92UL) ||
         (cp >= 0x1bc9dUL && cp <= 0x1bc9eUL) ||
         (cp >= 0x1d167UL && cp <= 0x1d169UL) ||
         (cp >= 0x1d17bUL && cp <= 0x1d182UL) ||
         (cp >= 0x1d185UL && cp <= 0x1d18bUL) ||
         (cp >= 0x1d1aaUL && cp <= 0x1d1adUL) ||
         (cp >= 0x1d242UL && cp <= 0x1d244UL) ||
         (cp >= 0x1da00UL && cp <= 0x1da36UL) ||
         (cp >= 0x1da3bUL && cp <= 0x1da6cUL) ||
         (cp >= 0x1da75UL && cp <= 0x1da75UL) ||
         (cp >= 0x1da84UL && cp <= 0x1da84UL) ||
         (cp >= 0x1da9bUL && cp <= 0x1da9fUL) ||
         (cp >= 0x1daa1UL && cp <= 0x1daafUL) ||
         (cp >= 0x1e000UL && cp <= 0x1e006UL) ||
         (cp >= 0x1e008UL && cp <= 0x1e018UL) ||
         (cp >= 0x1e01bUL && cp <= 0x1e021UL) ||
         (cp >= 0x1e023UL && cp <= 0x1e024UL) ||
         (cp >= 0x1e026UL && cp <= 0x1e02aUL) ||
         (cp >= 0x1e130UL && cp <= 0x1e136UL) ||
         (cp >= 0x1e2ecUL && cp <= 0x1e2efUL) ||
         (cp >= 0x1e8d0UL && cp <= 0x1e8d6UL) ||
         (cp >= 0x1e944UL && cp <= 0x1e94aUL) ||
         (cp >= 0xe0100UL && cp <= 0xe01efUL);
}

static int sl_codepoint_is_wide(unsigned long cp) {
  return (cp >= 0x1100UL &&
          (cp <= 0x115fUL || cp == 0x2329UL || cp == 0x232aUL ||
           (cp >= 0x2e80UL && cp <= 0xa4cfUL && cp != 0x303fUL) ||
           (cp >= 0xac00UL && cp <= 0xd7a3UL) ||
           (cp >= 0xf900UL && cp <= 0xfaffUL) ||
           (cp >= 0xfe10UL && cp <= 0xfe19UL) ||
           (cp >= 0xfe30UL && cp <= 0xfe6fUL) ||
           (cp >= 0xff00UL && cp <= 0xff60UL) ||
           (cp >= 0xffe0UL && cp <= 0xffe6UL) ||
           (cp >= 0x20000UL && cp <= 0x3fffdUL))) ||
         (cp >= 0x1f000UL && cp <= 0x1f9ffUL) ||
         (cp >= 0x1fa70UL && cp <= 0x1faffUL);
}

static int sl_codepoint_width(unsigned long cp) {
  if (cp == 0)
    return 0;
  if (cp < 32UL || (cp >= 0x7fUL && cp < 0xa0UL))
    return 0;
  if (cp < 0x300UL)
    return 1;
  if (sl_codepoint_is_combining(cp))
    return 0;
  return sl_codepoint_is_wide(cp) ? 2 : 1;
}

static int sl_codepoint_is_variation(unsigned long cp) {
  return (cp >= 0xfe00UL && cp <= 0xfe0fUL) ||
         (cp >= 0xe0100UL && cp <= 0xe01efUL);
}

static int sl_codepoint_is_regional_indicator(unsigned long cp) {
  return cp >= 0x1f1e6UL && cp <= 0x1f1ffUL;
}

static size_t sl_utf8_cluster_len_width(const char *buf, size_t len, size_t pos,
                                        int *width) {
  size_t n;
  size_t step;
  unsigned long cp;
  int w;
  int emoji_sequence;
  n = sl_utf8_decode(buf, len, pos, &cp);
  if (n == 0) {
    if (width)
      *width = 0;
    return 0;
  }
  w = sl_codepoint_width(cp);
  emoji_sequence = sl_codepoint_is_wide(cp);
  if (sl_codepoint_is_regional_indicator(cp)) {
    step = sl_utf8_decode(buf, len, pos + n, &cp);
    if (step > 0 && sl_codepoint_is_regional_indicator(cp)) {
      n += step;
      w = 2;
    }
  }
  for (;;) {
    if (pos + n >= len)
      break;
    step = sl_utf8_decode(buf, len, pos + n, &cp);
    if (step == 0)
      break;
    if (cp < 0x300UL && cp != 0x200dUL && cp != 0x20e3UL)
      break;
    if (sl_codepoint_is_combining(cp) || sl_codepoint_is_variation(cp)) {
      if (cp == 0xfe0fUL)
        emoji_sequence = 1;
      n += step;
      continue;
    }
    if (cp == 0x200dUL) {
      n += step;
      if (pos + n >= len)
        break;
      step = sl_utf8_decode(buf, len, pos + n, &cp);
      if (step == 0)
        break;
      n += step;
      emoji_sequence = 1;
      continue;
    }
    if (cp == 0x20e3UL) {
      n += step;
      emoji_sequence = 1;
      continue;
    }
    break;
  }
  if (emoji_sequence && w < 2)
    w = 2;
  if (width)
    *width = w;
  return n;
}

static size_t sl_utf8_next_cluster_len(const char *buf, size_t len,
                                       size_t pos) {
  return sl_utf8_cluster_len_width(buf, len, pos, NULL);
}

static size_t sl_utf8_prev_cluster_len(const char *buf, size_t pos) {
  size_t scan;
  size_t last;
  size_t n;
  if (pos == 0)
    return 0;
  scan = 0;
  last = 0;
  while (scan < pos) {
    last = scan;
    n = sl_utf8_next_cluster_len(buf, pos, scan);
    if (n == 0)
      break;
    scan += n;
  }
  if (scan == pos)
    return pos - last;
  return sl_utf8_prev_len(buf, pos);
}

static size_t sl_utf8_clamp_cluster_boundary(const char *buf, size_t len,
                                             size_t pos) {
  size_t scan;
  size_t n;
  if (!buf || pos == 0)
    return 0;
  if (pos >= len)
    return len;
  scan = 0;
  while (scan < len) {
    if (scan == pos)
      return pos;
    n = sl_utf8_next_cluster_len(buf, len, scan);
    if (n == 0)
      break;
    if (pos < scan + n)
      return scan;
    scan += n;
  }
  return pos;
}

static void sl_history_clear(sl_history_t *history) {
  int i;
  if (!history)
    return;
  for (i = 0; i < history->len; i++)
    free(history->items[i]);
  free(history->items);
  history->items = NULL;
  history->len = 0;
}

static int sl_history_set_max(sl_t *self, int max_len) {
  sl_impl_t *impl;
  sl_history_t *history;
  char **items;
  int keep;
  int start;
  int i;
  impl = sl_impl(self);
  if (!impl || max_len < 0) {
    sl_set_error(self, "invalid history length");
    return SL_ERROR_INVALID;
  }
  history = &impl->history;
  if (max_len == history->max_len)
    return SL_OK;
  if (max_len == 0) {
    sl_history_clear(history);
    history->max_len = 0;
    return SL_OK;
  }
  keep = history->len < max_len ? history->len : max_len;
  start = history->len - keep;
  items = (char **)calloc((size_t)max_len, sizeof(*items));
  if (!items) {
    sl_set_error(self, "out of memory while resizing history");
    return SL_ERROR_NOMEM;
  }
  for (i = 0; i < keep; i++)
    items[i] = history->items[start + i];
  for (i = 0; i < start; i++)
    free(history->items[i]);
  free(history->items);
  history->items = items;
  history->len = keep;
  history->max_len = max_len;
  return SL_OK;
}

static int sl_history_add_impl(sl_t *self, const char *line) {
  sl_impl_t *impl;
  sl_history_t *history;
  char *copy;
  size_t len;
  int i;
  impl = sl_impl(self);
  if (!impl || !line)
    return SL_ERROR_INVALID;
  history = &impl->history;
  if (history->max_len == 0 || line[0] == '\0')
    return SL_OK;
  len = strlen(line);
  if (len > impl->line_max_len) {
    sl_set_error(self, "history entry exceeds configured maximum length");
    return SL_ERROR_INVALID;
  }
  if (!history->items) {
    history->items =
        (char **)calloc((size_t)history->max_len, sizeof(*history->items));
    if (!history->items) {
      sl_set_error(self, "out of memory while allocating history");
      return SL_ERROR_NOMEM;
    }
  }
  if (history->len > 0 && strcmp(history->items[history->len - 1], line) == 0)
    return SL_OK;
  copy = sl_strdup(line);
  if (!copy) {
    sl_set_error(self, "out of memory while adding history entry");
    return SL_ERROR_NOMEM;
  }
  if (history->len == history->max_len) {
    free(history->items[0]);
    for (i = 1; i < history->len; i++)
      history->items[i - 1] = history->items[i];
    history->len--;
  }
  history->items[history->len] = copy;
  history->len++;
  return SL_OK;
}

static sl_key_binding_t *sl_find_key_binding(sl_impl_t *impl, sl_key_t key) {
  int i;
  if (!impl)
    return NULL;
  for (i = 0; i < SL_MAX_KEY_BINDINGS; i++) {
    if (impl->key_bindings[i].callback && impl->key_bindings[i].key == key)
      return &impl->key_bindings[i];
  }
  return NULL;
}

static int sl_bind_key_method(sl_t *self, sl_key_t key,
                              sl_key_callback_t callback, void *userdata) {
  sl_impl_t *impl;
  sl_key_binding_t *empty;
  int i;
  impl = sl_impl(self);
  if (!impl || key == SL_KEY_NONE)
    return SL_ERROR_INVALID;
  empty = NULL;
  for (i = 0; i < SL_MAX_KEY_BINDINGS; i++) {
    if (impl->key_bindings[i].callback && impl->key_bindings[i].key == key) {
      if (!callback) {
        impl->key_bindings[i].callback = NULL;
        impl->key_bindings[i].userdata = NULL;
      } else {
        impl->key_bindings[i].callback = callback;
        impl->key_bindings[i].userdata = userdata;
      }
      return SL_OK;
    }
    if (!empty && !impl->key_bindings[i].callback)
      empty = &impl->key_bindings[i];
  }
  if (!callback)
    return SL_OK;
  if (!empty) {
    sl_set_error(self, "too many key bindings");
    return SL_ERROR_NOMEM;
  }
  empty->key = key;
  empty->callback = callback;
  empty->userdata = userdata;
  return SL_OK;
}

static int sl_history_save_char(FILE *fp, int ch) {
  if (ch == '\n') {
    if (fputc('\\', fp) == EOF || fputc('n', fp) == EOF)
      return -1;
  } else if (ch == '\\') {
    if (fputc('\\', fp) == EOF || fputc('\\', fp) == EOF)
      return -1;
  } else if (fputc(ch, fp) == EOF) {
    return -1;
  }
  return 0;
}

static int sl_history_save_impl(sl_t *self, const char *filename) {
  sl_impl_t *impl;
  FILE *fp;
  int fd;
  int i;
  impl = sl_impl(self);
  if (!impl || !filename)
    return SL_ERROR_INVALID;
  fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    sl_set_error(self, "failed to open history file for writing");
    return SL_ERROR_IO;
  }
  if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
    close(fd);
    sl_set_error(self, "failed to set history file permissions");
    return SL_ERROR_IO;
  }
  fp = fdopen(fd, "w");
  if (!fp) {
    close(fd);
    sl_set_error(self, "failed to open history file for writing");
    return SL_ERROR_IO;
  }
  for (i = 0; i < impl->history.len; i++) {
    const char *p;
    p = impl->history.items[i];
    while (*p) {
      if (sl_history_save_char(fp, (unsigned char)*p) != 0) {
        fclose(fp);
        sl_set_error(self, "failed to write history file");
        return SL_ERROR_IO;
      }
      p++;
    }
    if (fputc('\n', fp) == EOF) {
      fclose(fp);
      sl_set_error(self, "failed to write history file");
      return SL_ERROR_IO;
    }
  }
  if (fclose(fp) != 0) {
    sl_set_error(self, "failed to close history file");
    return SL_ERROR_IO;
  }
  return SL_OK;
}

static int sl_history_load_record(FILE *fp, char *line, size_t line_cap,
                                  int *too_long) {
  size_t n;
  int ch;
  n = 0;
  *too_long = 0;
  while ((ch = fgetc(fp)) != EOF) {
    if (ch == '\n')
      break;
    if (ch == '\\') {
      ch = fgetc(fp);
      if (ch == EOF)
        ch = '\\';
      else if (ch == 'n')
        ch = '\n';
      else if (ch != '\\') {
        if (n + 1 < line_cap)
          line[n] = '\\';
        n++;
      }
    } else if (ch == '\r') {
      ch = fgetc(fp);
      if (ch == '\n')
        break;
      if (ch != EOF)
        ungetc(ch, fp);
      ch = '\n';
    }
    if (n + 1 >= line_cap)
      *too_long = 1;
    else
      line[n] = (char)ch;
    n++;
    if (*too_long) {
      while ((ch = fgetc(fp)) != EOF && ch != '\n') {
      }
      break;
    }
  }
  if (ch == EOF && n == 0)
    return 0;
  if (line_cap > 0) {
    if (n >= line_cap)
      line[line_cap - 1] = '\0';
    else
      line[n] = '\0';
  }
  return 1;
}

static int sl_history_load_impl(sl_t *self, const char *filename) {
  sl_impl_t *impl;
  FILE *fp;
  char *line;
  int had_too_long;
  int too_long;
  if (!self || !filename)
    return SL_ERROR_INVALID;
  impl = sl_impl(self);
  if (!impl)
    return SL_ERROR_INVALID;
  fp = fopen(filename, "r");
  if (!fp) {
    sl_set_error(self, "failed to open history file for reading");
    return SL_ERROR_IO;
  }
  line = (char *)malloc(impl->line_max_len + 1);
  if (!line) {
    fclose(fp);
    sl_set_error(self, "out of memory while loading history");
    return SL_ERROR_NOMEM;
  }
  had_too_long = 0;
  while (sl_history_load_record(fp, line, impl->line_max_len + 1, &too_long)) {
    if (too_long) {
      had_too_long = 1;
      sl_set_error(self, "history entry exceeds configured maximum length");
      continue;
    }
    if (sl_history_add_impl(self, line) != SL_OK) {
      free(line);
      fclose(fp);
      return SL_ERROR;
    }
  }
  free(line);
  if (ferror(fp)) {
    fclose(fp);
    sl_set_error(self, "failed to read history file");
    return SL_ERROR_IO;
  }
  if (fclose(fp) != 0) {
    sl_set_error(self, "failed to close history file");
    return SL_ERROR_IO;
  }
  if (had_too_long)
    return SL_ERROR_INVALID;
  return SL_OK;
}

static int sl_row_append(sl_row_t *row, const char *s, size_t n) {
  char *next;
  if (n == 0)
    return 0;
  next = (char *)realloc(row->text, row->len + n + 1);
  if (!next)
    return -1;
  row->text = next;
  memcpy(row->text + row->len, s, n);
  row->len += n;
  row->text[row->len] = '\0';
  return 0;
}

static int sl_row_append_cells(sl_row_t *row, const char *s, size_t n,
                               int cells) {
  if (sl_row_append(row, s, n) != 0)
    return -1;
  row->cols += cells;
  return 0;
}

static int sl_render_init(sl_t *self, sl_render_t *render) {
  if (!sl_impl(self))
    return -1;
  memset(render, 0, sizeof(*render));
  return 0;
}

static int sl_render_reserve_rows(sl_render_t *render, int rows) {
  sl_row_t *next;
  int cap;
  if (rows <= render->cap)
    return 0;
  cap = render->cap > 0 ? render->cap : 8;
  while (cap < rows) {
    if (cap > INT_MAX / 2) {
      cap = rows;
      break;
    }
    cap *= 2;
  }
  next = (sl_row_t *)realloc(render->rows, (size_t)cap * sizeof(*render->rows));
  if (!next)
    return -1;
  memset(next + render->cap, 0,
         (size_t)(cap - render->cap) * sizeof(*render->rows));
  render->rows = next;
  render->cap = cap;
  return 0;
}

static int sl_render_new_row_at(sl_render_t *render, size_t start,
                                int base_col) {
  if (sl_render_reserve_rows(render, render->count + 1) != 0)
    return -1;
  render->rows[render->count].text = NULL;
  render->rows[render->count].len = 0;
  render->rows[render->count].start = start;
  render->rows[render->count].end = start;
  render->rows[render->count].base_col = base_col;
  render->rows[render->count].cols = base_col;
  render->count++;
  return 0;
}

static int sl_render_new_indented_row(sl_render_t *render, int indent,
                                      size_t start) {
  if (sl_render_new_row_at(render, start, indent) != 0)
    return -1;
  render->rows[render->count - 1].cols = 0;
  while (indent-- > 0) {
    if (sl_row_append_cells(&render->rows[render->count - 1], " ", 1, 1) != 0)
      return -1;
  }
  return 0;
}

static void sl_render_free(sl_render_t *render) {
  int i;
  for (i = 0; i < render->count; i++)
    free(render->rows[i].text);
  free(render->rows);
  render->rows = NULL;
  render->cap = 0;
  render->count = 0;
}

static void sl_render_store_clear(sl_impl_t *impl) {
  int i;
  if (!impl)
    return;
  for (i = 0; i < impl->rendered_rows; i++)
    free(impl->rendered_lines[i]);
  free(impl->rendered_lines);
  free(impl->rendered_lens);
  free(impl->rendered_cols);
  impl->rendered_lines = NULL;
  impl->rendered_lens = NULL;
  impl->rendered_cols = NULL;
  impl->rendered_cap = 0;
  impl->rendered_rows = 0;
  impl->rendered_cursor_row = 0;
  impl->rendered_cursor_col = 0;
}

static int sl_render_store_reserve(sl_impl_t *impl, int rows) {
  char **lines;
  size_t *lens;
  int *cols;
  int i;
  if (rows <= impl->rendered_cap)
    return 0;
  lines = (char **)realloc(impl->rendered_lines,
                           (size_t)rows * sizeof(*impl->rendered_lines));
  if (!lines)
    return -1;
  impl->rendered_lines = lines;
  lens = (size_t *)realloc(impl->rendered_lens,
                           (size_t)rows * sizeof(*impl->rendered_lens));
  if (!lens)
    return -1;
  impl->rendered_lens = lens;
  cols = (int *)realloc(impl->rendered_cols,
                        (size_t)rows * sizeof(*impl->rendered_cols));
  if (!cols)
    return -1;
  impl->rendered_cols = cols;
  for (i = impl->rendered_cap; i < rows; i++) {
    impl->rendered_lines[i] = NULL;
    impl->rendered_lens[i] = 0;
    impl->rendered_cols[i] = 0;
  }
  impl->rendered_cap = rows;
  return 0;
}

static int sl_render_store_update(sl_impl_t *impl, sl_render_t *render,
                                  int first_row, int row_count, int cursor_row,
                                  int cursor_col, int top_row) {
  int i;
  if (sl_render_store_reserve(impl, row_count) != 0)
    return -1;
  for (i = 0; i < row_count; i++) {
    char *copy;
    sl_row_t *row;
    row = &render->rows[first_row + i];
    copy = (char *)malloc(row->len + 1);
    if (!copy)
      return -1;
    if (row->len > 0)
      memcpy(copy, row->text, row->len);
    copy[row->len] = '\0';
    free(impl->rendered_lines[i]);
    impl->rendered_lines[i] = copy;
    impl->rendered_lens[i] = row->len;
    impl->rendered_cols[i] = row->cols;
  }
  for (i = row_count; i < impl->rendered_rows; i++) {
    free(impl->rendered_lines[i]);
    impl->rendered_lines[i] = NULL;
    impl->rendered_lens[i] = 0;
    impl->rendered_cols[i] = 0;
  }
  impl->rendered_rows = row_count;
  impl->rendered_top_row = top_row;
  impl->rendered_cursor_row = cursor_row;
  impl->rendered_cursor_col = cursor_col;
  return 0;
}

static int sl_row_equal(sl_impl_t *impl, sl_render_t *render, int row,
                        int render_row) {
  if (row >= impl->rendered_rows || render_row >= render->count)
    return 0;
  if (impl->rendered_lens[row] != render->rows[render_row].len)
    return 0;
  if (impl->rendered_lens[row] == 0)
    return 1;
  return memcmp(impl->rendered_lines[row], render->rows[render_row].text,
                impl->rendered_lens[row]) == 0;
}

static int sl_render_visible_equal(sl_impl_t *impl, sl_render_t *render,
                                   int first_row, int row_count, int cursor_row,
                                   int cursor_col, int top_row) {
  int i;
  if (impl->rendered_rows != row_count)
    return 0;
  if (impl->rendered_top_row != top_row)
    return 0;
  if (impl->rendered_cursor_row != cursor_row ||
      impl->rendered_cursor_col != cursor_col)
    return 0;
  for (i = 0; i < row_count; i++) {
    if (!sl_row_equal(impl, render, i, first_row + i))
      return 0;
  }
  return 1;
}

static int sl_word_byte(char ch) {
  return ch != '\0' && ch != '\n' && ch != '\t' && !isspace((unsigned char)ch);
}

static size_t sl_next_word_len(const char *buf, size_t len, size_t pos) {
  size_t n;
  n = 0;
  while (pos + n < len && sl_word_byte(buf[pos + n]))
    n++;
  return n;
}

static int sl_text_width(const char *buf, size_t len) {
  size_t pos;
  int width;
  pos = 0;
  width = 0;
  while (pos < len) {
    int cells;
    size_t n;
    if (buf[pos] == '\t') {
      cells = 8 - (width % 8);
      pos++;
    } else {
      n = sl_utf8_cluster_len_width(buf, len, pos, &cells);
      if (n == 0)
        break;
      pos += n;
    }
    width += cells;
  }
  return width;
}

static int sl_word_width(const char *buf, size_t len, size_t pos,
                         size_t byte_len) {
  int width;
  size_t end;
  width = 0;
  end = pos + byte_len;
  while (pos < end && pos < len) {
    int cells;
    size_t n;
    n = sl_utf8_cluster_len_width(buf, len, pos, &cells);
    if (n == 0)
      break;
    width += cells;
    pos += n;
  }
  return width;
}

static int sl_render_build(sl_t *self, const char *prompt,
                           sl_render_t *render) {
  sl_impl_t *impl;
  int width;
  int col;
  int prompt_width;
  int indent;
  size_t i;
  memset(render, 0, sizeof(*render));
  impl = sl_impl(self);
  if (!impl)
    return -1;
  if (sl_render_init(self, render) != 0)
    return -1;
  width = sl_terminal_width(impl);
  if (width < 1)
    width = 1;
  prompt_width = prompt ? sl_text_width(prompt, strlen(prompt)) : 0;
  if (sl_render_new_row_at(render, 0, prompt_width) != 0)
    return -1;
  if (prompt && prompt[0] != '\0' &&
      sl_row_append(&render->rows[0], prompt, strlen(prompt)) != 0)
    return -1;
  indent = prompt_width;
  if (indent >= width)
    indent = width > 1 ? width - 1 : 0;
  col = prompt_width;
  if (impl->cursor == 0) {
    render->cursor_row = 0;
    render->cursor_col = col;
  }
  i = 0;
  while (i < impl->len) {
    char ch;
    size_t char_len;
    int char_width;
    ch = impl->buf[i];
    char_len = sl_utf8_cluster_len_width(impl->buf, impl->len, i, &char_width);
    if (char_len == 0)
      char_len = 1;
    if (ch == '\n') {
      if (i == impl->cursor) {
        render->cursor_row = render->count - 1;
        render->cursor_col = col;
      }
      render->rows[render->count - 1].end = i;
      if (sl_render_new_indented_row(render, indent, i + 1) != 0)
        return -1;
      col = indent;
      i++;
      continue;
    }
    if (ch == ' ' && col > indent) {
      size_t spaces;
      size_t word_len;
      int word_width;
      int avail;
      spaces = 0;
      while (i + spaces < impl->len && impl->buf[i + spaces] == ' ')
        spaces++;
      word_len = sl_next_word_len(impl->buf, impl->len, i + spaces);
      word_width = sl_word_width(impl->buf, impl->len, i + spaces, word_len);
      avail = width - indent;
      if (avail < 1)
        avail = 1;
      if (word_len > 0 && word_width <= avail &&
          col + (int)spaces + word_width > width) {
        render->rows[render->count - 1].end = i;
        if (sl_render_new_indented_row(render, indent, i + spaces) != 0)
          return -1;
        col = indent;
        if (impl->cursor >= i && impl->cursor <= i + spaces) {
          render->cursor_row = render->count - 1;
          render->cursor_col = col;
        }
        i += spaces;
        continue;
      }
    }
    if (sl_word_byte(ch) && col > indent) {
      size_t word_len;
      int word_width;
      int avail;
      word_len = sl_next_word_len(impl->buf, impl->len, i);
      word_width = sl_word_width(impl->buf, impl->len, i, word_len);
      avail = width - indent;
      if (avail < 1)
        avail = 1;
      if (word_width <= avail && col + word_width > width) {
        if (sl_render_new_indented_row(render, indent, i) != 0)
          return -1;
        col = indent;
      }
    }
    if (col >= width || (char_width > 0 && col + char_width > width)) {
      if (sl_render_new_indented_row(render, indent, i) != 0)
        return -1;
      col = indent;
    }
    if (i == impl->cursor) {
      render->cursor_row = render->count - 1;
      render->cursor_col = col;
    }
    if (ch == '\t') {
      int spaces;
      spaces = 8 - (col % 8);
      while (spaces-- > 0) {
        if (col >= width) {
          if (sl_render_new_indented_row(render, indent, i) != 0)
            return -1;
          col = indent;
        }
        if (sl_row_append_cells(&render->rows[render->count - 1], " ", 1, 1) !=
            0)
          return -1;
        col++;
      }
    } else {
      if (sl_row_append_cells(&render->rows[render->count - 1], impl->buf + i,
                              char_len, char_width) != 0)
        return -1;
      col += char_width;
    }
    render->rows[render->count - 1].end = i + char_len;
    i += char_len;
  }
  if (impl->cursor == impl->len) {
    if (col >= width) {
      if (sl_render_new_indented_row(render, indent, impl->len) != 0)
        return -1;
      col = indent;
    }
    render->cursor_row = render->count - 1;
    render->cursor_col = col;
  }
  return 0;
}

static size_t sl_cursor_for_render_col(sl_render_t *render, int row_index,
                                       int target_col, const char *buf) {
  sl_row_t *row;
  size_t pos;
  int col;
  if (row_index < 0 || row_index >= render->count)
    return 0;
  row = &render->rows[row_index];
  if (target_col <= row->base_col)
    return row->start;
  col = row->base_col;
  pos = row->start;
  while (pos < row->end) {
    int width;
    size_t char_len;
    if (buf[pos] == '\t') {
      width = 8 - (col % 8);
      char_len = 1;
    } else {
      char_len = sl_utf8_cluster_len_width(buf, row->end, pos, &width);
    }
    if (col + width > target_col)
      return pos;
    col += width;
    if (char_len == 0)
      char_len = 1;
    pos += char_len;
  }
  return row->end;
}

static int sl_move_visual(sl_t *self, const char *prompt, int direction) {
  sl_impl_t *impl;
  sl_render_t render;
  int target_row;
  size_t target;
  impl = sl_impl(self);
  if (!impl)
    return 0;
  if (sl_render_build(self, prompt, &render) != 0) {
    sl_render_free(&render);
    return 0;
  }
  target_row = render.cursor_row + direction;
  if (target_row < 0 || target_row >= render.count) {
    sl_render_free(&render);
    return 0;
  }
  target = sl_cursor_for_render_col(&render, target_row, render.cursor_col,
                                    impl->buf);
  sl_render_free(&render);
  if (target == impl->cursor)
    return 0;
  impl->cursor = target;
  return 1;
}

static int sl_render_apply_bounded(sl_t *self, sl_render_t *render) {
  sl_impl_t *impl;
  int visible;
  int first;
  int cursor_row;
  int top;
  int old_rows;
  int old_top;
  int clear_top;
  int clear_bottom;
  int i;
  int rc;
  int height;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  height = sl_terminal_height(impl);
  visible = render->count;
  if (visible > height)
    visible = height;
  if (visible < 1)
    visible = 1;
  first = render->cursor_row - visible + 1;
  if (first < 0)
    first = 0;
  if (first > render->count - visible)
    first = render->count - visible;
  cursor_row = render->cursor_row - first;
  if (cursor_row < 0)
    cursor_row = 0;
  if (cursor_row >= visible)
    cursor_row = visible - 1;
  top = sl_prompt_top(impl, visible);
  if (sl_render_visible_equal(impl, render, first, visible, cursor_row,
                              render->cursor_col, top))
    return 0;
  old_rows = impl->rendered_rows;
  old_top = impl->rendered_top_row;
  rc = 0;

  if (old_rows > 0 && old_top != top) {
    if (top < old_top && sl_scroll_region_up(impl->output_fd, impl->screen_y,
                                             old_top - 1, old_top - top) != 0)
      rc = -1;
    clear_top = old_top < top ? old_top : top;
    clear_bottom = sl_box_bottom(impl);
    for (i = clear_top; rc == 0 && i <= clear_bottom; i++) {
      if (sl_write_cursor_pos(impl->output_fd, i, impl->screen_x) != 0 ||
          sl_clear_box_tail(impl, 0) != 0)
        rc = -1;
    }
    sl_render_store_clear(impl);
    old_rows = 0;
  }

  for (i = 0; rc == 0 && i < visible; i++) {
    int render_row;
    render_row = first + i;
    if (i >= old_rows || !sl_row_equal(impl, render, i, render_row)) {
      if (sl_write_cursor_pos(impl->output_fd, top + i, impl->screen_x) != 0)
        rc = -1;
      if (rc == 0 && render->rows[render_row].len > 0 &&
          sl_write_all(impl->output_fd, render->rows[render_row].text,
                       render->rows[render_row].len) != 0)
        rc = -1;
      if (rc == 0 &&
          (i >= old_rows ||
           impl->rendered_cols[i] > render->rows[render_row].cols) &&
          sl_clear_box_tail(impl, render->rows[render_row].cols) != 0)
        rc = -1;
    }
  }
  for (i = visible; rc == 0 && i < old_rows; i++) {
    if (sl_write_cursor_pos(impl->output_fd, top + i, impl->screen_x) != 0 ||
        sl_clear_box_tail(impl, 0) != 0)
      rc = -1;
  }
  if (rc == 0 && sl_write_cursor_pos(impl->output_fd, top + cursor_row,
                                     impl->screen_x + render->cursor_col) != 0)
    rc = -1;
  if (rc == 0 &&
      sl_render_store_update(impl, render, first, visible, cursor_row,
                             render->cursor_col, top) != 0) {
    sl_set_error(self, "out of memory while storing terminal render state");
    rc = -1;
  }
  return rc;
}

static int sl_render_apply(sl_t *self, const char *prompt) {
  sl_impl_t *impl;
  sl_render_t render;
  int old_rows;
  int max_rows;
  int i;
  int fd;
  int rc;
  int initial;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  if (sl_render_build(self, prompt, &render) != 0) {
    sl_render_free(&render);
    sl_set_error(self, "failed to render editor state");
    return -1;
  }
  if (sl_bounded_mode(impl)) {
    rc = sl_render_apply_bounded(self, &render);
    sl_render_free(&render);
    if (rc != 0)
      sl_set_error(self, "failed to write bounded terminal output");
    return rc;
  }
  if (sl_render_visible_equal(impl, &render, 0, render.count, render.cursor_row,
                              render.cursor_col, -1)) {
    sl_render_free(&render);
    return 0;
  }
  fd = impl->output_fd;
  rc = 0;
  old_rows = impl->rendered_rows;
  initial = old_rows == 0;
  if (!initial && (sl_wchar(fd, '\r') != 0 ||
                   sl_write_cursor_up(fd, impl->rendered_cursor_row) != 0))
    rc = -1;
  max_rows = old_rows > render.count ? old_rows : render.count;
  for (i = 0; rc == 0 && i < max_rows; i++) {
    if (i >= render.count || !sl_row_equal(impl, &render, i, i)) {
      if (sl_wchar(fd, '\r') != 0)
        rc = -1;
      if (rc == 0 && i < render.count && render.rows[i].len > 0 &&
          sl_write_all(fd, render.rows[i].text, render.rows[i].len) != 0)
        rc = -1;
      if (rc == 0 &&
          (i >= render.count ||
           (i < old_rows && impl->rendered_cols[i] > render.rows[i].cols)) &&
          sl_wstr(fd, "\033[0K") != 0)
        rc = -1;
    }
    if (rc == 0 && i + 1 < max_rows) {
      if (initial) {
        if (sl_wstr(fd, "\r\n") != 0)
          rc = -1;
      } else if (sl_wstr(fd, "\r\n") != 0) {
        rc = -1;
      }
    }
  }
  if (rc == 0) {
    if (sl_write_cursor_up(fd, max_rows - 1 - render.cursor_row) != 0 ||
        sl_wchar(fd, '\r') != 0 ||
        sl_write_cursor_forward(fd, render.cursor_col) != 0)
      rc = -1;
  }
  if (rc == 0 &&
      sl_render_store_update(impl, &render, 0, render.count, render.cursor_row,
                             render.cursor_col, -1) != 0) {
    sl_set_error(self, "out of memory while storing terminal render state");
    rc = -1;
  }
  sl_render_free(&render);
  if (rc != 0)
    sl_set_error(self, "failed to write terminal output");
  return rc;
}

static int sl_render_finish(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl)
    return -1;
  if (sl_bounded_mode(impl)) {
    if (sl_write_cursor_pos(impl->output_fd, sl_box_bottom(impl) + 1, 0) != 0)
      return -1;
    sl_render_store_clear(impl);
    return 0;
  }
  if (sl_wstr(impl->output_fd, "\r\n") != 0) {
    sl_set_error(self, "failed to write final newline");
    return -1;
  }
  sl_render_store_clear(impl);
  return 0;
}

static int sl_render_clear_active(sl_t *self) {
  sl_impl_t *impl;
  int i;
  int rows;
  impl = sl_impl(self);
  if (!impl || impl->rendered_rows <= 0)
    return 0;
  if (sl_bounded_mode(impl)) {
    for (i = 0; i < impl->rendered_rows; i++) {
      if (sl_write_cursor_pos(impl->output_fd, impl->rendered_top_row + i,
                              impl->screen_x) != 0 ||
          sl_clear_box_tail(impl, 0) != 0)
        return -1;
    }
    sl_render_store_clear(impl);
    return 0;
  }
  rows = impl->rendered_rows;
  if (sl_wchar(impl->output_fd, '\r') != 0 ||
      sl_write_cursor_up(impl->output_fd, impl->rendered_cursor_row) != 0)
    return -1;
  for (i = 0; i < rows; i++) {
    if (sl_wstr(impl->output_fd, "\r\033[0K") != 0)
      return -1;
    if (i + 1 < rows && (sl_write_cursor_down(impl->output_fd, 1) != 0 ||
                         sl_wchar(impl->output_fd, '\r') != 0))
      return -1;
  }
  if (sl_write_cursor_up(impl->output_fd, rows - 1) != 0 ||
      sl_wchar(impl->output_fd, '\r') != 0)
    return -1;
  sl_render_store_clear(impl);
  return 0;
}

static int sl_write_stream_chunk(int fd, const char *chunk, size_t len) {
  const char *p;
  const char *start;
  size_t n;
  p = chunk;
  start = chunk;
  while ((size_t)(p - chunk) < len) {
    if (*p == '\n') {
      n = (size_t)(p - start);
      if (n > 0 && sl_write_all(fd, start, n) != 0)
        return -1;
      if (sl_wstr(fd, "\r\n") != 0)
        return -1;
      p++;
      start = p;
    } else {
      p++;
    }
  }
  n = (size_t)(p - start);
  if (n > 0 && sl_write_all(fd, start, n) != 0)
    return -1;
  return 0;
}

static int sl_write_stream(sl_t *self, sl_stream_callback_t callback,
                           void *userdata) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || !callback)
    return SL_ERROR_INVALID;
  for (;;) {
    const char *chunk;
    size_t len;
    int rc;
    chunk = NULL;
    len = 0;
    rc = callback(self, userdata, &chunk, &len);
    if (rc != SL_OK)
      return rc;
    if (len == 0)
      return SL_OK;
    if (!chunk)
      return SL_ERROR_INVALID;
    if (sl_write_stream_chunk(impl->output_fd, chunk, len) != 0)
      return SL_ERROR_IO;
  }
}

static int sl_print_above_method(sl_t *self, sl_stream_callback_t callback,
                                 void *userdata) {
  sl_impl_t *impl;
  int prompt_rows;
  int prompt_top;
  int content_top;
  int content_bottom;
  int rc;
  impl = sl_impl(self);
  if (!impl)
    return SL_ERROR_INVALID;
  if (sl_bounded_mode(impl)) {
    prompt_rows = impl->rendered_rows > 0 ? impl->rendered_rows : 1;
    prompt_top = sl_prompt_top(impl, prompt_rows);
    content_top = impl->screen_y;
    content_bottom = prompt_top - 1;
    if (content_bottom >= content_top) {
      if (!sl_bounded_scroll_spans_full_width(impl)) {
        sl_set_error(self,
                     "bounded print_above requires full-width terminal bounds");
        return SL_ERROR_INVALID;
      }
      if (sl_set_scroll_region(impl->output_fd, content_top, content_bottom) !=
              0 ||
          sl_write_cursor_pos(impl->output_fd, content_bottom,
                              impl->screen_x) != 0)
        return SL_ERROR_IO;
      rc = sl_write_stream(self, callback, userdata);
      if (sl_reset_scroll_region(impl->output_fd) != 0)
        return SL_ERROR_IO;
      if (rc != SL_OK)
        return rc;
      sl_render_store_clear(impl);
      if (sl_render_apply(self, impl->active_prompt) != 0)
        return SL_ERROR_IO;
      return SL_OK;
    }
    sl_set_error(self, "bounded prompt has no space above it");
    return SL_ERROR_INVALID;
  }
  if (impl->active_prompt) {
    if (sl_render_clear_active(self) != 0) {
      sl_set_error(self, "failed to clear active prompt before printing");
      return SL_ERROR_IO;
    }
  }
  rc = sl_write_stream(self, callback, userdata);
  if (rc != SL_OK)
    return rc;
  if (impl->active_prompt && sl_render_apply(self, impl->active_prompt) != 0)
    return SL_ERROR_IO;
  return SL_OK;
}

static ssize_t sl_read_escape_byte(int fd, char *ch) {
  fd_set readfds;
  struct timeval tv;
  int ready;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  ready = select(fd + 1, &readfds, NULL, NULL, &tv);
  if (ready <= 0)
    return ready;
  return read(fd, ch, 1);
}

static ssize_t sl_read_available_byte(int fd, char *ch) {
  fd_set readfds;
  struct timeval tv;
  int ready;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  ready = select(fd + 1, &readfds, NULL, NULL, &tv);
  if (ready <= 0)
    return ready;
  return read(fd, ch, 1);
}

static int sl_read_utf8_input(sl_impl_t *impl, char first, char *buf,
                              size_t *len) {
  unsigned char c;
  size_t need;
  size_t i;
  if (!buf || !len)
    return -1;
  buf[0] = first;
  *len = 1;
  c = (unsigned char)first;
  if ((c & 0x80) == 0)
    return 0;
  if ((c & 0xe0) == 0xc0)
    need = 2;
  else if ((c & 0xf0) == 0xe0)
    need = 3;
  else if ((c & 0xf8) == 0xf0)
    need = 4;
  else
    return 0;
  for (i = 1; i < need; i++) {
    char next;
    ssize_t n;
    n = sl_read_escape_byte(impl->input_fd, &next);
    if (n != 1)
      return 0;
    if (((unsigned char)next & 0xc0) != 0x80)
      return 0;
    buf[i] = next;
    *len = i + 1;
  }
  return 0;
}

static int sl_read_key(sl_impl_t *impl) {
  char ch;
  ssize_t n;
  errno = 0;
  n = read(impl->input_fd, &ch, 1);
  if (n <= 0)
    return SL_KEY_NONE;
  if (ch != '\033')
    return (int)(unsigned char)ch;
  n = sl_read_escape_byte(impl->input_fd, &ch);
  if (n <= 0)
    return SL_KEY_ESCAPE;
  if (ch == 'b')
    return SL_KEY_ALT_B;
  if (ch == 'f')
    return SL_KEY_ALT_F;
  if (ch != '[' && ch != 'O') {
    if (ch >= 'a' && ch <= 'z')
      return SL_KEY_ALT_BASE + (unsigned char)ch;
    if (ch >= 'A' && ch <= 'Z')
      return SL_KEY_ALT_BASE + (unsigned char)ch;
    return SL_KEY_UNKNOWN;
  }
  n = sl_read_escape_byte(impl->input_fd, &ch);
  if (n <= 0)
    return SL_KEY_UNKNOWN;
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
  case 'P':
    return SL_KEY_F1;
  case 'Q':
    return SL_KEY_F2;
  case 'R':
    return SL_KEY_F3;
  case 'S':
    return SL_KEY_F4;
  }
  if (ch >= '0' && ch <= '9') {
    int code;
    int modifier;
    code = ch - '0';
    modifier = 0;
    while (sl_read_escape_byte(impl->input_fd, &ch) == 1) {
      if (ch == '~')
        break;
      if (ch == ';') {
        while (sl_read_escape_byte(impl->input_fd, &ch) == 1) {
          if (ch < '0' || ch > '9')
            break;
          modifier = modifier * 10 + (ch - '0');
        }
        break;
      }
      if (ch < '0' || ch > '9')
        break;
      code = code * 10 + (ch - '0');
    }
    if (ch == 'u') {
      if (code == 13 && modifier == 5)
        return SL_KEY_CTRL_ENTER;
      return SL_KEY_UNKNOWN;
    }
    if (ch == '~') {
      if (code == 3)
        return SL_KEY_DELETE;
      if (code == 11)
        return SL_KEY_F1;
      if (code == 12)
        return SL_KEY_F2;
      if (code == 13)
        return SL_KEY_F3;
      if (code == 14)
        return SL_KEY_F4;
      if (code == 15)
        return SL_KEY_F5;
      if (code == 17)
        return SL_KEY_F6;
      if (code == 18)
        return SL_KEY_F7;
      if (code == 19)
        return SL_KEY_F8;
      if (code == 20)
        return SL_KEY_F9;
      if (code == 21)
        return SL_KEY_F10;
      if (code == 1 || code == 7)
        return SL_KEY_HOME;
      if (code == 4 || code == 8)
        return SL_KEY_END;
      if (code == 200)
        return SL_KEY_PASTE_BEGIN;
      if (code == 201)
        return SL_KEY_PASTE_END;
    }
  }
  return SL_KEY_UNKNOWN;
}

static int sl_read_paste_input(sl_impl_t *impl, char *buf, size_t *len) {
  static const char paste_end[] = "\033[201~";
  char ch;
  ssize_t n;
  size_t i;
  if (!impl || !buf || !len)
    return SL_KEY_NONE;
  *len = 0;
  errno = 0;
  n = read(impl->input_fd, &ch, 1);
  if (n <= 0)
    return SL_KEY_NONE;
  if (ch == '\r')
    ch = '\n';
  buf[0] = ch;
  *len = 1;
  if (ch != '\033') {
    if (((unsigned char)ch & 0x80) != 0)
      (void)sl_read_utf8_input(impl, ch, buf, len);
    return SL_KEY_UNKNOWN;
  }
  for (i = 1; i < sizeof(paste_end) - 1; i++) {
    n = sl_read_escape_byte(impl->input_fd, &ch);
    if (n != 1)
      return SL_KEY_UNKNOWN;
    buf[i] = ch;
    *len = i + 1;
    if (ch != paste_end[i])
      return SL_KEY_UNKNOWN;
  }
  *len = 0;
  return SL_KEY_PASTE_END;
}

static char *sl_readline_plain(sl_t *self, const char *prompt) {
  sl_impl_t *impl;
  char *result;
  char ch;
  int got;
  (void)prompt;
  impl = sl_impl(self);
  if (!impl)
    return NULL;
  sl_set_readline_status(self, SL_READLINE_NONE);
  if (sl_buf_set(self, "") != 0) {
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  got = 0;
  for (;;) {
    ssize_t nread;
    if (impl->plain_pending) {
      ch = impl->plain_pending_ch;
      impl->plain_pending = 0;
      nread = 1;
    } else {
      nread = read(impl->input_fd, &ch, 1);
    }
    if (nread == 0)
      break;
    if (nread < 0) {
      if (errno == EINTR)
        continue;
      sl_set_error(self, "failed to read input");
      sl_set_readline_status(self, SL_READLINE_ERROR);
      return NULL;
    }
    got = 1;
    if (ch == '\r') {
      char next;
      nread = sl_read_available_byte(impl->input_fd, &next);
      if (nread == 1 && next != '\n') {
        impl->plain_pending = 1;
        impl->plain_pending_ch = next;
      }
      break;
    }
    if (ch == '\n')
      break;
    if (ch == 4 && impl->len == 0) {
      sl_set_readline_status(self, SL_READLINE_EOF);
      return NULL;
    }
    if (sl_buf_insert(self, impl->len, &ch, 1) != 0 &&
        impl->len < impl->line_max_len) {
      sl_set_readline_status(self, SL_READLINE_ERROR);
      return NULL;
    }
  }
  if (!got && impl->len == 0) {
    sl_set_readline_status(self, SL_READLINE_EOF);
    return NULL;
  }
  result = sl_strdup(impl->buf);
  if (!result) {
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  sl_set_readline_status(self, SL_READLINE_SUBMITTED);
  return result;
}

static void sl_history_nav_start(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl)
    return;
  if (impl->history_index == -1) {
    free(impl->history_edit);
    impl->history_edit = sl_strdup(impl->buf);
    impl->history_index = impl->history.len;
  }
}

static void sl_history_nav(sl_t *self, int dir) {
  sl_impl_t *impl;
  int next;
  impl = sl_impl(self);
  if (!impl || impl->history.len == 0)
    return;
  sl_history_nav_start(self);
  next = impl->history_index + dir;
  if (next < 0)
    next = 0;
  if (next > impl->history.len)
    next = impl->history.len;
  impl->history_index = next;
  if (next == impl->history.len)
    (void)sl_buf_set(self, impl->history_edit ? impl->history_edit : "");
  else
    (void)sl_buf_set(self, impl->history.items[next]);
}

typedef struct sl_history_search {
  int active;
  char *saved;
  char *query;
  size_t query_len;
  size_t query_cap;
  int match_index;
  int has_match;
  char *prompt;
} sl_history_search_t;

static void sl_history_search_cleanup(sl_history_search_t *search) {
  if (!search)
    return;
  free(search->saved);
  free(search->query);
  free(search->prompt);
  memset(search, 0, sizeof(*search));
  search->match_index = -1;
}

static int sl_history_search_reserve(sl_history_search_t *search, size_t need) {
  size_t cap;
  char *next;
  if (need <= search->query_cap)
    return 0;
  cap = search->query_cap ? search->query_cap : 32;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2)
      return -1;
    cap *= 2;
  }
  next = (char *)realloc(search->query, cap);
  if (!next)
    return -1;
  search->query = next;
  search->query_cap = cap;
  return 0;
}

static int sl_history_search_append(sl_history_search_t *search,
                                    const char *bytes, size_t len) {
  if (!search || !bytes)
    return -1;
  if (sl_history_search_reserve(search, search->query_len + len + 1) != 0)
    return -1;
  memcpy(search->query + search->query_len, bytes, len);
  search->query_len += len;
  search->query[search->query_len] = '\0';
  return 0;
}

static void sl_history_search_backspace(sl_history_search_t *search) {
  size_t prev_len;
  if (!search || search->query_len == 0)
    return;
  prev_len = sl_utf8_prev_cluster_len(search->query, search->query_len);
  search->query_len -= prev_len;
  search->query[search->query_len] = '\0';
}

static int sl_history_search_item_matches(const char *item, const char *query) {
  if (!item || !query)
    return 0;
  if (query[0] == '\0')
    return 1;
  return strstr(item, query) != NULL;
}

static int sl_history_search_find_from(sl_history_t *history, const char *query,
                                       int start, int stop) {
  int i;
  if (!history || history->len <= 0)
    return -1;
  if (start >= history->len)
    start = history->len - 1;
  if (stop < 0)
    stop = 0;
  for (i = start; i >= stop; i--) {
    if (sl_history_search_item_matches(history->items[i], query))
      return i;
  }
  return -1;
}

static int sl_history_search_find(sl_impl_t *impl, sl_history_search_t *search,
                                  int repeat) {
  int found;
  if (!impl || !search)
    return -1;
  if (impl->history.len <= 0)
    return -1;
  if (repeat && search->has_match && search->match_index >= 0) {
    found = sl_history_search_find_from(&impl->history, search->query,
                                        search->match_index - 1, 0);
    if (found >= 0)
      return found;
    found = sl_history_search_find_from(&impl->history, search->query,
                                        impl->history.len - 1,
                                        search->match_index + 1);
    if (found >= 0)
      return found;
    if (sl_history_search_item_matches(impl->history.items[search->match_index],
                                       search->query))
      return search->match_index;
    return -1;
  }
  return sl_history_search_find_from(&impl->history, search->query,
                                     impl->history.len - 1, 0);
}

static int sl_history_search_update_prompt(sl_history_search_t *search) {
  const char *prefix;
  const char *suffix;
  size_t len;
  char *prompt;
  if (!search)
    return -1;
  prefix = search->has_match ? "(r-search)`" : "(failed)`";
  suffix = "': ";
  len = strlen(prefix) + search->query_len + strlen(suffix) + 1;
  prompt = (char *)malloc(len);
  if (!prompt)
    return -1;
  memcpy(prompt, prefix, strlen(prefix));
  memcpy(prompt + strlen(prefix), search->query, search->query_len);
  memcpy(prompt + strlen(prefix) + search->query_len, suffix,
         strlen(suffix) + 1);
  free(search->prompt);
  search->prompt = prompt;
  return 0;
}

static int sl_history_search_refresh(sl_t *self, sl_history_search_t *search,
                                     int repeat) {
  sl_impl_t *impl;
  int found;
  impl = sl_impl(self);
  if (!impl || !search || !search->active)
    return -1;
  found = sl_history_search_find(impl, search, repeat);
  if (found >= 0) {
    search->has_match = 1;
    search->match_index = found;
    if (sl_buf_set(self, impl->history.items[found]) != 0)
      return -1;
  } else {
    search->has_match = 0;
    search->match_index = -1;
    if (sl_buf_set(self, search->saved ? search->saved : "") != 0)
      return -1;
  }
  return sl_history_search_update_prompt(search);
}

static int sl_history_search_start(sl_t *self, sl_history_search_t *search) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || !search)
    return -1;
  if (!search->active) {
    sl_history_search_cleanup(search);
    search->saved = sl_strdup(impl->buf);
    if (!search->saved)
      return -1;
    if (sl_history_search_reserve(search, 1) != 0)
      return -1;
    search->query[0] = '\0';
    search->query_len = 0;
    search->match_index = -1;
    search->active = 1;
  }
  return sl_history_search_refresh(self, search, search->has_match);
}

static int sl_history_search_cancel(sl_t *self, sl_history_search_t *search) {
  int rc;
  rc = 0;
  if (search && search->active)
    rc = sl_buf_set(self, search->saved ? search->saved : "");
  sl_history_search_cleanup(search);
  return rc;
}

static int sl_history_search_accept(sl_history_search_t *search) {
  sl_history_search_cleanup(search);
  return 0;
}

static int sl_dispatch_key_binding(sl_t *self, int key,
                                   sl_key_action_t *action) {
  sl_impl_t *impl;
  sl_key_binding_t *binding;
  impl = sl_impl(self);
  if (!impl || !action)
    return SL_ERROR_INVALID;
  binding = sl_find_key_binding(impl, (sl_key_t)key);
  if (!binding)
    return SL_OK;
  *action = SL_KEY_ACTION_HANDLED;
  return binding->callback(self, (sl_key_t)key, binding->userdata, action);
}

static char *sl_readline_method(sl_t *self, const char *prompt) {
  sl_impl_t *impl;
  sl_history_search_t search;
  char *result;
  int done;
  int eof;
  int cancelled;
  int interrupted;
  int failed;
  impl = sl_impl(self);
  if (!impl)
    return NULL;
  sl_set_readline_status(self, SL_READLINE_NONE);
  if (!prompt)
    prompt = SL_DEFAULT_PROMPT;
  if (!isatty(impl->input_fd))
    return sl_readline_plain(self, prompt);
  if (sl_enable_raw(self) != 0) {
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  if (sl_buf_set(self, "") != 0) {
    sl_disable_raw(self);
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  if (sl_enable_bracketed_paste(impl) != 0) {
    sl_disable_raw(self);
    sl_set_error(self, "failed to enable bracketed paste");
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  memset(&search, 0, sizeof(search));
  search.match_index = -1;
  impl->history_index = -1;
  impl->bracketed_paste = 0;
  impl->request_submit = 0;
  impl->request_cancel = 0;
  impl->active_readline = 1;
  free(impl->history_edit);
  impl->history_edit = NULL;
  sl_render_store_clear(impl);
  impl->active_prompt = prompt;
  if (sl_render_apply(self, prompt) != 0) {
    impl->active_prompt = NULL;
    impl->active_readline = 0;
    sl_disable_bracketed_paste(impl);
    sl_disable_raw(self);
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  done = 0;
  eof = 0;
  cancelled = 0;
  interrupted = 0;
  failed = 0;
  while (!done) {
    const char *render_prompt;
    char paste_bytes[8];
    size_t paste_len;
    int key;
    sl_key_action_t action;
    render_prompt = search.active && search.prompt ? search.prompt : prompt;
    paste_len = 0;
    if (impl->bracketed_paste)
      key = sl_read_paste_input(impl, paste_bytes, &paste_len);
    else
      key = sl_read_key(impl);
    if (impl->bracketed_paste && key != SL_KEY_NONE) {
      if (key == SL_KEY_PASTE_END) {
        impl->bracketed_paste = 0;
      } else if (paste_len > 0 && paste_len <= impl->line_max_len - impl->len) {
        if (sl_buf_insert(self, impl->cursor, paste_bytes, paste_len) == 0) {
          impl->cursor += paste_len;
        } else {
          failed = 1;
          done = 1;
        }
      }
      render_prompt = search.active && search.prompt ? search.prompt : prompt;
      if (!done && sl_render_apply(self, render_prompt) != 0) {
        failed = 1;
        done = 1;
      }
      continue;
    }
    action = SL_KEY_ACTION_PASS;
    switch (key) {
    case SL_KEY_NONE:
      if (errno == EINTR) {
        errno = 0;
      } else if (errno != 0) {
        failed = 1;
        done = 1;
      } else {
        if (impl->idle_callback)
          impl->idle_callback(self, impl->idle_userdata);
        if (impl->request_submit) {
          done = 1;
          break;
        }
        if (impl->request_cancel) {
          cancelled = 1;
          done = 1;
          break;
        }
        render_prompt = search.active && search.prompt ? search.prompt : prompt;
        if (sl_render_apply(self, render_prompt) != 0) {
          failed = 1;
          done = 1;
        }
      }
      break;
    default:
      if (sl_dispatch_key_binding(self, key, &action) != SL_OK) {
        sl_set_error(self, "key callback failed");
        failed = 1;
        done = 1;
        break;
      }
      if (impl->request_submit)
        action = SL_KEY_ACTION_SUBMIT;
      else if (impl->request_cancel)
        action = SL_KEY_ACTION_CANCEL;
      if (action == SL_KEY_ACTION_HANDLED)
        break;
      if (action == SL_KEY_ACTION_SUBMIT) {
        sl_history_search_accept(&search);
        done = 1;
        break;
      }
      if (action == SL_KEY_ACTION_CANCEL) {
        (void)sl_history_search_cancel(self, &search);
        cancelled = 1;
        done = 1;
        break;
      }
      if (action == SL_KEY_ACTION_INTERRUPT) {
        interrupted = 1;
        done = 1;
        break;
      }
      if (search.active) {
        switch (key) {
        case SL_KEY_CTRL_C:
          interrupted = 1;
          done = 1;
          break;
        case SL_KEY_ENTER:
          sl_history_search_accept(&search);
          done = 1;
          break;
        case SL_KEY_CTRL_R:
          if (sl_history_search_refresh(self, &search, 1) != 0) {
            sl_set_error(self, "failed to update reverse history search");
            failed = 1;
            done = 1;
          }
          break;
        case SL_KEY_ESCAPE:
        case 7:
          if (sl_history_search_cancel(self, &search) != 0) {
            sl_set_error(self, "failed to cancel reverse history search");
            failed = 1;
            done = 1;
          }
          break;
        case SL_KEY_BACKSPACE:
        case 8:
          sl_history_search_backspace(&search);
          if (sl_history_search_refresh(self, &search, 0) != 0) {
            sl_set_error(self, "failed to update reverse history search");
            failed = 1;
            done = 1;
          }
          break;
        case SL_KEY_CTRL_U:
          search.query_len = 0;
          if (search.query)
            search.query[0] = '\0';
          if (sl_history_search_refresh(self, &search, 0) != 0) {
            sl_set_error(self, "failed to update reverse history search");
            failed = 1;
            done = 1;
          }
          break;
        default:
          if (key >= 32 && key < 256 && key != 127) {
            char bytes[4];
            size_t byte_len;
            (void)sl_read_utf8_input(impl, (char)key, bytes, &byte_len);
            if (search.query_len + byte_len > impl->line_max_len ||
                sl_history_search_append(&search, bytes, byte_len) != 0 ||
                sl_history_search_refresh(self, &search, 0) != 0) {
              sl_set_error(self, "failed to update reverse history search");
              failed = 1;
              done = 1;
            }
          }
          break;
        }
        break;
      }
      switch (key) {
      case SL_KEY_CTRL_C:
        interrupted = 1;
        done = 1;
        break;
      case SL_KEY_ENTER:
        done = 1;
        break;
      case SL_KEY_CTRL_J:
        if (sl_buf_insert(self, impl->cursor, "\n", 1) == 0)
          impl->cursor++;
        break;
      case SL_KEY_CTRL_R:
        if (sl_history_search_start(self, &search) != 0) {
          sl_set_error(self, "failed to start reverse history search");
          failed = 1;
          done = 1;
        }
        break;
      case SL_KEY_PASTE_BEGIN:
        impl->bracketed_paste = 1;
        break;
      case SL_KEY_PASTE_END:
        impl->bracketed_paste = 0;
        break;
      case SL_KEY_LEFT:
      case SL_KEY_CTRL_B:
        if (impl->cursor > 0)
          impl->cursor -= sl_utf8_prev_cluster_len(impl->buf, impl->cursor);
        break;
      case SL_KEY_RIGHT:
      case SL_KEY_CTRL_F:
        if (impl->cursor < impl->len)
          impl->cursor +=
              sl_utf8_next_cluster_len(impl->buf, impl->len, impl->cursor);
        break;
      case SL_KEY_UP:
        if (!sl_move_visual(self, prompt, -1) && impl->cursor == impl->len)
          sl_history_nav(self, -1);
        break;
      case SL_KEY_DOWN:
        if (!sl_move_visual(self, prompt, 1) && impl->history_index != -1)
          sl_history_nav(self, 1);
        break;
      case SL_KEY_HOME:
      case SL_KEY_CTRL_A:
        impl->cursor = 0;
        break;
      case SL_KEY_END:
      case SL_KEY_CTRL_E:
        impl->cursor = impl->len;
        break;
      case SL_KEY_CTRL_U:
        sl_buf_delete(self, 0, impl->cursor);
        impl->cursor = 0;
        break;
      case SL_KEY_CTRL_K:
        sl_buf_delete(self, impl->cursor, impl->len - impl->cursor);
        break;
      case SL_KEY_CTRL_W:
        if (impl->cursor > 0) {
          size_t wp;
          wp = sl_word_backward(impl->buf, impl->cursor);
          sl_buf_delete(self, wp, impl->cursor - wp);
          impl->cursor = wp;
        }
        break;
      case SL_KEY_BACKSPACE:
      case 8:
        if (impl->cursor > 0) {
          size_t prev_len;
          prev_len = sl_utf8_prev_cluster_len(impl->buf, impl->cursor);
          sl_buf_delete(self, impl->cursor - prev_len, prev_len);
          impl->cursor -= prev_len;
        }
        break;
      case SL_KEY_DELETE:
      case SL_KEY_CTRL_D:
        if (impl->cursor < impl->len)
          sl_buf_delete(
              self, impl->cursor,
              sl_utf8_next_cluster_len(impl->buf, impl->len, impl->cursor));
        else if (key == SL_KEY_CTRL_D && impl->len == 0) {
          eof = 1;
          done = 1;
        }
        break;
      case SL_KEY_ALT_B:
        impl->cursor = sl_word_backward(impl->buf, impl->cursor);
        break;
      case SL_KEY_ALT_F:
        impl->cursor = sl_word_forward(impl->buf, impl->len, impl->cursor);
        break;
      case SL_KEY_UNKNOWN:
      case SL_KEY_ESCAPE:
        break;
      default:
        if (key >= 32 && key < 256 && key != 127) {
          char bytes[4];
          size_t byte_len;
          (void)sl_read_utf8_input(impl, (char)key, bytes, &byte_len);
          if (sl_buf_insert(self, impl->cursor, bytes, byte_len) == 0)
            impl->cursor += byte_len;
        }
        break;
      }
      break;
    }
    render_prompt = search.active && search.prompt ? search.prompt : prompt;
    if (!done && sl_render_apply(self, render_prompt) != 0) {
      failed = 1;
      done = 1;
    }
  }
  impl->active_readline = 0;
  if (interrupted) {
    sl_history_search_cleanup(&search);
    sl_render_clear_active(self);
    impl->active_prompt = NULL;
    impl->bracketed_paste = 0;
    sl_disable_bracketed_paste(impl);
    sl_disable_raw(self);
    sl_set_readline_status(self, SL_READLINE_INTERRUPTED);
    raise(SIGINT);
    return NULL;
  }
  if (failed) {
    sl_history_search_cleanup(&search);
    impl->active_prompt = NULL;
    impl->bracketed_paste = 0;
    sl_disable_bracketed_paste(impl);
    sl_disable_raw(self);
    free(impl->history_edit);
    impl->history_edit = NULL;
    impl->history_index = -1;
    sl_buf_set(self, "");
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  impl->cursor = impl->len;
  if (eof || cancelled) {
    if (sl_history_search_cancel(self, &search) != 0)
      failed = 1;
  } else {
    sl_history_search_accept(&search);
  }
  if (!failed &&
      (sl_render_apply(self, prompt) != 0 || sl_render_finish(self) != 0))
    failed = 1;
  impl->bracketed_paste = 0;
  sl_disable_bracketed_paste(impl);
  sl_disable_raw(self);
  impl->active_prompt = NULL;
  if (failed) {
    sl_history_search_cleanup(&search);
    free(impl->history_edit);
    impl->history_edit = NULL;
    impl->history_index = -1;
    sl_buf_set(self, "");
    sl_set_readline_status(self, SL_READLINE_ERROR);
    return NULL;
  }
  if (eof || cancelled) {
    sl_history_search_cleanup(&search);
    free(impl->history_edit);
    impl->history_edit = NULL;
    impl->history_index = -1;
    sl_buf_set(self, "");
    sl_set_readline_status(self,
                           cancelled ? SL_READLINE_CANCELLED : SL_READLINE_EOF);
    return NULL;
  }
  result = sl_strdup(impl->buf);
  if (!result) {
    sl_set_readline_status(self, SL_READLINE_ERROR);
    sl_history_search_cleanup(&search);
    free(impl->history_edit);
    impl->history_edit = NULL;
    impl->history_index = -1;
    sl_buf_set(self, "");
    return NULL;
  }
  sl_set_readline_status(self, SL_READLINE_SUBMITTED);
  sl_history_search_cleanup(&search);
  free(impl->history_edit);
  impl->history_edit = NULL;
  impl->history_index = -1;
  sl_buf_set(self, "");
  return result;
}

static void sl_destroy_method(sl_t *self) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!self)
    return;
  if (impl) {
    sl_disable_raw(self);
    sl_history_clear(&impl->history);
    sl_render_store_clear(impl);
    free(impl->history_edit);
    free(impl->buf);
    free(impl);
  }
  free(self);
}

static void sl_free_string_method(sl_t *self, char *ptr) {
  (void)self;
  free(ptr);
}

static int sl_set_screen_width_method(sl_t *self, int width) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || width < 0) {
    sl_set_error(self, "invalid screen width");
    return SL_ERROR_INVALID;
  }
  impl->screen_width = width;
  impl->dynamic_width = width == 0;
  return SL_OK;
}

static int sl_set_bounds_method(sl_t *self, int x, int y, int width,
                                int height) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl || x < 0 || y < 0 || width < 0 || height < 0) {
    sl_set_error(self, "invalid terminal bounds");
    return SL_ERROR_INVALID;
  }
  impl->screen_x = x;
  impl->screen_y = y;
  impl->screen_width = width;
  impl->screen_height = height;
  impl->dynamic_width = width == 0;
  impl->dynamic_height = height == 0;
  impl->bounded = height > 0 || impl->dynamic_height;
  sl_render_store_clear(impl);
  return SL_OK;
}

static int sl_set_idle_callback_method(sl_t *self, sl_idle_callback_t callback,
                                       void *userdata) {
  sl_impl_t *impl;
  impl = sl_impl(self);
  if (!impl)
    return SL_ERROR_INVALID;
  impl->idle_callback = callback;
  impl->idle_userdata = userdata;
  return SL_OK;
}

static const char *sl_last_error_method(const sl_t *self) {
  const sl_impl_t *impl;
  impl = sl_impl_const(self);
  if (!impl || impl->error[0] == '\0')
    return NULL;
  return impl->error;
}

static sl_readline_status_t sl_last_readline_status_method(const sl_t *self) {
  const sl_impl_t *impl;
  impl = sl_impl_const(self);
  if (!impl)
    return SL_READLINE_NONE;
  return impl->last_readline_status;
}

void sl_config_init(sl_config_t *config) {
  if (!config)
    return;
  config->input_fd = STDIN_FILENO;
  config->output_fd = STDOUT_FILENO;
  config->screen_x = 0;
  config->screen_y = 0;
  config->screen_width = 0;
  config->screen_height = 0;
  config->bounded = 0;
  config->history_max_len = SL_HISTORY_DEFAULT_MAX;
  config->line_max_len = SL_LINE_DEFAULT_MAX;
}

static sl_t *sl_create_with_config_impl(const sl_config_t *config) {
  sl_config_t local;
  sl_t *self;
  sl_impl_t *impl;
  if (!config) {
    sl_config_init(&local);
    config = &local;
  }
  if (config->history_max_len < 0 || config->screen_x < 0 ||
      config->screen_y < 0 || config->screen_width < 0 ||
      config->screen_height < 0 || config->bounded < 0 ||
      config->line_max_len == 0 || config->line_max_len > (size_t)INT_MAX - 2)
    return NULL;
  self = (sl_t *)calloc(1, sizeof(*self));
  impl = (sl_impl_t *)calloc(1, sizeof(*impl));
  if (!self || !impl) {
    free(self);
    free(impl);
    return NULL;
  }
  self->readline = sl_readline_method;
  self->destroy = sl_destroy_method;
  self->free_string = sl_free_string_method;
  self->history_add = sl_history_add_impl;
  self->history_set_max_len = sl_history_set_max;
  self->history_save = sl_history_save_impl;
  self->history_load = sl_history_load_impl;
  self->set_bounds = sl_set_bounds_method;
  self->set_screen_width = sl_set_screen_width_method;
  self->set_idle_callback = sl_set_idle_callback_method;
  self->bind_key = sl_bind_key_method;
  self->insert = sl_buf_insert_cstr;
  self->set_buffer = sl_buf_set_public;
  self->buffer = sl_buffer_method;
  self->cursor = sl_cursor_method;
  self->set_cursor = sl_set_cursor_method;
  self->submit = sl_submit_method;
  self->cancel = sl_cancel_method;
  self->print_above = sl_print_above_method;
  self->last_readline_status = sl_last_readline_status_method;
  self->last_error = sl_last_error_method;
  self->impl = impl;
  impl->input_fd = config->input_fd >= 0 ? config->input_fd : STDIN_FILENO;
  impl->output_fd = config->output_fd >= 0 ? config->output_fd : STDOUT_FILENO;
  impl->screen_x = config->screen_x;
  impl->screen_y = config->screen_y;
  impl->screen_width = config->screen_width;
  impl->screen_height = config->screen_height;
  impl->line_max_len = config->line_max_len;
  impl->dynamic_width = config->screen_width == 0;
  impl->dynamic_height = config->bounded && config->screen_height == 0;
  impl->bounded = config->bounded || config->screen_height > 0;
  impl->history.max_len = config->history_max_len;
  impl->history_index = -1;
  if (sl_buf_reserve(self, 1) != 0) {
    sl_destroy_method(self);
    return NULL;
  }
  impl->buf[0] = '\0';
  return self;
}

sl_t *sl_create(void) {
  sl_config_t config;
  sl_config_init(&config);
  return sl_create_with_config_impl(&config);
}

sl_t *sl_create_with_config(const sl_config_t *config) {
  return sl_create_with_config_impl(config);
}

char *sl_readline(sl_t *self, const char *prompt) {
  if (!self || !self->readline)
    return NULL;
  return self->readline(self, prompt);
}

void sl_destroy(sl_t *self) {
  if (self && self->destroy)
    self->destroy(self);
}

void sl_free_string(sl_t *self, char *ptr) {
  if (self && self->free_string)
    self->free_string(self, ptr);
  else
    free(ptr);
}

int sl_history_add(sl_t *self, const char *line) {
  if (!self || !self->history_add)
    return SL_ERROR_INVALID;
  return self->history_add(self, line);
}

int sl_history_set_max_len(sl_t *self, int max_len) {
  if (!self || !self->history_set_max_len)
    return SL_ERROR_INVALID;
  return self->history_set_max_len(self, max_len);
}

int sl_history_save(sl_t *self, const char *filename) {
  if (!self || !self->history_save)
    return SL_ERROR_INVALID;
  return self->history_save(self, filename);
}

int sl_history_load(sl_t *self, const char *filename) {
  if (!self || !self->history_load)
    return SL_ERROR_INVALID;
  return self->history_load(self, filename);
}

int sl_set_bounds(sl_t *self, int x, int y, int width, int height) {
  if (!self || !self->set_bounds)
    return SL_ERROR_INVALID;
  return self->set_bounds(self, x, y, width, height);
}

int sl_set_screen_width(sl_t *self, int width) {
  if (!self || !self->set_screen_width)
    return SL_ERROR_INVALID;
  return self->set_screen_width(self, width);
}

int sl_set_idle_callback(sl_t *self, sl_idle_callback_t callback,
                         void *userdata) {
  if (!self || !self->set_idle_callback)
    return SL_ERROR_INVALID;
  return self->set_idle_callback(self, callback, userdata);
}

int sl_bind_key(sl_t *self, sl_key_t key, sl_key_callback_t callback,
                void *userdata) {
  if (!self || !self->bind_key)
    return SL_ERROR_INVALID;
  return self->bind_key(self, key, callback, userdata);
}

int sl_insert(sl_t *self, const char *text) {
  if (!self || !self->insert)
    return SL_ERROR_INVALID;
  return self->insert(self, text);
}

int sl_set_buffer(sl_t *self, const char *text) {
  if (!self || !self->set_buffer)
    return SL_ERROR_INVALID;
  return self->set_buffer(self, text);
}

const char *sl_buffer(const sl_t *self) {
  if (!self || !self->buffer)
    return NULL;
  return self->buffer(self);
}

size_t sl_cursor(const sl_t *self) {
  if (!self || !self->cursor)
    return 0;
  return self->cursor(self);
}

int sl_set_cursor(sl_t *self, size_t cursor) {
  if (!self || !self->set_cursor)
    return SL_ERROR_INVALID;
  return self->set_cursor(self, cursor);
}

int sl_submit(sl_t *self) {
  if (!self || !self->submit)
    return SL_ERROR_INVALID;
  return self->submit(self);
}

int sl_cancel(sl_t *self) {
  if (!self || !self->cancel)
    return SL_ERROR_INVALID;
  return self->cancel(self);
}

int sl_print_above(sl_t *self, sl_stream_callback_t callback, void *userdata) {
  if (!self || !self->print_above)
    return SL_ERROR_INVALID;
  return self->print_above(self, callback, userdata);
}

sl_readline_status_t sl_last_readline_status(const sl_t *self) {
  if (!self || !self->last_readline_status)
    return SL_READLINE_NONE;
  return self->last_readline_status(self);
}

const char *sl_last_error(const sl_t *self) {
  if (!self || !self->last_error)
    return NULL;
  return self->last_error(self);
}
