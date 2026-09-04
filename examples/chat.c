#include "softline/softline.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int set_prompt_theme_from_environment(sl_t *sl,
                                             sl_prompt_theme_t fallback) {
  const char *name;
  sl_prompt_theme_t theme;
  name = getenv("SOFTLINE_PROMPT_THEME");
  theme = fallback;
  if (name && name[0] != '\0') {
    if (strcmp(name, "default") == 0)
      theme = SL_PROMPT_THEME_DEFAULT;
    else if (strcmp(name, "plain") == 0)
      theme = SL_PROMPT_THEME_PLAIN;
    else if (strcmp(name, "accent") == 0)
      theme = SL_PROMPT_THEME_ACCENT;
    else if (strcmp(name, "dracula") == 0)
      theme = SL_PROMPT_THEME_DRACULA;
    else if (strcmp(name, "gruvbox") == 0)
      theme = SL_PROMPT_THEME_GRUVBOX;
    else if (strcmp(name, "monochrome") == 0)
      theme = SL_PROMPT_THEME_MONOCHROME;
    else if (strcmp(name, "monogreen") == 0)
      theme = SL_PROMPT_THEME_MONOGREEN;
    else if (strcmp(name, "outrun") == 0)
      theme = SL_PROMPT_THEME_OUTRUN;
    else if (strcmp(name, "riced") == 0)
      theme = SL_PROMPT_THEME_RICED;
    else if (strcmp(name, "synthwave") == 0)
      theme = SL_PROMPT_THEME_SYNTHWAVE;
    else {
      fprintf(stderr,
              "invalid SOFTLINE_PROMPT_THEME: %s (use default, plain, accent, "
              "dracula, gruvbox, monochrome, monogreen, outrun, riced, or "
              "synthwave)\n",
              name);
      return -1;
    }
  }
  return sl_set_prompt_theme(sl, theme) == SL_OK ? 0 : -1;
}

static int set_live_scroll_region_from_environment(sl_t *sl) {
  const char *value;
  int enabled;
  value = getenv("SOFTLINE_LIVE_SCROLL_REGION");
  if (!value || value[0] == '\0')
    return SL_OK;
  if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
    enabled = 1;
  else if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0)
    enabled = 0;
  else {
    fprintf(stderr,
            "invalid SOFTLINE_LIVE_SCROLL_REGION: %s (use 0, 1, false, or "
            "true)\n",
            value);
    return SL_ERROR_INVALID;
  }
  return sl_set_live_scroll_region(sl, enabled);
}

static void keep_chat_on_interrupt(int signo) { (void)signo; }

static int cancel_editor_key(sl_t *sl, sl_key_t key, void *userdata,
                             sl_key_action_t *action) {
  (void)sl;
  (void)key;
  (void)userdata;
  if (!action)
    return SL_ERROR_INVALID;
  *action = SL_KEY_ACTION_CANCEL;
  return SL_OK;
}

/* The default deliberately leaves enough time to edit and queue turns while a
 * visibly staged operation is running. Tests may shorten it explicitly. */
static unsigned int operation_step_delay_us(void) {
  const char *value;
  char *end;
  long milliseconds;
  value = getenv("SOFTLINE_CHAT_OPERATION_STEP_MS");
  if (!value || value[0] == '\0')
    return 1000000u;
  errno = 0;
  milliseconds = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || milliseconds < 0 ||
      milliseconds > 60000)
    return 1000000u;
  return (unsigned int)milliseconds * 1000u;
}

struct message_stream {
  const char *chunks[4];
  int index;
};

static int next_message_chunk(sl_t *sl, void *userdata, const char **chunk,
                              size_t *len) {
  struct message_stream *stream;
  const char *text;
  (void)sl;
  stream = (struct message_stream *)userdata;
  if (!stream) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  while (stream->index < 4) {
    text = stream->chunks[stream->index];
    stream->index++;
    if (!text) {
      *chunk = NULL;
      *len = 0;
      return SL_OK;
    }
    if (text[0] == '\0')
      continue;
    *chunk = text;
    *len = strlen(text);
    return SL_OK;
  }
  *chunk = NULL;
  *len = 0;
  return SL_OK;
}

