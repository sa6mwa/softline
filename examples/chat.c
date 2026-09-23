#define _POSIX_C_SOURCE 200809L

#include "softline/softline.h"

#include <libmdf/mdf.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *const response_templates[] = {
    "# A short answer\n\nHere is *italic* context, **bold** emphasis, and "
    "`code` in one paragraph.\n\n## Next step\n\nTry another prompt.\n",
    "## A longer answer\n\nFirst, inspect the input.\n\nThen use `code` "
    "for the operation; **bold** marks the result and *italic* marks a "
    "caveat.\n\n# Summary\n\nThe stream is still live.\n",
    "# Notes\n\nA single line can be *italic*, **bold**, or `code`.\n"};

struct chat_state {
  sl_t *sl;
  mdf *prompt_renderer;
  mdf *response_renderer;
  int prompt_documents;
  int response_documents;
  int response_open;
  int interactive;
  int columns;
  int watch_fd;
  sl_watch_id_t watch_id;
  pid_t worker_pid;
  int busy;
  unsigned int turn_index;
};

static int sink_to_softline(void *userdata, const char *bytes, size_t length) {
  struct chat_state *state;
  state = (struct chat_state *)userdata;
  return state && sl_output_stream_write(state->sl, bytes, length) == SL_OK
             ? 0
             : -1;
}

static int chat_margin_left(int columns) { return columns >= 5 ? 2 : 0; }

static int new_renderer(struct chat_state *state, mdf **out) {
  mdf_options options;
  mdf_sink sink;
  mdf_options_init(&options);
  options.width = state->columns;
  options.margin_left = chat_margin_left(state->columns);
  options.boring = !state->interactive;
  if (mdf_create(MDF_FORMAT_ANSI, &options, out) != MDF_OK)
    return -1;
  sink.userdata = state;
  sink.write = sink_to_softline;
  if ((*out)->set_sink(*out, &sink) != MDF_OK) {
    (*out)->destroy(*out);
    *out = NULL;
    return -1;
  }
  return 0;
}

static int sync_geometry(struct chat_state *state) {
  int columns;
  columns = mdf_terminal_width(STDOUT_FILENO, 80);
  if (columns == state->columns)
    return 0;
  /* The composer updates both handles in one owner-thread callback. A very
   * narrow terminal drops the margin so libmdf retains three content columns.
   */
  if (sl_set_bounds(state->sl, 0, 0, 0, 0) != SL_OK ||
      state->prompt_renderer->set_geometry(state->prompt_renderer, columns,
                                           chat_margin_left(columns),
                                           0) != MDF_OK ||
      state->response_renderer->set_geometry(state->response_renderer, columns,
                                             chat_margin_left(columns),
                                             0) != MDF_OK)
    return -1;
  state->columns = columns;
  return 0;
}

static int begin_prompt_document(struct chat_state *state) {
  if (sync_geometry(state) != 0)
    return -1;
  if (state->prompt_documents > 0 &&
      state->prompt_renderer->begin_document(state->prompt_renderer) != MDF_OK)
    return -1;
  return 0;
}

static int finish_prompt_document(struct chat_state *state) {
  if (state->prompt_renderer->finish_document(state->prompt_renderer) != MDF_OK)
    return -1;
  state->prompt_documents++;
  return 0;
}

static int render_note(struct chat_state *state, const char *note) {
  if (begin_prompt_document(state) != 0 ||
      state->prompt_renderer->feed(state->prompt_renderer, "\n", 1) != MDF_OK ||
      state->prompt_renderer->feed(state->prompt_renderer, note,
                                   strlen(note)) != MDF_OK ||
      state->prompt_renderer->feed(state->prompt_renderer, "\n\n", 2) != MDF_OK)
    return -1;
  return finish_prompt_document(state);
}

static int render_user_prompt(struct chat_state *state, const char *line) {
  const char *part;
  const char *newline;
  if (begin_prompt_document(state) != 0 ||
      state->prompt_renderer->feed(state->prompt_renderer, "\n> ", 3) != MDF_OK)
    return -1;
  part = line;
  while ((newline = strchr(part, '\n')) != NULL) {
    if (newline > part &&
        state->prompt_renderer->feed(state->prompt_renderer, part,
                                     (size_t)(newline - part)) != MDF_OK)
      return -1;
    if (state->prompt_renderer->feed(state->prompt_renderer, "\n> ", 3) !=
        MDF_OK)
      return -1;
    part = newline + 1;
  }
  if ((part[0] != '\0' &&
       state->prompt_renderer->feed(state->prompt_renderer, part,
                                    strlen(part)) != MDF_OK) ||
      state->prompt_renderer->feed(state->prompt_renderer, "\n\n", 2) != MDF_OK)
    return -1;
  return finish_prompt_document(state);
}

static int set_busy(struct chat_state *state, int busy) {
  if (sl_set_status_spinner(state->sl, busy) != SL_OK ||
      sl_set_status_busy(state->sl, busy) != SL_OK)
    return -1;
  state->busy = busy;
  return 0;
}

