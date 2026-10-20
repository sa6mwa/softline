#define _POSIX_C_SOURCE 200809L

#include "softline/softline.h"

#include <libmdf/mdf.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

static const char *const approval_details[] = {
    NULL,
    "Review the proposed changes before continuing.\n"
    "Check the public API and the tests.\n"
    "Report any remaining risks.",
    "Check the complete behavior before proceeding: confirm that ordinary "
    "user input stays responsive while output is streaming, that queued and "
    "steering messages keep their original destination, and that approval "
    "decisions remain in arrival order through editing and requeueing. "
    "Include the evidence from focused tests, describe any remaining "
    "uncertainty, and keep the explanation clear enough to review without "
    "opening the implementation. Paragraph complete.",
    "Verification plan:\n"
    "01. Check intent and constraints.\n"
    "02. Keep ordinary input responsive.\n"
    "03. Preserve FIFO approval order.\n"
    "04. Retain steering intent.\n"
    "05. Keep normal queue previews visible.\n"
    "06. Preserve the reasoning row.\n"
    "07. Wrap text to the terminal width.\n"
    "08. Limit previews to three lines.\n"
    "09. Mark omitted text with an ellipsis.\n"
    "10. Keep full messages editable.\n"
    "11. Retain holds after editing.\n"
    "12. Report verification evidence.\n"
    "End of verification plan."};

struct chat_state {
  sl_t *sl;
  mdf *note_renderer;
  mdf *response_renderer;
  int note_documents;
  int response_documents;
  int response_open;
  int interactive;
  int columns;
  int watch_fd;
  sl_watch_id_t watch_id;
  pid_t worker_pid;
  int busy;
  int exit_requested;
  unsigned int turn_index;
  size_t response_bytes;
  char previous_response_char;
  int approval_demo;
  unsigned int approval_interval_ms;
  unsigned int injected;
  double next_injection;
  int demo_failed;
};

static int start_operation(struct chat_state *state);
static int dispatch_next_turn(struct chat_state *state);
static int sync_geometry(struct chat_state *state);

/* Keep the reasoning row separate from queue/approval rows while idle.
 * Actual messages retain the normal status prefix. */
static int set_chat_message(sl_t *sl, const char *message) {
  int rc = sl_set_status_message_prefix(sl, message ? NULL : "");
  return rc == SL_OK ? sl_set_status_message(sl, message ? message : " ") : rc;
}

static int is_shell_command(const char *line) {
  return strcmp(line, "!sh") == 0 || strcmp(line, "/shell") == 0;
}

/* Run only in the idle owner loop, after next_prompt() has returned. Ending
 * the session releases termios and the scroll region; beginning a new one
 * discards the old prompt frame and probes the shell's final cursor position.
 * system() handles parent SIGINT/SIGQUIT while the interactive shell runs. */
