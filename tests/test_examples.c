#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct vt_screen {
  int rows;
  int cols;
  int row;
  int col;
  int scroll_top;
  int scroll_bottom;
  char cells[12][80];
};

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
  do {                                                                         \
    tests_run++;                                                               \
    printf("  %-58s", name);                                                   \
  } while (0)

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
    printf("PASS\n");                                                          \
  } while (0)

#define FAIL(msg)                                                              \
  do {                                                                         \
    printf("FAIL: %s\n", msg);                                                 \
    return;                                                                    \
  } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond))                                                               \
      FAIL(msg);                                                               \
  } while (0)

static int contains_bytes(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != NULL;
}

static int contains_after_bytes(const char *haystack, const char *first,
                                const char *second) {
  const char *p;
  p = strstr(haystack, first);
  if (!p)
    return 0;
  p += strlen(first);
  return strstr(p, second) != NULL;
}

static void vt_clear(struct vt_screen *screen) {
  int r;
  int c;
  for (r = 0; r < screen->rows; r++) {
    for (c = 0; c < screen->cols; c++)
      screen->cells[r][c] = ' ';
    screen->cells[r][screen->cols] = '\0';
  }
  screen->row = 0;
  screen->col = 0;
}

static void vt_init(struct vt_screen *screen, int rows, int cols) {
  screen->rows = rows > 12 ? 12 : rows;
  screen->cols = cols > 79 ? 79 : cols;
  vt_clear(screen);
  screen->scroll_top = 0;
  screen->scroll_bottom = screen->rows - 1;
}

static void vt_scroll(struct vt_screen *screen) {
  int r;
  for (r = screen->scroll_top + 1; r <= screen->scroll_bottom; r++)
    memcpy(screen->cells[r - 1], screen->cells[r], (size_t)screen->cols + 1);
  memset(screen->cells[screen->scroll_bottom], ' ', (size_t)screen->cols);
  screen->cells[screen->scroll_bottom][screen->cols] = '\0';
  screen->row = screen->scroll_bottom;
}

static void vt_lf(struct vt_screen *screen) {
  if (screen->row == screen->scroll_bottom) {
    vt_scroll(screen);
  } else if (screen->row < screen->rows - 1) {
    screen->row++;
  }
}

static void vt_put(struct vt_screen *screen, char ch) {
  if (screen->col >= screen->cols) {
    screen->col = 0;
    vt_lf(screen);
  }
  screen->cells[screen->row][screen->col] = ch;
  screen->col++;
}

static const char *vt_parse_number(const char *p, int *value) {
  int v;
  v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    p++;
  }
  *value = v;
  return p;
}

static const char *vt_csi(struct vt_screen *screen, const char *p) {
  int a;
  int b;
  int have_a;
  if (*p == '?') {
    while (*p && ((*p < '@') || (*p > '~')))
      p++;
    return *p ? p + 1 : p;
  }
  a = 0;
  b = 0;
  have_a = 0;
  if (*p >= '0' && *p <= '9') {
    have_a = 1;
    p = vt_parse_number(p, &a);
  }
  if (*p == ';') {
    p++;
    if (*p >= '0' && *p <= '9')
      p = vt_parse_number(p, &b);
  }
  switch (*p) {
  case 'A':
    screen->row -= have_a && a > 0 ? a : 1;
    if (screen->row < 0)
      screen->row = 0;
    break;
  case 'B':
    screen->row += have_a && a > 0 ? a : 1;
    if (screen->row >= screen->rows)
      screen->row = screen->rows - 1;
    break;
  case 'C':
    screen->col += have_a && a > 0 ? a : 1;
    if (screen->col >= screen->cols)
      screen->col = screen->cols - 1;
    break;
  case 'D':
    screen->col -= have_a && a > 0 ? a : 1;
    if (screen->col < 0)
      screen->col = 0;
    break;
  case 'H':
  case 'f':
    screen->row = have_a && a > 0 ? a - 1 : 0;
    screen->col = b > 0 ? b - 1 : 0;
    if (screen->row < 0)
      screen->row = 0;
    if (screen->row >= screen->rows)
      screen->row = screen->rows - 1;
    if (screen->col < 0)
      screen->col = 0;
    if (screen->col >= screen->cols)
      screen->col = screen->cols - 1;
    break;
  case 'J':
    if (a == 2)
      vt_clear(screen);
    break;
  case 'K':
    memset(screen->cells[screen->row] + screen->col, ' ',
           (size_t)(screen->cols - screen->col));
    break;
  case 'r':
    screen->scroll_top = have_a && a > 0 ? a - 1 : 0;
    screen->scroll_bottom = b > 0 ? b - 1 : screen->rows - 1;
    if (screen->scroll_top < 0)
      screen->scroll_top = 0;
    if (screen->scroll_top >= screen->rows)
      screen->scroll_top = screen->rows - 1;
    if (screen->scroll_bottom < screen->scroll_top)
      screen->scroll_bottom = screen->scroll_top;
    if (screen->scroll_bottom >= screen->rows)
      screen->scroll_bottom = screen->rows - 1;
    screen->row = 0;
    screen->col = 0;
    break;
  default:
    break;
  }
  return *p ? p + 1 : p;
}

