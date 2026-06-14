#ifndef SOFTLINE_INTERNAL_H
#define SOFTLINE_INTERNAL_H

#include "softline/softline.h"
#include "linenoise.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

#define SL_MAX_LINES 256
#define SL_LINE_INITIAL 256

struct sl_t {
  int screen_x;
  int screen_y;
  int screen_width;
  int screen_height;
  char *prompt_symbol;
  int indent_mode;
  struct linenoiseState ls;
  char *buf;
  size_t buflen;
  size_t buflen_max;
  int initialized;
};

#endif