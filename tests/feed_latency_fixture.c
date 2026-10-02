#define _POSIX_C_SOURCE 200809L
#include "softline/softline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct state {
  int control, started, resize, result;
};

static void feed(sl_t *sl, void *data) {
  struct state *state = data;
  const char *bytes = "\033[1m\342\224\200\033[0m";
  size_t i, j;
  if (state->started)
    return;
  state->started = 1;
  if (write(state->control, "S", 1) != 1) {
    state->result = 1;
    return;
  }
  (void)write(STDOUT_FILENO, "\033]777;feed-start\007", 17);
  for (i = 0; i < 128; i++) {
    if (state->resize && i == 64) {
      struct winsize size;
      memset(&size, 0, sizeof(size));
      size.ws_col = 61;
      size.ws_row = 18;
      if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &size) != 0)
        state->result = 1;
    }
    for (j = 0; bytes[j]; j++)
      if (sl_output_stream_write(sl, bytes + j, 1) != SL_OK)
        state->result = 1;
    if (sl_output_stream_write(sl, "\n", 1) != SL_OK)
      state->result = 1;
  }
  if (sl_output_stream_write_quoted_prompt(sl, "table complete") != SL_OK)
    state->result = 1;
  (void)write(STDOUT_FILENO, "\033]777;feed-end\007", 15);
  if (write(state->control, "D", 1) != 1)
    state->result = 1;
}

int main(int argc, char **argv) {
  struct state state;
  sl_t *sl;
  char *line;
  int result;
  if (argc != 3)
    return 2;
  memset(&state, 0, sizeof(state));
  state.control = atoi(argv[1]);
  state.resize = atoi(argv[2]);
  sl = sl_create();
  if (!sl || sl_output_stream_begin(sl) != SL_OK)
    return 2;
  sl_set_idle_callback(sl, feed, &state);
  line = sl_readline(sl, "> ");
  result = !line || strcmp(line, "typed") != 0 || state.result;
  sl_free_string(sl, line);
  if (sl_output_stream_end(sl) != SL_OK)
    result = 1;
  sl_destroy(sl);
  return result;
}