static unsigned int character_delay_us(void) {
  const char *value;
  char *end;
  long milliseconds;
  value = getenv("SOFTLINE_CHAT_CHAR_MS");
  if (!value || value[0] == '\0')
    return 20000u;
  errno = 0;
  milliseconds = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || milliseconds < 0 ||
      milliseconds > 60000)
    return 20000u;
  return (unsigned int)milliseconds * 1000u;
}

static void wait_character(unsigned int microseconds) {
  struct timespec delay;
  delay.tv_sec = (time_t)(microseconds / 1000000u);
  delay.tv_nsec = (long)(microseconds % 1000000u) * 1000L;
  while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    ;
}

static int finish_operation(struct chat_state *state) {
  if (state->watch_id != 0 &&
      sl_watch_remove(state->sl, state->watch_id) != SL_OK)
    return -1;
  state->watch_id = 0;
  if (state->watch_fd >= 0) {
    close(state->watch_fd);
    state->watch_fd = -1;
  }
  if (state->worker_pid > 0) {
    if (waitpid(state->worker_pid, NULL, 0) != state->worker_pid)
      return -1;
    state->worker_pid = -1;
  }
  if (state->response_open) {
    if (state->response_renderer->finish_document(state->response_renderer) !=
        MDF_OK)
      return -1;
    state->response_open = 0;
    state->response_documents++;
  }
  return set_busy(state, 0);
}

static int cancel_operation(struct chat_state *state) {
  if (state->watch_id != 0 &&
      sl_watch_remove(state->sl, state->watch_id) != SL_OK)
    return -1;
  state->watch_id = 0;
  if (state->watch_fd >= 0) {
    close(state->watch_fd);
    state->watch_fd = -1;
  }
  if (state->worker_pid > 0) {
    if (kill(state->worker_pid, SIGTERM) != 0 && errno != ESRCH)
      return -1;
    while (waitpid(state->worker_pid, NULL, 0) < 0) {
      if (errno != EINTR)
        return -1;
    }
    state->worker_pid = -1;
  }
  if (state->response_open) {
    if (state->response_renderer->finish_document(state->response_renderer) !=
        MDF_OK)
      return -1;
    state->response_open = 0;
    state->response_documents++;
  }
  return set_busy(state, 0);
}

static int operation_watch(sl_t *sl, const sl_watch_event_t *event,
                           void *userdata) {
  struct chat_state *state;
  char bytes[64];
  ssize_t amount;
  ssize_t i;
  (void)sl;
  state = (struct chat_state *)userdata;
  if (!state || !event ||
      (event->events & (SL_WATCH_READ | SL_WATCH_HANGUP | SL_WATCH_ERROR)) == 0)
    return SL_ERROR_INVALID;
  if (sync_geometry(state) != 0)
    return SL_ERROR;
  amount = read(state->watch_fd, bytes, sizeof(bytes));
  if (amount > 0) {
    for (i = 0; i < amount; i++) {
      if (state->response_renderer->feed(state->response_renderer, bytes + i,
                                         1) != MDF_OK)
        return SL_ERROR_IO;
    }
    return SL_OK;
  }
  if (amount == 0)
    return finish_operation(state) == 0 ? SL_OK : SL_ERROR_IO;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    return SL_OK;
  return SL_ERROR_IO;
}

static int start_operation(struct chat_state *state) {
  const char *answer;
  int pipe_fds[2];
  int flags;
  pid_t pid;
  unsigned int delay;
  size_t i;
  if (state->busy || sync_geometry(state) != 0)
    return -1;
  if (state->response_documents > 0 && state->response_renderer->begin_document(
                                           state->response_renderer) != MDF_OK)
    return -1;
  state->response_open = 1;
  answer =
      response_templates[state->turn_index++ % (sizeof(response_templates) /
                                                sizeof(response_templates[0]))];
  delay = character_delay_us();
  if (pipe(pipe_fds) != 0)
    return -1;
  flags = fcntl(pipe_fds[0], F_GETFL);
  if (flags < 0 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return -1;
  }
  pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return -1;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    (void)signal(SIGINT, SIG_IGN);
    for (i = 0; answer[i] != '\0'; i++) {
      if (write(pipe_fds[1], answer + i, 1) != 1)
        break;
      wait_character(delay);
    }
    close(pipe_fds[1]);
    _exit(0);
  }
  close(pipe_fds[1]);
  state->watch_fd = pipe_fds[0];
  state->worker_pid = pid;
  state->watch_id = 0;
  if (set_busy(state, 1) != 0 ||
      sl_watch_add(state->sl, state->watch_fd,
                   SL_WATCH_READ | SL_WATCH_HANGUP | SL_WATCH_ERROR,
                   operation_watch, state, &state->watch_id) != SL_OK) {
    (void)cancel_operation(state);
    return -1;
  }
  return 0;
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

static int set_prompt_theme_from_environment(sl_t *sl) {
  const char *name;
  sl_prompt_theme_t theme;
  name = getenv("SOFTLINE_PROMPT_THEME");
  theme = SL_PROMPT_THEME_DEFAULT;
  if (name && name[0] != '\0') {
    if (strcmp(name, "plain") == 0)
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
    else if (strcmp(name, "default") != 0) {
      fprintf(stderr, "invalid SOFTLINE_PROMPT_THEME: %s\n", name);
      return -1;
    }
  }
  return sl_set_prompt_theme(sl, theme) == SL_OK ? 0 : -1;
}