static void vt_apply(struct vt_screen *screen, const char *bytes) {
  const char *p;
  p = bytes;
  while (*p) {
    if (*p == '\033' && p[1] == '[') {
      p = vt_csi(screen, p + 2);
      continue;
    }
    if (*p == '\r') {
      screen->col = 0;
    } else if (*p == '\n') {
      vt_lf(screen);
    } else if ((unsigned char)*p >= 32) {
      vt_put(screen, *p);
    }
    p++;
  }
}

static int vt_contains(struct vt_screen *screen, const char *needle) {
  int r;
  for (r = 0; r < screen->rows; r++) {
    if (strstr(screen->cells[r], needle))
      return 1;
  }
  return 0;
}

static ssize_t read_some_with_timeout(int fd, char *buf, size_t cap) {
  fd_set readfds;
  struct timeval tv;
  int ready;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  ready = select(fd + 1, &readfds, NULL, NULL, &tv);
  if (ready <= 0)
    return ready;
  return read(fd, buf, cap);
}

static void append_terminal_bytes(char *terminal, size_t *terminal_len,
                                  size_t terminal_cap, const char *buf,
                                  ssize_t n) {
  if (n <= 0 || *terminal_len >= terminal_cap - 1)
    return;
  if ((size_t)n > terminal_cap - 1 - *terminal_len)
    n = (ssize_t)(terminal_cap - 1 - *terminal_len);
  memcpy(terminal + *terminal_len, buf, (size_t)n);
  *terminal_len += (size_t)n;
  terminal[*terminal_len] = '\0';
}

static int wait_for_text(int fd, char *terminal, size_t *terminal_len,
                         size_t terminal_cap, const char *needle) {
  char buf[512];
  ssize_t n;
  int tries;
  tries = 0;
  while (!contains_bytes(terminal, needle) && tries < 300) {
    n = read_some_with_timeout(fd, buf, sizeof(buf));
    if (n < 0)
      return contains_bytes(terminal, needle) ? 0 : -1;
    if (n == 0) {
      tries++;
      continue;
    }
    append_terminal_bytes(terminal, terminal_len, terminal_cap, buf, n);
    tries++;
  }
  return contains_bytes(terminal, needle) ? 0 : -1;
}

static int wait_for_text_after(int fd, char *terminal, size_t *terminal_len,
                               size_t terminal_cap, const char *first,
                               const char *second) {
  char buf[512];
  ssize_t n;
  int tries;
  tries = 0;
  while (!contains_after_bytes(terminal, first, second) && tries < 300) {
    n = read_some_with_timeout(fd, buf, sizeof(buf));
    if (n < 0)
      return contains_after_bytes(terminal, first, second) ? 0 : -1;
    if (n == 0) {
      tries++;
      continue;
    }
    append_terminal_bytes(terminal, terminal_len, terminal_cap, buf, n);
    tries++;
  }
  return contains_after_bytes(terminal, first, second) ? 0 : -1;
}

static pid_t spawn_example(const char *path, int *master_fd, int cols,
                           int rows) {
  int slave_fd;
  pid_t pid;
  struct winsize ws;
  char *argv[2];
  argv[0] = (char *)path;
  argv[1] = NULL;
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = (unsigned short)cols;
  ws.ws_row = (unsigned short)rows;
  if (openpty(master_fd, &slave_fd, NULL, NULL, &ws) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    close(*master_fd);
    (void)setsid();
    (void)ioctl(slave_fd, TIOCSCTTY, 0);
    (void)dup2(slave_fd, STDIN_FILENO);
    (void)dup2(slave_fd, STDOUT_FILENO);
    (void)dup2(slave_fd, STDERR_FILENO);
    if (slave_fd > STDERR_FILENO)
      close(slave_fd);
    execv(path, argv);
    _exit(127);
  }
  close(slave_fd);
  return pid;
}

