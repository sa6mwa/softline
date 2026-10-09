#include "terminal_pause.h"
#include "vte_test.h"

struct chat {
  struct screen screen;
  pid_t child, worker;
};
static struct snapshot frame(struct chat *c) {
  /* This regression observes physical rows through the hardware cursor. */
  return snapshot(&c->screen, 0);
}
static const char *last_row(const char *text) {
  const char *line = text, *at;
  for (at = text; *at; ++at)
    if (*at == '\n' && at[1])
      line = at + 1;
  return line;
}
static int idle(const struct snapshot *s) {
  return !strstr(s->text, "Thinking") && !strstr(s->text, "Reasoning");
}
static int wait_frame(struct chat *c, const char *marker, const char *prompt,
                      int complete, double timeout) {
  double deadline = now() + timeout;
  do {
    struct snapshot s = frame(c);
    const char *tail = last_row(s.text);
    if ((!marker || strstr(s.text, marker)) &&
        (!prompt || !strcmp(tail, prompt)) &&
        (complete == 0 || (complete > 0 ? idle(&s) : !idle(&s))))
      return 0;
    pump();
  } while (now() < deadline);
  fprintf(stderr, "Chat frame did not settle: %s\n",
          marker   ? marker
          : prompt ? prompt
                   : "requested busy/idle state");
  return 1;
}
static int drain(void *context) {
  struct chat *c = (struct chat *)context;
  int status;
  pid_t event;
  pump();
  event = waitpid(c->child, &status, WNOHANG);
  if (event == c->child) {
    c->child = -1;
    return 1;
  }
  return event < 0;
}
static void close_chat(struct chat *c) {
  int status;
  if (c->worker > 0)
    kill(c->worker, SIGCONT);
  if (c->child > 0) {
    kill(-c->child, SIGTERM);
    kill(c->child, SIGCONT);
    waitpid(c->child, &status, 0);
  }
  v.destroy(c->screen.window);
  pump();
}
static int start_chat(struct chat *c, const char *example, const char *theme,
                      int height, int delay) {
  struct winsize size;
  void *pty;
  int master, slave, index;
  char prior[4096], milliseconds[32];
  size_t used = 0;
  c->child = c->worker = -1;
  c->screen = create();
  resize(&c->screen, 97, height);
  for (index = 0; index < 100; ++index)
    used += (size_t)snprintf(prior + used, sizeof(prior) - used,
                             "existing terminal row %03d\r\n", index);
  v.feed(c->screen.terminal, prior, (long)used);
  pump();
  memset(&size, 0, sizeof(size));
  size.ws_col = 97;
  size.ws_row = (unsigned short)height;
  if (openpty(&master, &slave, NULL, NULL, &size))
    return 1;
  pty = v.pty(master, NULL, NULL);
  if (!pty) {
    close(master);
    close(slave);
    return 1;
  }
  v.set_pty(c->screen.terminal, pty);
  v.unref(pty);
  snprintf(milliseconds, sizeof(milliseconds), "%d", delay);
  c->child = fork();
  if (c->child == 0) {
    close(master);
    setsid();
    ioctl(slave, TIOCSCTTY, 0);
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    if (slave > 2)
      close(slave);
    setenv("TERM", "xterm-256color", 1);
    setenv("SOFTLINE_CHAT_CHAR_MS", milliseconds, 1);
    setenv("SOFTLINE_PROMPT_THEME", theme, 1);
    execl(example, example, (char *)NULL);
    _exit(127);
  }
  close(slave);
  return c->child < 0 || wait_frame(c, NULL, "> \n", 0, 8);
}
static void send_input(struct chat *c, const char *bytes) {
  v.send(c->screen.terminal, bytes, (long)strlen(bytes));
}
static void settle(double seconds) {
  double deadline = now() + seconds;
  do {
    pump();
  } while (now() < deadline);
}
static int window_resize(struct chat *c, int columns, int rows) {
  double deadline = now() + 5;
  int x = v.width(c->screen.terminal) +
          (columns - (int)v.columns(c->screen.terminal)) *
              (int)v.char_width(c->screen.terminal);
  int y =
      v.height(c->screen.terminal) + (rows - (int)v.rows(c->screen.terminal)) *
                                         (int)v.char_height(c->screen.terminal);
  v.resize(c->screen.window, x, y);
  do {
    pump();
  } while ((v.columns(c->screen.terminal) != columns ||
            v.rows(c->screen.terminal) != rows) &&
           now() < deadline);
  return v.columns(c->screen.terminal) != columns ||
         v.rows(c->screen.terminal) != rows;
}
static int coordinated_resize(struct chat *c, int columns, int rows,
                              int worker_already_paused) {
  int failed;
  if (!worker_already_paused && pause_worker(c->child, drain, c, &c->worker))
    return 1;
  if (pause_editor(c->child, drain, c)) {
    if (c->worker > 0)
      kill(c->worker, SIGCONT);
    c->worker = -1;
    return 1;
  }
  failed = window_resize(c, columns, rows);
  kill(c->child, SIGCONT);
  if (!worker_already_paused && c->worker > 0) {
    kill(c->worker, SIGCONT);
    c->worker = -1;
  }
  return failed;
}
static int transcript_part(const struct snapshot *s, char *result) {
  const char *begin = strstr(s->text, "> p[oj\n"), *line, *last = NULL;
  if (!begin)
    return 1;
  for (line = begin; *line;) {
    const char *end = strchr(line, '\n');
    if (!strncmp(line, "+ streaming demo", 16))
      last = line;
    if (!end)
      break;
    line = end + 1;
  }
  if (!last)
    return 1;
  memcpy(result, begin, (size_t)(last - begin));
  result[last - begin] = 0;
  return 0;
}
static int row_distance(const char *text, const char *first,
                        const char *second) {
  const char *a = strstr(text, first),
             *b = *second ? strstr(text, second) : text + strlen(text);
  int count = 0;
  if (!a || !b || b < a)
    return -1;
  while (a < b)
    if (*a++ == '\n')
      ++count;
  return count;
}
static void compact(const char *text, char *result) {
  while (*text) {
    if (*text != ' ' && *text != '\t' && *text != '\n' && *text != '\r')
      *result++ = *text;
    ++text;
  }
  *result = 0;
}
#define REQUIRE(cnd)                                                           \
  do {                                                                         \
    if (!(cnd)) {                                                              \
      struct snapshot s = transcript(&c.screen);                               \
      fprintf(stderr, "Chat VTE assertion: %s\n%s\n", #cnd, s.text);           \
      failed = 1;                                                              \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)
static int submit(struct chat *c, const char *message, const char *ending) {
  send_input(c, message);
  send_input(c, "\r");
  return wait_frame(c, ending, "> \n", 1, 8);
}
static int blank_case(const char *example, const char *theme, int height) {
  struct chat c;
  struct snapshot s;
  char baseline[32768], actual[32768];
  int failed = 0, width, index;
  const int widths[] = {96, 97, 96, 97, 95, 97, 98, 96, 97};
  const int later[] = {96, 97, 95, 97, 96, 97};
  REQUIRE(start_chat(&c, example, theme, height, 0) == 0);
  REQUIRE(submit(&c, "p[oj", "  Try another prompt.\n") == 0);
  REQUIRE(submit(&c, "oijh", "  The stream is still live.\n") == 0);
  s = frame(&c);
  REQUIRE(transcript_part(&s, baseline) == 0);
  REQUIRE(row_distance(baseline, "> oijh\n", "  ## A longer answer\n") == 2);
  REQUIRE(row_distance(baseline, "  The stream is still live.\n",
                       baseline + strlen(baseline)) == 3);
  for (index = 0; index < 50; ++index) {
    width = index < 9    ? widths[index]
            : index < 30 ? 110 - (index - 9)
                         : 91 + (index - 30);
    resize(&c.screen, width, height);
    REQUIRE(wait_frame(&c, NULL, "> \n", 0, 8) == 0);
    pump();
    s = frame(&c);
    REQUIRE(transcript_part(&s, actual) == 0);
    REQUIRE(!strcmp(actual, baseline));
  }
  REQUIRE(submit(&c, "café",
                 "  A single line can be italic, bold, or code.\n") == 0);
  resize(&c.screen, 97, height);
  s = frame(&c);
  REQUIRE(transcript_part(&s, baseline) == 0);
  REQUIRE(strstr(baseline, "> café\n") && strstr(baseline, "  # Notes\n"));
  for (index = 0; index < 6; ++index) {
    resize(&c.screen, later[index], height);
    REQUIRE(wait_frame(&c, NULL, "> \n", 0, 8) == 0);
    pump();
    s = frame(&c);
    REQUIRE(transcript_part(&s, actual) == 0);
    REQUIRE(!strcmp(actual, baseline));
  }
cleanup:
  close_chat(&c);
  return failed || errors;
}
static int streaming_case(const char *example, const char *theme) {
  struct chat c;
  struct snapshot s;
  char normalized[32768], expected[256];
  int failed = 0, turn, dimension;
  const int dimensions[][2] = {
      {45, 24}, {30, 18}, {80, 28}, {40, 12}, {97, 24}};
  const char *messages[] = {"p[oj", "oijh", "café"};
  const char *headings[] = {"  # A short answer\n", "  ## A longer answer\n",
                            "  # Notes\n"};
  const char *endings[] = {"  Try another prompt.\n",
                           "  The stream is still live.\n",
                           "  A single line can be italic, bold, or code.\n"};
  const char *sentences[] = {
      "Here is italic context, bold emphasis, and code in one paragraph.",
      "Then use code for the operation; bold marks the result and italic marks "
      "a caveat.",
      "A single line can be italic, bold, or code."};
  REQUIRE(start_chat(&c, example, theme, 24, 20) == 0);
  for (turn = 0; turn < 3; ++turn) {
    send_input(&c, "\025");
    send_input(&c, messages[turn]);
    send_input(&c, "\r");
    REQUIRE(wait_frame(&c, headings[turn], NULL, 0, 8) == 0);
    send_input(&c, "draft");
    for (dimension = 0; dimension < 5; ++dimension) {
      REQUIRE(coordinated_resize(&c, dimensions[dimension][0],
                                 dimensions[dimension][1], 0) == 0);
      REQUIRE(wait_frame(&c, NULL, "> draft\n", 0, 8) == 0);
      settle(.15);
    }
    REQUIRE(wait_frame(&c, endings[turn], "> draft\n", 1, 8) == 0);
    s = frame(&c);
    compact(s.text, normalized);
    compact(sentences[turn], expected);
    REQUIRE(strstr(normalized, expected));
    REQUIRE(!strcmp(last_row(s.text), "> draft\n"));
  }
cleanup:
  close_chat(&c);
  return failed || errors;
}
static int occurrences(const char *text, const char *needle) {
  int count = 0;
  while ((text = strstr(text, needle)) != NULL) {
    ++count;
    text += strlen(needle);
  }
  return count;
}
static int width_case(const char *example, const char *theme) {
  struct chat c;
  struct snapshot s;
  char normalized[32768], expected[256], message[64], typing[64];
  const char *documents[][4] = {
      {"# A short answer",
       "Here is italic context, bold emphasis, and code in one paragraph.",
       "## Next step", "Try another prompt."},
      {"## A longer answer", "First, inspect the input.",
       "Then use code for the operation; bold marks the result and italic "
       "marks a caveat.",
       "# Summary"},
      {"# Notes", "A single line can be italic, bold, or code.", NULL, NULL}};
  const int widths2[] = {80, 64, 45, 30, 44, 80, 97};
  const int widths3[] = {96, 93, 97, 100, 95, 97};
  const int prompt_widths[] = {40, 30, 80, 30, 80};
  const char *position;
  int failed = 0, turn, length, stage, width, count, prior, row;
  double started;
  REQUIRE(start_chat(&c, example, theme, 24, 20) == 0);
  for (turn = 0; turn < 4; ++turn) {
    snprintf(message, sizeof(message), "hello%d\r", turn);
    send_input(&c, message);
    REQUIRE(wait_frame(&c, NULL, NULL, -1, 5) == 0);
    if (turn == 0)
      REQUIRE(wait_frame(&c, "Here is", NULL, 0, 5) == 0);
    else {
      snprintf(message, sizeof(message), "> hello%d\n", turn);
      REQUIRE(wait_frame(&c, message, NULL, 0, 5) == 0);
    }
    if (turn == 0) {
      for (length = 1; length <= 6; ++length) {
        started = now();
        typing[0] = "typing"[length - 1];
        typing[1] = 0;
        send_input(&c, typing);
        snprintf(typing, sizeof(typing), "> %.*s\n", length, "typing");
        REQUIRE(wait_frame(&c, NULL, typing, 0, .1) == 0);
        REQUIRE(now() - started < .15);
      }
      send_input(&c, "\025");
    } else {
      send_input(&c, "draft");
      REQUIRE(wait_frame(&c, NULL, "> draft\n", 0, .1) == 0);
      REQUIRE(pause_worker(c.child, drain, &c, &c.worker) == 0);
      count = turn == 1 ? 161 : turn == 2 ? 14 : 12;
      for (stage = 0; stage < count; ++stage) {
        width = turn == 1   ? (stage < 80 ? 110 - stage : 30 + stage - 80)
                : turn == 2 ? widths2[stage % 7]
                            : widths3[stage % 6];
        REQUIRE(coordinated_resize(&c, width, 24, 1) == 0);
        settle(turn == 1 ? .005 : turn == 2 ? .06 : .12);
      }
      if (c.worker > 0) {
        kill(c.worker, SIGCONT);
        c.worker = -1;
      }
    }
    REQUIRE(wait_frame(&c, NULL, turn ? "> draft\n" : "> \n", 1, 7) == 0);
    if (turn) {
      send_input(&c, "\025");
      REQUIRE(wait_frame(&c, NULL, "> \n", 0, 5) == 0);
    }
    s = frame(&c);
    compact(s.text, normalized);
    for (prior = 0; prior <= turn; ++prior)
      for (row = 0; row < 4; ++row) {
        if (!documents[prior % 3][row])
          continue;
        compact(documents[prior % 3][row], expected);
        count = 0;
        for (stage = 0; stage <= turn; ++stage)
          if (stage % 3 == prior % 3)
            ++count;
        REQUIRE(occurrences(normalized, expected) == count);
      }
    if (turn >= 1) {
      compact("The stream is still live.", expected);
      REQUIRE(occurrences(normalized, expected) == 1);
    }
    REQUIRE(occurrences(s.text, "> hello") == turn + 1);
  }
  memset(typing, 'a', 60);
  typing[60] = 0;
  send_input(&c, typing);
  REQUIRE(
      wait_frame(
          &c, NULL,
          "> aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", 0,
          5) == 0);
  for (stage = 0; stage < 5; ++stage) {
    REQUIRE(window_resize(&c, prompt_widths[stage], 24) == 0);
    settle(.15);
    s = frame(&c);
    REQUIRE(occurrences(s.text, "streaming demo") == 1);
    position = strstr(s.text, "> aaa");
    REQUIRE(position);
    compact(position, normalized);
    strcpy(expected, ">");
    strcat(expected, typing);
    REQUIRE(!strcmp(normalized, expected));
    REQUIRE(s.row == (long)occurrences(s.text, "\n") - 1);
  }
  send_input(&c, "\025");
  REQUIRE(wait_frame(&c, NULL, "> \n", 0, 5) == 0);
  send_input(&c, "/quit\r");
  REQUIRE(wait_frame(&c, "Goodbye.", NULL, 0, 5) == 0);
cleanup:
  close_chat(&c);
  return failed || errors;
}
int main(int argc, char **argv) {
  const char *themes[] = {"plain", "default", "gruvbox"};
  int theme, height;
  if (argc < 3 || load())
    return 2;
  if (!strcmp(argv[1], "blank")) {
    for (theme = 0; theme < 3; ++theme)
      for (height = 24; height <= 40; height += 16)
        if (blank_case(argv[2], themes[theme], height))
          return 1;
  } else if (!strcmp(argv[1], "streaming")) {
    for (theme = 0; theme < 3; theme += 2)
      if (streaming_case(argv[2], themes[theme]))
        return 1;
  } else if (!strcmp(argv[1], "width") && argc == 4)
    return width_case(argv[2], argv[3]);
  else
    return 2;
  return errors != 0;
}
