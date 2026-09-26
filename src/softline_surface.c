#include "softline_surface.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct sl_surface {
  int fd;
  int width;
  int height;
  int terminal_rows;
  int terminal_columns;
  int native_stream;
  int native_row;
  int native_saved;
  int col;
  int (*cell_width)(unsigned long);
  int parser; /* 0 text, 1 ESC, 2 CSI */
  char csi[128];
  size_t csi_len;
  char utf8[4];
  unsigned int utf8_len;
  unsigned int utf8_need;
};

static int sl_surface_terminal_size(int fd, struct winsize *terminal) {
  if (ioctl(fd, TIOCGWINSZ, terminal) != 0)
    return -1;
  if (terminal->ws_col == 0)
    terminal->ws_col = 80;
  if (terminal->ws_row == 0)
    terminal->ws_row = 24;
  return 0;
}

static int sl_surface_write_all(int fd, const char *bytes, size_t length) {
  while (length > 0) {
    ssize_t written;
    written = write(fd, bytes, length);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      return -1;
    bytes += written;
    length -= (size_t)written;
  }
  return 0;
}

static int sl_surface_put(sl_surface_t *surface, unsigned long codepoint) {
  int cells;
  if (!surface->native_stream)
    return 0;
  cells = surface->cell_width(codepoint);
  if (cells < 0)
    cells = 1;
  if (surface->col + cells > surface->width) {
    surface->col = 0;
    if (surface->native_row + 1 < surface->height)
      surface->native_row++;
  }
  surface->col += cells;
  return 0;
}

static int sl_surface_parse_sgr(sl_surface_t *surface) {
  int params[32];
  size_t count;
  size_t i;
  int value;
  count = 0;
  value = 0;
  if (surface->csi_len == 0)
    params[count++] = 0;
  for (i = 0; i < surface->csi_len; i++) {
    unsigned char ch;
    ch = (unsigned char)surface->csi[i];
    if (ch == ';') {
      if (count >= sizeof(params) / sizeof(params[0]))
        return -2;
      params[count++] = value;
      value = 0;
    } else if (ch >= '0' && ch <= '9') {
      if (value > 100000)
        return -2;
      value = value * 10 + (int)(ch - '0');
    } else {
      return -2;
    }
  }
  if (surface->csi_len > 0) {
    if (count >= sizeof(params) / sizeof(params[0]))
      return -2;
    params[count++] = value;
  }
  for (i = 0; i < count; i++) {
    int code;
    code = params[i];
    if (code == 0 || code == 1 || code == 2 || code == 3 || code == 4 ||
        code == 7 || code == 9 || code == 22 || code == 23 || code == 24 ||
        code == 27 || code == 29 || (code >= 30 && code <= 37) || code == 39 ||
        (code >= 40 && code <= 47) || code == 49 ||
        (code >= 90 && code <= 97) || (code >= 100 && code <= 107))
      continue;
    if (code == 38 || code == 48) {
      if (i + 2 < count && params[i + 1] == 5 && params[i + 2] <= 255)
        i += 2;
      else if (i + 4 < count && params[i + 1] == 2 && params[i + 2] <= 255 &&
               params[i + 3] <= 255 && params[i + 4] <= 255)
        i += 4;
      else
        return -2;
    } else
      return -2;
  }
  return 0;
}