static int finish_child(pid_t pid, int master_fd) {
  int status;
  int tries;
  tries = 0;
  do {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      close(master_fd);
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
      return 0;
    }
    usleep(10000);
    tries++;
  } while (tries < 300);
  close(master_fd);
  (void)kill(pid, SIGTERM);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  return -1;
}

static void test_example_simple_wraps_near_bottom(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[16384];
  struct vt_screen screen;
  size_t terminal_len;
  const char *input;
  char buf[512];
  ssize_t n;
  int tries;

  TEST("example_simple wraps near bottom");
  pid = spawn_example(path, &master_fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "softline> ") == 0,
              "initial prompt missing");
  input = "dj jpsidjfpisj sdpijfijfa pidspifjxjp nextword";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (tries < 300) {
    vt_init(&screen, 6, 40);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "softline> dj jpsidjfpisj sdpijfijfa") &&
        vt_contains(&screen, "          pidspifjxjp nextword"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
  if (!vt_contains(&screen, "softline> dj jpsidjfpisj sdpijfijfa") ||
      !vt_contains(&screen, "          pidspifjxjp nextword")) {
    fprintf(stderr, "\n--- simple terminal ---\n%s\n--- end ---\n", terminal);
    FAIL("wrapped prompt text missing");
  }
  ASSERT_TRUE(vt_contains(&screen, "softline> dj jpsidjfpisj sdpijfijfa"),
              "first visible prompt row missing");
  ASSERT_TRUE(vt_contains(&screen, "          pidspifjxjp nextword"),
              "continuation visible prompt row missing");
  ASSERT_TRUE(!vt_contains(&screen, "sdpijfijfa pidsp"),
              "visible word was split across wrap");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "submitted: dj jpsidjfpisj") == 0,
              "submission output missing");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal),
                                  "submitted:", "softline> ") == 0,
              "next prompt did not follow output");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_uses_normal_scrollback(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[16384];
  size_t terminal_len;

  TEST("example_chat uses normal scrollback");
  pid = spawn_example(path, &master_fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[?1049h"),
              "chat entered the alternate screen");
  ASSERT_TRUE(write(master_fd, "hello\r", 6) == 6,
              "write first message failed");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "\033[?2004l",
                                  "[direct] hello\r\n") == 0,
              "direct chat message missing");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "[direct] hello\r\n",
                                  "> ") == 0,
              "next chat prompt did not follow output");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_dispatches_queued_prompts(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[16384];
  size_t terminal_len;

  TEST("example_chat posts direct and queued messages FIFO");
  pid = spawn_example(path, &master_fd, 40, 8);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(write(master_fd, "queued\tcurrent\r", 15) == 15,
              "write queue input failed");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "\033[?2004l",
                                  "[direct] current\r\n[queued] queued\r\n") ==
                  0,
              "queue dispatch output missing or out of order");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "[queued] queued\r\n",
                                  "\033[?2004h") == 0,
              "chat prompt did not resume after queued dispatch");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[?1049h"),
              "chat entered the alternate screen");
  ASSERT_TRUE(!contains_bytes(terminal, "\r\r\n"),
              "chat emitted a doubled terminal carriage return");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_recalls_sent_history(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[16384];
  size_t terminal_len;

  TEST("example_chat recalls sent prompts from history");
  pid = spawn_example(path, &master_fd, 40, 8);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(write(master_fd, "remember\r", 9) == 9,
              "write history source failed");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "\033[?2004l",
                                  "[direct] remember\r\n") == 0,
              "history source message missing");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "[direct] remember\r\n",
                                  "\033[?2004h") == 0,
              "prompt did not resume after history source message");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(write(master_fd, "\033[A\r", 4) == 4,
              "write history recall failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "[direct] remember\r\n") == 0,
              "Up did not recall sent history");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void
