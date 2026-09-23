#include "softline_surface.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
  SL_SURFACE_BOLD = 1u << 0,
  SL_SURFACE_DIM = 1u << 1,
  SL_SURFACE_ITALIC = 1u << 2,
  SL_SURFACE_UNDERLINE = 1u << 3,
  SL_SURFACE_REVERSE = 1u << 4,
  SL_SURFACE_STRIKE = 1u << 5
};

typedef struct sl_surface_color {
  int mode; /* 0 default, 1 basic SGR, 2 indexed, 3 RGB */
  int values[3];
} sl_surface_color_t;

typedef struct sl_surface_style {
  unsigned int flags;
  sl_surface_color_t fg;
  sl_surface_color_t bg;
} sl_surface_style_t;

typedef struct sl_surface_cell {
  char bytes[32];
  unsigned char len;
  unsigned char width; /* zero for a wide-glyph continuation cell */
  sl_surface_style_t style;
} sl_surface_cell_t;

struct sl_surface {
  int fd;
  int x;
  int y;
  int width;
  int height;
  int col;
  sl_surface_cell_t *cells;
  sl_surface_style_t style;
  int (*cell_width)(unsigned long);
  int parser; /* 0 text, 1 ESC, 2 CSI */
  char csi[128];
  size_t csi_len;
  char utf8[4];
  unsigned int utf8_len;
  unsigned int utf8_need;
  int draw_valid;
  int draw_row;
  int draw_col;
  sl_surface_style_t draw_style;
};

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

static int sl_surface_at(sl_surface_t *surface, int row, int col) {
  char seq[64];
  int count;
  count = snprintf(seq, sizeof(seq), "\033[%d;%dH", surface->y + row + 1,
                   surface->x + col + 1);
  if (count <= 0 || count >= (int)sizeof(seq))
    return -1;
  return sl_surface_write_all(surface->fd, seq, (size_t)count);
}

static int sl_surface_sgr(sl_surface_t *surface,
                          const sl_surface_style_t *style) {
  char seq[160];
  int count;
  int next;
  count = snprintf(seq, sizeof(seq), "\033[0");
  if (count < 0 || count >= (int)sizeof(seq))
    return -1;
#define SL_SURFACE_SGR(value)                                                  \
  do {                                                                         \
    next = snprintf(seq + count, sizeof(seq) - (size_t)count, ";%d", (value)); \
    if (next < 0 || next >= (int)(sizeof(seq) - (size_t)count))                \
      return -1;                                                               \
    count += next;                                                             \
  } while (0)
  if (style->flags & SL_SURFACE_BOLD)
    SL_SURFACE_SGR(1);
  if (style->flags & SL_SURFACE_DIM)
    SL_SURFACE_SGR(2);
  if (style->flags & SL_SURFACE_ITALIC)
    SL_SURFACE_SGR(3);
  if (style->flags & SL_SURFACE_UNDERLINE)
    SL_SURFACE_SGR(4);
  if (style->flags & SL_SURFACE_REVERSE)
    SL_SURFACE_SGR(7);
  if (style->flags & SL_SURFACE_STRIKE)
    SL_SURFACE_SGR(9);
  if (style->fg.mode == 1)
    SL_SURFACE_SGR(style->fg.values[0]);
  else if (style->fg.mode == 2) {
    SL_SURFACE_SGR(38);
    SL_SURFACE_SGR(5);
    SL_SURFACE_SGR(style->fg.values[0]);
  } else if (style->fg.mode == 3) {
    SL_SURFACE_SGR(38);
    SL_SURFACE_SGR(2);
    SL_SURFACE_SGR(style->fg.values[0]);
    SL_SURFACE_SGR(style->fg.values[1]);
    SL_SURFACE_SGR(style->fg.values[2]);
  }
  if (style->bg.mode == 1)
    SL_SURFACE_SGR(style->bg.values[0]);
  else if (style->bg.mode == 2) {
    SL_SURFACE_SGR(48);
    SL_SURFACE_SGR(5);
    SL_SURFACE_SGR(style->bg.values[0]);
  } else if (style->bg.mode == 3) {
    SL_SURFACE_SGR(48);
    SL_SURFACE_SGR(2);
    SL_SURFACE_SGR(style->bg.values[0]);
    SL_SURFACE_SGR(style->bg.values[1]);
    SL_SURFACE_SGR(style->bg.values[2]);
  }
#undef SL_SURFACE_SGR
  if (count + 1 >= (int)sizeof(seq))
    return -1;
  seq[count++] = 'm';
  return sl_surface_write_all(surface->fd, seq, (size_t)count);
}

