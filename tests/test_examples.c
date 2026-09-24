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
#include <time.h>
#include <unistd.h>

enum { MAX_ROWS = 40, MAX_COLS = 140, RAW_CAP = 262144 };
static const char *const observed[] = {
    "A short answer", "Next step",           "A longer answer", "> draft",
    "> queued",       "Operation cancelled", "Q 1. queued"};

struct terminal {
  int fd, rows, cols, row, col, escape;
  char csi[64];
  size_t csi_len, raw_len;
  unsigned int seen;
  char cells[MAX_ROWS][MAX_COLS + 1];
  char raw[RAW_CAP];
  unsigned int scrolls;
  char history[128][MAX_COLS + 1];
};

static int tests_run, tests_passed;
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

static void term_clear(struct terminal *t) {
  int row;
  for (row = 0; row < t->rows; row++) {
    memset(t->cells[row], ' ', (size_t)t->cols);
    t->cells[row][t->cols] = 0;
  }
  t->row = t->col = 0;
}

static void term_init(struct terminal *t, int fd, int cols, int rows) {
  memset(t, 0, sizeof(*t));
  t->fd = fd;
  t->cols = cols;
  t->rows = rows;
  term_clear(t);
}

static void term_scroll(struct terminal *t) {
  int row;
  memcpy(t->history[t->scrolls % 128u], t->cells[0], (size_t)t->cols + 1);
  t->scrolls++;
  for (row = 1; row < t->rows; row++)
    memcpy(t->cells[row - 1], t->cells[row], (size_t)t->cols + 1);
  memset(t->cells[t->rows - 1], ' ', (size_t)t->cols);
  t->cells[t->rows - 1][t->cols] = 0;
  t->row = t->rows - 1;
}

static void term_csi(struct terminal *t, char final) {
  int a, b;
  char *part;
  t->csi[t->csi_len] = 0;
  if (t->csi[0] == '?')
    return;
  a = t->csi_len ? atoi(t->csi) : 0;
  part = strchr(t->csi, ';');
  b = part ? atoi(part + 1) : 0;
  switch (final) {
  case 'H':
  case 'f':
    t->row = a > 0 ? a - 1 : 0;
    t->col = b > 0 ? b - 1 : 0;
    break;
  case 'A':
    t->row -= a > 0 ? a : 1;
    break;
  case 'B':
    t->row += a > 0 ? a : 1;
    break;
  case 'C':
    t->col += a > 0 ? a : 1;
    break;
  case 'D':
    t->col -= a > 0 ? a : 1;
    break;
  case 'J':
    if (a == 2)
      term_clear(t);
    break;
  case 'K':
    memset(t->cells[t->row] + t->col, ' ', (size_t)(t->cols - t->col));
    break;
  default:
    break;
  }
  if (t->row < 0)
    t->row = 0;
  if (t->row >= t->rows)
    t->row = t->rows - 1;
  if (t->col < 0)
    t->col = 0;
  if (t->col >= t->cols)
    t->col = t->cols - 1;
}

static void term_feed(struct terminal *t, const char *bytes, size_t len) {
  size_t i;
  if (len > RAW_CAP - 1 - t->raw_len)
    len = RAW_CAP - 1 - t->raw_len;
  memcpy(t->raw + t->raw_len, bytes, len);
  t->raw_len += len;
  t->raw[t->raw_len] = 0;
  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)bytes[i];
    if (t->escape == 1) {
      t->escape = ch == '[' ? 2 : 0;
      if (t->escape == 2)
        t->csi_len = 0;
      continue;
    }
    if (t->escape == 2) {
      if (ch >= '@' && ch <= '~') {
        term_csi(t, (char)ch);
        t->escape = 0;
      } else if (t->csi_len < sizeof(t->csi) - 1) {
        t->csi[t->csi_len++] = (char)ch;
      }
      continue;
    }
    if (ch == 27)
      t->escape = 1;
    else if (ch == '\r')
      t->col = 0;
    else if (ch == '\n') {
      if (t->row == t->rows - 1)
        term_scroll(t);
      else
        t->row++;
    } else if (ch >= 32) {
      size_t k;
      if (t->col >= t->cols) {
        t->col = 0;
        if (t->row == t->rows - 1)
          term_scroll(t);
        else
          t->row++;
      }
      t->cells[t->row][t->col++] = (char)ch;
      for (k = 0; k < sizeof(observed) / sizeof(observed[0]); k++) {
        if (t->seen & (1u << k))
          continue;
        if (strstr(t->cells[t->row], observed[k]))
          t->seen |= 1u << k;
      }
    }
  }
}

