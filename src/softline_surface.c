#include "softline_surface.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#define SL_ROW(surface)                                                        \
  ((surface)->terminal_rows - 1 - (surface)->producer_below)

struct sl_surface {
  int fd;
  int width;
  int height;
  int terminal_rows;
  int terminal_columns;
  int native_stream;
  int producer_cursor_live;
  int producer_below;
  int tracking;
  unsigned int attributes;
  int foreground[4];
  int background[4];
  int col;
  size_t line_cells;
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
  if (!surface->tracking)
    return 0;
  cells = surface->cell_width(codepoint);
  if (cells < 0)
    cells = 1;
  if (surface->col + cells > surface->width) {
    surface->col = 0;
    if (SL_ROW(surface) + 1 < surface->height)
      surface->producer_below--;
  }
  surface->col += cells;
  surface->line_cells += (size_t)cells;
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
  if (!surface->tracking)
    return 0;
  for (i = 0; i < count; i++) {
    int code = params[i];
    int *color = code == 38 || code == 39 || (code >= 30 && code <= 37) ||
                         (code >= 90 && code <= 97)
                     ? surface->foreground
                     : surface->background;
    if (code == 0) {
      surface->attributes = 0;
      memset(surface->foreground, 0, sizeof(surface->foreground));
      memset(surface->background, 0, sizeof(surface->background));
    } else if (code == 1 || code == 2 || code == 3 || code == 4 || code == 7 ||
               code == 9) {
      surface->attributes |= 1u << code;
    } else if (code == 22) {
      surface->attributes &= ~((1u << 1) | (1u << 2));
    } else if (code == 23 || code == 24 || code == 27 || code == 29) {
      surface->attributes &= ~(1u << (code - 20));
    } else if (code == 39 || code == 49) {
      memset(color, 0, 4 * sizeof(*color));
    } else if (code == 38 || code == 48) {
      color[0] = params[++i];
      color[1] = params[++i];
      if (color[0] == 2) {
        color[2] = params[++i];
        color[3] = params[++i];
      }
    } else {
      color[0] = 1;
      color[1] = code;
    }
  }
  return 0;
}

static int sl_surface_byte(sl_surface_t *surface, unsigned char byte,
                           int newline_returns) {
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
    if (surface->tracking) {
      if (newline_returns)
        surface->col = 0;
      else if (surface->col >= surface->width)
        surface->col = surface->width - 1;
      surface->line_cells = (size_t)surface->col;
      if (SL_ROW(surface) + 1 < surface->height)
        surface->producer_below--;
    }
    return 0;
  }
  if (byte == '\r') {
    if (surface->tracking) {
      surface->col = 0;
      surface->line_cells = 0;
    }
    return 0;
  }
  if (byte == '\t') {
    if (surface->tracking) {
      int next = surface->col + 8 - (surface->col % 8);
      if (next >= surface->width)
        next = surface->width - 1;
      /* HT cancels pending wrap, so a full-row endpoint can move back one. */
      surface->line_cells =
          surface->line_cells - (size_t)surface->col + (size_t)next;
      surface->col = next;
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
  if (surface->height < 2)
    return -1;
  count = snprintf(seq, sizeof(seq), "\033[1;%dr", surface->height);
  return count > 0 && count < (int)sizeof(seq)
             ? sl_surface_write_all(surface->fd, seq, (size_t)count)
             : -1;
}

/* Address the physical bottom before moving up. CUD inside DECSTBM cannot
 * cross its bottom margin, so returning to the prompt needs this anchor. */
static int sl_surface_cursor(char *seq, size_t capacity, int below, int col) {
  int count;
  count = snprintf(seq, capacity, "\033[65535;%dH", col + 1);
  if (below > 0 && count > 0 && (size_t)count < capacity)
    count += snprintf(seq + count, capacity - (size_t)count, "\033[%dA", below);
  return count > 0 && (size_t)count < capacity ? count : -1;
}

static int sl_surface_style(const sl_surface_t *surface, char *seq,
                            size_t capacity) {
  int count = snprintf(seq, capacity, "\033[0");
  int i;
  for (i = 1; i <= 9; i++) {
    if (surface->attributes & (1u << i))
      count += snprintf(seq + count, capacity - (size_t)count, ";%d", i);
  }
  for (i = 0; i < 2; i++) {
    const int *color = i == 0 ? surface->foreground : surface->background;
    if (color[0] == 1)
      count += snprintf(seq + count, capacity - (size_t)count, ";%d", color[1]);
    else if (color[0] == 5)
      count += snprintf(seq + count, capacity - (size_t)count, ";%d;5;%d",
                        i == 0 ? 38 : 48, color[1]);
    else if (color[0] == 2)
      count += snprintf(seq + count, capacity - (size_t)count, ";%d;2;%d;%d;%d",
                        i == 0 ? 38 : 48, color[1], color[2], color[3]);
  }
  if (count <= 0 || (size_t)count + 1 >= capacity)
    return -1;
  seq[count++] = 'm';
  return count;
}

sl_surface_t *sl_surface_create_native(int fd, int width, int height, int row,
                                       int col,
                                       int (*cell_width)(unsigned long)) {
  sl_surface_t *surface = sl_surface_create_validator();
  struct winsize terminal;
  char seq[128];
  int count, shift;
  if (!surface)
    return NULL;
  surface->fd = fd;
  surface->width = width;
  surface->height = height;
  surface->native_stream = 1;
  surface->col = col;
  surface->cell_width = cell_width;
  if (sl_surface_terminal_size(fd, &terminal) != 0)
    goto fail;
  surface->terminal_rows = terminal.ws_row;
  surface->terminal_columns = terminal.ws_col;
  shift = row >= height ? row - height + 1 : 0;
  surface->producer_below = surface->terminal_rows - 1 - row + shift;
  if (shift > 0) {
    count = snprintf(seq, sizeof(seq), "\033[r\033[%dS", shift);
    if (sl_surface_write_all(fd, seq, (size_t)count) != 0)
      goto fail;
  }
  count = sl_surface_cursor(seq, sizeof(seq), surface->producer_below, col);
  if (sl_surface_native_region(surface) != 0 || count < 0 ||
      sl_surface_write_all(fd, seq, (size_t)count) != 0 ||
      sl_surface_write_all(fd, "\033[0m", 4) != 0)
    goto fail;
  surface->line_cells = (size_t)col;
  surface->producer_cursor_live = 1;
  return surface;
fail:
  surface->native_stream = 0;
  sl_surface_destroy(surface);
  return NULL;
}

void sl_surface_native_position(const sl_surface_t *surface, int *row,
                                int *col) {
  if (row)
    *row = SL_ROW(surface);
  if (col)
    *col = surface->col;
}

/* Apply an observed bottom-relative cursor delta. For an input cursor the
 * caller excludes reflow inside its prompt. Transcript cells stay terminal
 * owned; no text is retained or moved here. */
void sl_surface_native_prompt_reflow(sl_surface_t *surface, int extra_rows) {
  if (sl_surface_is_native(surface))
    surface->producer_below += extra_rows;
}

/* SGR alone must not consume a pending wrap, nor may a following LF wrap
 * twice. Find the first text character without modifying producer bytes. */
static int sl_surface_continues_row(const char *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    unsigned char ch = (unsigned char)bytes[i++];
    if (ch == 27) {
      while (i < length && bytes[i++] != 'm')
        ;
      continue;
    }
    return ch != '\n' && ch != '\r' && ch != '\t';
  }
  return 0;
}

