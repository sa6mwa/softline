#include "softline/softline.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void leave_alt_screen_on_signal(int signo) {
  leave_alt_screen();
  _exit(128 + signo);
}

static void install_signal_cleanup(void) {
  (void)signal(SIGINT, leave_alt_screen_on_signal);
  (void)signal(SIGTERM, leave_alt_screen_on_signal);
}

static void enter_alt_screen(void) {
  if (!isatty(STDOUT_FILENO))
    return;
  if (write(STDOUT_FILENO, "\033[?1049h\033[2J\033[H", 15) == 15) {
    alt_screen_active = 1;
    (void)atexit(leave_alt_screen);
    install_signal_cleanup();
  }
}

struct message_stream {
  const char *chunks[2];
  int index;
};

static int next_message_chunk(sl_t *sl, void *userdata, const char **chunk,
                              size_t *len) {
  struct message_stream *stream;
  const char *text;
  (void)sl;
  stream = (struct message_stream *)userdata;
  if (!stream || stream->index >= 2) {
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
  stream.index = 0;
  return sl->print_above(sl, next_message_chunk, &stream);
}

int main(void) {
  sl_t *sl;
  char *line;
  sl_prompt_source_t source;

  enter_alt_screen();

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  (void)sl->set_bounds(sl, 0, 0, 0, 0);
  (void)sl->set_prompt_queue(sl, 1, 64, 3);
  if (set_prompt_theme_from_environment(sl, SL_PROMPT_THEME_ACCENT) != 0) {
    fprintf(stderr, "failed to set prompt theme\n");
    sl->destroy(sl);
    leave_alt_screen();
    return 1;
  }

  (void)print_message(sl, "softline chat example. Tab queues; Alt-E recalls "
                          "the newest queued prompt.");
  for (;;) {
    line = sl->next_prompt(sl, "chat> ", &source);
    if (!line)
      break;
    if (strcmp(line, "exit") == 0) {
      sl->free_string(sl, line);
      break;
    }
    (void)source;
    (void)print_message(sl, line);
    sl->free_string(sl, line);
  }

  sl->destroy(sl);
  leave_alt_screen();
  return 0;
}