test_example_chat_keeps_empty_direct_message_separate(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[16384];
  size_t terminal_len;

  TEST("example_chat separates empty direct and queued replies");
  pid = spawn_example(path, &master_fd, 40, 8);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(write(master_fd, "queued\t\r", 8) == 8,
              "write queue-and-empty-submit input failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal),
                            "\033[?2004l[direct] \r\n[queued] queued\r\n") == 0,
              "queued message missing");
  ASSERT_TRUE(
      contains_bytes(terminal, "\033[?2004l[direct] \r\n[queued] queued\r\n"),
      "empty direct message merged with queued message");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_receives_peer_messages(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[8192];
  size_t terminal_len;

  TEST("example_chat receives timed peer messages");
  pid = spawn_example(path, &master_fd, 40, 8);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "[peer] ") == 0,
              "timed peer message missing");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void
test_example_chat_updates_editor_in_normal_scrollback(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[8192];
  char buf[512];
  size_t terminal_len;
  size_t update_offset;
  ssize_t n;
  int tries;

  TEST("example_chat updates editor in normal scrollback");
  pid = spawn_example(path, &master_fd, 40, 8);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  for (tries = 0; tries < 10; tries++) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n <= 0)
      break;
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  update_offset = terminal_len;
  ASSERT_TRUE(write(master_fd, "hello", 5) == 5, "write input failed");
  tries = 0;
  while (!contains_bytes(terminal + update_offset, "hello") && tries < 100) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  ASSERT_TRUE(contains_bytes(terminal + update_offset, "hello"),
              "editor update missing");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal),
                            "\033[?2004l[direct] hello\r\n") == 0,
              "submitted message missing");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "[direct] hello\r\n",
                                  "\033[?2004h") == 0,
              "next prompt did not start");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_ctrl_c_cancels_and_continues(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[8192];
  size_t terminal_len;

  TEST("example_chat Ctrl-C cancels and continues");
  pid = spawn_example(path, &master_fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(write(master_fd, "\003", 1) == 1, "write Ctrl-C failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "[cancelled]") == 0,
              "cancel acknowledgement missing");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "[cancelled]",
                                  "\033[?2004h") == 0,
              "chat prompt did not resume after Ctrl-C");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_is_plain_without_tty(const char *path) {
  int input_pipe[2];
  int output_pipe[2];
  pid_t pid;
  char output[1024];
  char *argv[2];
  size_t output_len;
  ssize_t n;
  int status;

  TEST("example_chat stays plain without terminals");
  ASSERT_TRUE(pipe(input_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(output_pipe) == 0, "output pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    argv[0] = (char *)path;
    argv[1] = NULL;
    close(input_pipe[1]);
    close(output_pipe[0]);
    (void)dup2(input_pipe[0], STDIN_FILENO);
    (void)dup2(output_pipe[1], STDOUT_FILENO);
    /* Keep diagnostics separate: Valgrind traces exec'd children on stderr. */
    close(input_pipe[0]);
    close(output_pipe[1]);
    execv(path, argv);
    _exit(127);
  }
  close(input_pipe[0]);
  close(output_pipe[1]);
  ASSERT_TRUE(write(input_pipe[1], "hello\nexit\n", 11) == 11,
              "write non-tty input failed");
  close(input_pipe[1]);
  output_len = 0;
  while (output_len < sizeof(output) - 1) {
    n = read(output_pipe[0], output + output_len,
             sizeof(output) - 1 - output_len);
    if (n < 0)
      FAIL("read non-tty output failed");
    if (n == 0)
      break;
    output_len += (size_t)n;
  }
  output[output_len] = '\0';
  close(output_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "non-tty chat child failed");
  ASSERT_TRUE(strcmp(output, "[direct] hello\n") == 0,
              "non-tty chat output mismatch");
  ASSERT_TRUE(!contains_bytes(output, "\033["),
              "non-tty chat emitted terminal control sequences");
  PASS();
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc != 3) {
    fprintf(stderr, "usage: %s EXAMPLE_SIMPLE EXAMPLE_CHAT\n", argv[0]);
    return 2;
  }

  printf("softline example integration tests\n");
  printf("==================================\n\n");

  test_example_simple_wraps_near_bottom(argv[1]);
  test_example_chat_uses_normal_scrollback(argv[2]);
  test_example_chat_dispatches_queued_prompts(argv[2]);
  test_example_chat_recalls_sent_history(argv[2]);
  test_example_chat_keeps_empty_direct_message_separate(argv[2]);
  test_example_chat_receives_peer_messages(argv[2]);
  test_example_chat_updates_editor_in_normal_scrollback(argv[2]);
  test_example_chat_ctrl_c_cancels_and_continues(argv[2]);
  test_example_chat_is_plain_without_tty(argv[2]);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