int sl_surface_native_write(sl_surface_t *surface, const char *bytes,
                            size_t length, int prompt_row, int prompt_col) {
  struct iovec parts[3];
  sl_surface_t next;
  char prefix[192], suffix[64];
  int count, style_count, suffix_count, first, newline_returns;
  struct termios attributes;
  size_t i;
  if (!sl_surface_is_native(surface))
    return -1;
  if (tcgetattr(surface->fd, &attributes) != 0)
    return -1;
  /* Track the line discipline without changing the producer's bytes. */
  newline_returns =
      (attributes.c_oflag & OPOST) && (attributes.c_oflag & ONLCR);
  if (SL_ROW(surface) < 0 && !surface->producer_cursor_live) {
    /* Continue a clipped, unfinished line at the first visible output row.
     * At a hard line boundary there is no text to continue: start new output
     * next to the prompt. Existing scrollback remains untouched. */
    surface->producer_below = surface->line_cells > 0
                                  ? surface->terminal_rows - 1
                                  : surface->terminal_rows - surface->height;
    surface->col = 0;
    surface->line_cells = 0;
  }
  count = 0;
  if (!surface->producer_cursor_live || prompt_row >= 0) {
    count = sl_surface_cursor(
        prefix, sizeof(prefix), surface->producer_below,
        surface->col < surface->width ? surface->col : surface->width - 1);
    if (count < 0)
      return -1;
    /* Cursor addressing cancels pending wrap. Resume on the following row when
     * a printable continuation follows a completely filled row. */
    if (surface->col == surface->width &&
        sl_surface_continues_row(bytes, length)) {
      memcpy(prefix + count, "\r\033D", 3);
      count += 3;
      surface->line_cells = 0;
    }
  }
  style_count =
      sl_surface_style(surface, prefix + count, sizeof(prefix) - (size_t)count);
  if (style_count < 0)
    return -1;
  count += style_count;
  memcpy(suffix, "\033[0m", 4);
  suffix_count = 4;
  if (prompt_row >= 0) {
    int position =
        sl_surface_cursor(suffix + 4, sizeof(suffix) - 4,
                          surface->terminal_rows - 1 - prompt_row, prompt_col);
    if (position < 0)
      return -1;
    suffix_count += position;
  }
  next = *surface;
  sl_surface_reset_partial(&next);
  next.tracking = 1;
  for (i = 0; i < length; i++) {
    if (sl_surface_byte(&next, (unsigned char)bytes[i], newline_returns) != 0)
      return -1;
  }
  parts[0].iov_base = prefix;
  parts[0].iov_len = (size_t)count;
  parts[1].iov_base = (void *)bytes;
  parts[1].iov_len = length;
  parts[2].iov_base = suffix;
  parts[2].iov_len = (size_t)suffix_count;
  first = 0;
  while (first < 3) {
    ssize_t written = writev(surface->fd, parts + first, 3 - first);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0) {
      surface->producer_cursor_live = 0;
      return -1;
    }
    while (first < 3 && (size_t)written >= parts[first].iov_len) {
      written -= (ssize_t)parts[first].iov_len;
      first++;
    }
    if (first < 3) {
      parts[first].iov_base = (char *)parts[first].iov_base + written;
      parts[first].iov_len -= (size_t)written;
    }
  }
  surface->producer_cursor_live = prompt_row < 0;
  surface->producer_below = next.producer_below;
  surface->col = next.col;
  surface->line_cells = next.line_cells;
  surface->attributes = next.attributes;
  memcpy(surface->foreground, next.foreground, sizeof(next.foreground));
  memcpy(surface->background, next.background, sizeof(next.background));
  return 0;
}