static int run_shell(struct chat_state *state) {
  int status;
  if (sl_output_stream_end(state->sl) != SL_OK)
    return -1;
  /* Quoted expansion treats SHELL as one executable, without parsing its
   * contents as shell syntax. The POSIX shell supplies the unset/empty default.
   */
  status = system("exec \"${SHELL:-/bin/sh}\" -i");
  if (status == -1)
    perror("could not start shell");
  else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    fprintf(stderr, "shell exited unsuccessfully (exit %d)\n",
            WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    fprintf(stderr, "shell exited unsuccessfully (signal %d)\n",
            WTERMSIG(status));
  return sync_geometry(state) == 0 &&
                 sl_output_stream_begin(state->sl) == SL_OK &&
                 set_chat_message(state->sl, NULL) == SL_OK
             ? 0
             : -1;
}

static int sink_to_softline(void *userdata, const char *bytes, size_t length) {
  struct chat_state *state;
  state = (struct chat_state *)userdata;
  if (!state || (!bytes && length > 0))
    return -1;
  return sl_output_stream_write(state->sl, bytes, length) == SL_OK ? 0 : -1;
}

static int chat_margin_left(int columns) { return columns >= 5 ? 2 : 0; }

static int new_renderer(struct chat_state *state, mdf **out) {
  mdf_options options;
  mdf_sink sink;
  mdf_options_init(&options);
  options.output_fd = STDOUT_FILENO;
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
  if (state->note_renderer->set_geometry(state->note_renderer, columns,
                                         chat_margin_left(columns),
                                         0) != MDF_OK ||
      state->response_renderer->set_geometry(state->response_renderer, columns,
                                             chat_margin_left(columns),
                                             0) != MDF_OK)
    return -1;
  state->columns = columns;
  return 0;
}

static int begin_note_document(struct chat_state *state) {
  if (sync_geometry(state) != 0)
    return -1;
  if (state->note_documents > 0 &&
      state->note_renderer->begin_document(state->note_renderer) != MDF_OK)
    return -1;
  return 0;
}

static int finish_note_document(struct chat_state *state) {
  if (state->note_renderer->finish_document(state->note_renderer) != MDF_OK)
    return -1;
  state->note_documents++;
  return 0;
}

static int render_note(struct chat_state *state, const char *note) {
  if (begin_note_document(state) != 0 ||
      state->note_renderer->feed(state->note_renderer, "\n", 1) != MDF_OK ||
      state->note_renderer->feed(state->note_renderer, note, strlen(note)) !=
          MDF_OK ||
      state->note_renderer->feed(state->note_renderer, "\n\n", 2) != MDF_OK)
    return -1;
  return finish_note_document(state);
}

static int render_user_prompt(struct chat_state *state, const char *line) {
  return sl_output_stream_write_quoted_prompt(state->sl, line) == SL_OK ? 0
                                                                        : -1;
}

static int set_busy(struct chat_state *state, int busy) {
  if (sl_set_status_spinner(state->sl, busy) != SL_OK ||
      sl_set_status_busy(state->sl, busy) != SL_OK ||
      set_chat_message(state->sl, busy ? "Thinking..." : NULL) != SL_OK)
    return -1;
  state->busy = busy;
  return 0;
}

static int show_goodbye(struct chat_state *state) {
  if (!state->interactive)
    return 0;
  if (sl_set_status_spinner(state->sl, 0) != SL_OK ||
      sl_set_status_busy(state->sl, 0) != SL_OK ||
      set_chat_message(state->sl, "Goodbye.") != SL_OK)
    return -1;
  return 0;
}

static int finish_response_document(struct chat_state *state) {
  if (!state->response_open)
    return 0;
  if (state->response_renderer->finish_document(state->response_renderer) !=
      MDF_OK)
    return -1;
  state->response_open = 0;
  state->response_documents++;
  return 0;
}

static int deliver_steers(struct chat_state *state, int at_seam) {
  size_t index;
  sl_prompt_queue_entry_info_t info;
  char *line;
  for (index = 0; index < sl_prompt_queue_count(state->sl);) {
    if (sl_prompt_queue_info(state->sl, index, &info) != SL_OK)
      return -1;
    if (info.mode != SL_PROMPT_QUEUE_MODE_STEER || info.held) {
      index++;
      continue;
    }
    /* libmdf retains a block separator until the next block arrives. Close
     * this response segment before inserting a separately rendered prompt. */
    if (at_seam && finish_response_document(state) != 0)
      return -1;
    at_seam = 0;
    line = NULL;
    if (sl_prompt_queue_take(state->sl, index, &line) != SL_OK)
      return -1;
    if (strcmp(line, "/quit") == 0) {
      sl_free_string(state->sl, line);
      state->exit_requested = 1;
      return show_goodbye(state) == 0 && sl_cancel(state->sl) == SL_OK ? 0 : -1;
    }
    if (is_shell_command(line)) {
      sl_free_string(state->sl, line);
      if (render_note(state, "Shell commands cannot be queued or steered.") !=
          0)
        return -1;
      continue;
    }
    if (render_user_prompt(state, line) != 0) {
      sl_free_string(state->sl, line);
      return -1;
    }
    sl_free_string(state->sl, line);
  }
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
  if (finish_response_document(state) != 0 || set_busy(state, 0) != 0)
    return -1;
  /* A steer that misses the final response seam is still a user turn. Give
   * the oldest pending entry its own response instead of quoting it alone. */
  return dispatch_next_turn(state);
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
  if (finish_response_document(state) != 0)
    return -1;
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
      if (!state->response_open) {
        if (state->response_renderer->begin_document(
                state->response_renderer) != MDF_OK)
          return SL_ERROR_IO;
        state->response_open = 1;
      }
      if (state->response_renderer->feed(state->response_renderer, bytes + i,
                                         1) != MDF_OK)
        return SL_ERROR_IO;
      if (bytes[i] == '\n' && state->previous_response_char == '\n') {
        if (deliver_steers(state, 1) != 0)
          return SL_ERROR_IO;
        if (state->exit_requested)
          return SL_OK;
      }
      state->previous_response_char = bytes[i];
      state->response_bytes++;
      if (state->response_bytes == 15 &&
          set_chat_message(state->sl, "Reasoning...") != SL_OK)
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
  state->response_bytes = 0;
  state->previous_response_char = '\0';
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
      if (delay > 0)
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

static int dispatch_next_turn(struct chat_state *state) {
  char *line;
  size_t index;
  sl_prompt_queue_entry_info_t info;
  for (index = 0; index < sl_prompt_queue_count(state->sl);) {
    if (sl_prompt_queue_info(state->sl, index, &info) != SL_OK)
      return -1;
    if (info.held) {
      index++;
      continue;
    }
    line = NULL;
    if (sl_prompt_queue_take(state->sl, index, &line) != SL_OK)
      return -1;
    if (strcmp(line, "/quit") == 0) {
      sl_free_string(state->sl, line);
      state->exit_requested = 1;
      return show_goodbye(state) == 0 && sl_cancel(state->sl) == SL_OK ? 0 : -1;
    }
    if (is_shell_command(line)) {
      sl_free_string(state->sl, line);
      if (render_note(state, "Shell commands cannot be queued or steered.") !=
          0)
        return -1;
      continue;
    }
    if (render_user_prompt(state, line) != 0 || start_operation(state) != 0) {
      sl_free_string(state->sl, line);
      return -1;
    }
    sl_free_string(state->sl, line);
    break;
  }
  return 0;
}

static double monotonic_seconds(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    return -1;
  return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static void schedule_injection(struct chat_state *state, double now) {
  unsigned int interval = state->approval_interval_ms;
  if (!interval)
    interval = 10000u + (unsigned int)rand() % 10001u;
  state->next_injection = now + (double)interval / 1000.0;
}

/* The demo uses the editor's owner-thread idle hook. No timer worker or
 * competing input loop is needed; typing and stream watches keep priority. */
static void approval_demo_idle(sl_t *sl, void *userdata) {
  struct chat_state *state = userdata;
  sl_prompt_queue_delivery_t delivery;
  sl_prompt_queue_entry_info_t draft;
  double now = monotonic_seconds();
  int result = SL_OK;
  if (now < 0)
    result = SL_ERROR_IO;
  else if (now >= state->next_injection) {
    schedule_injection(state, now);
    if (sl_prompt_queue_count(sl) < 4) {
      char message[2048];
      sl_prompt_id_t id;
      int steering = (state->injected % 2u) != 0;
      unsigned int shape = state->injected ? (unsigned int)rand() % 4u : 0u;
      const char *details = shape ? approval_details[shape]
                            : steering
                                ? "Focus the next answer on verification."
                                : "Summarize the next steps before continuing.";
      (void)snprintf(message, sizeof(message), "Demo %s request #%u: %s",
                     steering ? "steering" : "queued", state->injected + 1u,
                     details);
      result = steering ? sl_inject_steer_prompt(sl, message, &id)
                        : sl_inject_queued_prompt(sl, message, &id);
      if (result == SL_OK)
        state->injected++;
    }
  }
  /* An approved steer arriving between turns gets its own response. Respect
   * the queue's stopped state after cancellation and an unfinished draft. */
  if (result == SL_OK && !state->busy && !sl_buffer(sl)[0] &&
      !sl->history_search_active(sl)) {
    result = sl_prompt_queue_draft_info(sl, &draft);
    if (result == SL_OK && !draft.id) {
      result = sl_get_prompt_queue_delivery(sl, &delivery);
      if (result == SL_OK && delivery == SL_PROMPT_QUEUE_DELIVERY_AUTO &&
          dispatch_next_turn(state) != 0)
        result = SL_ERROR_IO;
    }
  }
  if (result != SL_OK) {
    state->demo_failed = 1;
    (void)sl_cancel(sl);
  }
}

static int setup_approval_demo(struct chat_state *state) {
  const char *enabled = getenv("SOFTLINE_CHAT_APPROVAL_DEMO");
  const char *interval = getenv("SOFTLINE_CHAT_APPROVAL_MS");
  const char *seed_text = getenv("SOFTLINE_CHAT_APPROVAL_SEED");
  unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
  double now;
  if (!state->interactive || !enabled || strcmp(enabled, "1"))
    return 0;
  state->approval_demo = 1;
  if (interval && interval[0]) {
    char *end;
    long value;
    errno = 0;
    value = strtol(interval, &end, 10);
    if (errno || *end || value < 1 || value > 60000) {
      fprintf(stderr, "SOFTLINE_CHAT_APPROVAL_MS must be 1..60000\n");
      return -1;
    }
    state->approval_interval_ms = (unsigned int)value;
  }
  now = monotonic_seconds();
  if (now < 0)
    return -1;
  if (seed_text && seed_text[0]) {
    char *end;
    unsigned long value;
    errno = 0;
    value = strtoul(seed_text, &end, 10);
    if (seed_text[0] < '0' || seed_text[0] > '9' || errno || *end ||
        value > UINT_MAX) {
      fprintf(stderr, "SOFTLINE_CHAT_APPROVAL_SEED must be 0..%u\n", UINT_MAX);
      return -1;
    }
    seed = (unsigned int)value;
  }
  srand(seed);
  schedule_injection(state, now);
  return sl_set_prompt_queue_delivery(state->sl,
                                      SL_PROMPT_QUEUE_DELIVERY_AUTO) == SL_OK &&
                 sl_set_idle_callback(state->sl, approval_demo_idle, state) ==
                     SL_OK
             ? 0
             : -1;
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

/* Status changes must render before readline releases its active prompt. */
static int exit_editor_key(sl_t *sl, sl_key_t key, void *userdata,
                           sl_key_action_t *action) {
  struct chat_state *state = (struct chat_state *)userdata;
  const char *text = sl_buffer(sl);
  char *queued = NULL;
  int leaving = key == SL_KEY_CTRL_D && text[0] == '\0';
  *action = SL_KEY_ACTION_PASS;
  /* Search keys edit/select a match; they do not submit, queue or steer it. */
  if (sl->history_search_active(sl))
    return SL_OK;
  if ((key == SL_KEY_ENTER || key == SL_KEY_ALT_ENTER || key == SL_KEY_TAB) &&
      strncmp(text, "/approve", 8) == 0 &&
      (text[8] == '\0' || text[8] == ' ')) {
    int enabled;
    sl_prompt_queue_entry_info_t draft;
    *action = SL_KEY_ACTION_HANDLED;
    if (sl_prompt_queue_draft_info(sl, &draft) != SL_OK)
      return SL_ERROR;
    if (draft.id)
      return set_chat_message(sl, "Finish editing the queued message first.");
    if (!strcmp(text, "/approve")) {
      if (sl_get_auto_approval(sl, &enabled) != SL_OK)
        return SL_ERROR;
      enabled = !enabled;
    } else if (!strcmp(text, "/approve on"))
      enabled = 1;
    else if (!strcmp(text, "/approve off"))
      enabled = 0;
    else
      return set_chat_message(sl, "Usage: /approve [on|off]");
    if (sl_set_auto_approval(sl, enabled) != SL_OK ||
        sl_set_buffer(sl, "") != SL_OK)
      return SL_ERROR;
    return set_chat_message(
        sl, enabled ? "Auto-approval ON (all waiting requests approved)."
                    : "Auto-approval OFF (new requests need approval).");
  }
  if ((key == SL_KEY_ENTER || key == SL_KEY_ALT_ENTER || key == SL_KEY_TAB) &&
      is_shell_command(text) && (state->busy || key == SL_KEY_TAB)) {
    *action = SL_KEY_ACTION_HANDLED;
    return set_chat_message(
        sl, state->busy ? "Shell is available only between turns."
                        : "Use Enter to open the shell; it cannot be queued.");
  }
  if (!state->busy && (key == SL_KEY_ENTER || key == SL_KEY_ALT_ENTER)) {
    if (key == SL_KEY_ALT_ENTER && text[0] == '\0' &&
        sl_prompt_queue_count(sl) > 0) {
      int result =
          sl_prompt_queue_peek(sl, sl_prompt_queue_count(sl) - 1, &queued);
      if (result != SL_OK)
        return result;
      text = queued;
    }
    leaving = strcmp(text, "/quit") == 0;
  }
  sl_free_string(sl, queued);
  return leaving && show_goodbye(state) != 0 ? SL_ERROR_IO : SL_OK;
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
                                                "demo/project", "queue demo"};
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
  if (sl_history_open(state.sl, "softline.examples.chat",
                      getenv("SOFTLINE_HISTORY_DIR")) != SL_OK ||
      sl_history_set_auto_add(state.sl, 1) != SL_OK) {
    report_failure(&state, "history setup");
    sl_destroy(state.sl);
    return 1;
  }
  state.columns = mdf_terminal_width(STDOUT_FILENO, 80);
  if ((interactive &&
       (sl_set_prompt_queue(state.sl, 1, 64, 3) != SL_OK ||
        sl_set_prompt_queue_profile(
            state.sl, SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS) != SL_OK ||
        sl_set_prompt_queue_delivery(
            state.sl, SL_PROMPT_QUEUE_DELIVERY_MANUAL) != SL_OK ||
        sl_set_statusline(state.sl, 1, 0) != SL_OK ||
        sl_set_status_elements(state.sl, status_elements,
                               sizeof(status_elements) /
                                   sizeof(status_elements[0])) != SL_OK ||
        set_prompt_theme_from_environment(state.sl) != 0 ||
        sl_bind_key(state.sl, SL_KEY_ESCAPE, cancel_editor_key, NULL) !=
            SL_OK ||
        sl_bind_key(state.sl, SL_KEY_ENTER, exit_editor_key, &state) != SL_OK ||
        sl_bind_key(state.sl, SL_KEY_ALT_ENTER, exit_editor_key, &state) !=
            SL_OK ||
        sl_bind_key(state.sl, SL_KEY_TAB, exit_editor_key, &state) != SL_OK ||
        sl_bind_key(state.sl, SL_KEY_CTRL_D, exit_editor_key, &state) !=
            SL_OK)) ||
      state.sl->output_stream_begin(state.sl) != SL_OK ||
      new_renderer(&state, &state.note_renderer) != 0 ||
      new_renderer(&state, &state.response_renderer) != 0) {
    report_failure(&state, "setup");
    exit_code = 1;
    goto cleanup;
  }
  if (interactive) {
    (void)signal(SIGINT, keep_chat_on_interrupt);
    if (set_busy(&state, 0) != 0 || setup_approval_demo(&state) != 0 ||
        render_note(&state,
                    "Enter sends or queues; Alt-Enter steers or promotes; "
                    "Alt-E edits queue. Esc/Ctrl-C cancels; `/quit` leaves. "
                    "`!sh` or `/shell` opens your shell between turns.") != 0) {
      report_failure(&state, "initial output");
      exit_code = 1;
      goto cleanup;
    }
    if (state.approval_demo &&
        render_note(
            &state,
            "Approval demo: queued and steering requests arrive "
            "every 10-20 seconds. Alt-A focuses approvals; Alt-Y approves, "
            "Alt-N rejects while focused, Alt-E edits, Esc returns. "
            "`/approve` toggles auto-approval; `/approve on|off` sets it.") !=
            0) {
      exit_code = 1;
      goto cleanup;
    }
  }
  for (;;) {
    source = SL_PROMPT_SOURCE_NONE;
    line = sl_next_prompt(state.sl, NULL, &source);
    if (state.demo_failed) {
      sl_free_string(state.sl, line);
      report_failure(&state, "approval demo");
      exit_code = 1;
      break;
    }
    if (state.exit_requested) {
      if (line)
        sl_free_string(state.sl, line);
      break;
    }
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
    if (strcmp(line, "/quit") == 0) {
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
    if (is_shell_command(line)) {
      int failed = 0;
      if (!interactive || state.busy || source != SL_PROMPT_SOURCE_DIRECT) {
        failed = render_note(
            &state, !interactive ? "Shell requires an interactive terminal."
                                 : "Shell is available only between turns.");
      } else {
        failed =
            render_user_prompt(&state, line) != 0 || run_shell(&state) != 0;
      }
      sl_free_string(state.sl, line);
      if (failed) {
        report_failure(&state, "shell handoff");
        exit_code = 1;
        break;
      }
      continue;
    }
    if (render_user_prompt(&state, line) != 0) {
      sl_free_string(state.sl, line);
      report_failure(&state, "prompt render");
      exit_code = 1;
      break;
    }
    if (interactive && !state.busy && start_operation(&state) != 0) {
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
  if (state.note_renderer)
    state.note_renderer->destroy(state.note_renderer);
  if (state.response_renderer)
    state.response_renderer->destroy(state.response_renderer);
  if (state.sl) {
    if (state.sl->output_stream_end)
      (void)state.sl->output_stream_end(state.sl);
    sl_destroy(state.sl);
  }
  return exit_code;
}