static int term_read(struct terminal *t) {
  fd_set fds;
  struct timeval tv;
  char bytes[2048];
  ssize_t n;
  FD_ZERO(&fds);
  FD_SET(t->fd, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 50000;
  if (select(t->fd + 1, &fds, NULL, NULL, &tv) <= 0)
    return 0;
  n = read(t->fd, bytes, sizeof(bytes));
  if (n < 0 && (errno == EINTR || errno == EAGAIN))
    return 0;
  if (n <= 0) {
    fprintf(stderr, "terminal read ended: n=%ld errno=%d\n", (long)n, errno);
    return -1;
  }
  term_feed(t, bytes, (size_t)n);
  return 1;
}

static int term_contains(const struct terminal *t, const char *needle) {
  int row;
  for (row = 0; row < t->rows; row++)
    if (strstr(t->cells[row], needle))
      return 1;
  return 0;
}

static int term_history_contains(const struct terminal *t, const char *needle) {
  unsigned int i;
  unsigned int count = t->scrolls < 128u ? t->scrolls : 128u;
  for (i = 0; i < count; i++)
    if (strstr(t->history[i], needle))
      return 1;
  return 0;
}

static int term_row_of(const struct terminal *t, const char *needle) {
  int row;
  for (row = 0; row < t->rows; row++)
    if (strstr(t->cells[row], needle))
      return row;
  return -1;
}

static struct timespec deadline_after(int milliseconds) {
  struct timespec deadline;
  (void)clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_sec += milliseconds / 1000;
  deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }
  return deadline;
}

static int before_deadline(const struct timespec *deadline) {
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec < deadline->tv_sec ||
         (now.tv_sec == deadline->tv_sec && now.tv_nsec < deadline->tv_nsec);
}

static int wait_screen(struct terminal *t, const char *needle, int ms) {
  struct timespec deadline = deadline_after(ms);
  size_t k;
  unsigned int bit = 0;
  for (k = 0; k < sizeof(observed) / sizeof(observed[0]); k++)
    if (strcmp(needle, observed[k]) == 0)
      bit = 1u << k;
  while (!term_contains(t, needle) && !(t->seen & bit) &&
         before_deadline(&deadline))
    if (term_read(t) < 0)
      return -1;
  return term_contains(t, needle) || (t->seen & bit) ? 0 : -1;
}

static int wait_raw(struct terminal *t, const char *needle, int ms) {
  struct timespec deadline = deadline_after(ms);
  while (!strstr(t->raw, needle) && before_deadline(&deadline))
    if (term_read(t) < 0)
      return -1;
  return strstr(t->raw, needle) ? 0 : -1;
}

static int wait_raw_since(struct terminal *t, size_t mark, const char *needle,
                          int ms) {
  struct timespec deadline = deadline_after(ms);
  while (mark <= t->raw_len && !strstr(t->raw + mark, needle) &&
         before_deadline(&deadline))
    if (term_read(t) < 0)
      return -1;
  return mark <= t->raw_len && strstr(t->raw + mark, needle) ? 0 : -1;
}

static int wait_raw_after(struct terminal *t, const char *first,
                          const char *second, int ms) {
  struct timespec deadline = deadline_after(ms);
  const char *at;
  while (before_deadline(&deadline)) {
    at = strstr(t->raw, first);
    if (at && strstr(at + strlen(first), second))
      return 0;
    if (term_read(t) < 0)
      return -1;
  }
  return -1;
}