static int sl_surface_draw_cell(sl_surface_t *surface, int row, int col,
                                const sl_surface_cell_t *cell) {
  if (cell->len == 0)
    return 0;
  if ((!surface->draw_valid || surface->draw_row != row ||
       surface->draw_col != col) &&
      sl_surface_at(surface, row, col) != 0)
    return -1;
  if ((!surface->draw_valid ||
       memcmp(&surface->draw_style, &cell->style, sizeof(cell->style)) != 0) &&
      sl_surface_sgr(surface, &cell->style) != 0)
    return -1;
  if (sl_surface_write_all(surface->fd, cell->bytes, cell->len) != 0)
    return -1;
  surface->draw_valid = 1;
  surface->draw_row = row;
  surface->draw_col = col + (int)cell->width;
  surface->draw_style = cell->style;
  return 0;
}

static int sl_surface_clear_rect(sl_surface_t *surface, int x, int y, int width,
                                 int height) {
  static const char blanks[] =
      "                                                                ";
  int row;
  int remaining;
  sl_surface_t target;
  target = *surface;
  target.x = x;
  target.y = y;
  surface->draw_valid = 0;
  if (sl_surface_write_all(surface->fd, "\033[0m", 4) != 0)
    return -1;
  for (row = 0; row < height; row++) {
    if (sl_surface_at(&target, row, 0) != 0)
      return -1;
    remaining = width;
    while (remaining > 0) {
      size_t amount;
      amount = remaining > 64 ? 64u : (size_t)remaining;
      if (sl_surface_write_all(surface->fd, blanks, amount) != 0)
        return -1;
      remaining -= (int)amount;
    }
  }
  return 0;
}

static int sl_surface_repaint(sl_surface_t *surface) {
  int row;
  int col;
  if (sl_surface_clear_rect(surface, surface->x, surface->y, surface->width,
                            surface->height) != 0)
    return -1;
  for (row = 0; row < surface->height; row++) {
    for (col = 0; col < surface->width; col++) {
      sl_surface_cell_t *cell;
      cell =
          &surface->cells[(size_t)row * (size_t)surface->width + (size_t)col];
      if (sl_surface_draw_cell(surface, row, col, cell) != 0)
        return -1;
    }
  }
  surface->draw_valid = 0;
  return sl_surface_write_all(surface->fd, "\033[0m", 4);
}

static int sl_surface_scroll(sl_surface_t *surface) {
  size_t row_cells;
  if (surface->height <= 0 || surface->width <= 0)
    return -1;
  row_cells = (size_t)surface->width;
  if (surface->height > 1)
    memmove(surface->cells, surface->cells + row_cells,
            row_cells * (size_t)(surface->height - 1) *
                sizeof(*surface->cells));
  memset(surface->cells + row_cells * (size_t)(surface->height - 1), 0,
         row_cells * sizeof(*surface->cells));
  return sl_surface_repaint(surface);
}

