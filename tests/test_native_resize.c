#define _DEFAULT_SOURCE
#include "softline_surface.h"

#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int cell_width(unsigned long codepoint) {
  (void)codepoint;
  return 1;
}

static int drain(int fd, char *bytes, size_t capacity) {
  ssize_t length;
  size_t used = 0;
  while (used + 1 < capacity &&
         (length = read(fd, bytes + used, capacity - used - 1)) > 0)
    used += (size_t)length;
  bytes[used] = '\0';
  return (int)used;
}

static int has_scroll(const char *bytes) {
  const unsigned char *p = (const unsigned char *)bytes;
  while (*p) {
    if (*p++ != 27)
      continue;
    if (*p != '[')
      continue;
    p++;
    while (*p >= 0x20 && *p <= 0x3f)
      p++;
    if (*p == 'S' || *p == 'T' || *p == 'L' || *p == 'M')
      return 1;
    if (*p)
      p++;
  }
  return 0;
}

static int resize_case(int columns, int rows, int producer_row) {
  struct winsize size;
  sl_surface_t *surface;
  int master, slave, row, col, failed;
  char bytes[1024];
  memset(&size, 0, sizeof(size));
  size.ws_col = 40;
  size.ws_row = 12;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(
      slave, 40, 10, producer_row < 10 ? producer_row : 9, 3, cell_width);
  if (!surface)
    return 1;
  (void)drain(master, bytes, sizeof(bytes));
  size.ws_col = (unsigned short)columns;
  size.ws_row = (unsigned short)rows;
  if (ioctl(master, TIOCSWINSZ, &size) != 0)
    return 1;
  failed = sl_surface_resize(surface, columns, rows - 2) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  sl_surface_native_position(surface, &row, &col);
  failed |= has_scroll(bytes) || strchr(bytes, '\n') != NULL ||
            row >= rows - 2 || col != 3 || strstr(bytes, "\0337") != NULL ||
            strstr(bytes, "\0338") != NULL;
  if (failed)
    fprintf(stderr, "%dx%d producer row %d: unexpected resize %s\n", columns,
            rows, producer_row, bytes);
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  return failed;
}

static int prompt_growth_case(void) {
  struct winsize size;
  sl_surface_t *surface;
  int master, slave, failed, row;
  char bytes[1024];
  memset(&size, 0, sizeof(size));
  size.ws_col = 40;
  size.ws_row = 12;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(slave, 40, 10, 9, 0, cell_width);
  if (!surface)
    return 1;
  (void)drain(master, bytes, sizeof(bytes));
  failed = sl_surface_resize(surface, 40, 8) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  sl_surface_native_position(surface, &row, NULL);
  failed |= strstr(bytes, "\033[2S") == NULL || row != 7;
  (void)drain(master, bytes, sizeof(bytes));
  failed |= sl_surface_resize(surface, 40, 10) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  sl_surface_native_position(surface, &row, NULL);
  failed |= has_scroll(bytes) || row != 7;
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  return failed;
}

static int exact_width_case(void) {
  struct winsize size;
  sl_surface_t *surface;
  int master, slave, failed, col;
  char bytes[1024];
  memset(&size, 0, sizeof(size));
  size.ws_col = 40;
  size.ws_row = 12;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(slave, 40, 10, 9, 0, cell_width);
  if (!surface)
    return 1;
  failed = sl_surface_native_write(surface, "abcdefghijkl", 12, 11, 2) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  size.ws_col = 12;
  failed |= ioctl(master, TIOCSWINSZ, &size) != 0 ||
            sl_surface_resize(surface, 12, 10) != 0;
  sl_surface_native_position(surface, NULL, &col);
  /* A filled row and a fresh empty row have different continuation behavior. */
  failed |= col != 12;
  (void)drain(master, bytes, sizeof(bytes));
  failed |= sl_surface_native_write(surface, "m", 1, 11, 2) != 0;
  sl_surface_native_position(surface, NULL, &col);
  failed |= col != 1;
  (void)drain(master, bytes, sizeof(bytes));
  failed |= strstr(bytes, "abcdefghijkl") != NULL ||
            strstr(bytes, "\0337") != NULL || strstr(bytes, "\0338") != NULL;
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  return failed;
}

static int clipped_tail_case(int line_open) {
  struct winsize size;
  sl_surface_t *surface;
  int master, slave, row, col, failed;
  char bytes[1024];
  const char tail[] = "\033[31mTAIL\033[0m";
  memset(&size, 0, sizeof(size));
  size.ws_col = 40;
  size.ws_row = 12;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(slave, 40, 10, 0, 0, cell_width);
  if (!surface)
    return 1;
  failed = sl_surface_native_write(surface, line_open ? "prefix" : "prefix\n",
                                   line_open ? 6 : 7, 11, 2) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  size.ws_col = 16;
  size.ws_row = 5;
  failed |= ioctl(master, TIOCSWINSZ, &size) != 0 ||
            sl_surface_resize(surface, 16, 3) != 0;
  sl_surface_native_position(surface, &row, NULL);
  failed |= row >= 0;
  (void)drain(master, bytes, sizeof(bytes));
  failed |= sl_surface_native_write(surface, tail, sizeof(tail) - 1, 4, 2) != 0;
  sl_surface_native_position(surface, &row, &col);
  (void)drain(master, bytes, sizeof(bytes));
  failed |= row != (line_open ? 0 : 2) || col != 4 || has_scroll(bytes) ||
            strstr(bytes, tail) == NULL || strchr(bytes, '\n') != NULL ||
            strstr(bytes, "prefix") != NULL;
  if (failed)
    fprintf(stderr, "clipped %s resumed at the wrong row\n",
            line_open ? "continuation" : "new line");
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  return failed;
}