static void term_dump(const struct terminal *t) {
  int row;
  size_t i;
  const char *failure;
  fprintf(stderr, "\nterminal snapshot (%lu bytes):\n",
          (unsigned long)t->raw_len);
  for (row = 0; row < t->rows; row++)
    fprintf(stderr, "%2d |%s|\n", row, t->cells[row]);
  failure = strstr(t->raw, "chat ");
  if (failure)
    fprintf(stderr, "chat diagnostic: %.200s\n", failure);
  fprintf(stderr, "tail: ");
  for (i = t->raw_len > 240 ? t->raw_len - 240 : 0; i < t->raw_len; i++) {
    unsigned char ch = (unsigned char)t->raw[i];
    fputc(ch >= 32 && ch < 127 ? ch : '.', stderr);
  }
  fputc('\n', stderr);
}

static pid_t spawn(const char *path, int *fd, int cols, int rows) {
  struct winsize ws;
  int slave;
  pid_t pid;
  char *args[2];
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = (unsigned short)cols;
  ws.ws_row = (unsigned short)rows;
  if (openpty(fd, &slave, NULL, NULL, &ws) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    close(*fd);
    (void)setsid();
    (void)ioctl(slave, TIOCSCTTY, 0);
    (void)dup2(slave, STDIN_FILENO);
    (void)dup2(slave, STDOUT_FILENO);
    (void)dup2(slave, STDERR_FILENO);
    if (slave > STDERR_FILENO)
      close(slave);
    args[0] = (char *)path;
    args[1] = NULL;
    execv(path, args);
    _exit(127);
  }
  close(slave);
  return pid;
}

static int finish(pid_t pid, int fd) {
  int status, i;
  char tail[1024];
  size_t tail_len = 0;
  struct timespec deadline = deadline_after(10000);
  while (before_deadline(&deadline)) {
    fd_set fds;
    struct timeval tv;
    char discard[2048];
    int ready;
    if (waitpid(pid, &status, WNOHANG) == pid) {
      close(fd);
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
      fprintf(stderr, "example child exited with status=%d\n", status);
      fwrite(tail, 1, tail_len, stderr);
      fputc('\n', stderr);
      return -1;
    }
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    ready = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ready > 0) {
      ssize_t amount = read(fd, discard, sizeof(discard));
      if (amount > 0) {
        size_t keep = (size_t)amount;
        if (keep > sizeof(tail)) {
          memcpy(tail, discard + keep - sizeof(tail), sizeof(tail));
          tail_len = sizeof(tail);
        } else {
          if (tail_len + keep > sizeof(tail)) {
            size_t shift = tail_len + keep - sizeof(tail);
            memmove(tail, tail + shift, tail_len - shift);
            tail_len -= shift;
          }
          memcpy(tail + tail_len, discard, keep);
          tail_len += keep;
        }
      } else {
        usleep(10000);
      }
    }
  }
  (void)kill(-pid, SIGTERM);
  fprintf(stderr, "example child did not exit before timeout\n");
  for (i = 0; i < (int)tail_len; i++) {
    unsigned char ch = (unsigned char)tail[i];
    fputc(ch >= 32 && ch < 127 ? ch : '.', stderr);
  }
  fputc('\n', stderr);
  (void)kill(pid, SIGTERM);
  for (i = 0; i < 100; i++) {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      close(fd);
      return -1;
    }
    usleep(10000);
  }
  (void)kill(-pid, SIGKILL);
  (void)kill(pid, SIGKILL);
  (void)waitpid(pid, &status, 0);
  close(fd);
  return -1;
}

static int cancel_and_exit(struct terminal *t, int fd, pid_t pid) {
  size_t mark = t->raw_len;
  if (write(fd, "\003", 1) != 1 ||
      wait_raw_since(t, mark, "\033[?2004h", 3000) != 0 ||
      write(fd, "exit\r", 5) != 5) {
    term_dump(t);
    return -1;
  }
  return finish(pid, fd);
}