static int sl_surface_put(sl_surface_t *surface, const char *bytes,
                          unsigned int length, unsigned long codepoint) {
  sl_surface_cell_t *cell;
  int cells;
  cells = surface->cell_width(codepoint);
  if (cells < 0)
    cells = 1;
  if (cells == 0 && surface->col > 0 && surface->cells) {
    int base_col;
    base_col = surface->col - 1;
    if (base_col > 0 &&
        surface->cells[(size_t)(surface->height - 1) * (size_t)surface->width +
                       (size_t)base_col]
                .width == 0)
      base_col--;
    cell =
        &surface->cells[(size_t)(surface->height - 1) * (size_t)surface->width +
                        (size_t)base_col];
    if ((size_t)cell->len + length <= sizeof(cell->bytes)) {
      memcpy(cell->bytes + cell->len, bytes, length);
      cell->len = (unsigned char)(cell->len + length);
      return sl_surface_draw_cell(surface, surface->height - 1, base_col, cell);
    }
    return -1;
  }
  if (cells == 0)
    cells = 1;
  if (cells > surface->width)
    return -2;
  if (surface->col + cells > surface->width) {
    surface->col = 0;
    if (sl_surface_scroll(surface) != 0)
      return -1;
  }
  cell =
      &surface->cells[(size_t)(surface->height - 1) * (size_t)surface->width +
                      (size_t)surface->col];
  memset(cell, 0, sizeof(*cell));
  memcpy(cell->bytes, bytes, length);
  cell->len = (unsigned char)length;
  cell->width = (unsigned char)cells;
  cell->style = surface->style;
  if (cells == 2)
    memset(cell + 1, 0, sizeof(*cell));
  if (sl_surface_draw_cell(surface, surface->height - 1, surface->col, cell) !=
      0)
    return -1;
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
    if (code == 0)
      memset(&surface->style, 0, sizeof(surface->style));
    else if (code == 1)
      surface->style.flags |= SL_SURFACE_BOLD;
    else if (code == 2)
      surface->style.flags |= SL_SURFACE_DIM;
    else if (code == 3)
      surface->style.flags |= SL_SURFACE_ITALIC;
    else if (code == 4)
      surface->style.flags |= SL_SURFACE_UNDERLINE;
    else if (code == 7)
      surface->style.flags |= SL_SURFACE_REVERSE;
    else if (code == 9)
      surface->style.flags |= SL_SURFACE_STRIKE;
    else if (code == 22)
      surface->style.flags &= ~(SL_SURFACE_BOLD | SL_SURFACE_DIM);
    else if (code == 23)
      surface->style.flags &= ~SL_SURFACE_ITALIC;
    else if (code == 24)
      surface->style.flags &= ~SL_SURFACE_UNDERLINE;
    else if (code == 27)
      surface->style.flags &= ~SL_SURFACE_REVERSE;
    else if (code == 29)
      surface->style.flags &= ~SL_SURFACE_STRIKE;
    else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
      surface->style.fg.mode = 1;
      surface->style.fg.values[0] = code;
    } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
      surface->style.bg.mode = 1;
      surface->style.bg.values[0] = code;
    } else if (code == 39)
      memset(&surface->style.fg, 0, sizeof(surface->style.fg));
    else if (code == 49)
      memset(&surface->style.bg, 0, sizeof(surface->style.bg));
    else if (code == 38 || code == 48) {
      sl_surface_color_t *color;
      color = code == 38 ? &surface->style.fg : &surface->style.bg;
      if (i + 2 < count && params[i + 1] == 5) {
        color->mode = 2;
        color->values[0] = params[i + 2];
        i += 2;
      } else if (i + 4 < count && params[i + 1] == 2) {
        color->mode = 3;
        color->values[0] = params[i + 2];
        color->values[1] = params[i + 3];
        color->values[2] = params[i + 4];
        i += 4;
      } else
        return -2;
    }
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
    if (surface->csi_len >= sizeof(surface->csi))
      return -2;
    surface->csi[surface->csi_len++] = (char)byte;
    return 0;
  }
  if (surface->utf8_need != 0) {
    if ((byte & 0xc0u) != 0x80u)
      return -2;
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
        (cp >= 0xd800ul && cp <= 0xdffful) || cp > 0x10fffful)
      return -2;
    surface->utf8_need = 0;
    return sl_surface_put(surface, surface->utf8, surface->utf8_len, cp);
  }
  if (byte == 0x1bu) {
    surface->parser = 1;
    return 0;
  }
  if (byte == '\n') {
    surface->col = 0;
    return sl_surface_scroll(surface);
  }
  if (byte == '\r') {
    surface->col = 0;
    return 0;
  }
  if (byte == '\t') {
    int spaces;
    spaces = 8 - (surface->col % 8);
    while (spaces-- > 0) {
      if (sl_surface_put(surface, " ", 1, ' ') != 0)
        return -1;
    }
    return 0;
  }
  if (byte < 32u || byte == 127u)
    return -2;
  if (byte < 128u) {
    char ch;
    ch = (char)byte;
    return sl_surface_put(surface, &ch, 1, byte);
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

sl_surface_t *sl_surface_create(int fd, int x, int y, int width, int height,
                                int (*cell_width)(unsigned long)) {
  sl_surface_t *surface;
  size_t count;
  if (fd < 0 || x < 0 || y < 0 || width < 1 || height < 0 || !cell_width)
    return NULL;
  if ((size_t)height > ((size_t)-1) / (size_t)width / sizeof(sl_surface_cell_t))
    return NULL;
  surface = (sl_surface_t *)calloc(1, sizeof(*surface));
  if (!surface)
    return NULL;
  count = (size_t)width * (size_t)height;
  if (count > 0) {
    surface->cells =
        (sl_surface_cell_t *)calloc(count, sizeof(*surface->cells));
    if (!surface->cells) {
      free(surface);
      return NULL;
    }
  }
  surface->fd = fd;
  surface->x = x;
  surface->y = y;
  surface->width = width;
  surface->height = height;
  surface->cell_width = cell_width;
  return surface;
}

void sl_surface_destroy(sl_surface_t *surface) {
  if (!surface)
    return;
  free(surface->cells);
  free(surface);
}

int sl_surface_matches(const sl_surface_t *surface, int x, int y, int width,
                       int height) {
  return surface && surface->x == x && surface->y == y &&
         surface->width == width && surface->height == height;
}

int sl_surface_resize(sl_surface_t *surface, int x, int y, int width,
                      int height) {
  sl_surface_cell_t *new_cells;
  size_t count;
  int rows;
  int cols;
  int row;
  if (!surface || x < 0 || y < 0 || width < 1 || height < 0)
    return -1;
  if (sl_surface_matches(surface, x, y, width, height))
    return 0;
  if ((size_t)height > ((size_t)-1) / (size_t)width / sizeof(sl_surface_cell_t))
    return -1;
  count = (size_t)width * (size_t)height;
  new_cells =
      count > 0 ? (sl_surface_cell_t *)calloc(count, sizeof(*new_cells)) : NULL;
  if (count > 0 && !new_cells)
    return -1;
  rows = height < surface->height ? height : surface->height;
  cols = width < surface->width ? width : surface->width;
  for (row = 0; row < rows; row++) {
    int col;
    memcpy(new_cells + (size_t)(height - rows + row) * (size_t)width,
           surface->cells +
               (size_t)(surface->height - rows + row) * (size_t)surface->width,
           (size_t)cols * sizeof(*new_cells));
    for (col = 0; col < cols; col++) {
      sl_surface_cell_t *cell;
      cell = &new_cells[(size_t)(height - rows + row) * (size_t)width +
                        (size_t)col];
      if (cell->width == 2 && col + 1 >= cols) {
        cell->bytes[0] = ' ';
        cell->len = 1;
        cell->width = 1;
      }
    }
  }
  if (sl_surface_clear_rect(surface, surface->x, surface->y, surface->width,
                            surface->height) != 0) {
    free(new_cells);
    return -1;
  }
  free(surface->cells);
  surface->cells = new_cells;
  surface->x = x;
  surface->y = y;
  surface->width = width;
  surface->height = height;
  if (surface->col > width)
    surface->col = width;
  return sl_surface_repaint(surface);
}

int sl_surface_write(sl_surface_t *surface, const char *bytes, size_t length) {
  size_t i;
  if (!surface || (!bytes && length > 0))
    return -2;
  if (length == 0)
    return 0;
  if (surface->height <= 0)
    return -2;
  surface->draw_valid = 0;
  for (i = 0; i < length; i++) {
    {
      int status;
      status = sl_surface_byte(surface, (unsigned char)bytes[i]);
      if (status != 0)
        return status;
    }
  }
  surface->draw_valid = 0;
  return sl_surface_write_all(surface->fd, "\033[0m", 4);
}

int sl_surface_complete(const sl_surface_t *surface) {
  return surface && surface->parser == 0 && surface->utf8_need == 0;
}
