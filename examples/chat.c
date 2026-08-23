#include "softline/softline.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int set_prompt_theme_from_environment(sl_t *sl,
                                             sl_prompt_theme_t fallback) {
  const char *name;
  sl_prompt_theme_t theme;
  name = getenv("SOFTLINE_PROMPT_THEME");
  theme = fallback;
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
    else {
      fprintf(stderr,
              "invalid SOFTLINE_PROMPT_THEME: %s (use plain, accent, dracula, "
              "gruvbox, monochrome, monogreen, outrun, riced, or synthwave)\n",
              name);
      return -1;
    }
  }
  return sl_set_prompt_theme(sl, theme) == SL_OK ? 0 : -1;
}

static void keep_chat_on_interrupt(int signo) { (void)signo; }

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
  stream.chunks[0] =
      source == SL_PROMPT_SOURCE_QUEUED ? "[queued] " : "[direct] ";
  stream.chunks[1] = text;
  stream.chunks[2] = "\n";
  stream.chunks[3] = NULL;
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

struct chat_idle_state {
  time_t next_message_at;
  time_t status_started_at;
  int status_phase;
  int status_marker_cycle;
};

static int update_status_presentation(sl_t *sl, struct chat_idle_state *state,
                                      time_t now) {
  int busy;
  int spinner;
  int phase;
  int marker_cycle;
  if (!sl || !state)
    return SL_ERROR_INVALID;
  if (state->status_started_at == (time_t)0)
    state->status_started_at = now;
  phase = (int)(((now - state->status_started_at) / 5) % 8);
  marker_cycle = (int)(((now - state->status_started_at) / 40) % 2);
  if (phase == state->status_phase &&
      marker_cycle == state->status_marker_cycle)
    return SL_OK;
  spinner = phase == 1 || phase == 3;
  busy = spinner || phase == 4 || phase == 6;
  if (sl->set_status_idle_marker(sl, marker_cycle ? '-' : '\0') != SL_OK ||
      sl->set_status_spinner(sl, spinner) != SL_OK ||
      sl->set_status_busy(sl, busy) != SL_OK)
    return SL_ERROR;
  state->status_phase = phase;
  state->status_marker_cycle = marker_cycle;
  return SL_OK;
}

static void run_chat_idle(sl_t *sl, void *userdata) {
  static const char *const messages[] = {
      "[peer] I found a calm corner of the conversation.",
      "[peer] The kettle is on; take your time.",
      "[peer] A small detail can change the whole picture.",
      "[peer] I am following along from the other side of the room."};
  struct chat_idle_state *state;
  time_t now;
  size_t count;
  state = (struct chat_idle_state *)userdata;
  if (!state)
    return;
  now = time(NULL);
  (void)update_status_presentation(sl, state, now);
  if (now < state->next_message_at)
    return;
  state->next_message_at = now + 2;
  count = sizeof(messages) / sizeof(messages[0]);
  (void)print_message(sl, messages[(size_t)rand() % count]);
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
      "gpt-5.6-terra high", "ctx 36%",           "~/g/softline",
      "weekly 56%",         "feat/prompt-queue", "pursuing chat"};
  sl_t *sl;
  char *line;
  sl_prompt_source_t source;
  sl_readline_status_t status;
  struct chat_idle_state idle_state;
  int interactive;
  int exit_code;

  interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
  exit_code = 0;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  if (sl->set_prompt_queue(sl, 1, 64, 3) != SL_OK) {
    fprintf(stderr, "failed to enable prompt queue\n");
    sl->destroy(sl);
    return 1;
  }
  if (interactive) {
    (void)signal(SIGINT, keep_chat_on_interrupt);
    idle_state.next_message_at = time(NULL) + 2;
    idle_state.status_started_at = time(NULL);
    idle_state.status_phase = -1;
    idle_state.status_marker_cycle = -1;
    srand((unsigned int)(time(NULL) ^ (time_t)getpid()));
    if (sl->set_idle_callback(sl, run_chat_idle, &idle_state) != SL_OK) {
      fprintf(stderr, "failed to enable simulated peer messages\n");
      sl->destroy(sl);
      return 1;
    }
  }
  if (set_prompt_theme_from_environment(sl, SL_PROMPT_THEME_ACCENT) != 0) {
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
  if (interactive &&
      update_status_presentation(sl, &idle_state, time(NULL)) != SL_OK) {
    fprintf(stderr, "failed to start chat status demonstration\n");
    sl->destroy(sl);
    return 1;
  }

  if (interactive &&
      print_message(sl, "softline chat example. Tab queues; Alt-E recalls "
                        "the newest queued prompt. Status alternates "
                        "40-second idle-marker cycles.") != SL_OK) {
    (void)print_last_error(sl);
    sl->destroy(sl);
    return 1;
  }
  for (;;) {
    source = SL_PROMPT_SOURCE_NONE;
    line = sl->next_prompt(sl, NULL, &source);
    if (!line) {
      status = sl->last_readline_status(sl);
      if (status == SL_READLINE_CANCELLED ||
          status == SL_READLINE_INTERRUPTED) {
        if (interactive && print_message(sl, "[cancelled]") != SL_OK) {
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
    if (print_dispatched_message(sl, source, line) != SL_OK) {
      sl->free_string(sl, line);
      (void)print_last_error(sl);
      exit_code = 1;
      break;
    }
    sl->free_string(sl, line);
  }

  sl->destroy(sl);
  return exit_code;
}
