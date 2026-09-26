#ifndef SOFTLINE_INTERNAL_H
#define SOFTLINE_INTERNAL_H

#include "softline/softline.h"
#include "softline_surface.h"

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
#define SL_MAX_WATCHES 32
#define SL_PENDING_INPUT_MAX 32

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

typedef struct sl_prompt_queue_entry {
  char *text;
  sl_prompt_queue_mode_t mode;
} sl_prompt_queue_entry_t;

typedef struct sl_prompt_queue {
  sl_prompt_queue_entry_t *items;
  int len;
  int cap;
  int max_entries;
  int preview_entries;
  int enabled;
  /* A queued-turns cancellation retains drafts but stops automatic FIFO
   * release until the user intentionally submits or promotes a turn. */
  int stopped;
  int host_controls_delivery;
  sl_prompt_queue_delivery_t delivery;
  sl_prompt_queue_profile_t profile;
  sl_prompt_queue_keys_t keys;
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

typedef struct sl_watch {
  sl_watch_id_t id;
  int fd;
  unsigned int events;
  sl_watch_callback_t callback;
  void *userdata;
} sl_watch_t;

typedef struct sl_impl {
  int input_fd;
  int output_fd;
  int screen_width;
  int live_scroll_region;
  int clear_prompt_on_exit;
  int auto_scroll_pinned;
  int output_stream_active;
  int output_trailing_newlines;
  int output_ansi_state;
  /* Hold at most ESC [, 128 CSI bytes, and a terminator across writes. */
  char output_pending[132];
  size_t output_pending_len;
  sl_surface_t *output_surface;
  int cursor_position_probe;
  int probed_cursor_col;
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
  char *quoted_prompt_prefix;
  sl_quote_style_t quoted_prompt_style;
  int quoted_prompt_style_custom;
  sl_statusline_t statusline;
  char *status_message;
  char *status_message_prefix;
  sl_theme_color_t status_message_prefix_color;
  sl_theme_color_t status_message_text_color;
  int history_index;
  char *history_edit;
  int bracketed_paste;
  int cursor_hidden;
  int rendered_rows;
  int rendered_editor_first;
  int rendered_top_row;
  int rendered_cursor_row;
  int rendered_cursor_col;
  /* Logical cursor is part of the prompt frame; this tracks whether the
   * terminal cursor currently matches it. Output may invalidate only this. */
  int rendered_cursor_valid;
  int rendered_width;
  int rendered_height;
  int native_prompt_rows;
  char **rendered_lines;
  size_t *rendered_lens;
  int *rendered_cols;
  int rendered_cap;
  const char *active_prompt;
  int active_readline;
  int request_submit;
  int request_cancel;
  int request_queue_dispatch;
  int plain_pending;
  char plain_pending_ch;
  char *pending_input;
  size_t pending_input_len;
  size_t pending_input_cap;
  sl_readline_status_t last_readline_status;
  sl_idle_callback_t idle_callback;
  void *idle_userdata;
  sl_watch_t watches[SL_MAX_WATCHES];
  sl_watch_id_t next_watch_id;
  /* Next registry slot considered first when dispatching ready watches. */
  unsigned int watch_dispatch_cursor;
  /* Nonzero while one or more watch callbacks are using this handle. */
  unsigned int watch_callback_depth;
  sl_key_binding_t key_bindings[SL_MAX_KEY_BINDINGS];
  char error[SL_ERROR_LEN];
} sl_impl_t;

#endif