static void test_simple(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  TEST("simple example accepts wrapped input");
  pid = spawn(path, &fd, 40, 6);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 40, 6);
  ASSERT_TRUE(wait_screen(&t, "softline> ", 3000) == 0, "prompt missing");
  ASSERT_TRUE(write(fd, "a long example input nextword", 29) == 29,
              "input failed");
  ASSERT_TRUE(wait_screen(&t, "nextword", 3000) == 0, "editing failed");
  ASSERT_TRUE(write(fd, "\r", 1) == 1, "submit failed");
  ASSERT_TRUE(wait_raw(&t, "submitted: a long", 3000) == 0,
              "submission missing");
  ASSERT_TRUE(wait_raw_after(&t, "submitted: a long", "\033[?2004h", 3000) == 0,
              "next prompt missing");
  ASSERT_TRUE(write(fd, "exit\r", 5) == 5, "exit failed");
  ASSERT_TRUE(finish(pid, fd) == 0, "child failed");
  PASS();
}

static void test_chat_live_queue(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  TEST("chat streams Markdown while editing and dispatches FIFO");
  pid = spawn(path, &fd, 80, 14);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 80, 14);
  if (wait_raw(&t, "\033[?2004h", 4000) != 0) {
    int status = 0;
    term_dump(&t);
    fprintf(stderr, "child status: waitpid=%ld status=%d\n",
            (long)waitpid(pid, &status, WNOHANG), status);
    FAIL("editor missing");
  }
  ASSERT_TRUE(strstr(t.raw, "\033[?1049h") == NULL, "alternate screen used");
  ASSERT_TRUE(write(fd, "first\r", 6) == 6, "first send failed");
  ASSERT_TRUE(wait_screen(&t, "> first", 3000) == 0, "quote missing");
  ASSERT_TRUE(wait_screen(&t, "! Thinking...", 3000) == 0,
              "status message missing");
  ASSERT_TRUE(wait_screen(&t, "streaming demo", 3000) == 0,
              "status line missing while thinking");
  ASSERT_TRUE(term_row_of(&t, "Thinking...") <
                  term_row_of(&t, "streaming demo"),
              "status message is not above the status line");
  ASSERT_TRUE(strstr(t.raw, "\033[3;") != NULL,
              "italic status or prompt treatment missing");
  ASSERT_TRUE(wait_screen(&t, "A short answer", 3000) == 0,
              "heading not streamed");
  ASSERT_TRUE(strstr(t.raw, "\033[0;3;96mfirst") != NULL,
              "quoted prompt did not use its italic accent style");
  ASSERT_TRUE(wait_screen(&t, "! Reasoning...", 3000) == 0,
              "status message did not update mid-stream");
  ASSERT_TRUE(write(fd, "draft", 5) == 5, "draft failed");
  ASSERT_TRUE(wait_screen(&t, "> draft", 3000) == 0,
              "editing blocked during stream");
  ASSERT_TRUE(write(fd, "\rsecond\r", 8) == 8, "queue input failed");
  ASSERT_TRUE(wait_screen(&t, "Q 1. draft", 3000) == 0,
              "first queue item invisible");
  ASSERT_TRUE(wait_screen(&t, "2. second", 3000) == 0,
              "second queue item invisible");
  t.seen &= ~(1u << 3);
  if (wait_screen(&t, "Next step", 6000) != 0) {
    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    fprintf(stderr, "chat child wait=%ld status=%d\n", (long)waited, status);
    term_dump(&t);
    FAIL("stream stopped before completion");
  }
  ASSERT_TRUE(wait_screen(&t, "> draft", 6000) == 0,
              "first queued turn not dispatched");
  ASSERT_TRUE(wait_screen(&t, "A longer answer", 6000) == 0,
              "queued turn did not start next operation");
  {
    struct timespec history_deadline = deadline_after(6000);
    while (!term_history_contains(&t, "> first") &&
           before_deadline(&history_deadline))
      if (term_read(&t) < 0)
        break;
  }
  if (!(t.scrolls > 0 && term_history_contains(&t, "> first"))) {
    unsigned int hi;
    fprintf(stderr, "native scrolls: %u\n", t.scrolls);
    for (hi = 0; hi < t.scrolls && hi < 128u; hi++)
      fprintf(stderr, "history %u: |%s|\n", hi, t.history[hi]);
    term_dump(&t);
    FAIL("first prompt was not preserved in terminal scrollback");
  }
  {
    const char *sync_start = strstr(t.raw, "\033[?2026h");
    const char *sync_end =
        sync_start ? strstr(sync_start, "\033[?2026l") : NULL;
    const char *top_repaint =
        sync_start ? strstr(sync_start, "\033[1;1H") : NULL;
    ASSERT_TRUE(sync_start && sync_end &&
                    (!top_repaint || top_repaint > sync_end),
                "native scroll repainted the output viewport");
  }
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_queued_exit(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  TEST("queued exit ends chat after the active response");
  pid = spawn(path, &fd, 80, 14);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 80, 14);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(write(fd, "first\r", 6) == 6, "first send failed");
  ASSERT_TRUE(wait_screen(&t, "! Thinking...", 3000) == 0,
              "response did not start");
  ASSERT_TRUE(write(fd, "exit\r", 5) == 5, "queued exit failed");
  ASSERT_TRUE(wait_screen(&t, "Q 1. exit", 3000) == 0,
              "exit was not queued while busy");
  ASSERT_TRUE(finish(pid, fd) == 0, "queued exit did not terminate chat");
  PASS();
}