static int newline_case(int mode) {
  struct winsize size;
  struct termios attributes;
  sl_surface_t *surface;
  int master, slave, failed, row, col;
  char bytes[1024];
  memset(&size, 0, sizeof(size));
  size.ws_col = 20;
  size.ws_row = 8;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0 ||
      tcgetattr(slave, &attributes) != 0)
    return 1;
  attributes.c_oflag |= OPOST | ONLCR;
  if (mode == 1)
    attributes.c_oflag &= ~OPOST;
  else if (mode == 2)
    attributes.c_oflag &= ~ONLCR;
  if (tcsetattr(slave, TCSANOW, &attributes) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(slave, 20, 7, 0, 0, cell_width);
  if (!surface)
    return 1;
  (void)drain(master, bytes, sizeof(bytes));
  failed = sl_surface_native_write(surface, "a\nb", 3, 7, 0) != 0;
  sl_surface_native_position(surface, &row, &col);
  failed |= row != 1 || col != (mode == 0 ? 1 : 2);
  (void)drain(master, bytes, sizeof(bytes));
  failed |= strstr(bytes, mode == 0 ? "a\r\nb" : "a\nb") == NULL;
  failed |= sl_surface_native_write(surface, "c", 1, 7, 0) != 0;
  sl_surface_native_position(surface, &row, &col);
  failed |= row != 1 || col != (mode == 0 ? 2 : 3);
  (void)drain(master, bytes, sizeof(bytes));
  failed |=
      strstr(bytes, mode == 0 ? "\033[65535;2H" : "\033[65535;3H") == NULL;
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  if (failed)
    fprintf(stderr, "newline mode %d overwrote a native continuation\n", mode);
  return failed;
}

static int clipped_tab_case(int full_row) {
  struct winsize size;
  sl_surface_t *surface;
  int master, slave, failed, row, col;
  char bytes[1024];
  memset(&size, 0, sizeof(size));
  size.ws_col = 20;
  size.ws_row = 8;
  if (openpty(&master, &slave, NULL, NULL, &size) != 0)
    return 1;
  (void)fcntl(master, F_SETFL, O_NONBLOCK);
  surface = sl_surface_create_native(slave, 20, 7, 0, 0, cell_width);
  if (!surface)
    return 1;
  failed = sl_surface_native_write(surface,
                                   full_row ? "abcdefghijklmnopqrst\t"
                                            : "abcdefghijklmnopqr\tX",
                                   full_row ? 21 : 20, 7, 0) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  size.ws_col = 40;
  failed |= ioctl(master, TIOCSWINSZ, &size) != 0;
  failed |= sl_surface_resize(surface, 40, 7) != 0;
  (void)drain(master, bytes, sizeof(bytes));
  failed |= sl_surface_native_write(surface, "Y", 1, 7, 0) != 0;
  sl_surface_native_position(surface, &row, &col);
  failed |= row != 0 || col != (full_row ? 20 : 21);
  (void)drain(master, bytes, sizeof(bytes));
  failed |=
      strstr(bytes, full_row ? "\033[65535;20H" : "\033[65535;21H") == NULL;
  sl_surface_destroy(surface);
  close(slave);
  close(master);
  if (failed)
    fprintf(stderr, "clipped tab introduced a gap after width growth\n");
  return failed;
}

int main(void) {
  static const int dimensions[][2] = {{16, 5},  {40, 5},  {80, 5}, {16, 12},
                                      {80, 12}, {16, 18}, {80, 18}};
  size_t i;
  int failed = 0;
  for (i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
    failed |= resize_case(dimensions[i][0], dimensions[i][1], 0);
    failed |= resize_case(dimensions[i][0], dimensions[i][1], 8);
    failed |=
        resize_case(dimensions[i][0], dimensions[i][1], dimensions[i][1] - 1);
  }
  failed |= prompt_growth_case();
  failed |= exact_width_case();
  failed |= clipped_tail_case(1);
  failed |= clipped_tail_case(0);
  for (i = 0; i < 3; i++)
    failed |= newline_case((int)i);
  failed |= clipped_tab_case(0);
  failed |= clipped_tab_case(1);
  if (!failed)
    puts("21 physical resize cases preserve cells; prompt growth still "
         "scrolls.");
  return failed;
}
