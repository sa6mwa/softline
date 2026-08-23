#ifndef SOFTLINE_INTERNAL_H
#define SOFTLINE_INTERNAL_H

#include "softline/softline.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#define SL_BUF_INITIAL 256
#define SL_LINE_DEFAULT_MAX 4096
#define SL_HISTORY_DEFAULT_MAX 100
#define SL_PROMPT_QUEUE_DEFAULT_MAX 64
#define SL_PROMPT_QUEUE_DEFAULT_PREVIEWS 3
#define SL_ERROR_LEN 160
#define SL_DEFAULT_PROMPT "> "
#define SL_MAX_KEY_BINDINGS 64

typedef struct sl_key_binding {
  sl_key_t key;
  sl_key_callback_t callback;
  void *userdata;
} sl_key_binding_t;

typedef struct sl_history {
  char **items;
  int len;
  int max_len;
} sl_history_t;

typedef struct sl_prompt_queue {
  char **items;
  int len;
  int cap;
  int max_entries;
  int preview_entries;
  int enabled;
} sl_prompt_queue_t;

typedef struct sl_statusline {
  char *elements[SL_STATUS_MAX_ELEMENTS];
  size_t count;
  size_t start_element;
  int enabled;
  int busy;
  int spinner;
  char idle_marker;
  int truncated;
  int spinner_frame;
  int spinner_time_valid;
  struct timeval spinner_time;
} sl_statusline_t;

typedef struct sl_impl {
  int input_fd;
  int output_fd;
  int screen_x;
  int screen_y;
  int screen_width;
  int screen_height;
  int bounded;
  int dynamic_width;
  int dynamic_height;
  char *buf;
  size_t len;
  size_t cap;
  size_t line_max_len;
  size_t cursor;
  int raw_active;
  struct termios original_termios;
  sl_history_t history;
  sl_prompt_queue_t prompt_queue;
  sl_prompt_theme_t prompt_theme;
  sl_statusline_t statusline;
  int history_index;
  char *history_edit;
  int bracketed_paste;
  int cursor_hidden;
  int rendered_rows;
  int rendered_top_row;
  int rendered_cursor_row;
  int rendered_cursor_col;
  int rendered_width;
  int rendered_height;
  char **rendered_lines;
  size_t *rendered_lens;
  int *rendered_cols;
  int rendered_cap;
  const char *active_prompt;
  int active_readline;
  int request_submit;
  int request_cancel;
  int plain_pending;
  char plain_pending_ch;
  sl_readline_status_t last_readline_status;
  sl_idle_callback_t idle_callback;
  void *idle_userdata;
  sl_key_binding_t key_bindings[SL_MAX_KEY_BINDINGS];
  char error[SL_ERROR_LEN];
} sl_impl_t;

#endif