int sl_surface_native_finish(sl_surface_t *surface, int prompt_row) {
  char seq[96];
  int count, position;
  if (!sl_surface_is_native(surface))
    return -1;
  memcpy(seq, "\033[r\033[0m", 7);
  count = 7;
  position = sl_surface_cursor(seq + count, sizeof(seq) - (size_t)count,
                               prompt_row >= 0
                                   ? surface->terminal_rows - 1 - prompt_row
                                   : surface->producer_below,
                               0);
  if (position < 0)
    return -1;
  count += position;
  if (prompt_row < 0 && surface->col > 0)
    seq[count++] = '\n';
  memcpy(seq + count, "\033[?25h", 6);
  count += 6;
  if (sl_surface_write_all(surface->fd, seq, (size_t)count) != 0)
    return -1;
  surface->native_stream = 0;
  return 0;
}

void sl_surface_destroy(sl_surface_t *surface) {
  if (!surface)
    return;
  if (surface->native_stream)
    (void)sl_surface_native_finish(surface, -1);
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
  char seq[64];
  int shift, count, previous_height, retain_cursor;
  if (!sl_surface_is_native(surface) || width < 1 || height < 2 ||
      sl_surface_terminal_size(surface->fd, &terminal) != 0)
    return -1;
  /* A full-height region follows native terminal resize. Keep its live
   * producer cursor and pending wrap; no margin command is necessary. */
  retain_cursor = surface->producer_cursor_live &&
                  surface->height == surface->terminal_rows &&
                  height == (int)terminal.ws_row;
  previous_height =
      surface->height + (int)terminal.ws_row - surface->terminal_rows;
  if (previous_height < 2)
    previous_height = 2;
  if (previous_height > (int)terminal.ws_row)
    previous_height = terminal.ws_row;
  surface->terminal_rows = terminal.ws_row;
  surface->terminal_columns = terminal.ws_col;
  if (surface->width != width && surface->line_cells > 0) {
    surface->col = (int)(surface->line_cells % (size_t)width);
    /* Keep an exact right-edge endpoint distinct from an empty row. Cursor
     * addressing loses the terminal's pending-wrap flag; the next write must
     * continue below this row rather than overwrite its first character. */
    if (surface->col == 0)
      surface->col = width;
  }
  shift = SL_ROW(surface) >= height ? SL_ROW(surface) - height + 1 : 0;
  if (shift > 0) {
    /* Only actual prompt growth reserves additional cells. The physical
     * resize has already moved the terminal's transcript. */
    if (height < previous_height) {
      count = snprintf(seq, sizeof(seq), "\033[1;%dr\033[%dS", previous_height,
                       shift);
      if (count <= 0 || count >= (int)sizeof(seq) ||
          sl_surface_write_all(surface->fd, seq, (size_t)count) != 0)
        return -1;
    }
    surface->producer_below += shift;
  }
  surface->width = width;
  surface->height = height;
  if (retain_cursor)
    return 0;
  surface->producer_cursor_live = 0;
  return sl_surface_native_region(surface);
}

int sl_surface_validate(sl_surface_t *surface, const char *bytes, size_t length,
                        size_t *accepted) {
  size_t i;
  int status;
  if (!surface || (!bytes && length > 0) || !accepted)
    return -2;
  *accepted = 0;
  for (i = 0; i < length; i++) {
    status = sl_surface_byte(surface, (unsigned char)bytes[i], 0);
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

int sl_surface_native_cursor_live(const sl_surface_t *surface) {
  return sl_surface_is_native(surface) && surface->producer_cursor_live;
}
void sl_surface_native_observe(sl_surface_t *surface, int width, int rows,
                               int row, int col) {
  if (!sl_surface_is_native(surface))
    return;
  surface->producer_below = rows - 1 - row;
  /* CPR cannot distinguish a known pending wrap from the final physical cell.
   */
  if (!(surface->width == width && surface->col == width && col == width - 1))
    surface->col = col;
}

void sl_surface_native_release_cursor(sl_surface_t *surface) {
  if (sl_surface_is_native(surface))
    surface->producer_cursor_live = 0;
}