static int print_message(sl_t *sl, const char *text) {
  struct message_stream stream;
  stream.chunks[0] = text;
  stream.chunks[1] = "\n";
  stream.chunks[2] = NULL;
  stream.chunks[3] = NULL;
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

static int print_dispatched_message(sl_t *sl, sl_prompt_source_t source,
                                    const char *text) {
  struct message_stream stream;
  stream.chunks[0] = source == SL_PROMPT_SOURCE_PROMOTED ? "[promoted] "
                     : source == SL_PROMPT_SOURCE_QUEUED ? "[queued] "
                                                         : "[turn] ";
  stream.chunks[1] = text;
  stream.chunks[2] = "\n";
  stream.chunks[3] = NULL;
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

static int consume_active_operation_input(sl_t *sl, const char *text) {
  struct message_stream stream;
  stream.chunks[0] = "[active operation] consumed: ";
  stream.chunks[1] = text;
  stream.chunks[2] = "\n";
  stream.chunks[3] = NULL;
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

struct chat_operation_state {
  int watch_fd;
  sl_watch_id_t watch_id;
  pid_t worker_pid;
  int busy;
};

static int set_operation_busy(sl_t *sl, struct chat_operation_state *state,
                              int busy) {
  if (!sl || !state)
    return SL_ERROR_INVALID;
  if (sl->set_status_spinner(sl, busy) != SL_OK ||
      sl->set_status_busy(sl, busy) != SL_OK)
    return SL_ERROR;
  state->busy = busy;
  return SL_OK;
}

static int finish_operation(sl_t *sl, struct chat_operation_state *state) {
  if (!sl || !state)
    return SL_ERROR_INVALID;
  if (state->watch_id != 0 && sl->watch_remove(sl, state->watch_id) != SL_OK)
    return SL_ERROR;
  state->watch_id = 0;
  if (state->watch_fd >= 0) {
    close(state->watch_fd);
    state->watch_fd = -1;
  }
  if (state->worker_pid > 0) {
    if (waitpid(state->worker_pid, NULL, 0) != state->worker_pid)
      return SL_ERROR_IO;
    state->worker_pid = -1;
  }
  return set_operation_busy(sl, state, 0);
}

/* Escape is returned by Softline as SL_READLINE_CANCELLED. The application
 * owns the operation, so it decides that this cancellation stops the current
 * simulated work rather than only clearing the editor. */
static int cancel_operation(sl_t *sl, struct chat_operation_state *state) {
  if (!sl || !state)
    return SL_ERROR_INVALID;
  if (state->watch_id != 0 && sl->watch_remove(sl, state->watch_id) != SL_OK)
    return SL_ERROR;
  state->watch_id = 0;
  if (state->watch_fd >= 0) {
    close(state->watch_fd);
    state->watch_fd = -1;
  }
  if (state->worker_pid > 0) {
    if (kill(state->worker_pid, SIGTERM) != 0 && errno != ESRCH)
      return SL_ERROR_IO;
    while (waitpid(state->worker_pid, NULL, 0) < 0) {
      if (errno != EINTR)
        return SL_ERROR_IO;
    }
    state->worker_pid = -1;
  }
  return set_operation_busy(sl, state, 0);
}

static int operation_watch_callback(sl_t *sl, const sl_watch_event_t *event,
                                    void *userdata) {
  struct chat_operation_state *state;
  char events[16];
  ssize_t n;
  ssize_t i;
  state = (struct chat_operation_state *)userdata;
  if (!state || !event)
    return SL_ERROR_INVALID;
  if ((event->events & (SL_WATCH_READ | SL_WATCH_HANGUP | SL_WATCH_ERROR)) == 0)
    return SL_OK;
  n = read(state->watch_fd, events, sizeof(events));
  if (n > 0) {
    for (i = 0; i < n; i++) {
      const char *message;
      message = events[i] == 'P'   ? "[operation] processing input."
                : events[i] == 'L' ? "[operation] planning next steps."
                : events[i] == 'W' ? "[operation] running work."
                : events[i] == 'C' ? "[operation] checking result."
                : events[i] == 'R' ? "[operation] produced a result."
                                   : NULL;
      if (message && print_message(sl, message) != SL_OK)
        return SL_ERROR_IO;
    }
    return SL_OK;
  }
  if (n == 0)
    return finish_operation(sl, state);
  if (errno == EAGAIN || errno == EWOULDBLOCK)
    return SL_OK;
  return SL_ERROR_IO;
}

static int start_operation(sl_t *sl, struct chat_operation_state *state) {
  int pipe_fds[2];
  int flags;
  pid_t pid;
  unsigned int delay_us;
  if (!sl || !state || state->busy)
    return SL_ERROR_INVALID;
  delay_us = operation_step_delay_us();
  if (pipe(pipe_fds) != 0)
    return SL_ERROR_IO;
  flags = fcntl(pipe_fds[0], F_GETFL);
  if (flags < 0 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return SL_ERROR_IO;
  }
  pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return SL_ERROR_IO;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    (void)write(pipe_fds[1], "P", 1);
    usleep(delay_us);
    (void)write(pipe_fds[1], "L", 1);
    usleep(delay_us);
    (void)write(pipe_fds[1], "W", 1);
    usleep(delay_us);
    (void)write(pipe_fds[1], "C", 1);
    usleep(delay_us);
    (void)write(pipe_fds[1], "R", 1);
    close(pipe_fds[1]);
    _exit(0);
  }
  close(pipe_fds[1]);
  state->watch_fd = pipe_fds[0];
  state->worker_pid = pid;
  state->watch_id = 0;
  if (set_operation_busy(sl, state, 1) != SL_OK ||
      sl->watch_add(
          sl, state->watch_fd, SL_WATCH_READ | SL_WATCH_HANGUP | SL_WATCH_ERROR,
          operation_watch_callback, state, &state->watch_id) != SL_OK) {
    close(state->watch_fd);
    state->watch_fd = -1;
    (void)waitpid(state->worker_pid, NULL, 0);
    state->worker_pid = -1;
    return SL_ERROR;
  }
  return SL_OK;
}

static int print_last_error(sl_t *sl) {
  const char *error;
  error = sl->last_error(sl);
  if (!error)
    error = "unknown softline failure";
  return print_message(sl, error);
}

int main(void) {
  static const char *const status_elements[] = {
      "gpt-5.6-terra high", "ctx 36%",    "~/g/softline",
      "weekly 56%",         "queue demo", "turn processor"};
  sl_t *sl;
  char *line;
  sl_prompt_source_t source;
  sl_readline_status_t status;
  struct chat_operation_state operation_state;
  int interactive;
  int exit_code;
  int operation_cancelled;

  interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
  exit_code = 0;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  if (interactive) {
    if (sl->set_prompt_queue(sl, 1, 64, 3) != SL_OK) {
      fprintf(stderr, "failed to enable prompt queue\n");
      sl->destroy(sl);
      return 1;
    }
    if (sl->set_prompt_queue_profile(
            sl, SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS) != SL_OK) {
      fprintf(stderr, "failed to configure prompt queue controls\n");
      sl->destroy(sl);
      return 1;
    }
    if (set_live_scroll_region_from_environment(sl) != SL_OK) {
      fprintf(stderr, "failed to configure live scroll region\n");
      sl->destroy(sl);
      return 1;
    }
    (void)signal(SIGINT, keep_chat_on_interrupt);
    if (sl->bind_key(sl, SL_KEY_ESCAPE, cancel_editor_key, NULL) != SL_OK) {
      fprintf(stderr, "failed to bind Escape cancellation\n");
      sl->destroy(sl);
      return 1;
    }
    operation_state.watch_fd = -1;
    operation_state.watch_id = 0;
    operation_state.worker_pid = -1;
    operation_state.busy = 0;
    if (set_prompt_theme_from_environment(sl, SL_PROMPT_THEME_DEFAULT) != 0) {
      fprintf(stderr, "failed to set prompt theme\n");
      sl->destroy(sl);
      return 1;
    }
    if (sl->set_statusline(sl, 1, 0) != SL_OK ||
        sl->set_status_elements(sl, status_elements,
                                sizeof(status_elements) /
                                    sizeof(status_elements[0])) != SL_OK) {
      fprintf(stderr, "failed to configure chat status line\n");
      sl->destroy(sl);
      return 1;
    }
    if (set_operation_busy(sl, &operation_state, 0) != SL_OK) {
      fprintf(stderr, "failed to configure available chat state\n");
      sl->destroy(sl);
      return 1;
    }
    if (print_message(sl,
                      "softline turn processor. A staged operation streams "
                      "for about four seconds. Enter sends while available "
                      "and queues while an operation is running; Alt-Enter "
                      "sends immediate input to the running operation; empty "
                      "Alt-Enter promotes the newest queued turn; Alt-E edits "
                      "it. Escape or Ctrl-C stops the operation.") != SL_OK) {
      (void)print_last_error(sl);
      sl->destroy(sl);
      return 1;
    }
  }
  for (;;) {
    source = SL_PROMPT_SOURCE_NONE;
    line = sl->next_prompt(sl, NULL, &source);
    if (!line) {
      status = sl->last_readline_status(sl);
      if (status == SL_READLINE_CANCELLED ||
          status == SL_READLINE_INTERRUPTED) {
        operation_cancelled = interactive && operation_state.busy;
        if (operation_cancelled &&
            cancel_operation(sl, &operation_state) != SL_OK) {
          (void)print_last_error(sl);
          exit_code = 1;
          break;
        }
        if (interactive &&
            print_message(sl, operation_cancelled ? "[operation] cancelled"
                                                  : "[cancelled]") != SL_OK) {
          exit_code = 1;
          break;
        }
        continue;
      }
      if (status == SL_READLINE_ERROR) {
        (void)print_last_error(sl);
        exit_code = 1;
      }
      break;
    }
    if (strcmp(line, "exit") == 0) {
      sl->free_string(sl, line);
      break;
    }
    if (line[0] == '\0') {
      sl->free_string(sl, line);
      if (interactive && print_message(sl, "[empty turn ignored]") != SL_OK) {
        exit_code = 1;
        break;
      }
      continue;
    }
    if (sl->history_add(sl, line) != SL_OK) {
      sl->free_string(sl, line);
      (void)print_last_error(sl);
      exit_code = 1;
      break;
    }
    if (print_dispatched_message(sl, source, line) != SL_OK) {
      sl->free_string(sl, line);
      (void)print_last_error(sl);
      exit_code = 1;
      break;
    }
    if (interactive && operation_state.busy) {
      if (consume_active_operation_input(sl, line) != SL_OK) {
        sl->free_string(sl, line);
        (void)print_last_error(sl);
        exit_code = 1;
        break;
      }
    } else if (interactive && start_operation(sl, &operation_state) != SL_OK) {
      sl->free_string(sl, line);
      (void)print_last_error(sl);
      exit_code = 1;
      break;
    }
    sl->free_string(sl, line);
  }

  if (interactive && operation_state.busy)
    (void)finish_operation(sl, &operation_state);
  sl->destroy(sl);
  return exit_code;
}