static int sl_surface_byte(sl_surface_t *surface, unsigned char byte) {
  unsigned long cp;
  unsigned int i;
  if (surface->parser == 1) {
    if (byte != '[') {
      surface->parser = 0;
      return -2;
    }
    surface->parser = 2;
    surface->csi_len = 0;
    return 0;
  }
  if (surface->parser == 2) {
    if (byte == 'm') {
      surface->parser = 0;
      return sl_surface_parse_sgr(surface);
    }
    if (byte != ';' && (byte < '0' || byte > '9')) {
      surface->parser = 0;
      return -2;
    }
    if (surface->csi_len >= sizeof(surface->csi)) {
      surface->parser = 0;
      return -2;
    }
    surface->csi[surface->csi_len++] = (char)byte;
    return 0;
  }
  if (surface->utf8_need != 0) {
    if ((byte & 0xc0u) != 0x80u || surface->utf8_len >= sizeof(surface->utf8)) {
      surface->utf8_need = 0;
      surface->utf8_len = 0;
      return -2;
    }
    surface->utf8[surface->utf8_len++] = (char)byte;
    if (surface->utf8_len < surface->utf8_need)
      return 0;
    cp = (unsigned char)surface->utf8[0] & (surface->utf8_need == 2   ? 0x1fu
                                            : surface->utf8_need == 3 ? 0x0fu
                                                                      : 0x07u);
    for (i = 1; i < surface->utf8_need; i++)
      cp = (cp << 6) | ((unsigned char)surface->utf8[i] & 0x3fu);
    if ((surface->utf8_need == 2 && cp < 0x80ul) ||
        (surface->utf8_need == 3 && cp < 0x800ul) ||
        (surface->utf8_need == 4 && cp < 0x10000ul) ||
        (cp >= 0x80ul && cp <= 0x9ful) || (cp >= 0xd800ul && cp <= 0xdffful) ||
        cp > 0x10fffful) {
      surface->utf8_need = 0;
      surface->utf8_len = 0;
      return -2;
    }
    surface->utf8_need = 0;
    surface->utf8_len = 0;
    return sl_surface_put(surface, cp);
  }
  if (byte == 0x1bu) {
    surface->parser = 1;
    return 0;
  }
  if (byte == '\n') {
    if (surface->native_stream) {
      surface->col = 0;
      if (surface->native_row + 1 < surface->height)
        surface->native_row++;
    }
    return 0;
  }
  if (byte == '\r') {
    surface->col = 0;
    return 0;
  }
  if (byte == '\t') {
    if (surface->native_stream) {
      surface->col += 8 - (surface->col % 8);
      if (surface->col >= surface->width)
        surface->col = surface->width - 1;
    }
    return 0;
  }
  if (byte < 32u || byte == 127u)
    return -2;
  if (byte < 128u) {
    return sl_surface_put(surface, byte);
  }
  if (byte >= 0xc2u && byte <= 0xdfu)
    surface->utf8_need = 2;
  else if (byte >= 0xe0u && byte <= 0xefu)
    surface->utf8_need = 3;
  else if (byte >= 0xf0u && byte <= 0xf4u)
    surface->utf8_need = 4;
  else
    return -2;
  surface->utf8[0] = (char)byte;
  surface->utf8_len = 1;
  return 0;
}

sl_surface_t *sl_surface_create_validator(void) {
  return (sl_surface_t *)calloc(1, sizeof(sl_surface_t));
}

int sl_surface_is_native(const sl_surface_t *surface) {
  return surface && surface->native_stream;
}

static int sl_surface_native_region(sl_surface_t *surface) {
  char seq[48];
  int count;
  if (surface->height < 1)
    return -1;
  count = surface->height > 1
              ? snprintf(seq, sizeof(seq), "\033[1;%dr", surface->height)
              : snprintf(seq, sizeof(seq), "\033[r");
  return count > 0 && count < (int)sizeof(seq)
             ? sl_surface_write_all(surface->fd, seq, (size_t)count)
             : -1;
}

sl_surface_t *sl_surface_create_native(int fd, int width, int height, int row,
                                       int col,
                                       int (*cell_width)(unsigned long)) {
  sl_surface_t *surface;
  struct winsize terminal;
  char seq[64];
  int count;
  int shift;
  surface = sl_surface_create_validator();
  if (!surface)
    return NULL;
  surface->fd = fd;
  surface->width = width;
  surface->height = height;
  surface->native_stream = 1;
  surface->native_row = row < height ? row : height - 1;
  surface->col = col;
  surface->cell_width = cell_width;
  if (sl_surface_terminal_size(fd, &terminal) != 0 ||
      sl_surface_write_all(fd, "\0337", 2) != 0) {
    sl_surface_destroy(surface);
    return NULL;
  }
  shift = row >= height ? row - height + 1 : 0;
  if (shift > 0) {
    count = snprintf(seq, sizeof(seq), "\033[r\033[%dS\0338\033[%dA\0337",
                     shift, shift);
    if (count <= 0 || count >= (int)sizeof(seq) ||
        sl_surface_write_all(fd, seq, (size_t)count) != 0) {
      sl_surface_destroy(surface);
      return NULL;
    }
  }
  if (sl_surface_native_region(surface) != 0 ||
      sl_surface_write_all(fd, "\0338\033[0m", 6) != 0) {
    sl_surface_destroy(surface);
    return NULL;
  }
  surface->terminal_rows = terminal.ws_row;
  surface->terminal_columns = terminal.ws_col;
  surface->native_saved = 1;
  return surface;
}