static void
test_chat_preserves_transcript_and_prompt_spacing(const char *path) {
  int fd;
  int note_row;
  int quote_row;
  int answer_row;
  pid_t pid;
  size_t output_mark;
  struct terminal t;
  TEST("chat keeps visible transcript and blank rows around rendered prompt");
  pid = spawn(path, &fd, 120, 30);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 120, 30);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(term_contains(&t, "Enter sends or queues"),
              "introductory transcript missing");
  output_mark = t.raw_len;
  ASSERT_TRUE(write(fd, "hello\r", 6) == 6, "send failed");
  ASSERT_TRUE(wait_screen(&t, "A short answer", 3000) == 0,
              "response heading missing");
  note_row = term_row_of(&t, "Enter sends or queues");
  quote_row = term_row_of(&t, "> hello");
  answer_row = term_row_of(&t, "A short answer");
  ASSERT_TRUE(strstr(t.raw + output_mark, "\033[1;1H") == NULL,
              "submission repainted the full terminal viewport");
  ASSERT_TRUE(note_row >= 0 && quote_row == note_row + 2 &&
                  answer_row == quote_row + 2,
              "visible transcript or prompt spacing was lost");
  ASSERT_TRUE(t.cells[quote_row][0] == '>' && t.cells[quote_row][1] == ' ',
              "quoted prompt prefix is indented");
  ASSERT_TRUE(quote_row > 0 &&
                  strspn(t.cells[quote_row - 1], " ") == (size_t)t.cols &&
                  strspn(t.cells[quote_row + 1], " ") == (size_t)t.cols,
              "rendered prompt lacks a blank row on each side");
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_spacing_across_turns(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  TEST("chat keeps one empty row across consecutive response turns");
  ASSERT_TRUE(setenv("SOFTLINE_CHAT_CHAR_MS", "0", 1) == 0,
              "failed to set fast stream delay");
  pid = spawn(path, &fd, 120, 40);
  ASSERT_TRUE(setenv("SOFTLINE_CHAT_CHAR_MS", "20", 1) == 0,
              "failed to restore stream delay");
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 120, 40);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(write(fd, "one\r", 4) == 4, "first send failed");
  ASSERT_TRUE(wait_screen(&t, "Try another prompt.", 3000) == 0,
              "first response missing");
  ASSERT_TRUE(write(fd, "two\r", 4) == 4, "second send failed");
  ASSERT_TRUE(wait_screen(&t, "The stream is still live.", 3000) == 0,
              "second response missing");
  ASSERT_TRUE(write(fd, "three\r", 6) == 6, "third send failed");
  ASSERT_TRUE(wait_screen(&t, "A single line can be", 3000) == 0,
              "third response missing");
  ASSERT_TRUE(term_row_of(&t, "# A short answer") ==
                      term_row_of(&t, "> one") + 2 &&
                  term_row_of(&t, "> two") ==
                      term_row_of(&t, "Try another prompt.") + 2 &&
                  term_row_of(&t, "## A longer answer") ==
                      term_row_of(&t, "> two") + 2 &&
                  term_row_of(&t, "> three") ==
                      term_row_of(&t, "The stream is still live.") + 2 &&
                  term_row_of(&t, "# Notes") == term_row_of(&t, "> three") + 2,
              "consecutive turns have extra blank rows");
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_cancel(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  TEST("chat cancel holds queue until manual promotion");
  pid = spawn(path, &fd, 80, 14);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 80, 14);
  if (wait_raw(&t, "\033[?2004h", 4000) != 0) {
    int status = 0;
    term_dump(&t);
    fprintf(stderr, "child status: waitpid=%ld status=%d\n",
            (long)waitpid(pid, &status, WNOHANG), status);
    FAIL("editor missing");
  }
  ASSERT_TRUE(write(fd, "work\r", 5) == 5, "send failed");
  if (wait_screen(&t, "A short answer", 3000) != 0) {
    term_dump(&t);
    FAIL("operation missing");
  }
  ASSERT_TRUE(write(fd, "queued\r", 7) == 7, "enqueue failed");
  if (wait_screen(&t, "Q 1. queued", 3000) != 0) {
    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    fprintf(stderr, "cancel chat child wait=%ld status=%d\n", (long)waited,
            status);
    term_dump(&t);
    FAIL("queue item invisible");
  }
  t.seen &= ~(1u << 6);
  ASSERT_TRUE(write(fd, "\003", 1) == 1, "cancel failed");
  ASSERT_TRUE(wait_screen(&t, "Operation cancelled", 3000) == 0,
              "operation not cancelled");
  ASSERT_TRUE(term_row_of(&t, "Reasoning...") < 0,
              "status message was not cleared after cancellation");
  if (wait_screen(&t, "Q 1. queued", 3000) != 0) {
    term_dump(&t);
    FAIL("cancel unexpectedly dequeued turn");
  }
  ASSERT_TRUE(write(fd, "\033\r", 2) == 2, "promotion failed");
  ASSERT_TRUE(wait_screen(&t, "> queued", 3000) == 0,
              "manual promotion missing");
  ASSERT_TRUE(wait_screen(&t, "A longer answer", 3000) == 0,
              "promoted operation did not start");
  {
    size_t mark = t.raw_len;
    ASSERT_TRUE(wait_raw_since(&t, mark, "\033[32m+ ", 6000) == 0,
                "promoted operation did not finish");
  }
  ASSERT_TRUE(write(fd, "exit\r", 5) == 5, "exit failed");
  ASSERT_TRUE(finish(pid, fd) == 0, "child failed");
  PASS();
}

