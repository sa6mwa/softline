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
}

static void vt_scroll(struct vt_screen *screen) {
  int r;
  for (r = 1; r < screen->rows; r++)
    memcpy(screen->cells[r - 1], screen->cells[r], (size_t)screen->cols + 1);
  memset(screen->cells[screen->rows - 1], ' ', (size_t)screen->cols);
  screen->cells[screen->rows - 1][screen->cols] = '\0';
  screen->row = screen->rows - 1;
}

static void vt_lf(struct vt_screen *screen) {
  screen->row++;
  if (screen->row >= screen->rows)
    vt_scroll(screen);
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
  if (wait_for_text(master_fd, terminal, &terminal_len, sizeof(terminal),
                    "pidspifjxjp nextword") != 0) {
    fprintf(stderr, "\n--- simple terminal ---\n%s\n--- end ---\n", terminal);
    FAIL("wrapped prompt text missing");
  }
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
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

static void test_example_chat_reflows_after_resize(const char *path) {
  int master_fd;
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  struct vt_screen screen;
  size_t terminal_len;
  size_t resize_offset;
  const char *input;

  TEST("example_chat reflows after resize");
  pid = spawn_example(path, &master_fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "chat> ") == 0,
              "initial prompt missing");
  input = "hello world sentence\r";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write first message failed");
  if (wait_for_text(master_fd, terminal, &terminal_len, sizeof(terminal),
                    "hello world sentence") != 0) {
    fprintf(stderr, "\n--- chat terminal ---\n%s\n--- end ---\n", terminal);
    FAIL("first message output missing");
  }
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "\033[?2004l",
                                  "\033[?2004h") == 0,
              "next chat readline did not start");
  input = "hello world sentence";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write prompt text failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal),
                            "chat> hello world sentence") == 0,
              "wide prompt render missing");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 16;
  ws.ws_row = 6;
  resize_offset = terminal_len;
  ASSERT_TRUE(ioctl(master_fd, TIOCSWINSZ, &ws) == 0, "resize failed");
  if (wait_for_text(master_fd, terminal, &terminal_len, sizeof(terminal),
                    "      sentence") != 0) {
    fprintf(stderr, "\n--- chat terminal after resize ---\n%s\n--- end ---\n",
            terminal + resize_offset);
    FAIL("resized continuation row missing");
  }
  vt_init(&screen, 6, 16);
  vt_apply(&screen, terminal + resize_offset);
  ASSERT_TRUE(vt_contains(&screen, "chat> hello"),
              "resized first visible prompt row missing");
  ASSERT_TRUE(vt_contains(&screen, "      sentence"),
              "resized continuation visible row missing");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit resized text failed");
  ASSERT_TRUE(wait_for_text_after(master_fd, terminal, &terminal_len,
                                  sizeof(terminal), "      sentence",
                                  "chat> ") == 0,
              "next chat prompt did not follow resized submit");
  ASSERT_TRUE(write(master_fd, "exit\r", 5) == 5, "write exit failed");
  ASSERT_TRUE(finish_child(pid, master_fd) == 0, "child failed");
  PASS();
}

static void test_example_chat_leaves_alt_screen_on_ctrl_c(const char *path) {
  int master_fd;
  pid_t pid;
  char terminal[8192];
  size_t terminal_len;
  int status;

  TEST("example_chat leaves alternate screen on Ctrl-C");
  pid = spawn_example(path, &master_fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  terminal_len = 0;
  terminal[0] = '\0';
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "chat> ") == 0,
              "initial prompt missing");
  ASSERT_TRUE(write(master_fd, "\003", 1) == 1, "write Ctrl-C failed");
  ASSERT_TRUE(wait_for_text(master_fd, terminal, &terminal_len,
                            sizeof(terminal), "\033[?1049l") == 0,
              "alternate screen was not restored");
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  close(master_fd);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 130,
              "chat example did not exit as interrupted");
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
  test_example_chat_reflows_after_resize(argv[2]);
  test_example_chat_leaves_alt_screen_on_ctrl_c(argv[2]);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