void sl_surface_native_cursor(sl_surface_t *surface, int row, int col) {
  if (!sl_surface_is_native(surface) || row < 0 || col < 0)
    return;
  surface->native_row = row < surface->height ? row : surface->height - 1;
  surface->col = col;
}

void sl_surface_native_position(const sl_surface_t *surface, int *row,
                                int *col) {
  if (row)
    *row = surface->native_row;
  if (col)
    *col = surface->col;
}

int sl_surface_native_write(sl_surface_t *surface, const char *bytes,
                            size_t length) {
  if (!sl_surface_is_native(surface) ||
      sl_surface_native_region(surface) != 0 ||
      sl_surface_write_all(surface->fd, "\0338", 2) != 0 ||
      sl_surface_write_all(surface->fd, bytes, length) != 0 ||
      sl_surface_write_all(surface->fd, "\0337\033[0m", 6) != 0)
    return -1;
  return 0;
}

void sl_surface_destroy(sl_surface_t *surface) {
  if (!surface)
    return;
  if (surface->native_stream) {
    (void)sl_surface_write_all(surface->fd, "\033[r", 3);
    if (surface->native_saved) {
      (void)sl_surface_write_all(surface->fd, "\0338\033[0m\r", 7);
      if (surface->col > 0)
        (void)sl_surface_write_all(surface->fd, "\n", 1);
    }
    (void)sl_surface_write_all(surface->fd, "\033[?25h", 6);
  }
  free(surface);
}

int sl_surface_matches(const sl_surface_t *surface, int width, int height) {
  struct winsize terminal;
  return surface && surface->width == width && surface->height == height &&
         sl_surface_terminal_size(surface->fd, &terminal) == 0 &&
         surface->terminal_rows == (int)terminal.ws_row &&
         surface->terminal_columns == (int)terminal.ws_col;
}

void sl_surface_geometry(const sl_surface_t *surface, int *width, int *height,
                         int *terminal_rows) {
  if (!surface)
    return;
  if (width)
    *width = surface->width;
  if (height)
    *height = surface->height;
  if (terminal_rows)
    *terminal_rows = surface->terminal_rows;
}

int sl_surface_native_resize_pending(const sl_surface_t *surface) {
  struct winsize terminal;
  return surface && surface->native_stream &&
         surface->width == surface->terminal_columns &&
         sl_surface_terminal_size(surface->fd, &terminal) == 0 &&
         ((int)terminal.ws_col != surface->terminal_columns ||
          (int)terminal.ws_row != surface->terminal_rows);
}

int sl_surface_resize(sl_surface_t *surface, int width, int height) {
  struct winsize terminal;
  char seq[48];
  int shift;
  int count;
  if (!sl_surface_is_native(surface) || width < 1 || height < 1 ||
      sl_surface_terminal_size(surface->fd, &terminal) != 0)
    return -1;
  if (surface->native_row >= (int)terminal.ws_row)
    surface->native_row = (int)terminal.ws_row - 1;
  shift = surface->native_row >= height ? surface->native_row - height + 1 : 0;
  surface->width = width;
  surface->height = height;
  surface->terminal_rows = terminal.ws_row;
  surface->terminal_columns = terminal.ws_col;
  if (sl_surface_native_region(surface) != 0 ||
      sl_surface_write_all(surface->fd, "\0338", 2) != 0)
    return -1;
  if (shift > 0) {
    count = snprintf(seq, sizeof(seq), "\033[%dA", shift);
    if (count <= 0 || count >= (int)sizeof(seq) ||
        sl_surface_write_all(surface->fd, seq, (size_t)count) != 0)
      return -1;
    surface->native_row -= shift;
  }
  return sl_surface_write_all(surface->fd, "\0337\033[0m", 6);
}

int sl_surface_validate(sl_surface_t *surface, const char *bytes, size_t length,
                        size_t *accepted) {
  size_t i;
  int status;
  if (!surface || (!bytes && length > 0) || !accepted)
    return -2;
  *accepted = 0;
  for (i = 0; i < length; i++) {
    status = sl_surface_byte(surface, (unsigned char)bytes[i]);
    if (status != 0) {
      *accepted = i;
      return status;
    }
  }
  *accepted = length;
  return 0;
}

int sl_surface_complete(const sl_surface_t *surface) {
  return surface && surface->parser == 0 && surface->utf8_need == 0;
}

void sl_surface_reset_partial(sl_surface_t *surface) {
  if (!surface)
    return;
  surface->parser = 0;
  surface->csi_len = 0;
  surface->utf8_len = 0;
  surface->utf8_need = 0;
}