static void report_failure(struct chat_state *state, const char *operation) {
  const char *softline_error;
  const char *mdf_error;
  softline_error = state->sl ? sl_last_error(state->sl) : NULL;
  mdf_error = state->response_renderer
                  ? state->response_renderer->error(state->response_renderer)
                  : NULL;
  fprintf(stderr, "chat %s failed: %s%s%s\n", operation,
          softline_error ? softline_error : "unknown error",
          mdf_error ? "; libmdf: " : "", mdf_error ? mdf_error : "");
}

int main(void) {
  static const char *const status_elements[] = {"streaming demo", "ctx 36%",
                                                "~/g/softline", "queue demo"};
  struct chat_state state;
  sl_prompt_source_t source;
  sl_readline_status_t status;
  char *line;
  int interactive;
  int exit_code;
  int cancelled_busy;

  memset(&state, 0, sizeof(state));
  state.watch_fd = -1;
  state.worker_pid = -1;
  interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
  state.interactive = interactive;
  exit_code = 0;
  state.sl = sl_create();
  if (!state.sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  state.columns = mdf_terminal_width(STDOUT_FILENO, 80);
  if ((interactive &&
       (sl_set_bounds(state.sl, 0, 0, 0, 0) != SL_OK ||
        sl_set_prompt_queue(state.sl, 1, 64, 3) != SL_OK ||
        sl_set_prompt_queue_profile(
            state.sl, SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS) != SL_OK ||
        sl_set_statusline(state.sl, 1, 0) != SL_OK ||
        sl_set_status_elements(state.sl, status_elements,
                               sizeof(status_elements) /
                                   sizeof(status_elements[0])) != SL_OK ||
        set_prompt_theme_from_environment(state.sl) != 0 ||
        sl_bind_key(state.sl, SL_KEY_ESCAPE, cancel_editor_key, NULL) !=
            SL_OK)) ||
      state.sl->output_stream_begin(state.sl) != SL_OK ||
      new_renderer(&state, &state.prompt_renderer) != 0 ||
      new_renderer(&state, &state.response_renderer) != 0) {
    report_failure(&state, "setup");
    exit_code = 1;
    goto cleanup;
  }
  if (interactive) {
    (void)signal(SIGINT, keep_chat_on_interrupt);
    if (set_busy(&state, 0) != 0 ||
        render_note(&state,
                    "Enter sends or queues; Alt-Enter steers or promotes; "
                    "Alt-E edits queue. Esc/Ctrl-C cancels; `exit` leaves.") !=
            0) {
      report_failure(&state, "initial output");
      exit_code = 1;
      goto cleanup;
    }
  }
  for (;;) {
    source = SL_PROMPT_SOURCE_NONE;
    line = sl_next_prompt(state.sl, NULL, &source);
    if (!line) {
      status = sl_last_readline_status(state.sl);
      if (status == SL_READLINE_CANCELLED ||
          status == SL_READLINE_INTERRUPTED) {
        cancelled_busy = interactive && state.busy;
        if ((cancelled_busy && cancel_operation(&state) != 0) ||
            (interactive &&
             render_note(&state, cancelled_busy ? "*Operation cancelled.*"
                                                : "*Cancelled.*") != 0)) {
          report_failure(&state, "cancel");
          exit_code = 1;
          break;
        }
        continue;
      }
      if (status == SL_READLINE_ERROR) {
        report_failure(&state, "readline");
        exit_code = 1;
      }
      break;
    }
    if (strcmp(line, "exit") == 0) {
      sl_free_string(state.sl, line);
      break;
    }
    if (line[0] == '\0') {
      sl_free_string(state.sl, line);
      if (interactive && render_note(&state, "*Empty turn ignored.*") != 0) {
        exit_code = 1;
        break;
      }
      continue;
    }
    if (sl_history_add(state.sl, line) != SL_OK ||
        render_user_prompt(&state, line) != 0) {
      sl_free_string(state.sl, line);
      report_failure(&state, "prompt render");
      exit_code = 1;
      break;
    }
    if (interactive && state.busy) {
      if (render_note(&state, "*Immediate input received while busy.*") != 0) {
        sl_free_string(state.sl, line);
        exit_code = 1;
        break;
      }
    } else if (interactive && start_operation(&state) != 0) {
      sl_free_string(state.sl, line);
      report_failure(&state, "operation start");
      exit_code = 1;
      break;
    }
    sl_free_string(state.sl, line);
    (void)source;
  }

cleanup:
  if (state.busy)
    (void)cancel_operation(&state);
  if (state.prompt_renderer)
    state.prompt_renderer->destroy(state.prompt_renderer);
  if (state.response_renderer)
    state.response_renderer->destroy(state.response_renderer);
  if (state.sl) {
    (void)state.sl->output_stream_end(state.sl);
    sl_destroy(state.sl);
  }
  return exit_code;
}