static void test_chat_resizes_while_streaming(const char *path) {
  struct winsize ws;
  struct terminal t;
  int fd;
  pid_t pid;
  TEST("chat resizes renderer and editor during a live stream");
  pid = spawn(path, &fd, 80, 14);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 80, 14);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(write(fd, "resize\r", 7) == 7, "send failed");
  ASSERT_TRUE(wait_screen(&t, "A short answer", 3000) == 0,
              "stream did not start");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 50;
  ws.ws_row = 12;
  ASSERT_TRUE(ioctl(fd, TIOCSWINSZ, &ws) == 0, "resize failed");
  t.cols = 50;
  t.rows = 12;
  term_clear(&t);
  ASSERT_TRUE(write(fd, "draft", 5) == 5, "draft failed");
  ASSERT_TRUE(wait_screen(&t, "> draft", 3000) == 0,
              "draft lost after midstream resize");
  ASSERT_TRUE(wait_screen(&t, "Next step", 6000) == 0,
              "Markdown stopped after midstream resize");
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_queued_steer(const char *path) {
  struct terminal t;
  int fd;
  pid_t pid;
  int first_heading_row;
  int paragraph_row;
  int steer_row;
  int heading_row;
  int valid_seam;
  TEST("chat queues Alt-Enter and inserts steer at a Markdown seam");
  pid = spawn(path, &fd, 120, 30);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 120, 30);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(write(fd, "work\r", 5) == 5, "initial send failed");
  ASSERT_TRUE(wait_screen(&t, "A short answer", 3000) == 0,
              "operation did not start");
  ASSERT_TRUE(write(fd, "queued\rsteer\033\r", 14) == 14,
              "queue and steer input failed");
  ASSERT_TRUE(wait_screen(&t, "S 2. steer", 3000) == 0,
              "steer mode was not shown in the queue");
  ASSERT_TRUE(wait_screen(&t, "> steer", 5000) == 0,
              "steer was not delivered at a seam");
  {
    struct timespec queue_deadline = deadline_after(1500);
    while (!term_contains(&t, "Q 1. queued") &&
           before_deadline(&queue_deadline))
      if (term_read(&t) < 0)
        break;
  }
  if (!term_contains(&t, "Q 1. queued"))
    term_dump(&t);
  ASSERT_TRUE(term_contains(&t, "Q 1. queued"),
              "ordinary queued turn was consumed at the steer seam");
  ASSERT_TRUE(wait_screen(&t, "Next step", 6000) == 0,
              "response did not continue after steer");
  paragraph_row = term_row_of(&t, "Here is italic context");
  steer_row = term_row_of(&t, "> steer");
  heading_row = term_row_of(&t, "Next step");
  first_heading_row = term_row_of(&t, "A short answer");
  valid_seam =
      first_heading_row >= 0 && paragraph_row >= 0 && steer_row >= 0 &&
      heading_row >= 0 &&
      ((steer_row == paragraph_row + 2 && heading_row == steer_row + 2) ||
       (steer_row == first_heading_row + 2 && paragraph_row == steer_row + 2 &&
        heading_row == paragraph_row + 2));
  if (!valid_seam)
    term_dump(&t);
  ASSERT_TRUE(valid_seam,
              "steer split a Markdown paragraph or lacked blank rows");
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_steer_after_paragraph(const char *path) {
  int fd;
  pid_t pid;
  struct terminal t;
  int paragraph_row;
  int steer_row;
  int heading_row;
  TEST("chat inserts a late steer with one empty row at the paragraph seam");
  pid = spawn(path, &fd, 120, 30);
  ASSERT_TRUE(pid > 0, "spawn failed");
  term_init(&t, fd, 120, 30);
  ASSERT_TRUE(wait_raw(&t, "\033[?2004h", 4000) == 0, "editor missing");
  ASSERT_TRUE(write(fd, "work\r", 5) == 5, "initial send failed");
  ASSERT_TRUE(wait_screen(&t, "A short answer", 3000) == 0,
              "operation did not start");
  usleep(400000);
  ASSERT_TRUE(write(fd, "late\033\r", 6) == 6, "late steer input failed");
  ASSERT_TRUE(wait_screen(&t, "S 1. late", 3000) == 0,
              "late steer was not queued");
  ASSERT_TRUE(wait_screen(&t, "> late", 5000) == 0,
              "late steer was not delivered");
  ASSERT_TRUE(wait_screen(&t, "Next step", 6000) == 0,
              "response did not continue after late steer");
  paragraph_row = term_row_of(&t, "Here is italic context");
  steer_row = term_row_of(&t, "> late");
  heading_row = term_row_of(&t, "Next step");
  if (paragraph_row < 0 || steer_row != paragraph_row + 2 ||
      heading_row != steer_row + 2)
    term_dump(&t);
  ASSERT_TRUE(paragraph_row >= 0 && steer_row == paragraph_row + 2 &&
                  heading_row == steer_row + 2,
              "late steer has extra blank rows or split the paragraph");
  ASSERT_TRUE(cancel_and_exit(&t, fd, pid) == 0, "child failed");
  PASS();
}

