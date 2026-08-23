#include "softline/softline.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t alt_screen_active = 0;

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
    else if (strcmp(name, "riced") == 0)
      theme = SL_PROMPT_THEME_RICED;
    else {
      fprintf(
          stderr,
          "invalid SOFTLINE_PROMPT_THEME: %s (use plain, accent, or riced)\n",
          name);
      return -1;
    }
  }
  return sl_set_prompt_theme(sl, theme) == SL_OK ? 0 : -1;
}

static void leave_alt_screen(void) {
  if (!alt_screen_active)
    return;
  (void)write(STDOUT_FILENO, "\033[?1049l", 8);
  alt_screen_active = 0;
}

static void leave_alt_screen_on_terminate(int signo) {
  leave_alt_screen();
  _exit(128 + signo);
}

static void keep_chat_on_interrupt(int signo) { (void)signo; }

static void install_signal_cleanup(void) {
  (void)signal(SIGINT, keep_chat_on_interrupt);
  (void)signal(SIGTERM, leave_alt_screen_on_terminate);
}

static int enter_alt_screen(void) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    return 0;
  if (write(STDOUT_FILENO, "\033[?1049h\033[2J\033[H", 15) == 15) {
    alt_screen_active = 1;
    (void)atexit(leave_alt_screen);
    install_signal_cleanup();
    return 1;
  }
  return 0;
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
  if (!stream || stream->index >= 4) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  text = stream->chunks[stream->index];
  stream->index++;
  if (!text) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  *chunk = text;
  *len = strlen(text);
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

static int print_reply(sl_t *sl, sl_prompt_source_t source, const char *text) {
  struct message_stream stream;
  stream.chunks[0] =
      source == SL_PROMPT_SOURCE_QUEUED ? "[queued] " : "[direct] ";
  stream.chunks[1] = "I read back: ";
  stream.chunks[2] = text;
  stream.chunks[3] = "\n";
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

struct peer_state {
  time_t next_message_at;
};

static void print_peer_message(sl_t *sl, void *userdata) {
  static const char *const messages[] = {
      "[peer] I found a calm corner of the conversation.",
      "[peer] The kettle is on; take your time.",
      "[peer] A small detail can change the whole picture.",
      "[peer] I am following along from the other side of the room."};
  struct peer_state *state;
  time_t now;
  size_t count;
  state = (struct peer_state *)userdata;
  if (!state)
    return;
  now = time(NULL);
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
  sl_t *sl;
  char *line;
  sl_prompt_source_t source;
  sl_readline_status_t status;
  struct peer_state peer;
  int interactive;
  int exit_code;

  interactive = enter_alt_screen();
  exit_code = 0;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  if (interactive && sl->set_bounds(sl, 0, 0, 0, 0) != SL_OK) {
    fprintf(stderr, "failed to set chat bounds\n");
    sl->destroy(sl);
    leave_alt_screen();
    return 1;
  }
  if (interactive && sl->set_prompt_queue(sl, 1, 64, 3) != SL_OK) {
    fprintf(stderr, "failed to enable prompt queue\n");
    sl->destroy(sl);
    leave_alt_screen();
    return 1;
  }
  if (interactive) {
    peer.next_message_at = time(NULL) + 2;
    srand((unsigned int)(time(NULL) ^ (time_t)getpid()));
    if (sl->set_idle_callback(sl, print_peer_message, &peer) != SL_OK) {
      fprintf(stderr, "failed to enable simulated peer messages\n");
      sl->destroy(sl);
      leave_alt_screen();
      return 1;
    }
  }
  if (set_prompt_theme_from_environment(sl, SL_PROMPT_THEME_ACCENT) != 0) {
    fprintf(stderr, "failed to set prompt theme\n");
    sl->destroy(sl);
    leave_alt_screen();
    return 1;
  }

  if (interactive &&
      print_message(sl, "softline chat example. Tab queues; Alt-E recalls "
                        "the newest queued prompt.") != SL_OK) {
    (void)print_last_error(sl);
    sl->destroy(sl);
    leave_alt_screen();
    return 1;
  }
  for (;;) {
    line = sl->next_prompt(sl, "chat> ", &source);
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
    if (print_reply(sl, source, line) != SL_OK) {
      sl->free_string(sl, line);
      (void)print_last_error(sl);
      exit_code = 1;
      break;
    }
    sl->free_string(sl, line);
  }

  sl->destroy(sl);
  leave_alt_screen();
  return exit_code;
}
