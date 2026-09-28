#define _POSIX_C_SOURCE 200809L
#include "softline/softline.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct producer {
  int commands, replies, trace, input, ready, result, resize_ack;
  const char *first, *second;
};

static int emit(sl_t *sl, struct producer *p, const char *bytes) {
  size_t i, length = strlen(bytes);
  /* Deliberately use one-byte producer writes. No prompt handoff is needed
   * at a write boundary, including boundaries inside ANSI or UTF-8. */
  for (i = 0; i < length; i++) {
    if (sl_output_stream_write(sl, bytes + i, 1) != SL_OK ||
        write(p->trace, bytes + i, 1) != 1)
      return SL_ERROR_IO;
  }
  return SL_OK;
}

static int watch(sl_t *sl, const sl_watch_event_t *event, void *userdata) {
  struct producer *p = (struct producer *)userdata;
  const char *draft;
  char command;
  int result = SL_OK;
  (void)event;
  if (read(p->commands, &command, 1) != 1)
    return SL_ERROR_IO;
  switch (command) {
  case 'a':
    result = emit(sl, p, p->first);
    break;
  case 'b':
    result = emit(sl, p, p->second);
    break;
  case 'd': {
    size_t length = strlen(p->second);
    result = sl_output_stream_write(sl, p->second, length);
    if (result == SL_OK &&
        write(p->trace, p->second, length) != (ssize_t)length)
      result = SL_ERROR_IO;
    break;
  }
  case 'c':
    result = emit(sl, p, "\251");
    break;
  case 'n':
    result = emit(sl, p, "\n");
    break;
  case 's':
    result = sl_set_status_message(sl, "Working.");
    break;
  case 'q':
    result = sl_prompt_queue_append(sl, "queued draft");
    break;
  case 'e':
    result = sl_output_stream_end(sl);
    break;
  case 'x':
    result = sl_cancel(sl);
    break;
  case 'r':
    /* Idle precedes rendering; the second tick follows this frame update. */
    p->resize_ack = 2;
    return SL_OK;
  case 'g':
    draft = sl_buffer(sl);
    if (ftruncate(p->input, 0) != 0 || lseek(p->input, 0, SEEK_SET) < 0 ||
        write(p->input, draft, strlen(draft)) != (ssize_t)strlen(draft))
      result = SL_ERROR_IO;
    break;
  default:
    result = SL_ERROR_INVALID;
    break;
  }
  if (result != SL_OK)
    p->result = result;
  return write(p->replies, &command, 1) == 1 ? result : SL_ERROR_IO;
}

static void idle(sl_t *sl, void *userdata) {
  struct producer *p = (struct producer *)userdata;
  (void)sl;
  if (p->resize_ack && --p->resize_ack == 0) {
    if (write(p->replies, "r", 1) != 1)
      p->result = SL_ERROR_IO;
  }
  if (!p->ready) {
    p->ready = 1;
    if (write(p->replies, "R", 1) != 1)
      p->result = SL_ERROR_IO;
  }
}

int main(int argc, char **argv) {
  struct producer p;
  struct termios before, after;
  sl_config_t config;
  sl_t *sl;
  sl_watch_id_t id;
  char *line;
  if (argc != 9 || tcgetattr(STDIN_FILENO, &before) != 0)
    return 2;
  memset(&p, 0, sizeof(p));
  p.commands = open(argv[1], O_RDWR | O_NONBLOCK);
  p.replies = open(argv[2], O_RDWR | O_NONBLOCK);
  p.trace = open(argv[5], O_CREAT | O_TRUNC | O_WRONLY, 0600);
  p.input = open(argv[6], O_CREAT | O_TRUNC | O_WRONLY, 0600);
  p.first = argv[3];
  p.second = argv[4];
  if (p.commands < 0 || p.replies < 0 || p.trace < 0 || p.input < 0)
    return 2;
  sl_config_init(&config);
  config.prompt_theme = SL_PROMPT_THEME_PLAIN;
  if (strcmp(argv[7], "default") != 0)
    config.prompt_handoff_timeout_ms = atoi(argv[7]);
  config.clear_prompt_on_exit = atoi(argv[8]);
  sl = sl_create_with_config(&config);
  (void)signal(SIGINT, SIG_IGN);
  if (!sl || sl_set_prompt_queue(sl, 1, 8, 2) != SL_OK ||
      sl_output_stream_begin(sl) != SL_OK ||
      sl_set_idle_callback(sl, idle, &p) != SL_OK ||
      sl_watch_add(sl, p.commands, SL_WATCH_READ, watch, &p, &id) != SL_OK)
    return 2;
  line = sl_readline(sl, "> ");
  if (line)
    sl_free_string(sl, line);
  else if (sl_last_readline_status(sl) != SL_READLINE_CANCELLED &&
           sl_last_readline_status(sl) != SL_READLINE_INTERRUPTED)
    p.result = SL_ERROR;
  sl_destroy(sl);
  close(p.commands);
  close(p.replies);
  close(p.trace);
  close(p.input);
  if (tcgetattr(STDIN_FILENO, &after) != 0 || before.c_iflag != after.c_iflag ||
      before.c_oflag != after.c_oflag || before.c_cflag != after.c_cflag ||
      before.c_lflag != after.c_lflag ||
      memcmp(before.c_cc, after.c_cc, sizeof(before.c_cc)) != 0)
    return 2;
  return p.result == SL_OK ? 0 : 1;
}