static void test_chat_non_tty(const char *path) {
  int input[2], output[2], status;
  pid_t pid;
  char bytes[2048], *args[2];
  size_t used;
  ssize_t n;
  TEST("chat emits plain output when redirected");
  ASSERT_TRUE(pipe(input) == 0 && pipe(output) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    close(input[1]);
    close(output[0]);
    (void)dup2(input[0], STDIN_FILENO);
    (void)dup2(output[1], STDOUT_FILENO);
    close(input[0]);
    close(output[1]);
    args[0] = (char *)path;
    args[1] = NULL;
    execv(path, args);
    _exit(127);
  }
  close(input[0]);
  close(output[1]);
  ASSERT_TRUE(write(input[1], "hello\nok\n**literal** # heading\nexit\n", 36) ==
                  36,
              "input failed");
  close(input[1]);
  used = 0;
  while (used < sizeof(bytes) - 1) {
    n = read(output[0], bytes + used, sizeof(bytes) - 1 - used);
    if (n <= 0)
      break;
    used += (size_t)n;
  }
  bytes[used] = 0;
  close(output[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                  WEXITSTATUS(status) == 0,
              "child failed");
  ASSERT_TRUE(strstr(bytes, "> hello") != NULL, "quote missing");
  ASSERT_TRUE(
      strcmp(bytes, "\n\n> hello\n\n> ok\n\n> **literal** # heading\n\n") == 0,
      "renderer output has more or fewer than one empty prompt row");
  ASSERT_TRUE(strstr(bytes, "\033[") == NULL, "terminal controls in pipe");
  PASS();
}

static void test_chat_piped_input_terminal_output(const char *path) {
  int input[2], master_fd, slave_fd;
  pid_t pid;
  char bytes[2048], *args[2];
  size_t used;
  struct timespec deadline;

  TEST("chat with piped input keeps terminal output line-oriented");
  ASSERT_TRUE(pipe(input) == 0 &&
                  openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "pipe or pty failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    close(input[1]);
    close(master_fd);
    (void)dup2(input[0], STDIN_FILENO);
    (void)dup2(slave_fd, STDOUT_FILENO);
    close(input[0]);
    close(slave_fd);
    args[0] = (char *)path;
    args[1] = NULL;
    execv(path, args);
    _exit(127);
  }
  close(input[0]);
  close(slave_fd);
  ASSERT_TRUE(write(input[1], "hello\nexit\n", 11) == 11, "input failed");
  close(input[1]);
  used = 0;
  deadline = deadline_after(3000);
  while (before_deadline(&deadline) && used < sizeof(bytes) - 1) {
    fd_set readfds;
    struct timeval timeout;
    ssize_t amount;
    FD_ZERO(&readfds);
    FD_SET(master_fd, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;
    if (select(master_fd + 1, &readfds, NULL, NULL, &timeout) <= 0)
      continue;
    amount = read(master_fd, bytes + used, sizeof(bytes) - 1 - used);
    if (amount <= 0)
      break;
    used += (size_t)amount;
  }
  bytes[used] = '\0';
  ASSERT_TRUE(finish(pid, master_fd) == 0, "child failed");
  ASSERT_TRUE(strstr(bytes, "> hello") != NULL, "quote missing");
  ASSERT_TRUE(strstr(bytes, "\033[") == NULL,
              "piped chat input activated terminal viewport");
  PASS();
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc != 3)
    return 2;
  if (setenv("SOFTLINE_CHAT_CHAR_MS", "20", 1) != 0)
    return 1;
  printf("softline example integration tests\n");
  test_simple(argv[1]);
  test_chat_live_queue(argv[2]);
  test_chat_queued_exit(argv[2]);
  test_chat_preserves_transcript_and_prompt_spacing(argv[2]);
  test_chat_spacing_across_turns(argv[2]);
  test_chat_cancel(argv[2]);
  test_chat_resizes_while_streaming(argv[2]);
  test_chat_queued_steer(argv[2]);
  test_chat_steer_after_paragraph(argv[2]);
  test_chat_non_tty(argv[2]);
  test_chat_piped_input_terminal_output(argv[2]);
  printf("%d/%d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
