#include "vte_test.h"

struct editor {
  struct screen screen;
  pid_t child;
};
static void stop_editor(struct editor *e) {
  int status;
  if (e->child > 0) {
    kill(e->child, SIGTERM);
    waitpid(e->child, &status, 0);
  }
  v.destroy(e->screen.window);
  pump();
}
static int wait_marker(struct editor *e, const char *marker) {
  double deadline = now() + 3;
  do {
    struct snapshot current = transcript(&e->screen);
    if (strstr(current.text, marker))
      return 0;
    pump();
  } while (now() < deadline);
  fprintf(stderr, "Readline marker missing: %s\n", marker);
  return 1;
}
static int start_editor(struct editor *e, const char *example, int width,
                        const char *prior, const char *source,
                        const char *position) {
  struct winsize size;
  void *pty;
  int master, slave, index;
  e->child = -1;
  e->screen = create();
  resize(&e->screen, width, 8);
  v.feed(e->screen.terminal, prior, (long)strlen(prior));
  pump();
  memset(&size, 0, sizeof(size));
  size.ws_col = (unsigned short)width;
  size.ws_row = 8;
  if (openpty(&master, &slave, NULL, NULL, &size))
    return 1;
  pty = v.pty(master, NULL, NULL);
  if (!pty) {
    close(master);
    close(slave);
    return 1;
  }
  v.set_pty(e->screen.terminal, pty);
  v.unref(pty);
  e->child = fork();
  if (e->child == 0) {
    close(master);
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    if (slave > 2)
      close(slave);
    setenv("SOFTLINE_PROMPT_THEME", "plain", 1);
    execl(example, example, (char *)NULL);
    _exit(127);
  }
  close(slave);
  if (e->child < 0 || wait_marker(e, "softline> "))
    return 1;
  v.send(e->screen.terminal, source, (long)strlen(source));
  v.send(e->screen.terminal, position, (long)strlen(position));
  for (index = 0; index < 10; ++index)
    pump();
  return 0;
}
static int prompt(struct editor *e, struct snapshot *result) {
  struct snapshot body;
  char *begin;
  size_t index;
  body = transcript(&e->screen);
  *result = snapshot(&e->screen, 0);
  strcpy(result->text, body.text);
  begin = strstr(result->text, "softline> ");
  if (!begin)
    return 1;
  for (index = 0; result->text + index < begin; ++index)
    if (result->text[index] == '\n')
      --result->row;
  memmove(result->text, begin, strlen(begin) + 1);
  return 0;
}
static int case_editor(const char *example, const char *source,
                       const char *position, int filled) {
  struct editor actual, reference;
  struct snapshot observed, expected, current;
  char prior[4096], kept[4096], submitted[4096], label[32];
  int count = filled ? 20 : 2, index, width, stage, failed = 1;
  const int widths[] = {20, 36, 18};
  size_t used = 0, keep_used, submit_used, length;
  const char *line, *end, *begin;
  memset(&observed, 0, sizeof(observed));
  memset(&expected, 0, sizeof(expected));
  for (index = 0; index < count; ++index)
    used += (size_t)snprintf(prior + used, sizeof(prior) - used, "keep%03d\r\n",
                             index);
  if (start_editor(&actual, example, 40, prior, source, position))
    goto cleanup;
  for (stage = 0; stage < 3; ++stage) {
    width = widths[stage];
    resize(&actual.screen, width, 8);
    for (index = 0; index < 10; ++index)
      pump();
    current = transcript(&actual.screen);
    keep_used = 0;
    for (line = current.text; *line; line = end + 1) {
      end = strchr(line, '\n');
      if (!end)
        break;
      if (!strncmp(line, "keep", 4)) {
        length = (size_t)(end - line);
        if (keep_used + length + 2 > sizeof(kept))
          goto cleanup;
        memcpy(kept + keep_used, line, length);
        keep_used += length;
        kept[keep_used++] = '\r';
        kept[keep_used++] = '\n';
      }
    }
    if (keep_used != used || memcmp(kept, prior, used)) {
      fprintf(stderr, "Readline overwrote retained output\n%s\n", current.text);
      goto cleanup;
    }
    if (start_editor(&reference, example, width, prior, source, position)) {
      stop_editor(&reference);
      goto cleanup;
    }
    if (prompt(&actual, &observed) || prompt(&reference, &expected) ||
        strcmp(observed.text, expected.text) || observed.row != expected.row ||
        observed.column != expected.column) {
      fprintf(stderr,
              "Resized readline differs from fresh terminal: width=%d "
              "source=%s\n%s\n%s\n",
              width, source, observed.text, expected.text);
      stop_editor(&reference);
      goto cleanup;
    }
    stop_editor(&reference);
  }
  v.send(actual.screen.terminal, "\r", 1);
  if (wait_marker(&actual, "submitted: "))
    goto cleanup;
  /* Wait for the next editable prompt, then concatenate only submission rows.
   */
  for (index = 0; index < 10; ++index)
    pump();
  current = transcript(&actual.screen);
  begin = strstr(current.text, "submitted: ");
  if (!begin)
    goto cleanup;
  submit_used = 0;
  for (line = begin; *line; line = end + 1) {
    if (!strncmp(line, "softline> ", 10))
      break;
    end = strchr(line, '\n');
    if (!end)
      goto cleanup;
    length = (size_t)(end - line);
    if (submit_used + length + 1 > sizeof(submitted))
      goto cleanup;
    memcpy(submitted + submit_used, line, length);
    submit_used += length;
  }
  submitted[submit_used] = 0;
  snprintf(label, sizeof(label), "%s", "submitted: ");
  if (strncmp(submitted, label, strlen(label)) ||
      strcmp(submitted + strlen(label), source)) {
    fprintf(stderr, "Readline draft changed: source=%s\n%s\n", source,
            current.text);
    goto cleanup;
  }
  failed = 0;
cleanup:
  stop_editor(&actual);
  return failed || errors;
}
int main(int argc, char **argv) {
  const char *sources[] = {"one two three four",
                           "alpha beta gamma delta epsilon zeta eta",
                           "café 中文 é words", "abcdefghij"};
  const char *positions[] = {"\001", "\001\033[C\033[C\033[C", ""};
  size_t source, position;
  int filled;
  if (argc != 2 || load())
    return 2;
  for (source = 0; source < 4; ++source)
    for (position = 0; position < 3; ++position)
      for (filled = 0; filled < 2; ++filled)
        if (case_editor(argv[1], sources[source], positions[position], filled))
          return 1;
  return errors != 0;
}
