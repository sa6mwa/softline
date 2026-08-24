#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "softline/softline.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__linux__)
#define SL_TEST_PTY 1
#include <pty.h>
#else
#define SL_TEST_PTY 0
#endif

static int tests_run = 0;
static int tests_passed = 0;
static volatile sig_atomic_t signal_seen = 0;
static volatile sig_atomic_t winch_seen = 0;
static int signal_ack_fd = -1;

static void remember_signal(int signo) {
  unsigned char byte;
  (void)signo;
  signal_seen = 1;
  if (signal_ack_fd >= 0) {
    byte = 's';
    (void)write(signal_ack_fd, &byte, 1);
  }
}

static void ignore_signal(int signo) { (void)signo; }

static void remember_winch(int signo) {
  (void)signo;
  winch_seen = 1;
}

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

static int one_chunk_stream(sl_t *sl, void *userdata, const char **chunk,
                            size_t *len) {
  const char *text;
  (void)sl;
  text = (const char *)userdata;
  if (!text) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  *chunk = text;
  *len = strlen(text);
  return SL_OK;
}

struct one_chunk_once {
  const char *text;
  int sent;
};

static int one_chunk_once_stream(sl_t *sl, void *userdata, const char **chunk,
                                 size_t *len) {
  struct one_chunk_once *stream;
  (void)sl;
  stream = (struct one_chunk_once *)userdata;
  if (!stream || stream->sent) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  stream->sent = 1;
  *chunk = stream->text;
  *len = strlen(stream->text);
  return SL_OK;
}

static int failing_stream(sl_t *sl, void *userdata, const char **chunk,
                          size_t *len) {
  (void)sl;
  (void)userdata;
  *chunk = NULL;
  *len = 0;
  return SL_ERROR;
}

static int invalid_chunk_stream(sl_t *sl, void *userdata, const char **chunk,
                                size_t *len) {
  (void)sl;
  (void)userdata;
  *chunk = NULL;
  *len = 1;
  return SL_OK;
}

static int empty_stream(sl_t *sl, void *userdata, const char **chunk,
                        size_t *len) {
  (void)sl;
  (void)userdata;
  *chunk = NULL;
  *len = 0;
  return SL_OK;
}

static void test_config_init(void) {
  sl_config_t cfg;

  TEST("config_init sets documented defaults");
  memset(&cfg, 0x7f, sizeof(cfg));
  sl_config_init(&cfg);
  ASSERT_TRUE(cfg.input_fd == STDIN_FILENO, "input fd default");
  ASSERT_TRUE(cfg.output_fd == STDOUT_FILENO, "output fd default");
  ASSERT_TRUE(cfg.screen_x == 0, "screen x default");
  ASSERT_TRUE(cfg.screen_y == 0, "screen y default");
  ASSERT_TRUE(cfg.screen_width == 0, "screen width default");
  ASSERT_TRUE(cfg.screen_height == 0, "screen height default");
  ASSERT_TRUE(cfg.bounded == 0, "bounded default");
  ASSERT_TRUE(cfg.live_scroll_region == 0, "live scroll region default");
  ASSERT_TRUE(cfg.history_max_len == 100, "history max default");
  ASSERT_TRUE(cfg.line_max_len == 4096, "line max default");
  ASSERT_TRUE(cfg.prompt_queue == 0, "prompt queue default");
  ASSERT_TRUE(cfg.prompt_queue_max_entries == 64, "prompt queue max default");
  ASSERT_TRUE(cfg.prompt_queue_preview_entries == 3,
              "prompt queue preview default");
  ASSERT_TRUE(cfg.prompt_theme == SL_PROMPT_THEME_DEFAULT,
              "prompt theme default");
  ASSERT_TRUE(cfg.statusline == 0, "status line default");
  ASSERT_TRUE(cfg.statusline_start_element == 0,
              "status element start default");
  ASSERT_TRUE(cfg.status_spinner == 0, "status spinner default");
  ASSERT_TRUE(cfg.status_busy == 0, "status busy default");
  ASSERT_TRUE(cfg.status_idle_marker == '+', "status idle marker default");
  PASS();
}

static void test_plain_config_init(sl_config_t *config) {
  sl_config_init(config);
  config->prompt_theme = SL_PROMPT_THEME_PLAIN;
}

#define sl_config_init test_plain_config_init

static void test_receiver_shell(void) {
  sl_t *sl;

  TEST("constructor returns receiver shell");
  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->readline != NULL, "readline method missing");
  ASSERT_TRUE(sl->next_prompt != NULL, "next_prompt method missing");
  ASSERT_TRUE(sl->destroy != NULL, "destroy method missing");
  ASSERT_TRUE(sl->free_string != NULL, "free_string method missing");
  ASSERT_TRUE(sl->history_add != NULL, "history_add method missing");
  ASSERT_TRUE(sl->history_set_max_len != NULL,
              "history_set_max_len method missing");
  ASSERT_TRUE(sl->history_save != NULL, "history_save method missing");
  ASSERT_TRUE(sl->history_load != NULL, "history_load method missing");
  ASSERT_TRUE(sl->set_bounds != NULL, "set_bounds method missing");
  ASSERT_TRUE(sl->set_screen_width != NULL, "set_screen_width method missing");
  ASSERT_TRUE(sl->set_live_scroll_region != NULL,
              "set_live_scroll_region method missing");
  ASSERT_TRUE(sl->set_prompt_queue != NULL, "set_prompt_queue method missing");
  ASSERT_TRUE(sl->set_prompt_theme != NULL, "set_prompt_theme method missing");
  ASSERT_TRUE(sl->set_statusline != NULL, "set_statusline method missing");
  ASSERT_TRUE(sl->set_status_elements != NULL,
              "set_status_elements method missing");
  ASSERT_TRUE(sl->set_status_element != NULL,
              "set_status_element method missing");
  ASSERT_TRUE(sl->set_status_busy != NULL, "set_status_busy method missing");
  ASSERT_TRUE(sl->set_status_spinner != NULL,
              "set_status_spinner method missing");
  ASSERT_TRUE(sl->set_status_idle_marker != NULL,
              "set_status_idle_marker method missing");
  ASSERT_TRUE(sl->set_idle_callback != NULL,
              "set_idle_callback method missing");
  ASSERT_TRUE(sl->bind_key != NULL, "bind_key method missing");
  ASSERT_TRUE(sl->insert != NULL, "insert method missing");
  ASSERT_TRUE(sl->set_buffer != NULL, "set_buffer method missing");
  ASSERT_TRUE(sl->buffer != NULL, "buffer method missing");
  ASSERT_TRUE(sl->cursor != NULL, "cursor method missing");
  ASSERT_TRUE(sl->set_cursor != NULL, "set_cursor method missing");
  ASSERT_TRUE(sl->submit != NULL, "submit method missing");
  ASSERT_TRUE(sl->cancel != NULL, "cancel method missing");
  ASSERT_TRUE(sl->print_above != NULL, "print_above method missing");
  ASSERT_TRUE(sl->last_readline_status != NULL,
              "last_readline_status method missing");
  ASSERT_TRUE(sl->last_error != NULL, "last_error method missing");
  ASSERT_TRUE(sl->impl != NULL, "impl missing");
  ASSERT_TRUE(sl->last_readline_status(sl) == SL_READLINE_NONE,
              "initial readline status mismatch");
  sl->destroy(sl);
  PASS();
}

static void test_free_function_wrappers_use_receiver_methods(void) {
  sl_t *sl;

  TEST("free-function wrappers use receiver methods");
  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl_history_set_max_len(sl, 2) == SL_OK,
              "wrapper history max failed");
  ASSERT_TRUE(sl_history_add(sl, "alpha") == SL_OK,
              "wrapper history add failed");
  ASSERT_TRUE(sl_set_screen_width(sl, 12) == SL_OK,
              "wrapper screen width failed");
  ASSERT_TRUE(sl_set_bounds(sl, 1, 2, 12, 4) == SL_OK, "wrapper bounds failed");
  ASSERT_TRUE(sl_set_live_scroll_region(sl, 1) == SL_OK,
              "wrapper live scroll region failed");
  ASSERT_TRUE(sl_set_prompt_queue(sl, 1, 4, 2) == SL_OK,
              "wrapper prompt queue failed");
  ASSERT_TRUE(sl_set_prompt_theme(sl, SL_PROMPT_THEME_ACCENT) == SL_OK,
              "wrapper prompt theme failed");
  ASSERT_TRUE(sl_set_statusline(sl, 1, 15) == SL_OK,
              "wrapper status line failed");
  ASSERT_TRUE(sl_set_status_element(sl, 0, "model") == SL_OK,
              "wrapper status element failed");
  ASSERT_TRUE(sl_set_status_busy(sl, 1) == SL_OK, "wrapper status busy failed");
  ASSERT_TRUE(sl_set_status_spinner(sl, 1) == SL_OK,
              "wrapper status spinner failed");
  ASSERT_TRUE(sl_set_status_idle_marker(sl, '-') == SL_OK,
              "wrapper status idle marker failed");
  ASSERT_TRUE(sl_set_status_idle_marker(sl, '\0') == SL_OK,
              "wrapper clear status idle marker failed");
  ASSERT_TRUE(sl_set_buffer(sl, "draft") == SL_OK, "wrapper set_buffer failed");
  ASSERT_TRUE(strcmp(sl_buffer(sl), "draft") == 0, "wrapper buffer mismatch");
  ASSERT_TRUE(sl_set_cursor(sl, 2) == SL_OK, "wrapper set_cursor failed");
  ASSERT_TRUE(sl_cursor(sl) == 2, "wrapper cursor mismatch");
  ASSERT_TRUE(sl_insert(sl, "X") == SL_OK, "wrapper insert failed");
  ASSERT_TRUE(strcmp(sl_buffer(sl), "drXaft") == 0,
              "wrapper insert did not mutate receiver");
  ASSERT_TRUE(sl_last_readline_status(sl) == SL_READLINE_NONE,
              "wrapper initial readline status mismatch");
  ASSERT_TRUE(sl_last_error(sl) == NULL, "wrapper last_error mismatch");
  sl_destroy(sl);
  PASS();
}

static void test_set_cursor_clamps_to_utf8_cluster_boundary(void) {
  sl_t *sl;

  TEST("set_cursor clamps to UTF-8 cluster boundary");
  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");

  ASSERT_TRUE(sl_set_buffer(sl, "\303\245") == SL_OK, "set_buffer failed");
  ASSERT_TRUE(sl_set_cursor(sl, 1) == SL_OK, "set_cursor inside UTF-8 failed");
  ASSERT_TRUE(sl_cursor(sl) == 0, "cursor did not clamp to UTF-8 start");
  ASSERT_TRUE(sl_insert(sl, "x") == SL_OK, "insert failed");
  ASSERT_TRUE(strcmp(sl_buffer(sl), "x\303\245") == 0,
              "insert split UTF-8 sequence");

  ASSERT_TRUE(sl_set_buffer(sl, "ze\314\201y") == SL_OK,
              "set combining buffer failed");
  ASSERT_TRUE(sl_set_cursor(sl, 2) == SL_OK,
              "set_cursor inside combining cluster failed");
  ASSERT_TRUE(sl_cursor(sl) == 1, "cursor did not clamp to cluster start");
  ASSERT_TRUE(sl_insert(sl, "X") == SL_OK, "combining insert failed");
  ASSERT_TRUE(strcmp(sl_buffer(sl), "zXe\314\201y") == 0,
              "insert split combining cluster");

  sl_destroy(sl);
  PASS();
}

static void test_destroy_null(void) {
  TEST("sl_destroy NULL is safe");
  sl_destroy(NULL);
  PASS();
}

static void test_invalid_config_is_rejected(void) {
  sl_config_t cfg;
  sl_t *sl;

  TEST("invalid create config is rejected");
  sl_config_init(&cfg);
  cfg.history_max_len = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL, "negative history accepted");
  sl_config_init(&cfg);
  cfg.line_max_len = 0;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL, "zero line max accepted");
  sl_config_init(&cfg);
  cfg.screen_x = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL,
              "negative screen x accepted");
  sl_config_init(&cfg);
  cfg.screen_y = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL,
              "negative screen y accepted");
  sl_config_init(&cfg);
  cfg.screen_width = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL,
              "negative screen width accepted");
  sl_config_init(&cfg);
  cfg.screen_height = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL,
              "negative screen height accepted");
  sl_config_init(&cfg);
  cfg.bounded = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL, "negative bounded accepted");
  sl_config_init(&cfg);
  cfg.live_scroll_region = -1;
  ASSERT_TRUE(sl_create_with_config(&cfg) == NULL,
              "negative live scroll region accepted");
  sl_config_init(&cfg);
  cfg.prompt_queue = 1;
  cfg.bounded = 0;
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "normal prompt queue rejected");
  sl->destroy(sl);
  PASS();
}

static void test_invalid_receiver_arguments(void) {
  sl_t *sl;

  TEST("invalid receiver arguments return errors");
  ASSERT_TRUE(sl_readline(NULL, "p> ") == NULL, "NULL readline accepted");
  ASSERT_TRUE(sl_history_add(NULL, "x") == SL_ERROR_INVALID,
              "NULL history_add accepted");
  ASSERT_TRUE(sl_history_set_max_len(NULL, 1) == SL_ERROR_INVALID,
              "NULL history_set_max_len accepted");
  ASSERT_TRUE(sl_history_save(NULL, "x") == SL_ERROR_INVALID,
              "NULL history_save accepted");
  ASSERT_TRUE(sl_history_load(NULL, "x") == SL_ERROR_INVALID,
              "NULL history_load accepted");
  ASSERT_TRUE(sl_set_bounds(NULL, 0, 0, 1, 1) == SL_ERROR_INVALID,
              "NULL set_bounds accepted");
  ASSERT_TRUE(sl_set_screen_width(NULL, 1) == SL_ERROR_INVALID,
              "NULL set_screen_width accepted");
  ASSERT_TRUE(sl_set_live_scroll_region(NULL, 1) == SL_ERROR_INVALID,
              "NULL set_live_scroll_region accepted");
  ASSERT_TRUE(sl_set_idle_callback(NULL, NULL, NULL) == SL_ERROR_INVALID,
              "NULL set_idle_callback accepted");
  ASSERT_TRUE(sl_bind_key(NULL, SL_KEY_TAB, NULL, NULL) == SL_ERROR_INVALID,
              "NULL bind_key accepted");
  ASSERT_TRUE(sl_insert(NULL, "x") == SL_ERROR_INVALID, "NULL insert accepted");
  ASSERT_TRUE(sl_set_buffer(NULL, "x") == SL_ERROR_INVALID,
              "NULL set_buffer accepted");
  ASSERT_TRUE(sl_next_prompt(NULL, "p> ", NULL) == NULL,
              "NULL next_prompt accepted");
  ASSERT_TRUE(sl_set_prompt_queue(NULL, 1, 1, 1) == SL_ERROR_INVALID,
              "NULL prompt queue accepted");
  ASSERT_TRUE(sl_set_prompt_theme(NULL, SL_PROMPT_THEME_PLAIN) ==
                  SL_ERROR_INVALID,
              "NULL prompt theme accepted");
  ASSERT_TRUE(sl_set_statusline(NULL, 1, 0) == SL_ERROR_INVALID,
              "NULL status line accepted");
  ASSERT_TRUE(sl_set_status_elements(NULL, NULL, 0) == SL_ERROR_INVALID,
              "NULL status elements accepted");
  ASSERT_TRUE(sl_set_status_element(NULL, 0, "x") == SL_ERROR_INVALID,
              "NULL status element accepted");
  ASSERT_TRUE(sl_set_status_busy(NULL, 1) == SL_ERROR_INVALID,
              "NULL status busy accepted");
  ASSERT_TRUE(sl_set_status_spinner(NULL, 1) == SL_ERROR_INVALID,
              "NULL status spinner accepted");
  ASSERT_TRUE(sl_set_status_idle_marker(NULL, '-') == SL_ERROR_INVALID,
              "NULL status idle marker accepted");
  ASSERT_TRUE(sl_buffer(NULL) == NULL, "NULL buffer returned text");
  ASSERT_TRUE(sl_cursor(NULL) == 0, "NULL cursor returned offset");
  ASSERT_TRUE(sl_set_cursor(NULL, 0) == SL_ERROR_INVALID,
              "NULL set_cursor accepted");
  ASSERT_TRUE(sl_submit(NULL) == SL_ERROR_INVALID, "NULL submit accepted");
  ASSERT_TRUE(sl_cancel(NULL) == SL_ERROR_INVALID, "NULL cancel accepted");
  ASSERT_TRUE(sl_print_above(NULL, empty_stream, NULL) == SL_ERROR_INVALID,
              "NULL print_above accepted");
  ASSERT_TRUE(sl_last_readline_status(NULL) == SL_READLINE_NONE,
              "NULL last_readline_status returned status");
  ASSERT_TRUE(sl_last_error(NULL) == NULL, "NULL last_error returned text");

  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->history_add(sl, NULL) == SL_ERROR_INVALID,
              "NULL history entry accepted");
  ASSERT_TRUE(sl->history_set_max_len(sl, -1) == SL_ERROR_INVALID,
              "negative history max accepted");
  ASSERT_TRUE(sl->set_screen_width(sl, -1) == SL_ERROR_INVALID,
              "negative screen width accepted");
  ASSERT_TRUE(sl->set_live_scroll_region(sl, -1) == SL_ERROR_INVALID,
              "negative live scroll region accepted");
  ASSERT_TRUE(sl->set_bounds(sl, -1, 0, 1, 1) == SL_ERROR_INVALID,
              "negative bound accepted");
  ASSERT_TRUE(sl->set_prompt_queue(sl, 1, 1, 1) == SL_OK,
              "normal prompt queue rejected");
  ASSERT_TRUE(sl->set_prompt_theme(sl, (sl_prompt_theme_t)99) ==
                  SL_ERROR_INVALID,
              "invalid prompt theme accepted");
  ASSERT_TRUE(sl->set_statusline(sl, -1, 0) == SL_ERROR_INVALID,
              "negative status line accepted");
  ASSERT_TRUE(sl->set_status_elements(sl, NULL, 1) == SL_ERROR_INVALID,
              "NULL status elements accepted");
  ASSERT_TRUE(sl->set_status_element(sl, SL_STATUS_MAX_ELEMENTS, "x") ==
                  SL_ERROR_INVALID,
              "out-of-range status element accepted");
  ASSERT_TRUE(sl->set_status_element(sl, 0, "bad\ntext") == SL_ERROR_INVALID,
              "status control text accepted");
  ASSERT_TRUE(sl->set_status_busy(sl, -1) == SL_ERROR_INVALID,
              "negative status busy accepted");
  ASSERT_TRUE(sl->set_status_spinner(sl, -1) == SL_ERROR_INVALID,
              "negative status spinner accepted");
  ASSERT_TRUE(sl->set_status_idle_marker(sl, '\n') == SL_ERROR_INVALID,
              "status control idle marker accepted");
  ASSERT_TRUE(sl->bind_key(sl, SL_KEY_NONE, NULL, NULL) == SL_ERROR_INVALID,
              "SL_KEY_NONE binding accepted");
  ASSERT_TRUE(sl->insert(sl, NULL) == SL_ERROR_INVALID,
              "NULL insert text accepted");
  ASSERT_TRUE(sl->set_buffer(sl, NULL) == SL_ERROR_INVALID,
              "NULL set_buffer text accepted");
  ASSERT_TRUE(sl->submit(sl) == SL_ERROR_INVALID, "inactive submit accepted");
  ASSERT_TRUE(sl->cancel(sl) == SL_ERROR_INVALID, "inactive cancel accepted");
  ASSERT_TRUE(sl->print_above(sl, NULL, NULL) == SL_ERROR_INVALID,
              "NULL stream callback accepted");
  ASSERT_TRUE(sl->last_error(sl) != NULL, "last_error not set");
  sl->destroy(sl);
  PASS();
}

static void test_plain_readline(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;
  char out[64];
  ssize_t n;

  TEST("non-tty readline submits on newline");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  ASSERT_TRUE(write(in_pipe[1], "hello\n", 6) == 6, "write input failed");
  close(in_pipe[1]);

  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line != NULL, "readline returned NULL");
  ASSERT_TRUE(strcmp(line, "hello") == 0, "line mismatch");
  ASSERT_TRUE(sl->last_readline_status(sl) == SL_READLINE_SUBMITTED,
              "submitted readline status mismatch");
  sl->free_string(sl, line);
  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[1]);
  n = read(out_pipe[0], out, sizeof(out) - 1);
  ASSERT_TRUE(n >= 0, "read output failed");
  out[n] = '\0';
  close(out_pipe[0]);
  ASSERT_TRUE(strcmp(out, "") == 0, "non-tty readline emitted prompt output");
  PASS();
}

static void test_plain_readline_uses_default_prompt(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;
  char out[64];
  ssize_t n;

  TEST("non-tty default prompt stays silent");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  ASSERT_TRUE(write(in_pipe[1], "hello\n", 6) == 6, "write input failed");
  close(in_pipe[1]);

  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  line = sl->readline(sl, NULL);
  ASSERT_TRUE(line != NULL, "readline returned NULL");
  ASSERT_TRUE(strcmp(line, "hello") == 0, "line mismatch");
  sl->free_string(sl, line);
  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[1]);
  n = read(out_pipe[0], out, sizeof(out) - 1);
  ASSERT_TRUE(n >= 0, "read output failed");
  out[n] = '\0';
  close(out_pipe[0]);
  ASSERT_TRUE(strcmp(out, "") == 0, "non-tty default prompt emitted output");
  PASS();
}

static void test_plain_readline_consumes_crlf_once(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;

  TEST("non-tty readline consumes CRLF as one line ending");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  ASSERT_TRUE(write(in_pipe[1], "one\r\ntwo\r\n", 10) == 10,
              "write input failed");
  close(in_pipe[1]);

  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");

  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line != NULL, "first readline returned NULL");
  ASSERT_TRUE(strcmp(line, "one") == 0, "first line mismatch");
  sl->free_string(sl, line);

  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line != NULL, "second readline returned NULL");
  ASSERT_TRUE(strcmp(line, "two") == 0, "second line mismatch");
  sl->free_string(sl, line);

  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[0]);
  close(out_pipe[1]);
  PASS();
}

static void test_plain_readline_empty_eof_returns_null(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;

  TEST("non-tty empty EOF returns NULL");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  close(in_pipe[1]);
  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line == NULL, "empty EOF returned a line");
  ASSERT_TRUE(sl_last_readline_status(sl) == SL_READLINE_EOF,
              "EOF readline status mismatch");
  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[0]);
  close(out_pipe[1]);
  PASS();
}

static void test_plain_readline_retries_eintr(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;
  pid_t pid;
  int status;
  struct sigaction sa;
  struct sigaction old_sa;

  TEST("non-tty readline retries interrupted reads");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = ignore_signal;
  sigemptyset(&sa.sa_mask);
  ASSERT_TRUE(sigaction(SIGUSR1, &sa, &old_sa) == 0, "sigaction failed");
  signal_seen = 0;

  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    close(in_pipe[0]);
    close(out_pipe[0]);
    close(out_pipe[1]);
    usleep(50000);
    (void)kill(getppid(), SIGUSR1);
    usleep(50000);
    (void)write(in_pipe[1], "hello\n", 6);
    close(in_pipe[1]);
    _exit(0);
  }

  close(in_pipe[1]);
  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line != NULL, "readline returned NULL after EINTR");
  ASSERT_TRUE(strcmp(line, "hello") == 0, "line mismatch after EINTR");
  ASSERT_TRUE(sl->last_readline_status(sl) == SL_READLINE_SUBMITTED,
              "submitted readline status mismatch after EINTR");
  sl->free_string(sl, line);
  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[0]);
  close(out_pipe[1]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "signal writer failed");
  ASSERT_TRUE(sigaction(SIGUSR1, &old_sa, NULL) == 0,
              "restore sigaction failed");
  PASS();
}

static void test_plain_readline_stops_at_line_max(void) {
  int in_pipe[2];
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;

  TEST("non-tty readline stops at configured line max");
  ASSERT_TRUE(pipe(in_pipe) == 0, "input pipe failed");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  ASSERT_TRUE(write(in_pipe[1], "abcdef\n", 7) == 7, "write input failed");
  close(in_pipe[1]);

  sl_config_init(&cfg);
  cfg.input_fd = in_pipe[0];
  cfg.output_fd = out_pipe[1];
  cfg.line_max_len = 4;
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  line = sl->readline(sl, "p> ");
  ASSERT_TRUE(line != NULL, "readline returned NULL");
  ASSERT_TRUE(strcmp(line, "abcd") == 0, "line max result mismatch");
  ASSERT_TRUE(sl->last_error(sl) != NULL, "line max error not recorded");
  sl->free_string(sl, line);
  sl->destroy(sl);
  close(in_pipe[0]);
  close(out_pipe[0]);
  close(out_pipe[1]);
  PASS();
}

static void test_readline_status_is_per_instance(void) {
  int in_pipe1[2];
  int in_pipe2[2];
  sl_config_t cfg;
  sl_t *sl1;
  sl_t *sl2;
  char *line;

  TEST("readline status is per-instance");
  ASSERT_TRUE(pipe(in_pipe1) == 0, "first input pipe failed");
  ASSERT_TRUE(pipe(in_pipe2) == 0, "second input pipe failed");
  ASSERT_TRUE(write(in_pipe1[1], "one\n", 4) == 4, "write first input failed");
  close(in_pipe1[1]);
  close(in_pipe2[1]);

  sl_config_init(&cfg);
  cfg.input_fd = in_pipe1[0];
  sl1 = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl1 != NULL, "first create failed");
  sl_config_init(&cfg);
  cfg.input_fd = in_pipe2[0];
  sl2 = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl2 != NULL, "second create failed");

  line = sl1->readline(sl1, "p> ");
  ASSERT_TRUE(line != NULL, "first readline returned NULL");
  ASSERT_TRUE(strcmp(line, "one") == 0, "first readline mismatch");
  sl1->free_string(sl1, line);
  line = sl2->readline(sl2, "p> ");
  ASSERT_TRUE(line == NULL, "second readline returned text");
  ASSERT_TRUE(sl1->last_readline_status(sl1) == SL_READLINE_SUBMITTED,
              "first instance status changed");
  ASSERT_TRUE(sl2->last_readline_status(sl2) == SL_READLINE_EOF,
              "second instance status mismatch");

  sl1->destroy(sl1);
  sl2->destroy(sl2);
  close(in_pipe1[0]);
  close(in_pipe2[0]);
  PASS();
}

static void test_history_is_per_instance(void) {
  char file1[] = "/tmp/softline-history-1-XXXXXX";
  char file2[] = "/tmp/softline-history-2-XXXXXX";
  int fd1;
  int fd2;
  sl_t *a;
  sl_t *b;
  FILE *fp;
  char buf[128];

  TEST("history is per-instance");
  fd1 = mkstemp(file1);
  fd2 = mkstemp(file2);
  ASSERT_TRUE(fd1 >= 0 && fd2 >= 0, "mkstemp failed");
  close(fd1);
  close(fd2);
  a = sl_create();
  b = sl_create();
  ASSERT_TRUE(a != NULL && b != NULL, "create failed");
  ASSERT_TRUE(a->history_add(a, "alpha") == SL_OK, "history add a failed");
  ASSERT_TRUE(b->history_add(b, "beta") == SL_OK, "history add b failed");
  ASSERT_TRUE(a->history_save(a, file1) == SL_OK, "history save a failed");
  ASSERT_TRUE(b->history_save(b, file2) == SL_OK, "history save b failed");
  fp = fopen(file1, "r");
  ASSERT_TRUE(fp != NULL, "open file1 failed");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) != NULL, "read file1 failed");
  fclose(fp);
  ASSERT_TRUE(strcmp(buf, "alpha\n") == 0, "file1 mismatch");
  fp = fopen(file2, "r");
  ASSERT_TRUE(fp != NULL, "open file2 failed");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) != NULL, "read file2 failed");
  fclose(fp);
  ASSERT_TRUE(strcmp(buf, "beta\n") == 0, "file2 mismatch");
  a->destroy(a);
  b->destroy(b);
  unlink(file1);
  unlink(file2);
  PASS();
}

static void test_history_io_failures_are_reported(void) {
  sl_t *sl;

  TEST("history I/O failures are reported");
  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->history_save(sl, NULL) == SL_ERROR_INVALID,
              "NULL save path accepted");
  ASSERT_TRUE(sl->history_load(sl, NULL) == SL_ERROR_INVALID,
              "NULL load path accepted");
  ASSERT_TRUE(sl->history_load(sl, "/tmp/softline-no-such-file") == SL_ERROR_IO,
              "missing history load did not fail");
  ASSERT_TRUE(sl->last_error(sl) != NULL, "history error not recorded");
  sl->destroy(sl);
  PASS();
}

static void test_history_save_uses_private_permissions(void) {
  char file[] = "/tmp/softline-history-private-XXXXXX";
  int fd;
  struct stat st;
  sl_t *sl;

  TEST("history save forces owner-only permissions");
  fd = mkstemp(file);
  ASSERT_TRUE(fd >= 0, "mkstemp failed");
  close(fd);
  ASSERT_TRUE(chmod(file, 0666) == 0, "chmod fixture failed");
  sl = sl_create();
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->history_add(sl, "secret") == SL_OK, "history add failed");
  ASSERT_TRUE(sl->history_save(sl, file) == SL_OK, "history save failed");
  ASSERT_TRUE(stat(file, &st) == 0, "stat failed");
  ASSERT_TRUE((st.st_mode & 0777) == 0600, "history file is not private");
  sl->destroy(sl);
  unlink(file);
  PASS();
}

static void test_history_round_trips_multiline_entries(void) {
  char file1[] = "/tmp/softline-history-multiline-1-XXXXXX";
  char file2[] = "/tmp/softline-history-multiline-2-XXXXXX";
  int fd1;
  int fd2;
  sl_t *a;
  sl_t *b;
  FILE *fp;
  char buf[128];

  TEST("history save/load preserves multiline entries");
  fd1 = mkstemp(file1);
  fd2 = mkstemp(file2);
  ASSERT_TRUE(fd1 >= 0 && fd2 >= 0, "mkstemp failed");
  close(fd1);
  close(fd2);
  a = sl_create();
  b = sl_create();
  ASSERT_TRUE(a != NULL && b != NULL, "create failed");
  ASSERT_TRUE(a->history_add(a, "alpha\nbeta\\gamma") == SL_OK,
              "history add failed");
  ASSERT_TRUE(a->history_save(a, file1) == SL_OK, "history save failed");
  fp = fopen(file1, "r");
  ASSERT_TRUE(fp != NULL, "open saved history failed");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) != NULL, "read saved history failed");
  fclose(fp);
  ASSERT_TRUE(strcmp(buf, "alpha\\nbeta\\\\gamma\n") == 0,
              "encoded history mismatch");
  ASSERT_TRUE(b->history_load(b, file1) == SL_OK, "history load failed");
  ASSERT_TRUE(b->history_save(b, file2) == SL_OK, "history resave failed");
  fp = fopen(file2, "r");
  ASSERT_TRUE(fp != NULL, "open resaved history failed");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) != NULL,
              "read resaved history failed");
  fclose(fp);
  ASSERT_TRUE(strcmp(buf, "alpha\\nbeta\\\\gamma\n") == 0,
              "round-trip history mismatch");
  a->destroy(a);
  b->destroy(b);
  unlink(file1);
  unlink(file2);
  PASS();
}

static void test_history_rejects_oversized_entries(void) {
  char file1[] = "/tmp/softline-history-oversized-1-XXXXXX";
  char file2[] = "/tmp/softline-history-oversized-2-XXXXXX";
  int fd1;
  int fd2;
  sl_config_t cfg;
  sl_t *sl;
  FILE *fp;
  char buf[128];

  TEST("history rejects oversized entries without splitting");
  fd1 = mkstemp(file1);
  fd2 = mkstemp(file2);
  ASSERT_TRUE(fd1 >= 0 && fd2 >= 0, "mkstemp failed");
  fp = fdopen(fd1, "w");
  ASSERT_TRUE(fp != NULL, "fdopen failed");
  ASSERT_TRUE(fputs("123456789\nok\n", fp) >= 0, "write fixture failed");
  ASSERT_TRUE(fclose(fp) == 0, "close fixture failed");
  close(fd2);
  sl_config_init(&cfg);
  cfg.line_max_len = 4;
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->history_add(sl, "12345") == SL_ERROR_INVALID,
              "oversized history add accepted");
  ASSERT_TRUE(sl->history_load(sl, file1) == SL_ERROR_INVALID,
              "oversized history load accepted");
  ASSERT_TRUE(sl->history_save(sl, file2) == SL_OK, "history save failed");
  fp = fopen(file2, "r");
  ASSERT_TRUE(fp != NULL, "open saved history failed");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) != NULL, "read saved history failed");
  ASSERT_TRUE(strcmp(buf, "ok\n") == 0, "oversized record was split or kept");
  ASSERT_TRUE(fgets(buf, sizeof(buf), fp) == NULL,
              "unexpected extra history record");
  fclose(fp);
  sl->destroy(sl);
  unlink(file1);
  unlink(file2);
  PASS();
}

static void test_stream_failures_are_reported(void) {
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;

  TEST("print_above stream failures are reported");
  ASSERT_TRUE(pipe(out_pipe) == 0, "pipe failed");
  sl_config_init(&cfg);
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->print_above(sl, failing_stream, NULL) == SL_ERROR,
              "stream callback failure not propagated");
  ASSERT_TRUE(sl->print_above(sl, invalid_chunk_stream, NULL) ==
                  SL_ERROR_INVALID,
              "invalid stream chunk accepted");
  close(out_pipe[1]);
  ASSERT_TRUE(sl->print_above(sl, one_chunk_stream, "x") == SL_ERROR_IO,
              "closed output fd did not fail");
  sl->destroy(sl);
  close(out_pipe[0]);
  PASS();
}

static void test_print_above_uses_lf_for_non_tty_output(void) {
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char out[64];
  ssize_t n;
  struct one_chunk_once stream;

  TEST("print_above uses LF for non-tty output");
  ASSERT_TRUE(pipe(out_pipe) == 0, "pipe failed");
  sl_config_init(&cfg);
  cfg.output_fd = out_pipe[1];
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  stream.text = "alpha\n";
  stream.sent = 0;
  ASSERT_TRUE(sl->print_above(sl, one_chunk_once_stream, &stream) == SL_OK,
              "print_above failed");
  sl->destroy(sl);
  close(out_pipe[1]);
  n = read(out_pipe[0], out, sizeof(out) - 1);
  ASSERT_TRUE(n >= 0, "read output failed");
  out[n] = '\0';
  close(out_pipe[0]);
  ASSERT_TRUE(strcmp(out, "alpha\n") == 0,
              "non-tty stream output did not use LF");
  PASS();
}

static void test_bounded_print_above_without_space_is_error(void) {
  int out_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char out[64];
  ssize_t n;

  TEST("bounded print_above errors without output space");
  ASSERT_TRUE(pipe(out_pipe) == 0, "output pipe failed");
  sl_config_init(&cfg);
  cfg.output_fd = out_pipe[1];
  cfg.screen_width = 20;
  cfg.screen_height = 1;
  sl = sl_create_with_config(&cfg);
  ASSERT_TRUE(sl != NULL, "create failed");
  ASSERT_TRUE(sl->print_above(sl, one_chunk_stream, "hidden") ==
                  SL_ERROR_INVALID,
              "bounded print_above without space succeeded");
  ASSERT_TRUE(sl->last_error(sl) != NULL, "last_error not set");
  sl->destroy(sl);
  close(out_pipe[1]);
  n = read(out_pipe[0], out, sizeof(out) - 1);
  ASSERT_TRUE(n >= 0, "read output failed");
  out[n] = '\0';
  close(out_pipe[0]);
  ASSERT_TRUE(strcmp(out, "") == 0,
              "bounded print_above wrote into prompt area");
  PASS();
}

static int contains_bytes(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != NULL;
}

static int count_bytes(const char *haystack, const char *needle) {
  int count;
  size_t needle_len;
  const char *p;
  count = 0;
  needle_len = strlen(needle);
  if (needle_len == 0)
    return 0;
  p = haystack;
  while ((p = strstr(p, needle)) != NULL) {
    count++;
    p += needle_len;
  }
  return count;
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

struct vt_screen {
  int rows;
  int cols;
  int row;
  int col;
  int scroll_top;
  int scroll_bottom;
  char cells[24][120];
};

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
  screen->scroll_top = 0;
  screen->scroll_bottom = screen->rows - 1;
}

static void vt_init(struct vt_screen *screen, int rows, int cols) {
  screen->rows = rows > 24 ? 24 : rows;
  screen->cols = cols > 119 ? 119 : cols;
  vt_clear(screen);
}

static void vt_scroll_region(struct vt_screen *screen, int top, int bottom) {
  int r;
  if (top < 0)
    top = 0;
  if (bottom >= screen->rows)
    bottom = screen->rows - 1;
  if (bottom < top)
    return;
  for (r = top + 1; r <= bottom; r++)
    memcpy(screen->cells[r - 1], screen->cells[r], (size_t)screen->cols + 1);
  memset(screen->cells[bottom], ' ', (size_t)screen->cols);
  screen->cells[bottom][screen->cols] = '\0';
}

static void vt_scroll(struct vt_screen *screen) {
  vt_scroll_region(screen, screen->scroll_top, screen->scroll_bottom);
  screen->row = screen->scroll_bottom;
}

static void vt_lf(struct vt_screen *screen) {
  if (screen->row == screen->scroll_bottom) {
    vt_scroll_region(screen, screen->scroll_top, screen->scroll_bottom);
    return;
  }
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
  case 'r':
    if (!have_a) {
      screen->scroll_top = 0;
      screen->scroll_bottom = screen->rows - 1;
    } else {
      screen->scroll_top = a > 0 ? a - 1 : 0;
      screen->scroll_bottom = b > 0 ? b - 1 : screen->rows - 1;
      if (screen->scroll_top < 0)
        screen->scroll_top = 0;
      if (screen->scroll_bottom >= screen->rows)
        screen->scroll_bottom = screen->rows - 1;
      if (screen->scroll_bottom < screen->scroll_top)
        screen->scroll_bottom = screen->scroll_top;
    }
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

static int vt_count(struct vt_screen *screen, const char *needle) {
  int r;
  int count;
  size_t needle_len;
  count = 0;
  needle_len = strlen(needle);
  if (needle_len == 0)
    return 0;
  for (r = 0; r < screen->rows; r++) {
    const char *p;
    p = screen->cells[r];
    while ((p = strstr(p, needle)) != NULL) {
      count++;
      p += needle_len;
    }
  }
  return count;
}

static void vt_dump(struct vt_screen *screen) {
  int r;
  for (r = 0; r < screen->rows; r++)
    fprintf(stderr, "\n%02d:%s", r, screen->cells[r]);
  fprintf(stderr, "\n");
}

static ssize_t read_some_with_timeout(int fd, char *buf, size_t cap) {
  fd_set readfds;
  struct timeval tv;
  int ready;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  ready = select(fd + 1, &readfds, NULL, NULL, &tv);
  if (ready <= 0)
    return ready;
  return read(fd, buf, cap);
}

static ssize_t read_some_with_timeout_ms(int fd, char *buf, size_t cap,
                                         long timeout_ms) {
  fd_set readfds;
  struct timeval tv;
  int ready;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  ready = select(fd + 1, &readfds, NULL, NULL, &tv);
  if (ready <= 0)
    return ready;
  return read(fd, buf, cap);
}

static ssize_t read_until_eof_with_timeout(int fd, char *buf, size_t cap) {
  ssize_t total;
  total = 0;
  while ((size_t)total < cap) {
    ssize_t n;
    n = read_some_with_timeout(fd, buf + total, cap - (size_t)total);
    if (n < 0)
      return n;
    if (n == 0)
      return total;
    total += n;
  }
  return total;
}

#if defined(__linux__)
struct idle_print_state {
  int printed;
};

struct idle_scroll_region_state {
  int calls;
};

struct idle_count_print_state {
  int calls;
  int printed;
  const char *text;
};

struct idle_finish_state {
  int calls;
  const char *text;
  int cancel;
};

struct idle_ready_state {
  int fd;
  int ready;
};

struct idle_queue_limit_state {
  const char *expected_buffer;
  int max_entries;
  int status;
  int attempted;
};

struct text_stream_state {
  const char *chunks[4];
  int index;
  int calls;
};

static volatile sig_atomic_t ctrl_c_sigint_count = 0;

static void count_ctrl_c_sigint(int signum) {
  if (signum == SIGINT)
    ctrl_c_sigint_count++;
}

struct enter_override_state {
  int calls;
};

static int insert_text_key(sl_t *sl, sl_key_t key, void *userdata,
                           sl_key_action_t *action) {
  const char *text;
  (void)key;
  text = (const char *)userdata;
  if (sl->insert(sl, text) != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int submit_action_key(sl_t *sl, sl_key_t key, void *userdata,
                             sl_key_action_t *action) {
  (void)sl;
  (void)key;
  (void)userdata;
  *action = SL_KEY_ACTION_SUBMIT;
  return SL_OK;
}

static int submit_method_key(sl_t *sl, sl_key_t key, void *userdata,
                             sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (sl->submit(sl) != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int edit_buffer_key(sl_t *sl, sl_key_t key, void *userdata,
                           sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (sl->set_buffer(sl, "abcd") != SL_OK)
    return SL_ERROR;
  if (sl->set_cursor(sl, 2) != SL_OK)
    return SL_ERROR;
  if (sl->insert(sl, "XX") != SL_OK)
    return SL_ERROR;
  if (strcmp(sl->buffer(sl), "abXXcd") != 0)
    return SL_ERROR;
  if (sl->cursor(sl) != 4)
    return SL_ERROR;
  *action = SL_KEY_ACTION_SUBMIT;
  return SL_OK;
}

static int cancel_action_key(sl_t *sl, sl_key_t key, void *userdata,
                             sl_key_action_t *action) {
  (void)sl;
  (void)key;
  (void)userdata;
  *action = SL_KEY_ACTION_CANCEL;
  return SL_OK;
}

static int enter_override_key(sl_t *sl, sl_key_t key, void *userdata,
                              sl_key_action_t *action) {
  struct enter_override_state *state;
  (void)key;
  state = (struct enter_override_state *)userdata;
  state->calls++;
  if (state->calls == 1) {
    if (sl->insert(sl, "\n") != SL_OK)
      return SL_ERROR;
    *action = SL_KEY_ACTION_HANDLED;
  } else {
    *action = SL_KEY_ACTION_PASS;
  }
  return SL_OK;
}

static int next_text_chunk(sl_t *sl, void *userdata, const char **chunk,
                           size_t *len) {
  struct text_stream_state *state;
  const char *text;
  (void)sl;
  state = (struct text_stream_state *)userdata;
  if (!state || state->index >= 4) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  text = state->chunks[state->index];
  state->index++;
  state->calls++;
  if (!text) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  *chunk = text;
  *len = strlen(text);
  return SL_OK;
}

static void idle_print_once(sl_t *sl, void *userdata) {
  struct idle_print_state *state;
  struct text_stream_state stream;
  state = (struct idle_print_state *)userdata;
  if (!state || state->printed)
    return;
  state->printed = 1;
  stream.chunks[0] = "idle-";
  stream.chunks[1] = "output";
  stream.chunks[2] = "\n";
  stream.chunks[3] = NULL;
  stream.index = 0;
  stream.calls = 0;
  (void)sl->print_above(sl, next_text_chunk, &stream);
}

static void idle_print_through_scroll_region(sl_t *sl, void *userdata) {
  struct idle_scroll_region_state *state;
  struct text_stream_state stream;
  const char *text;
  state = (struct idle_scroll_region_state *)userdata;
  if (!state)
    return;
  state->calls++;
  if (state->calls != 1 && state->calls != 4)
    return;
  text = state->calls == 1 ? "first-live\n" : "second-live\n";
  stream.chunks[0] = text;
  stream.chunks[1] = NULL;
  stream.chunks[2] = NULL;
  stream.chunks[3] = NULL;
  stream.index = 0;
  stream.calls = 0;
  (void)sl->print_above(sl, next_text_chunk, &stream);
}

static void idle_print_after_two_ticks(sl_t *sl, void *userdata) {
  struct idle_count_print_state *state;
  struct text_stream_state stream;
  state = (struct idle_count_print_state *)userdata;
  if (!state || state->printed)
    return;
  state->calls++;
  if (state->calls < 2)
    return;
  state->printed = 1;
  stream.chunks[0] = state->text;
  stream.chunks[1] = NULL;
  stream.chunks[2] = NULL;
  stream.chunks[3] = NULL;
  stream.index = 0;
  stream.calls = 0;
  (void)sl->print_above(sl, next_text_chunk, &stream);
}

static void idle_finish_after_two_ticks(sl_t *sl, void *userdata) {
  struct idle_finish_state *state;
  state = (struct idle_finish_state *)userdata;
  if (!state)
    return;
  state->calls++;
  if (state->calls < 2)
    return;
  if (state->cancel) {
    (void)sl->cancel(sl);
    return;
  }
  if (state->text)
    (void)sl->insert(sl, state->text);
  (void)sl->submit(sl);
}

static void idle_signal_ready_once(sl_t *sl, void *userdata) {
  struct idle_ready_state *state;
  (void)sl;
  state = (struct idle_ready_state *)userdata;
  if (!state || state->ready)
    return;
  state->ready = 1;
  (void)write(state->fd, "R", 1);
}

static void idle_reduce_prompt_queue_limit(sl_t *sl, void *userdata) {
  struct idle_queue_limit_state *state;
  const char *buffer;
  state = (struct idle_queue_limit_state *)userdata;
  if (!state || state->attempted)
    return;
  buffer = sl->buffer(sl);
  if (!buffer || strcmp(buffer, state->expected_buffer) != 0)
    return;
  state->attempted = 1;
  state->status = sl->set_prompt_queue(sl, 1, state->max_entries, 1);
  (void)sl->submit(sl);
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

static int run_pty_readline_case_with_prompt_limit(
    const char *input, const char *prompt, const char *expected_prompt,
    int width, int height, size_t line_max_len, const char *print_above,
    char *terminal, size_t terminal_cap, char *result, size_t result_cap,
    int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  size_t terminal_len;
  ssize_t n;
  int status;
  struct winsize ws;

  memset(&ws, 0, sizeof(ws));
  if (width > 0)
    ws.ws_col = (unsigned short)width;
  if (height > 0)
    ws.ws_row = (unsigned short)height;
  if (openpty(&master_fd, &slave_fd, NULL, NULL,
              (width > 0 || height > 0) ? &ws : NULL) != 0)
    return -1;
  if (pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = width;
    cfg.screen_height = height;
    if (line_max_len > 0)
      cfg.line_max_len = line_max_len;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (print_above) {
      struct text_stream_state stream;
      stream.chunks[0] = print_above;
      stream.chunks[1] = NULL;
      stream.chunks[2] = NULL;
      stream.chunks[3] = NULL;
      stream.index = 0;
      stream.calls = 0;
      (void)sl->print_above(sl, next_text_chunk, &stream);
    }
    line = sl->readline(sl, prompt);
    if (!line) {
      (void)write(result_pipe[1], "<NULL>", 6);
    } else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }

  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, expected_prompt)) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, input, strlen(input)) != (ssize_t)strlen(input))
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int run_pty_readline_case_with_limit(const char *input, int width,
                                            int height, size_t line_max_len,
                                            const char *print_above,
                                            char *terminal, size_t terminal_cap,
                                            char *result, size_t result_cap,
                                            int *exit_status) {
  return run_pty_readline_case_with_prompt_limit(
      input, "p> ", "p> ", width, height, line_max_len, print_above, terminal,
      terminal_cap, result, result_cap, exit_status);
}

static int run_pty_readline_default_prompt_case(const char *input,
                                                char *terminal,
                                                size_t terminal_cap,
                                                char *result, size_t result_cap,
                                                int *exit_status) {
  return run_pty_readline_case_with_prompt_limit(
      input, NULL, "> ", 20, 0, 0, NULL, terminal, terminal_cap, result,
      result_cap, exit_status);
}

static int run_pty_readline_case(const char *input, int width, int height,
                                 const char *print_above, char *terminal,
                                 size_t terminal_cap, char *result,
                                 size_t result_cap, int *exit_status) {
  return run_pty_readline_case_with_limit(input, width, height, 0, print_above,
                                          terminal, terminal_cap, result,
                                          result_cap, exit_status);
}

static int run_pty_readline_completion_case(int bounded, int prompt_queue,
                                            char *terminal, size_t terminal_cap,
                                            char *result, size_t result_cap,
                                            int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  size_t terminal_len;
  ssize_t n;
  int status;

  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) != 0 ||
      pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.prompt_queue = prompt_queue;
    if (bounded) {
      cfg.bounded = 1;
      cfg.screen_width = 20;
      cfg.screen_height = 3;
    }
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    if (write(slave_fd, "after\n", 6) != 6)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }

  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, "hello\r", 6) != 6)
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int run_pty_history_case_with_options(
    const char *input, const char **history, int history_len,
    const char *history_file, int width, int height, char *terminal,
    size_t terminal_cap, char *result, size_t result_cap, int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  size_t terminal_len;
  ssize_t n;
  int status;
  int i;

  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0)
    return -1;
  if (pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = width;
    cfg.screen_height = height;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    for (i = 0; i < history_len; i++)
      (void)sl->history_add(sl, history[i]);
    if (history_file && sl->history_load(sl, history_file) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line) {
      (void)write(result_pipe[1], "<NULL>", 6);
    } else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }

  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, input, strlen(input)) != (ssize_t)strlen(input))
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int run_pty_history_case_with_file(const char *input,
                                          const char **history, int history_len,
                                          const char *history_file,
                                          char *terminal, size_t terminal_cap,
                                          char *result, size_t result_cap,
                                          int *exit_status) {
  return run_pty_history_case_with_options(
      input, history, history_len, history_file, 20, 0, terminal, terminal_cap,
      result, result_cap, exit_status);
}

static int run_pty_history_case(const char *input, const char **history,
                                int history_len, char *terminal,
                                size_t terminal_cap, char *result,
                                size_t result_cap, int *exit_status) {
  return run_pty_history_case_with_file(input, history, history_len, NULL,
                                        terminal, terminal_cap, result,
                                        result_cap, exit_status);
}

static int run_pty_key_binding_case(const char *input, sl_key_t key,
                                    sl_key_callback_t callback, void *userdata,
                                    char *terminal, size_t terminal_cap,
                                    char *result, size_t result_cap,
                                    int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  size_t terminal_len;
  ssize_t n;
  int status;

  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0)
    return -1;
  if (pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 24;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->bind_key(sl, key, callback, userdata) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line) {
      (void)write(result_pipe[1], "<NULL>", 6);
    } else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }

  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, input, strlen(input)) != (ssize_t)strlen(input))
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int run_pty_prompt_queue_case(const char *input, sl_prompt_theme_t theme,
                                     int bounded, int reduced_max_entries,
                                     int expected_prompts, char *terminal,
                                     size_t terminal_cap, char *result,
                                     size_t result_cap, int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  size_t terminal_len;
  ssize_t n;
  int status;

  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 8;
  if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) != 0 ||
      pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char header[32];
    char output[512];
    struct idle_queue_limit_state limit_state;
    size_t header_len;
    size_t used;
    int i;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    if (bounded) {
      cfg.screen_width = 40;
      cfg.screen_height = 8;
      cfg.bounded = 1;
    }
    cfg.prompt_queue = 1;
    cfg.prompt_queue_max_entries = 8;
    cfg.prompt_queue_preview_entries = 1;
    cfg.prompt_theme = theme;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    memset(&limit_state, 0, sizeof(limit_state));
    limit_state.expected_buffer = "third";
    limit_state.max_entries = reduced_max_entries;
    limit_state.status = SL_ERROR;
    if (reduced_max_entries > 0 &&
        sl->set_idle_callback(sl, idle_reduce_prompt_queue_limit,
                              &limit_state) != SL_OK)
      _exit(5);
    used = 0;
    output[0] = '\0';
    for (i = 0; i < expected_prompts; i++) {
      char *line;
      sl_prompt_source_t source;
      int written;
      source = SL_PROMPT_SOURCE_NONE;
      line = sl_next_prompt(sl, "chat> ", &source);
      if (!line)
        _exit(3);
      written = snprintf(output + used, sizeof(output) - used, "%s%d:%s",
                         i == 0 ? "" : "|", (int)source, line);
      sl_free_string(sl, line);
      if (written < 0 || (size_t)written >= sizeof(output) - used)
        _exit(4);
      used += (size_t)written;
    }
    if (reduced_max_entries > 0) {
      int written;
      written = snprintf(header, sizeof(header), "%d;", limit_state.status);
      if (written < 0 || written >= (int)sizeof(header))
        _exit(6);
      header_len = (size_t)written;
      if (header_len > sizeof(output) - used)
        _exit(7);
      memmove(output + header_len, output, used);
      memcpy(output, header, header_len);
      used += header_len;
    }
    (void)write(result_pipe[1], output, used);
    sl_destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "chat> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, input, strlen(input)) != (ssize_t)strlen(input))
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int run_pty_themed_readline_case(sl_prompt_theme_t theme, char *terminal,
                                        size_t terminal_cap, char *result,
                                        size_t result_cap, int *exit_status) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  size_t terminal_len;
  ssize_t n;
  int status;

  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0 ||
      pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 24;
    cfg.prompt_theme = theme;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl_readline(sl, "normal> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl_free_string(sl, line);
    sl_destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "normal> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  if (write(master_fd, "ok\r", 3) != 3)
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < terminal_cap - 1);
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int set_status_idle_marker_key(sl_t *sl, sl_key_t key, void *userdata,
                                      sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (!action || sl_set_status_idle_marker(sl, '\0') != SL_OK ||
      sl_set_status_spinner(sl, 0) != SL_OK ||
      sl_set_status_busy(sl, 0) != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int set_status_dash_marker_key(sl_t *sl, sl_key_t key, void *userdata,
                                      sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (!action || sl_set_status_idle_marker(sl, '-') != SL_OK ||
      sl_set_status_spinner(sl, 0) != SL_OK ||
      sl_set_status_busy(sl, 0) != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int set_status_busy_spinner_key(sl_t *sl, sl_key_t key, void *userdata,
                                       sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (!action || sl_set_status_spinner(sl, 1) != SL_OK ||
      sl_set_status_busy(sl, 1) != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int run_pty_statusline_case(sl_prompt_theme_t theme, char *terminal,
                                   size_t terminal_cap, char *result,
                                   size_t result_cap, int *exit_status) {
  static const char *const elements[] = {
      "e0",  "e1",  "e2",  "e3",  "e4",  "e5",  "e6",  "e7",  "e8",
      "e9",  "e10", "e11", "e12", "e13", "e14", "e15", "e16", "e17",
      "e18", "e19", "e20", "e21", "e22", "e23", "e24", "e25", "e26",
      "e27", "e28", "e29", "e30", "e31", "e32"};
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;
  const char *idle_style;
  const char *busy_style;
  const char *element_style;

  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0 ||
      pipe(result_pipe) != 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 24;
    cfg.prompt_theme = theme;
    cfg.statusline = 1;
    cfg.statusline_start_element = 15;
    cfg.status_spinner = 1;
    cfg.status_busy = 0;
    sl = sl_create_with_config(&cfg);
    if (!sl ||
        sl_set_status_elements(sl, elements,
                               sizeof(elements) / sizeof(elements[0])) != SL_OK)
      _exit(2);
    if (sl_bind_key(sl, SL_KEY_ALT_M, set_status_idle_marker_key, NULL) !=
        SL_OK)
      _exit(4);
    if (sl_bind_key(sl, (sl_key_t)(SL_KEY_ALT_BASE + 'n'),
                    set_status_dash_marker_key, NULL) != SL_OK)
      _exit(5);
    if (sl_bind_key(sl, (sl_key_t)(SL_KEY_ALT_BASE + 'p'),
                    set_status_busy_spinner_key, NULL) != SL_OK)
      _exit(6);
    line = sl_readline(sl, "status> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl_free_string(sl, line);
    sl_destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  idle_style = theme == SL_PROMPT_THEME_DEFAULT ? "\033[32m+ "
                                                : "\033[38;2;57;255;20m+ ";
  busy_style = theme == SL_PROMPT_THEME_DEFAULT ? "\033[31m- "
                                                : "\033[38;2;255;51;51m- ";
  element_style = theme == SL_PROMPT_THEME_DEFAULT
                      ? "\r  \033[97me0"
                      : "\r  \033[38;2;172;164;184me0";
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "status> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n <= 0)
      return -1;
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    if (terminal_len >= terminal_cap - 1)
      return -1;
  }
  tries = 0;
  while (!contains_bytes(terminal, idle_style) && tries < 20) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n < 0)
      return -1;
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
      if (terminal_len >= terminal_cap - 1)
        return -1;
    }
    tries++;
  }
  if (!contains_bytes(terminal, idle_style))
    return -1;
  if (write(master_fd, "\033p", 2) != 2)
    return -1;
  tries = 0;
  while (!contains_bytes(terminal, busy_style) && tries < 20) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n < 0)
      return -1;
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
      if (terminal_len >= terminal_cap - 1)
        return -1;
    }
    tries++;
  }
  if (!contains_bytes(terminal, busy_style))
    return -1;
  if (write(master_fd, "\033m", 2) != 2)
    return -1;
  tries = 0;
  while (!contains_bytes(terminal, element_style) && tries < 20) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n < 0)
      return -1;
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
      if (terminal_len >= terminal_cap - 1)
        return -1;
    }
    tries++;
  }
  if (!contains_bytes(terminal, element_style))
    return -1;
  if (write(master_fd, "\033n", 2) != 2)
    return -1;
  tries = 0;
  while (!contains_bytes(terminal, theme == SL_PROMPT_THEME_DEFAULT
                                       ? "\033[32m- "
                                       : "\033[38;2;57;255;20m- ") &&
         tries < 20) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               terminal_cap - 1 - terminal_len);
    if (n < 0)
      return -1;
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
      if (terminal_len >= terminal_cap - 1)
        return -1;
    }
    tries++;
  }
  if (!contains_bytes(terminal, theme == SL_PROMPT_THEME_DEFAULT
                                    ? "\033[32m- "
                                    : "\033[38;2;57;255;20m- "))
    return -1;
  if (write(master_fd, "ok\r", 3) != 3)
    return -1;
  n = read_some_with_timeout(result_pipe[0], result, result_cap - 1);
  if (n <= 0)
    return -1;
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  if (waitpid(pid, &status, 0) != pid)
    return -1;
  *exit_status = status;
  return 0;
}

static int termios_same_observable(const struct termios *a,
                                   const struct termios *b) {
  int i;
  if (a->c_iflag != b->c_iflag || a->c_oflag != b->c_oflag ||
      a->c_cflag != b->c_cflag || a->c_lflag != b->c_lflag)
    return 0;
  for (i = 0; i < NCCS; i++) {
    if (a->c_cc[i] != b->c_cc[i])
      return 0;
  }
  return 1;
}

static void test_pty_enter_and_ctrl_j(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("pty readline is non-destructive and Ctrl-J inserts newline");
  ASSERT_TRUE(run_pty_readline_case("ab\ncd\r", 20, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ab\ncd") == 0, "result mismatch");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[2J"), "clear-screen emitted");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[?25l"), "cursor-hide emitted");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[H"), "home-position emitted");
  PASS();
}

static void test_pty_readline_uses_default_prompt(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("pty readline uses default prompt");
  ASSERT_TRUE(run_pty_readline_default_prompt_case(
                  "ok\r", terminal, sizeof(terminal), result, sizeof(result),
                  &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "> "), "default prompt missing");
  ASSERT_TRUE(!contains_bytes(terminal, "p> "), "test prompt was rendered");
  PASS();
}

static void test_pty_readline_stops_at_line_max(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("pty readline stops at configured line max");
  ASSERT_TRUE(run_pty_readline_case_with_limit(
                  "abcdef\r", 20, 0, 4, NULL, terminal, sizeof(terminal),
                  result, sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abcd") == 0, "line max result mismatch");
  PASS();
}

static void test_normal_prompt_proceeds_after_output(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char terminal[4096];
  char result[256];
  size_t terminal_len;
  ssize_t n;
  int status;

  TEST("normal prompt proceeds after output");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *first;
    char *second;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 20;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    first = sl->readline(sl, "p> ");
    if (!first)
      _exit(3);
    (void)write(slave_fd, "out:", 4);
    (void)write(slave_fd, first, strlen(first));
    (void)write(slave_fd, "\r\n", 2);
    second = sl->readline(sl, "p> ");
    if (!second)
      _exit(4);
    (void)write(result_pipe[1], first, strlen(first));
    (void)write(result_pipe[1], "|", 1);
    (void)write(result_pipe[1], second, strlen(second));
    sl->free_string(sl, first);
    sl->free_string(sl, second);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "first prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "one\r", 4) == 4, "first submit failed");
  while (!contains_after_bytes(terminal, "out:one", "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "second prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "two\r", 4) == 4, "second submit failed");
  n = read_until_eof_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  close(master_fd);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "one|two") == 0, "sequential result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "out:one"),
              "output between prompts missing");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[1;"),
              "normal prompt used scroll region");
  PASS();
}

static void test_normal_wrapped_prompt_proceeds_after_output(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("normal wrapped prompt proceeds after output");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 80;
  ws.ws_row = 8;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "softline> ");
    if (!line)
      _exit(3);
    (void)write(slave_fd, "submitted: ", 11);
    (void)write(slave_fd, line, strlen(line));
    (void)write(slave_fd, "\r\n", 2);
    sl->free_string(sl, line);
    line = sl->readline(sl, "softline> ");
    if (!line)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "softline> ")) {
    n = read_some_with_timeout_ms(master_fd, buf, sizeof(buf), 20);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "hello world jspdi jsdip jfjpisd rjfsdpi fpisdmjfpisi "
          "jpdstjfjpisjdfpijj\r";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (!contains_after_bytes(terminal, "submitted:", "softline> ") &&
         tries < 300) {
    n = read_some_with_timeout_ms(master_fd, buf, sizeof(buf), 20);
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 8, 80);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "submitted: hello world jspdi jsdip"),
              "submitted output missing");
  ASSERT_TRUE(vt_contains(&screen, "softline>"),
              "next prompt missing after wrapped submit");
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "second submit failed");
  n = read_some_with_timeout(result_pipe[0], buf, sizeof(buf));
  ASSERT_TRUE(n > 0, "read result failed");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  PASS();
}

static void test_pty_history_navigation_restores_draft(void) {
  const char *history[2];
  char terminal[4096];
  char result[256];
  int status;

  history[0] = "first";
  history[1] = "second";
  TEST("history up recalls newest entry");
  ASSERT_TRUE(run_pty_history_case("\033[A\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "history case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "second") == 0, "newest history mismatch");
  PASS();

  TEST("history down restores edited draft");
  ASSERT_TRUE(run_pty_history_case("draft\033[A\033[B\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "history draft case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0, "draft restore mismatch");
  PASS();

  TEST("Ctrl-P and Ctrl-N navigate history entries");
  ASSERT_TRUE(run_pty_history_case("\020\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "Ctrl-P history case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "second") == 0, "Ctrl-P history mismatch");
  ASSERT_TRUE(run_pty_history_case("draft\020\016\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "Ctrl-N history case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0, "Ctrl-N draft restore mismatch");
  PASS();
}

static void test_pty_readline_does_not_auto_add_history(void) {
  char file[] = "/tmp/softline-history-no-auto-XXXXXX";
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  int fd;
  int status;
  char terminal[1024];
  char result[16];
  size_t terminal_len;
  ssize_t n;
  struct stat st;

  TEST("TTY readline does not auto-add submitted history");
  fd = mkstemp(file);
  ASSERT_TRUE(fd >= 0, "mkstemp failed");
  close(fd);
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    sl->free_string(sl, line);
    if (sl->history_save(sl, file) != SL_OK)
      _exit(4);
    (void)write(result_pipe[1], "S", 1);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt missing");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
    ASSERT_TRUE(terminal_len < sizeof(terminal) - 1, "terminal buffer full");
  }
  ASSERT_TRUE(write(master_fd, "secret\r", 7) == 7, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result));
  ASSERT_TRUE(n == 1, "child did not save history");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(stat(file, &st) == 0, "stat history file failed");
  unlink(file);
  ASSERT_TRUE(st.st_size == 0, "readline submission was auto-added to history");
  PASS();
}

static void test_ctrl_r_searches_memory_history(void) {
  const char *history[3];
  char terminal[8192];
  char result[256];
  int status;

  history[0] = "alpha deploy";
  history[1] = "beta build";
  history[2] = "gamma deploy";
  TEST("Ctrl-R searches in-memory history newest first");
  ASSERT_TRUE(run_pty_history_case("\022deploy\r", history, 3, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "gamma deploy") == 0,
              "newest matching history entry was not submitted");
  ASSERT_TRUE(contains_bytes(terminal, "(r-search)`deploy': "),
              "reverse search prompt missing");
  ASSERT_TRUE(contains_bytes(terminal, "gamma deploy"),
              "matching history entry was not rendered");
  PASS();
}

static void test_ctrl_r_repeats_and_wraps_matches(void) {
  const char *history[3];
  const char *single_history[2];
  char terminal[8192];
  char result[256];
  int status;

  history[0] = "alpha deploy";
  history[1] = "beta deploy";
  history[2] = "gamma deploy";
  TEST("Ctrl-R repeats cycle older matches and wrap");
  ASSERT_TRUE(run_pty_history_case("\022deploy\022\r", history, 3, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "repeat history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "repeat child editor failed");
  ASSERT_TRUE(strcmp(result, "beta deploy") == 0,
              "repeat did not select older match");

  ASSERT_TRUE(run_pty_history_case("\022deploy\022\022\022\r", history, 3,
                                   terminal, sizeof(terminal), result,
                                   sizeof(result), &status) == 0,
              "wrapped repeat history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "wrapped repeat child editor failed");
  ASSERT_TRUE(strcmp(result, "gamma deploy") == 0,
              "repeat did not wrap to newest match");

  single_history[0] = "alpha deploy";
  single_history[1] = "beta build";
  ASSERT_TRUE(run_pty_history_case("\022deploy\022\r", single_history, 2,
                                   terminal, sizeof(terminal), result,
                                   sizeof(result), &status) == 0,
              "single-match repeat history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "single-match repeat child editor failed");
  ASSERT_TRUE(strcmp(result, "alpha deploy") == 0,
              "single repeated match did not remain selected");
  PASS();
}

static void test_ctrl_r_no_match_and_cancel_restore_draft(void) {
  const char *history[2];
  char terminal[8192];
  char result[256];
  int status;

  history[0] = "alpha";
  history[1] = "beta";
  TEST("Ctrl-R no-match submit preserves draft");
  ASSERT_TRUE(run_pty_history_case("draft\022zzz\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "no-match history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "no-match child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0,
              "no-match search did not preserve draft");
  ASSERT_TRUE(contains_bytes(terminal, "(failed)`zzz': "),
              "failed reverse search prompt missing");
  PASS();

  TEST("Ctrl-R cancel restores edited draft");
  ASSERT_TRUE(run_pty_history_case("draft\022alpha\007\r", history, 2, terminal,
                                   sizeof(terminal), result, sizeof(result),
                                   &status) == 0,
              "cancel history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "cancel child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0, "cancel did not restore draft");
  PASS();
}

static void test_ctrl_r_searches_loaded_history_file(void) {
  char file[] = "/tmp/softline-history-search-XXXXXX";
  const char *history[1];
  char terminal[8192];
  char result[256];
  const char *data;
  int status;
  int fd;

  TEST("Ctrl-R searches loaded history file");
  fd = mkstemp(file);
  ASSERT_TRUE(fd >= 0, "mkstemp failed");
  data = "file alpha\nfile deploy\nhead\\ntail\n";
  ASSERT_TRUE(write(fd, data, strlen(data)) == (ssize_t)strlen(data),
              "write history file failed");
  close(fd);
  history[0] = "memory deploy";
  ASSERT_TRUE(run_pty_history_case_with_file("\022file\r", history, 1, file,
                                             terminal, sizeof(terminal), result,
                                             sizeof(result), &status) == 0,
              "loaded history search case failed");
  unlink(file);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "loaded history child editor failed");
  ASSERT_TRUE(strcmp(result, "file deploy") == 0,
              "loaded file history match mismatch");
  PASS();
}

static void test_ctrl_r_searches_unicode_and_multiline_history(void) {
  char file[] = "/tmp/softline-history-search-unicode-XXXXXX";
  char terminal[8192];
  char result[256];
  const char *data;
  int status;
  int fd;

  TEST("Ctrl-R searches Unicode and multiline history");
  fd = mkstemp(file);
  ASSERT_TRUE(fd >= 0, "mkstemp failed");
  data = "unicode \303\245\303\244\303\266\nhead\\ntail\n";
  ASSERT_TRUE(write(fd, data, strlen(data)) == (ssize_t)strlen(data),
              "write unicode history file failed");
  close(fd);
  ASSERT_TRUE(run_pty_history_case_with_file("\022\303\244\r", NULL, 0, file,
                                             terminal, sizeof(terminal), result,
                                             sizeof(result), &status) == 0,
              "unicode history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "unicode history child editor failed");
  ASSERT_TRUE(strcmp(result, "unicode \303\245\303\244\303\266") == 0,
              "unicode history match mismatch");

  ASSERT_TRUE(run_pty_history_case_with_file("\022tail\r", NULL, 0, file,
                                             terminal, sizeof(terminal), result,
                                             sizeof(result), &status) == 0,
              "multiline history search case failed");
  unlink(file);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "multiline history child editor failed");
  ASSERT_TRUE(strcmp(result, "head\ntail") == 0,
              "multiline history match mismatch");
  PASS();
}

static void test_ctrl_r_renders_inside_bounded_prompt(void) {
  const char *history[2];
  char terminal[8192];
  char result[256];
  int status;

  history[0] = "bounded alpha";
  history[1] = "bounded target";
  TEST("Ctrl-R renders inside bounded prompt");
  ASSERT_TRUE(run_pty_history_case_with_options(
                  "\022target\r", history, 2, NULL, 24, 5, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "bounded history search case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "bounded history child editor failed");
  ASSERT_TRUE(strcmp(result, "bounded target") == 0,
              "bounded history match mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[5;1H"),
              "bounded prompt did not render at bottom");
  ASSERT_TRUE(contains_bytes(terminal, "(r-search)`"),
              "bounded reverse search prompt prefix missing");
  ASSERT_TRUE(contains_bytes(terminal, "t': "),
              "bounded reverse search prompt update missing");
  PASS();
}

static void test_bracketed_paste_is_literal_content(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("bracketed paste treats control bytes as content");
  ASSERT_TRUE(run_pty_readline_case("pre\033[200~\tab\025\033[D\rcd\033[201~\r",
                                    20, 0, NULL, terminal, sizeof(terminal),
                                    result, sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "pre\tab\025\033[D\ncd") == 0,
              "paste result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[?2004h"),
              "bracketed paste was not enabled");
  ASSERT_TRUE(contains_bytes(terminal, "\033[?2004l"),
              "bracketed paste was not disabled");
  PASS();
}

static void test_bracketed_paste_stops_at_line_max(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("bracketed paste stops at configured line max");
  ASSERT_TRUE(run_pty_readline_case_with_limit(
                  "\033[200~abcdef\033[201~\r", 20, 0, 4, NULL, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abcd") == 0, "paste line max result mismatch");
  PASS();
}

static void test_tab_render_expands_beyond_line_bytes(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("tab rendering can exceed line byte count");
  ASSERT_TRUE(run_pty_readline_case_with_limit(
                  "\033[200~\t\033[201~\r", 1, 0, 4, NULL, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "\t") == 0, "tab result mismatch");
  PASS();
}

static void test_key_binding_tab_inserts_text(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding TAB inserts text");
  ASSERT_TRUE(run_pty_key_binding_case("a\tb\r", SL_KEY_TAB, insert_text_key,
                                       "COMP", terminal, sizeof(terminal),
                                       result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "aCOMPb") == 0, "TAB binding result mismatch");
  PASS();
}

static void test_prompt_queue_dispatches_fifo_with_themes(void) {
  static const sl_prompt_theme_t stash_themes[] = {
      SL_PROMPT_THEME_DRACULA,    SL_PROMPT_THEME_GRUVBOX,
      SL_PROMPT_THEME_MONOCHROME, SL_PROMPT_THEME_MONOGREEN,
      SL_PROMPT_THEME_OUTRUN,     SL_PROMPT_THEME_SYNTHWAVE};
  static const char *const control_styles[] = {
      "\033[38;2;98;114;164mQ ",  "\033[38;2;131;165;152mQ ",
      "\033[38;2;125;110;72mQ ",  "\033[38;2;42;107;58mQ ",
      "\033[38;2;122;107;143mQ ", "\033[38;2;130;120;156mQ "};
  static const char *const text_styles[] = {
      "\033[38;2;195;183;201mfirst", "\033[38;2;213;196;161mfirst",
      "\033[38;2;169;152;101mfirst", "\033[38;2;95;158;111mfirst",
      "\033[38;2;184;169;201mfirst", "\033[38;2;179;169;192mfirst"};
  char terminal[16384];
  char result[512];
  int status;
  size_t i;

  TEST("prompt queue previews and dispatches FIFO across themes");
  ASSERT_TRUE(run_pty_prompt_queue_case("first\tsecond\tthird\r",
                                        SL_PROMPT_THEME_PLAIN, 1, 0, 3,
                                        terminal, sizeof(terminal), result,
                                        sizeof(result), &status) == 0,
              "plain prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "plain prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:third|2:first|2:second") == 0,
              "plain prompt queue dispatch order mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "Q 1. first"),
              "plain FIFO preview missing");
  ASSERT_TRUE(contains_bytes(terminal, "... 1 more"),
              "plain overflow count missing");

  ASSERT_TRUE(run_pty_prompt_queue_case(
                  "first\tsecond\r", SL_PROMPT_THEME_ACCENT, 1, 0, 2, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "accent prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "accent prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
              "accent prompt queue dispatch mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;6;182;212mQ ") &&
                  contains_bytes(terminal, "\033[38;2;148;163;184mfirst") &&
                  contains_bytes(terminal, "\033[1;36mchat> \033[0m"),
              "accent full prompt treatment missing");

  ASSERT_TRUE(run_pty_prompt_queue_case(
                  "first\tsecond\r", SL_PROMPT_THEME_RICED, 1, 0, 2, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "riced prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "riced prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
              "riced prompt queue dispatch mismatch");
  ASSERT_TRUE(
      contains_bytes(terminal, "\033[38;2;255;126;219mQ ") &&
          contains_bytes(terminal, "\033[38;2;172;164;184mfirst") &&
          contains_bytes(terminal, "\033[1;38;2;255;255;255mchat> \033[0m"),
      "riced full prompt treatment missing");
  ASSERT_TRUE(run_pty_prompt_queue_case(
                  "first\tsecond\r", SL_PROMPT_THEME_DEFAULT, 1, 0, 2, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "default prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "default prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
              "default prompt queue dispatch mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[90mQ ") &&
                  contains_bytes(terminal, "\033[90mfirst") &&
                  contains_bytes(terminal, "\033[1;97mchat> \033[0m") &&
                  !contains_bytes(terminal, "\033[38;2;"),
              "default ANSI queue treatment missing");
  for (i = 0; i < sizeof(stash_themes) / sizeof(stash_themes[0]); i++) {
    ASSERT_TRUE(run_pty_prompt_queue_case("first\tsecond\r", stash_themes[i], 1,
                                          0, 2, terminal, sizeof(terminal),
                                          result, sizeof(result), &status) == 0,
                "stash theme prompt queue pty case failed");
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "stash theme prompt queue child failed");
    ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
                "stash theme prompt queue dispatch mismatch");
    ASSERT_TRUE(contains_bytes(terminal, control_styles[i]) &&
                    contains_bytes(terminal, text_styles[i]),
                "stash theme queue palette treatment missing");
  }
  PASS();
}

static void test_prompt_queue_dispatches_fifo_in_normal_scrollback(void) {
  char terminal[16384];
  char result[512];
  int status;

  TEST("prompt queue dispatches FIFO in normal scrollback");
  ASSERT_TRUE(run_pty_prompt_queue_case(
                  "first\tsecond\r", SL_PROMPT_THEME_ACCENT, 0, 0, 2, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "normal prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "normal prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
              "normal prompt queue dispatch mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;6;182;212mQ ") &&
                  contains_bytes(terminal, "\033[1;36mchat> \033[0m"),
              "normal prompt queue preview missing");
  PASS();
}

static void test_prompt_queue_rejects_reduced_capacity(void) {
  char terminal[16384];
  char result[512];
  int status;

  TEST("prompt queue rejects reduced capacity with pending prompts");
  ASSERT_TRUE(run_pty_prompt_queue_case("first\tsecond\tthird",
                                        SL_PROMPT_THEME_PLAIN, 0, 1, 3,
                                        terminal, sizeof(terminal), result,
                                        sizeof(result), &status) == 0,
              "queue capacity pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "queue capacity child failed");
  ASSERT_TRUE(strcmp(result, "-2;1:third|2:first|2:second") == 0,
              "reduced capacity changed queued dispatch");
  PASS();
}

static void test_prompt_queue_alt_e_recalls_newest(void) {
  char terminal[16384];
  char result[512];
  int status;

  TEST("Alt-E recalls the newest queued prompt into the editor");
  ASSERT_TRUE(run_pty_prompt_queue_case("first\tsecond\t\033e\r",
                                        SL_PROMPT_THEME_PLAIN, 1, 0, 2,
                                        terminal, sizeof(terminal), result,
                                        sizeof(result), &status) == 0,
              "Alt-E prompt queue pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "Alt-E prompt queue child failed");
  ASSERT_TRUE(strcmp(result, "1:second|2:first") == 0,
              "Alt-E did not restore queue tail");
  ASSERT_TRUE(contains_bytes(terminal, "Q 1. first"),
              "Alt-E did not redraw the remaining queued preview");
  PASS();
}

static void test_prompt_themes_style_normal_readline(void) {
  static const sl_prompt_theme_t themes[] = {
      SL_PROMPT_THEME_DEFAULT,    SL_PROMPT_THEME_ACCENT,
      SL_PROMPT_THEME_DRACULA,    SL_PROMPT_THEME_GRUVBOX,
      SL_PROMPT_THEME_MONOCHROME, SL_PROMPT_THEME_MONOGREEN,
      SL_PROMPT_THEME_OUTRUN,     SL_PROMPT_THEME_RICED,
      SL_PROMPT_THEME_SYNTHWAVE};
  static const char *const styles[] = {"\033[1;97mnormal> ",
                                       "\033[1;36mnormal> ",
                                       "\033[38;2;98;114;164mnormal> ",
                                       "\033[1;38;2;184;187;38mnormal> ",
                                       "\033[1;38;2;168;118;40mnormal> ",
                                       "\033[1;38;2;22;122;31mnormal> ",
                                       "\033[38;2;78;69;99mnormal> ",
                                       "\033[1;38;2;255;255;255mnormal> ",
                                       "\033[1;38;2;255;126;219mnormal> "};
  static const char *const input_styles[] = {"",
                                             "",
                                             "",
                                             "",
                                             "\033[38;2;255;224;138mok\033[0m",
                                             "\033[1;38;2;51;255;51mok\033[0m",
                                             "",
                                             "",
                                             ""};
  char terminal[4096];
  char result[64];
  char styled_prompt[128];
  int status;
  size_t i;

  TEST("prompt themes style normal readline UI");
  for (i = 0; i < sizeof(themes) / sizeof(themes[0]); i++) {
    ASSERT_TRUE(run_pty_themed_readline_case(themes[i], terminal,
                                             sizeof(terminal), result,
                                             sizeof(result), &status) == 0,
                "themed normal prompt pty case failed");
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "themed normal prompt child failed");
    ASSERT_TRUE(strcmp(result, "ok") == 0,
                "themed normal prompt result mismatch");
    ASSERT_TRUE(contains_bytes(terminal, styles[i]),
                "themed normal prompt treatment missing");
    (void)snprintf(styled_prompt, sizeof(styled_prompt), "%s\033[0m",
                   styles[i]);
    ASSERT_TRUE(contains_bytes(terminal, styled_prompt),
                "themed prompt did not reset before typed input");
    if (themes[i] == SL_PROMPT_THEME_DEFAULT)
      ASSERT_TRUE(!contains_bytes(terminal, "\033[38;2;"),
                  "default prompt used true-colour output");
    if (input_styles[i][0] != '\0')
      ASSERT_TRUE(contains_bytes(terminal, input_styles[i]),
                  "themed input treatment missing");
  }
  PASS();
}

static void test_statusline_uses_palette_offset_and_truncation(void) {
  char terminal[16384];
  char result[64];
  int status;

  TEST("status line offsets colours and truncates after 32 elements");
  ASSERT_TRUE(run_pty_statusline_case(SL_PROMPT_THEME_RICED, terminal,
                                      sizeof(terminal), result, sizeof(result),
                                      &status) == 0,
              "status line pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "status line child failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "status line result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;255;51;51m/ "),
              "status spinner marker missing");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;255;51;51m- "),
              "status spinner did not advance");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;57;255;20m+ "),
              "default status idle marker missing");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;57;255;20m- "),
              "status idle marker missing");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;172;164;184me0"),
              "status starting palette colour missing");
  ASSERT_TRUE(contains_bytes(terminal, "\033[38;2;54;249;246me1"),
              "status palette did not wrap after index seven");
  ASSERT_TRUE(contains_bytes(terminal, "e30") &&
                  !contains_bytes(terminal, "e31") &&
                  contains_bytes(terminal, "..."),
              "status line did not retain 31 elements plus ellipsis");
  ASSERT_TRUE(contains_after_bytes(terminal, "e7", "\n"),
              "status elements did not wrap between elements");
  PASS();
}

static void test_default_statusline_uses_ansi_palette(void) {
  char terminal[16384];
  char result[64];
  int status;

  TEST("default status line uses standard ANSI palette");
  ASSERT_TRUE(run_pty_statusline_case(SL_PROMPT_THEME_DEFAULT, terminal,
                                      sizeof(terminal), result, sizeof(result),
                                      &status) == 0,
              "default status line pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "default status line child failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "default status line result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[31m/ ") &&
                  contains_bytes(terminal, "\033[31m- ") &&
                  contains_bytes(terminal, "\033[32m+ ") &&
                  contains_bytes(terminal, "\033[32m- "),
              "default status markers did not use ANSI red and green");
  ASSERT_TRUE(contains_bytes(terminal, "\033[97me0") &&
                  contains_bytes(terminal, "\033[36me1") &&
                  contains_bytes(terminal, "\033[90m : ") &&
                  !contains_bytes(terminal, "\033[38;2;"),
              "default status palette did not use ANSI colours");
  PASS();
}

static void test_key_binding_alt_m_inserts_text(void) {
  char terminal[4096];
  char result[256];
  int status;
  sl_key_t documented_alt_m;

  TEST("key binding Alt-M inserts text");
  documented_alt_m = (sl_key_t)(SL_KEY_ALT_BASE + (unsigned char)'m');
  ASSERT_TRUE(SL_KEY_ALT_M == documented_alt_m,
              "SL_KEY_ALT_M does not match documented Alt-letter encoding");
  ASSERT_TRUE(run_pty_key_binding_case(
                  "\033m\r", documented_alt_m, insert_text_key, "ALT", terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ALT") == 0, "Alt-M binding result mismatch");
  PASS();
}

static void test_key_binding_f1_submits(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding F1 submits");
  ASSERT_TRUE(run_pty_key_binding_case(
                  "draft\033OP", SL_KEY_F1, submit_action_key, NULL, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0, "F1 submit result mismatch");
  PASS();
}

static void test_key_binding_can_edit_buffer_and_submit(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding can edit buffer and submit");
  ASSERT_TRUE(run_pty_key_binding_case("\033OQ", SL_KEY_F2, edit_buffer_key,
                                       NULL, terminal, sizeof(terminal), result,
                                       sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abXXcd") == 0, "edit binding result mismatch");
  PASS();
}

static void test_key_binding_can_cancel(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding can cancel");
  ASSERT_TRUE(run_pty_key_binding_case(
                  "draft\033OR", SL_KEY_F3, cancel_action_key, NULL, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "<NULL>") == 0, "cancel result mismatch");
  PASS();
}

static void test_key_binding_enter_can_pass_to_default(void) {
  char terminal[4096];
  char result[256];
  int status;
  struct enter_override_state state;

  TEST("key binding Enter can pass to default");
  state.calls = 0;
  ASSERT_TRUE(run_pty_key_binding_case(
                  "a\rb\r", SL_KEY_ENTER, enter_override_key, &state, terminal,
                  sizeof(terminal), result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "a\nb") == 0, "Enter binding result mismatch");
  PASS();
}

static void test_key_binding_ctrl_enter_can_call_submit(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding Ctrl-Enter can call submit");
  ASSERT_TRUE(run_pty_key_binding_case("draft\033[13;5u", SL_KEY_CTRL_ENTER,
                                       submit_method_key, NULL, terminal,
                                       sizeof(terminal), result, sizeof(result),
                                       &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "draft") == 0,
              "Ctrl-Enter submit result mismatch");
  PASS();
}

static void test_key_binding_ctrl_r_overrides_history_search(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("key binding Ctrl-R overrides history search");
  ASSERT_TRUE(run_pty_key_binding_case("\022\r", SL_KEY_CTRL_R, insert_text_key,
                                       "BOUND", terminal, sizeof(terminal),
                                       result, sizeof(result), &status) == 0,
              "key binding case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "BOUND") == 0, "Ctrl-R binding result mismatch");
  ASSERT_TRUE(!contains_bytes(terminal, "(r-search)"),
              "built-in reverse search ran despite binding");
  PASS();
}

static void test_terminal_mode_is_restored(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char terminal[4096];
  char result[256];
  size_t terminal_len;
  ssize_t n;
  int status;

  TEST("readline restores terminal mode before returning");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct termios before;
    struct termios after;
    int restored;
    close(master_fd);
    close(result_pipe[0]);
    if (tcgetattr(slave_fd, &before) != 0)
      _exit(2);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(3);
    line = sl->readline(sl, "p> ");
    restored = tcgetattr(slave_fd, &after) == 0 &&
               termios_same_observable(&before, &after);
    if (line) {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    if (restored)
      (void)write(result_pipe[1], "|RESTORED", 9);
    else
      (void)write(result_pipe[1], "|RAW", 4);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  n = read_until_eof_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok|RESTORED") == 0,
              "terminal mode was not restored");
  PASS();
}

static void test_non_ctrl_c_signal_does_not_interrupt_readline(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  int ack_pipe[2];
  pid_t pid;
  char terminal[4096];
  char result[256];
  char ack;
  size_t terminal_len;
  size_t result_len;
  ssize_t n;
  int status;
  int tries;

  TEST("non-Ctrl-C signal does not interrupt readline");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  ASSERT_TRUE(pipe(ack_pipe) == 0, "ack pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct sigaction sa;
    close(master_fd);
    close(result_pipe[0]);
    close(ack_pipe[0]);
    signal_ack_fd = ack_pipe[1];
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = remember_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) != 0)
      _exit(2);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      (void)write(result_pipe[1], "<NULL>", 6);
    else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    if (signal_seen)
      (void)write(result_pipe[1], "|SIG", 4);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    close(ack_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  close(ack_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(kill(pid, SIGUSR1) == 0, "signal failed");
  n = read_some_with_timeout(ack_pipe[0], &ack, 1);
  ASSERT_TRUE(n == 1, "signal handler did not run");
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  result_len = 0;
  result[0] = '\0';
  tries = 0;
  while (!contains_bytes(result, "|SIG") && tries < 250) {
    n = read_some_with_timeout_ms(result_pipe[0], result + result_len,
                                  sizeof(result) - 1 - result_len, 20);
    if (n > 0) {
      result_len += (size_t)n;
      result[result_len] = '\0';
    }
    tries++;
  }
  ASSERT_TRUE(result_len > 0, "read result failed");
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  close(master_fd);
  close(ack_pipe[0]);
  close(result_pipe[0]);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok|SIG") == 0, "signal interrupted readline");
  PASS();
}

static void test_pty_ctrl_d_exits(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("pty Ctrl-D on empty prompt returns NULL");
  ASSERT_TRUE(run_pty_readline_case("\004", 20, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "<NULL>") == 0, "Ctrl-D result mismatch");
  PASS();
}

static void test_pty_multiline_deletes_are_buffer_wide(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("Ctrl-U deletes from buffer start across lines");
  ASSERT_TRUE(run_pty_readline_case("ab\ncd\025X\r", 20, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "X") == 0, "Ctrl-U result mismatch");
  PASS();

  TEST("Ctrl-K deletes to buffer end across lines");
  ASSERT_TRUE(run_pty_readline_case("ab\ncd\001\013Y\r", 20, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "Y") == 0, "Ctrl-K result mismatch");
  PASS();
}

static void test_bounded_print_above_uses_scroll_region(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("bounded prompt prints output through scroll region");
  ASSERT_TRUE(run_pty_readline_case("ok\r", 20, 5, "hello\n", terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "bounded result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[1;4r"), "scroll region not set");
  ASSERT_TRUE(contains_bytes(terminal, "\033[r"), "scroll region not reset");
  PASS();
}

static void test_bounded_print_above_rejects_narrow_scroll(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[256];
  char result[64];
  ssize_t n;
  int status;
  int rc;

  TEST("bounded print_above rejects narrow scroll region");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char msg[32];
    int written;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->set_bounds(sl, 5, 0, 10, 5) != SL_OK)
      _exit(3);
    rc = sl->print_above(sl, one_chunk_stream, "hello\n");
    written = snprintf(msg, sizeof(msg), "%d", rc);
    if (written > 0)
      (void)write(result_pipe[1], msg, (size_t)written);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  n = read_until_eof_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  terminal[0] = '\0';
  n = read_some_with_timeout_ms(master_fd, terminal, sizeof(terminal) - 1, 20);
  if (n > 0)
    terminal[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "-2") == 0, "narrow print_above did not fail");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[1;4r"),
              "narrow print_above emitted full-row scroll region");
  PASS();
}

static void test_bounded_prompt_starts_at_bottom(void) {
  char terminal[4096];
  char result[256];
  struct vt_screen screen;
  int status;

  TEST("bounded prompt starts on bottom row");
  ASSERT_TRUE(run_pty_readline_case("ok\r", 20, 5, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "bounded result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[5;1Hp> "),
              "prompt was not rendered on bottom row");
  vt_init(&screen, 5, 20);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> ok"),
              "bounded prompt did not retain submitted text");
  ASSERT_TRUE(contains_bytes(terminal, "\033[6;1H"),
              "cursor was not positioned below the bounded prompt");
  PASS();
}

static void test_bounded_readline_leaves_output_below_prompt(void) {
  char terminal[4096];
  char result[256];
  struct vt_screen screen;
  int status;

  TEST("bounded readline leaves following output below prompt");
  ASSERT_TRUE(run_pty_readline_completion_case(1, 0, terminal, sizeof(terminal),
                                               result, sizeof(result),
                                               &status) == 0,
              "bounded completion pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "bounded completion child failed");
  ASSERT_TRUE(strcmp(result, "hello") == 0,
              "bounded completion result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[4;1H"),
              "following output was not positioned below bounded prompt");
  vt_init(&screen, 5, 20);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> hello"),
              "bounded prompt was overwritten by following output");
  ASSERT_TRUE(vt_contains(&screen, "after"), "following output missing");
  PASS();
}

static void test_readline_keeps_normal_completion_with_queueing(void) {
  char terminal[4096];
  char result[256];
  struct vt_screen screen;
  int status;

  TEST("readline retains completion output when queueing is enabled");
  ASSERT_TRUE(run_pty_readline_completion_case(0, 1, terminal, sizeof(terminal),
                                               result, sizeof(result),
                                               &status) == 0,
              "queued readline completion pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "queued readline completion child failed");
  ASSERT_TRUE(strcmp(result, "hello") == 0,
              "queued readline completion result mismatch");
  vt_init(&screen, 5, 20);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> hello"),
              "queued direct readline prompt was cleared");
  ASSERT_TRUE(vt_contains(&screen, "after"),
              "following output missing after queued direct readline");
  PASS();
}

static void test_config_zero_bounds_start_at_terminal_bottom(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[4096];
  char result[256];
  size_t terminal_len;
  ssize_t n;
  int status;

  TEST("zero config bounds start on terminal bottom row");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_x = 0;
    cfg.screen_y = 0;
    cfg.screen_width = 0;
    cfg.screen_height = 0;
    cfg.bounded = 1;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(contains_bytes(terminal, "\033[5;1Hp> "),
              "prompt was not rendered on terminal bottom row");
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "bounded result mismatch");
  PASS();
}

static void test_dynamic_bounds_origin_uses_remaining_terminal_area(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  struct vt_screen screen;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;
  int saw_first;
  int saw_second;
  int saw_explicit_continuation;
  int saw_offscreen_cursor;

  TEST("dynamic bounds origin uses remaining terminal area");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (ioctl(slave_fd, TIOCSWINSZ, &ws) != 0)
      _exit(3);
    if (sl->set_bounds(sl, 10, 2, 0, 0) != SL_OK)
      _exit(4);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(5);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(contains_bytes(terminal, "\033[5;11Hp> "),
              "prompt was not rendered at dynamic origin bottom");
  ASSERT_TRUE(write(master_fd, "abcdefg", 7) == 7, "write first input failed");
  saw_first = 0;
  saw_second = 0;
  saw_explicit_continuation = 0;
  saw_offscreen_cursor = 0;
  tries = 0;
  while (tries < 80 && !saw_first) {
    vt_init(&screen, 5, 20);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> abcdefg") || vt_contains(&screen, "p> abc"))
      saw_first = 1;
    if (contains_bytes(terminal, "\033[4;24H"))
      saw_offscreen_cursor = 1;
    n = read_some_with_timeout_ms(master_fd, terminal + terminal_len,
                                  sizeof(terminal) - 1 - terminal_len, 25);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
    tries++;
  }
  ASSERT_TRUE(write(master_fd, "hij", 3) == 3,
              "write continuation input failed");
  tries = 0;
  while (tries < 80 && !saw_second) {
    vt_init(&screen, 5, 20);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "hij"))
      saw_second = 1;
    if (contains_bytes(terminal, "\033[4;11H   ") ||
        contains_bytes(terminal, "\033[5;11H   "))
      saw_explicit_continuation = 1;
    if (contains_bytes(terminal, "\033[4;24H"))
      saw_offscreen_cursor = 1;
    n = read_some_with_timeout_ms(master_fd, terminal + terminal_len,
                                  sizeof(terminal) - 1 - terminal_len, 25);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
    tries++;
  }
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < sizeof(terminal) - 1);
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abcdefghij") == 0, "bounded result mismatch");
  ASSERT_TRUE(saw_first, "dynamic width did not use remaining columns");
  ASSERT_TRUE(saw_second, "dynamic wrap did not stay inside remaining columns");
  ASSERT_TRUE(saw_explicit_continuation,
              "dynamic wrap did not render an explicit continuation row");
  ASSERT_TRUE(!saw_offscreen_cursor, "cursor moved past terminal edge");
  PASS();
}

static void test_bounded_redraw_clears_only_box_width(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  size_t terminal_len;
  ssize_t n;
  int status;

  TEST("bounded redraw clears only configured width");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_x = 5;
    cfg.screen_y = 1;
    cfg.screen_width = 10;
    cfg.screen_height = 3;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "abcdef\177\177\177\r", 10) == 10,
              "write input failed");
  n = read_some_with_timeout_ms(result_pipe[0], result, sizeof(result) - 1,
                                5000);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    if (n > 0) {
      terminal_len += (size_t)n;
      terminal[terminal_len] = '\0';
    }
  } while (n > 0 && terminal_len < sizeof(terminal) - 1);
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abc") == 0, "bounded redraw result mismatch");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[0K"),
              "bounded redraw used erase-to-end-of-line");
  PASS();
}

static void test_bounded_wrapped_shrink_clears_continuation_tail(void) {
  char terminal[8192];
  char result[256];
  struct vt_screen screen;
  int status;

  TEST("bounded wrapped shrink clears continuation row tail");
  ASSERT_TRUE(run_pty_readline_case("abc defghijklm\177\177\177\177\r", 12, 3,
                                    NULL, terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abc defghi") == 0,
              "bounded wrapped shrink result mismatch");
  vt_init(&screen, 5, 12);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> abc"), "first wrapped row missing");
  if (!vt_contains(&screen, "   defghi  "))
    vt_dump(&screen);
  ASSERT_TRUE(vt_contains(&screen, "   defghi  "),
              "continuation row tail was not cleared");
  ASSERT_TRUE(!vt_contains(&screen, "   defghij"),
              "continuation row kept stale deleted bytes");
  PASS();
}

static void test_bounded_viewport_follows_cursor(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  char buf[512];
  char result[256];
  struct vt_screen screen;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("bounded viewport follows cursor above bottom page");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 12;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 12;
    cfg.screen_height = 3;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }

  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout_ms(master_fd, buf, sizeof(buf), 20);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  ASSERT_TRUE(write(master_fd, "one two three four five six", 27) == 27,
              "write input failed");
  tries = 0;
  while (!contains_bytes(terminal, "six") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  ASSERT_TRUE(write(master_fd, "\033[A\033[A\033[A", 9) == 9,
              "cursor-up input failed");
  tries = 0;
  do {
    vt_init(&screen, 5, 12);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> one two"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  } while (tries < 300);
  vt_init(&screen, 5, 12);
  vt_apply(&screen, terminal);
  if (!vt_contains(&screen, "p> one two"))
    vt_dump(&screen);
  ASSERT_TRUE(vt_contains(&screen, "p> one two"),
              "bounded viewport did not reveal cursor row");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "one two three four five six") == 0,
              "bounded viewport result mismatch");
  PASS();
}

static void test_print_above_pulls_stream_chunks(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char terminal[4096];
  char result[256];
  size_t terminal_len;
  ssize_t n;
  int status;

  TEST("print_above pulls streamed chunks");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct text_stream_state stream;
    char calls[32];
    int written;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 20;
    cfg.screen_height = 5;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    stream.chunks[0] = "alpha";
    stream.chunks[1] = "-";
    stream.chunks[2] = "beta\n";
    stream.chunks[3] = NULL;
    stream.index = 0;
    stream.calls = 0;
    if (sl->print_above(sl, next_text_chunk, &stream) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (line)
      sl->free_string(sl, line);
    written = snprintf(calls, sizeof(calls), "%d", stream.calls);
    if (written > 0)
      (void)write(result_pipe[1], calls, (size_t)written);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "4") == 0, "stream callback count mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "alpha-beta"),
              "streamed chunks were not written");
  PASS();
}

static void test_bounded_prompt_growth_scrolls_output_region(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("bounded prompt growth scrolls output region");
  ASSERT_TRUE(run_pty_readline_case("hello world sentence\r", 16, 5, "hello\n",
                                    terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "hello world sentence") == 0,
              "bounded growth result mismatch");
  ASSERT_TRUE(count_bytes(terminal, "\033[1;4r") >= 2,
              "prompt growth did not scroll output region");
  PASS();
}

static void test_bounded_print_above_does_not_overwrite_prompt(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("bounded print_above does not overwrite active prompt");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 6;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_count_print_state state;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->set_bounds(sl, 0, 0, 0, 0) != SL_OK)
      _exit(3);
    state.calls = 0;
    state.printed = 0;
    state.text = "streamed output one\nstreamed output two\n"
                 "streamed output three\n";
    if (sl->set_idle_callback(sl, idle_print_after_two_ticks, &state) != SL_OK)
      _exit(4);
    line = sl->readline(sl, "chat> ");
    if (!line)
      _exit(5);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "chat> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "hello world jspdi jsdip jfjpisd rjfsdpi fpisdmjfpisi";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (!contains_bytes(terminal, "fpisdmjfpisi") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  tries = 0;
  while (!contains_bytes(terminal, "streamed output three") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  tries = 0;
  do {
    vt_init(&screen, 6, 40);
    vt_apply(&screen, terminal);
    if (screen.row == 5 && screen.col == 26)
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  } while (tries < 300);
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
  if (!vt_contains(&screen, "chat> hello world jspdi jsdip jfjpisd") ||
      !vt_contains(&screen, "      rjfsdpi fpisdmjfpisi"))
    vt_dump(&screen);
  ASSERT_TRUE(vt_contains(&screen, "streamed output three"),
              "streamed output missing from visible screen");
  ASSERT_TRUE(vt_contains(&screen, "chat> hello world jspdi jsdip jfjpisd"),
              "prompt first row was overwritten");
  ASSERT_TRUE(vt_contains(&screen, "      rjfsdpi fpisdmjfpisi"),
              "prompt continuation row was overwritten");
  if (screen.row != 5 || screen.col != 26)
    vt_dump(&screen);
  ASSERT_TRUE(screen.row == 5 && screen.col == 26,
              "bounded print_above did not restore prompt cursor");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], buf, sizeof(buf));
  ASSERT_TRUE(n > 0, "read result failed");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  PASS();
}

static void test_word_wrap_keeps_words_intact(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("word wrap reflows at word boundaries");
  ASSERT_TRUE(run_pty_readline_case("hello world sentence\r", 16, 0, NULL,
                                    terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "hello world sentence") == 0,
              "word wrap result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "p> ") &&
                  contains_bytes(terminal, "hello world"),
              "first wrapped row missing");
  ASSERT_TRUE(contains_bytes(terminal, "   s") &&
                  contains_bytes(terminal, "entence"),
              "continuation word row missing");
  PASS();
}

static void test_word_wrap_cursor_at_skipped_space(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("word wrap preserves cursor at skipped space");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 12;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "word nextword\033[D\033[D\033[D\033[D\033[D"
          "\033[D\033[D\033[D\033[D";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (!contains_bytes(terminal, "   nextword") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  for (tries = 0; tries < 300; tries++) {
    vt_init(&screen, 5, 12);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> word") &&
        vt_contains(&screen, "   nextword") && screen.row == 1 &&
        screen.col == 3)
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  vt_init(&screen, 5, 12);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> word"), "first wrapped row missing");
  ASSERT_TRUE(vt_contains(&screen, "   nextword"),
              "continuation wrapped row missing");
  ASSERT_TRUE(screen.row == 1, "cursor row did not stay on wrapped row");
  ASSERT_TRUE(screen.col == 3, "cursor column did not stay at wrap boundary");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], buf, sizeof(buf));
  ASSERT_TRUE(n > 0, "read result failed");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  PASS();
}

static void test_active_word_wrap_does_not_duplicate_prompt(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("active word wrap does not duplicate prompt");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 80;
  ws.ws_row = 8;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "softline> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "softline> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "hello world jspdi jsdip jfjpisd rjfsdpi fpisdmjfpisi "
          "jpdstjfjpisjdfpijj";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (tries < 300) {
    vt_init(&screen, 8, 80);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "          jpdstjfjpisjdfpijj"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 8, 80);
  vt_apply(&screen, terminal);
  if (!vt_contains(&screen, "          jpdstjfjpisjdfpijj"))
    vt_dump(&screen);
  ASSERT_TRUE(vt_count(&screen, "softline>") == 1,
              "active wrap duplicated prompt");
  ASSERT_TRUE(vt_contains(&screen, "softline> hello world jspdi jsdip"),
              "first prompt row missing");
  ASSERT_TRUE(vt_contains(&screen, "          jpdstjfjpisjdfpijj"),
              "continuation row missing prompt indent");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], buf, sizeof(buf));
  ASSERT_TRUE(n > 0, "read result failed");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  PASS();
}

static void test_active_exact_width_row_does_not_autowrap_prompt(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[16384];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("active exact-width row does not autowrap prompt");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 6;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "softline> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "softline> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "aaaaa bbbbb ccccc ddddd eeeeee fffff ggggg";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (!contains_bytes(terminal, "ggggg") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_count(&screen, "softline>") == 1,
              "exact-width render duplicated prompt");
  ASSERT_TRUE(vt_contains(&screen, "softline> aaaaa bbbbb ccccc ddddd eeeeee"),
              "first exact-width row missing");
  ASSERT_TRUE(vt_contains(&screen, "          fffff ggggg"),
              "continuation after exact-width row missing");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], buf, sizeof(buf));
  ASSERT_TRUE(n > 0, "read result failed");
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  PASS();
}

static void test_normal_prompt_wraps_at_bottom_with_long_prompt(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  char buf[512];
  const char *input;
  struct vt_screen screen;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("normal prompt wraps at bottom with long prompt");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 6;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    if (write(slave_fd, "1\r\n2\r\n3\r\n4\r\n5\r\n", 15) != 15)
      _exit(2);
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(3);
    line = sl->readline(sl, "softline> ");
    if (!line)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "softline> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "dj jpsidjfpisj sdpijfijfa pidspifjxjp nextword\r";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (!contains_bytes(terminal, "pidspifjxjp nextword") && tries < 300) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(
      strcmp(result, "dj jpsidjfpisj sdpijfijfa pidspifjxjp nextword") == 0,
      "wrapped prompt result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "softline> dj jpsidjfpisj sdpijfijfa"),
              "first prompt row missing");
  ASSERT_TRUE(contains_bytes(terminal, "pidspifjxjp nextword"),
              "continuation word run missing");
  ASSERT_TRUE(!contains_bytes(terminal, "sdpijfijfa pidsp"),
              "word was split across wrap");
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "softline> dj jpsidjfpisj sdpijfijfa"),
              "final screen first prompt row missing");
  ASSERT_TRUE(vt_contains(&screen, "          pidspifjxjp nextword"),
              "final screen continuation row missing");
  ASSERT_TRUE(!vt_contains(&screen, "sdpijfijfa pidsp"),
              "final screen split a word across wrap");
  PASS();
}

static void test_normal_prompt_growth_scrolls_at_screen_bottom(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  char buf[512];
  struct vt_screen screen;
  const char *input;
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("normal prompt growth scrolls at screen bottom");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 16;
  ws.ws_row = 3;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    if (write(slave_fd, "one\r\ntwo\r\n", 10) != 10)
      _exit(2);
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  input = "hello world sentence";
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  tries = 0;
  while (tries < 300) {
    vt_init(&screen, 3, 16);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> hello world") &&
        vt_contains(&screen, "   sentence"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 3, 16);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world"),
              "bottom growth first row missing");
  ASSERT_TRUE(vt_contains(&screen, "   sentence"),
              "bottom growth continuation row missing");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "hello world sentence") == 0,
              "bottom growth result mismatch");
  PASS();
}

static void test_exact_width_cursor_uses_explicit_wrap_row(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("exact-width cursor uses explicit wrap row");
  ASSERT_TRUE(run_pty_readline_case("abcdefghijklm\r", 16, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "abcdefghijklm") == 0,
              "exact-width result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "p> ") &&
                  contains_bytes(terminal, "abcdefghijklm"),
              "normal exact-width first row missing");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[16C"),
              "cursor was positioned past terminal edge");
  PASS();

  TEST("bounded exact-width cursor uses explicit wrap row");
  ASSERT_TRUE(run_pty_readline_case("abcdefghijklm\r", 16, 5, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "bounded pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "bounded child editor failed");
  ASSERT_TRUE(strcmp(result, "abcdefghijklm") == 0,
              "bounded exact-width result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[4;1Hp> ") &&
                  contains_bytes(terminal, "abcdefghijklm"),
              "bounded exact-width first row missing");
  ASSERT_TRUE(contains_bytes(terminal, "\033[5;1H   "),
              "bounded exact-width wrap row missing");
  PASS();
}

static void test_resize_reflows_without_keypress(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  char buf[512];
  struct vt_screen screen;
  size_t terminal_len;
  size_t resize_offset;
  ssize_t n;
  int status;
  int tries;

  TEST("resize reflows while readline is idle");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 20;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 0;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  ASSERT_TRUE(write(master_fd, "hello world sentence", 20) == 20,
              "write input failed");
  tries = 0;
  while (tries < 100) {
    vt_init(&screen, 20, 40);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> hello world sentence"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 20, 40);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world sentence"),
              "wide render missing");
  ws.ws_col = 16;
  ws.ws_row = 20;
  resize_offset = terminal_len;
  ASSERT_TRUE(ioctl(master_fd, TIOCSWINSZ, &ws) == 0, "resize ioctl failed");
  tries = 0;
  while (tries < 100) {
    vt_init(&screen, 20, 16);
    vt_apply(&screen, terminal + resize_offset);
    if (vt_contains(&screen, "p> hello world") &&
        vt_contains(&screen, "   sentence"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 20, 16);
  vt_apply(&screen, terminal + resize_offset);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world"),
              "idle resize did not reflow before input");
  ASSERT_TRUE(vt_contains(&screen, "   sentence"),
              "idle resize continuation was not rendered before input");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "hello world sentence") == 0,
              "resize result mismatch");
  vt_init(&screen, 20, 16);
  vt_apply(&screen, terminal + resize_offset);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world"),
              "resized final screen first row missing");
  ASSERT_TRUE(vt_contains(&screen, "   sentence"),
              "resized final screen continuation row missing");
  PASS();
}

static void test_dynamic_bounded_resize_reflows_without_keypress(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[8192];
  char result[256];
  char buf[512];
  struct vt_screen screen;
  size_t terminal_len;
  size_t resize_offset;
  ssize_t n;
  int status;
  int tries;

  TEST("dynamic bounded prompt reflows and preserves SIGWINCH handler");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 40;
  ws.ws_row = 6;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    struct sigaction action;
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    char message[256];
    int written;
    close(master_fd);
    close(result_pipe[0]);
    memset(&action, 0, sizeof(action));
    action.sa_handler = remember_winch;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGWINCH, &action, NULL) != 0)
      _exit(2);
    winch_seen = 0;
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->set_bounds(sl, 0, 0, 0, 0) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(4);
    written =
        snprintf(message, sizeof(message), "%d:%s", (int)winch_seen, line);
    if (written > 0)
      (void)write(result_pipe[1], message, (size_t)written);
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  }
  ASSERT_TRUE(contains_bytes(terminal, "\033[6;1Hp> "),
              "dynamic prompt did not start at bottom");
  ASSERT_TRUE(kill(pid, SIGWINCH) == 0, "SIGWINCH failed");
  usleep(50000);
  ASSERT_TRUE(write(master_fd, "hello world sentence", 20) == 20,
              "write input failed");
  tries = 0;
  while (tries < 100) {
    vt_init(&screen, 6, 40);
    vt_apply(&screen, terminal);
    if (vt_contains(&screen, "p> hello world sentence"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 6, 40);
  vt_apply(&screen, terminal);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world sentence"),
              "wide bounded render missing");
  ws.ws_col = 16;
  ws.ws_row = 6;
  resize_offset = terminal_len;
  ASSERT_TRUE(ioctl(master_fd, TIOCSWINSZ, &ws) == 0, "resize ioctl failed");
  tries = 0;
  while (tries < 300) {
    vt_init(&screen, 6, 16);
    vt_apply(&screen, terminal + resize_offset);
    if (vt_contains(&screen, "p> hello world") &&
        vt_contains(&screen, "   sentence"))
      break;
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  vt_init(&screen, 6, 16);
  vt_apply(&screen, terminal + resize_offset);
  ASSERT_TRUE(vt_contains(&screen, "p> hello world"),
              "bounded final screen first row missing");
  ASSERT_TRUE(vt_contains(&screen, "   sentence"),
              "bounded final screen continuation row missing");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "1:hello world sentence") == 0,
              "dynamic bounded resize or SIGWINCH handler mismatch");
  PASS();
}

static void test_visual_up_moves_across_wrapped_rows(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("up arrow moves across wrapped visual rows");
  ASSERT_TRUE(run_pty_readline_case("hello world sentence\033[AX\r", 16, 0,
                                    NULL, terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "hello woXrld sentence") == 0,
              "visual up insertion mismatch");
  PASS();
}

static void test_ctrl_c_interrupts_child(void) {
  int master_fd;
  int slave_fd;
  pid_t pid;
  int status;
  char terminal[512];
  size_t terminal_len;
  ssize_t n;
  int tries;

  TEST("Ctrl-C interrupts active readline");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    close(master_fd);
    (void)setsid();
    (void)ioctl(slave_fd, TIOCSCTTY, 0);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    if (line)
      sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    _exit(0);
  }
  close(slave_fd);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "\003", 1) == 1, "write Ctrl-C failed");
  tries = 0;
  do {
    n = waitpid(pid, &status, WNOHANG);
    if (n == 0)
      usleep(10000);
    tries++;
  } while (n == 0 && tries < 200);
  close(master_fd);
  ASSERT_TRUE(n == pid, "child did not exit after Ctrl-C");
  ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT,
              "child was not interrupted by SIGINT");
  PASS();
}

static void test_ctrl_c_signal_is_not_delivered_twice(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  int status;
  char terminal[512];
  char result[128];
  size_t terminal_len;
  ssize_t n;
  int tries;

  TEST("Ctrl-C is raised once after raw cleanup");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "result pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct termios before;
    struct termios after;
    char msg[128];
    int restored;
    int readline_status;
    int written;

    close(master_fd);
    close(result_pipe[0]);
    (void)setsid();
    (void)ioctl(slave_fd, TIOCSCTTY, 0);
    if (tcgetattr(slave_fd, &before) != 0)
      _exit(2);
    ctrl_c_sigint_count = 0;
    if (signal(SIGINT, count_ctrl_c_sigint) == SIG_ERR)
      _exit(3);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(4);
    line = sl->readline(sl, "p> ");
    if (line)
      sl->free_string(sl, line);
    readline_status = (int)sl->last_readline_status(sl);
    if (tcgetattr(slave_fd, &after) != 0)
      _exit(5);
    restored = termios_same_observable(&before, &after);
    sl->destroy(sl);
    written = snprintf(msg, sizeof(msg), "%d %d %d", (int)ctrl_c_sigint_count,
                       restored, readline_status);
    if (written > 0)
      (void)write(result_pipe[1], msg, (size_t)written);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  while (!contains_bytes(terminal, "p> ")) {
    n = read_some_with_timeout(master_fd, terminal + terminal_len,
                               sizeof(terminal) - 1 - terminal_len);
    ASSERT_TRUE(n > 0, "prompt was not rendered");
    terminal_len += (size_t)n;
    terminal[terminal_len] = '\0';
  }
  ASSERT_TRUE(write(master_fd, "\003", 1) == 1, "write Ctrl-C failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  tries = 0;
  do {
    n = waitpid(pid, &status, WNOHANG);
    if (n == 0)
      usleep(10000);
    tries++;
  } while (n == 0 && tries < 200);
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(n == pid, "child did not exit after Ctrl-C");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "1 1 4") == 0,
              "Ctrl-C signal count or terminal cleanup mismatch");
  PASS();
}

static void test_idle_callback_can_submit_without_input(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char result[128];
  ssize_t n;
  int status;

  TEST("idle callback can submit without input");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "result pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_finish_state state;

    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    state.calls = 0;
    state.text = "idle-result";
    state.cancel = 0;
    if (sl->set_idle_callback(sl, idle_finish_after_two_ticks, &state) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      (void)write(result_pipe[1], "<NULL>", 6);
    else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "idle submit did not return");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "idle-result") == 0, "idle submit mismatch");
  PASS();
}

static void test_idle_callback_can_cancel_without_input(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char result[128];
  ssize_t n;
  int status;

  TEST("idle callback can cancel without input");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "result pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_finish_state state;

    close(master_fd);
    close(result_pipe[0]);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    state.calls = 0;
    state.text = NULL;
    state.cancel = 1;
    if (sl->set_idle_callback(sl, idle_finish_after_two_ticks, &state) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line) {
      if (sl->last_readline_status(sl) == SL_READLINE_CANCELLED)
        (void)write(result_pipe[1], "<NULL>|CANCELLED", 16);
      else
        (void)write(result_pipe[1], "<NULL>", 6);
    } else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "idle cancel did not return");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "<NULL>|CANCELLED") == 0,
              "idle cancel status mismatch");
  PASS();
}

static void test_final_render_failure_reports_error(void) {
  int master_fd;
  int slave_fd;
  int output_pipe[2];
  int result_pipe[2];
  pid_t pid;
  char terminal[256];
  char result[128];
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("final render failure reports readline error");
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(output_pipe) == 0, "output pipe failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "result pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    int readline_status;
    char msg[64];
    int written;

    close(master_fd);
    close(output_pipe[0]);
    close(result_pipe[0]);
    signal(SIGPIPE, SIG_IGN);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = output_pipe[1];
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    line = sl->readline(sl, "p> ");
    readline_status = (int)sl->last_readline_status(sl);
    if (line) {
      written = snprintf(msg, sizeof(msg), "%s|%d", line, readline_status);
      sl->free_string(sl, line);
    } else {
      written = snprintf(msg, sizeof(msg), "<NULL>|%d", readline_status);
    }
    if (written > 0)
      (void)write(result_pipe[1], msg, (size_t)written);
    sl->destroy(sl);
    close(slave_fd);
    close(output_pipe[1]);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(output_pipe[1]);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  tries = 0;
  while (!contains_bytes(terminal, "p> ") && tries < 100) {
    n = read_some_with_timeout(output_pipe[0], result, sizeof(result));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), result,
                            n);
    tries++;
  }
  ASSERT_TRUE(contains_bytes(terminal, "p> "), "initial prompt not rendered");
  close(output_pipe[0]);
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "<NULL>|5") == 0,
              "readline did not report final render error");
  PASS();
}

static void test_narrow_terminal_does_not_submit_before_enter(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  int ready_pipe[2];
  pid_t pid;
  char input[121];
  char result[1024];
  ssize_t n;
  int status;
  int i;

  TEST("narrow terminal does not submit before Enter");
  for (i = 0; i < 120; i++)
    input[i] = 'x';
  input[120] = '\0';
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "result pipe failed");
  ASSERT_TRUE(pipe(ready_pipe) == 0, "ready pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_ready_state ready;
    int null_fd;

    close(master_fd);
    close(result_pipe[0]);
    close(ready_pipe[0]);
    null_fd = open("/dev/null", O_WRONLY);
    if (null_fd < 0)
      _exit(2);
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = null_fd;
    cfg.screen_width = 1;
    cfg.line_max_len = 120;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(3);
    ready.fd = ready_pipe[1];
    ready.ready = 0;
    if (sl->set_idle_callback(sl, idle_signal_ready_once, &ready) != SL_OK)
      _exit(4);
    line = sl->readline(sl, "p> ");
    if (!line)
      (void)write(result_pipe[1], "<NULL>", 6);
    else {
      (void)write(result_pipe[1], line, strlen(line));
      sl->free_string(sl, line);
    }
    sl->destroy(sl);
    close(null_fd);
    close(slave_fd);
    close(ready_pipe[1]);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  close(ready_pipe[1]);
  n = read_some_with_timeout(ready_pipe[0], result, sizeof(result));
  ASSERT_TRUE(n == 1, "readline did not become idle");
  ASSERT_TRUE(write(master_fd, input, strlen(input)) == (ssize_t)strlen(input),
              "write input failed");
  for (i = 0; i < 50; i++) {
    n = read_some_with_timeout_ms(result_pipe[0], result, sizeof(result) - 1,
                                  20);
    ASSERT_TRUE(n == 0, "readline returned before Enter");
  }
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  close(ready_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strlen(result) == strlen(input), "submitted length mismatch");
  ASSERT_TRUE(strcmp(result, input) == 0, "submitted input mismatch");
  PASS();
}

static void test_idle_callback_prints_above_active_prompt(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  char terminal[4096];
  char result[256];
  char buf[512];
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;
  struct winsize ws;

  TEST("unbounded prompts clear and redraw live output by default");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 1;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_print_state state;
    close(master_fd);
    close(result_pipe[0]);
    state.printed = 0;
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.screen_width = 20;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    sl->set_idle_callback(sl, idle_print_once, &state);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(3);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  tries = 0;
  while (!contains_bytes(terminal, "idle-output") && tries < 100) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  ASSERT_TRUE(contains_bytes(terminal, "idle-output"),
              "idle output was not printed");
  ASSERT_TRUE(contains_bytes(terminal, "p> "), "prompt was not redrawn");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[6n"),
              "default live output requested a cursor-position probe");
  ASSERT_TRUE(!contains_bytes(terminal, "\033[1;"),
              "default live output entered a scroll region");
  ASSERT_TRUE(write(master_fd, "ok\r", 3) == 3, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "idle callback result mismatch");
  PASS();
}

static void test_normal_prompt_pins_at_bottom_for_live_output(void) {
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  struct vt_screen screen;
  char terminal[8192];
  char result[64];
  char buf[512];
  size_t terminal_len;
  ssize_t n;
  int status;
  int replied;
  int tries;
  int prompts_after_second;
  int first_row;
  int second_row;
  int queue_row;
  int prompt_row;
  int i;

  TEST("normal prompt pins at bottom for live output");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_scroll_region_state state;
    close(master_fd);
    close(result_pipe[0]);
    state.calls = 0;
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.prompt_queue = 1;
    cfg.live_scroll_region = 1;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->set_idle_callback(sl, idle_print_through_scroll_region, &state) !=
        SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  replied = 0;
  tries = 0;
  while (!contains_bytes(terminal, "first-live") && tries < 100) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    if (!replied && contains_bytes(terminal, "\033[6n")) {
      ASSERT_TRUE(write(master_fd, "q\033[5;4R", 7) == 7,
                  "concurrent input and cursor report write failed");
      replied = 1;
    }
    tries++;
  }
  ASSERT_TRUE(replied, "normal prompt did not request cursor position");
  ASSERT_TRUE(contains_bytes(terminal, "first-live"),
              "first live message missing");
  ASSERT_TRUE(count_bytes(terminal, "p> ") == 1,
              "first live message repainted the prompt");
  ASSERT_TRUE(contains_bytes(terminal, "\033[1;4r"),
              "bottom prompt did not enter scroll region");
  ASSERT_TRUE(write(master_fd, "ueued\tok", 8) == 8,
              "queue-and-text write failed");
  tries = 0;
  while (!contains_bytes(terminal, "second-live") && tries < 100) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  ASSERT_TRUE(contains_bytes(terminal, "Q 1. queued"),
              "queued prompt did not reflow the pinned region");
  ASSERT_TRUE(contains_bytes(terminal, "\033[1;3r"),
              "scroll region did not shrink after queue reflow");
  ASSERT_TRUE(contains_bytes(terminal, "second-live"),
              "second live message missing");
  prompts_after_second = count_bytes(terminal, "p> ");
  n = read_some_with_timeout(master_fd, buf, sizeof(buf));
  if (n > 0)
    append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  ASSERT_TRUE(count_bytes(terminal, "p> ") == prompts_after_second,
              "live message repainted the reflowed prompt");
  ASSERT_TRUE(write(master_fd, "\r", 1) == 1, "submit failed");
  n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
  ASSERT_TRUE(n > 0, "read result failed");
  result[n] = '\0';
  do {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
  } while (n > 0 && terminal_len < sizeof(terminal) - 1);
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0, "pinned prompt result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "\033[r"),
              "scroll region was not restored");
  vt_init(&screen, 5, 20);
  screen.row = 4;
  screen.col = 0;
  vt_apply(&screen, terminal);
  first_row = -1;
  second_row = -1;
  queue_row = -1;
  prompt_row = -1;
  for (i = 0; i < screen.rows; i++) {
    if (strstr(screen.cells[i], "first-live"))
      first_row = i;
    if (strstr(screen.cells[i], "second-live"))
      second_row = i;
    if (strstr(screen.cells[i], "Q 1. queued"))
      queue_row = i;
    if (strstr(screen.cells[i], "p> ok"))
      prompt_row = i;
  }
  ASSERT_TRUE(first_row >= 0 && second_row == first_row + 1 &&
                  queue_row == second_row + 1 && prompt_row == queue_row + 1,
              "live transcript content was overwritten or on the wrong row");
  PASS();
}

static void test_cursor_probe_preserves_concurrent_queue_input(void) {
  static const char input[] =
      "\033[9999999999;9999999999R"
      "queued-012345678901234567890123456789012345\tok\r";
  int master_fd;
  int slave_fd;
  int result_pipe[2];
  pid_t pid;
  struct winsize ws;
  char terminal[4096];
  char result[64];
  char buf[512];
  size_t terminal_len;
  ssize_t n;
  int status;
  int tries;

  TEST("cursor probe preserves concurrent queue input");
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = 20;
  ws.ws_row = 5;
  ASSERT_TRUE(openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == 0,
              "openpty failed");
  ASSERT_TRUE(pipe(result_pipe) == 0, "pipe failed");
  pid = fork();
  ASSERT_TRUE(pid >= 0, "fork failed");
  if (pid == 0) {
    sl_config_t cfg;
    sl_t *sl;
    char *line;
    struct idle_print_state state;
    close(master_fd);
    close(result_pipe[0]);
    state.printed = 0;
    sl_config_init(&cfg);
    cfg.input_fd = slave_fd;
    cfg.output_fd = slave_fd;
    cfg.prompt_queue = 1;
    cfg.live_scroll_region = 1;
    sl = sl_create_with_config(&cfg);
    if (!sl)
      _exit(2);
    if (sl->set_idle_callback(sl, idle_print_once, &state) != SL_OK)
      _exit(3);
    line = sl->readline(sl, "p> ");
    if (!line)
      _exit(4);
    (void)write(result_pipe[1], line, strlen(line));
    sl->free_string(sl, line);
    sl->destroy(sl);
    close(slave_fd);
    close(result_pipe[1]);
    _exit(0);
  }
  close(slave_fd);
  close(result_pipe[1]);
  terminal_len = 0;
  terminal[0] = '\0';
  tries = 0;
  while (!contains_bytes(terminal, "\033[6n") && tries < 100) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    tries++;
  }
  ASSERT_TRUE(contains_bytes(terminal, "\033[6n"),
              "cursor-position probe was not requested");
  ASSERT_TRUE(
      write(master_fd, input, sizeof(input) - 1) ==
          (ssize_t)(sizeof(input) - 1),
      "concurrent malformed cursor report and queue input write failed");
  tries = 0;
  n = 0;
  while (n == 0 && tries < 50) {
    n = read_some_with_timeout(master_fd, buf, sizeof(buf));
    if (n > 0)
      append_terminal_bytes(terminal, &terminal_len, sizeof(terminal), buf, n);
    n = read_some_with_timeout(result_pipe[0], result, sizeof(result) - 1);
    tries++;
  }
  ASSERT_TRUE(n > 0, "concurrent queue input did not complete");
  result[n] = '\0';
  close(master_fd);
  close(result_pipe[0]);
  ASSERT_TRUE(waitpid(pid, &status, 0) == pid, "waitpid failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "ok") == 0,
              "cursor probe did not preserve queued editor input");
  PASS();
}

static void test_utf8_input_and_backspace(void) {
  char terminal[4096];
  char result[256];
  int status;

  TEST("UTF-8 input is preserved and backspace is character-wide");
  ASSERT_TRUE(run_pty_readline_case("\303\245b\177\r", 20, 0, NULL, terminal,
                                    sizeof(terminal), result, sizeof(result),
                                    &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "\303\245") == 0, "UTF-8 result mismatch");
  PASS();
}

static void test_utf8_swedish_input_is_rendered_as_full_sequences(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("UTF-8 Swedish input renders as full sequences");
  ASSERT_TRUE(run_pty_readline_case("\303\245\303\244\303\266\r", 20, 0, NULL,
                                    terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "\303\245\303\244\303\266") == 0,
              "Swedish UTF-8 result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "p> \303\245\303\244\303\266"),
              "Swedish UTF-8 render missing");
  PASS();
}

static void test_unicode_width_wraps_japanese_and_emoji(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("Unicode width wraps Japanese and emoji");
  ASSERT_TRUE(run_pty_readline_case("\346\227\245\346\234\254\350\252\236"
                                    "\346\227\245\346\234\254\350\252\236"
                                    "\360\237\231\202x\r",
                                    12, 0, NULL, terminal, sizeof(terminal),
                                    result, sizeof(result), &status) == 0,
              "pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child editor failed");
  ASSERT_TRUE(strcmp(result, "\346\227\245\346\234\254\350\252\236"
                             "\346\227\245\346\234\254\350\252\236"
                             "\360\237\231\202x") == 0,
              "wide UTF-8 result mismatch");
  ASSERT_TRUE(contains_bytes(terminal, "p> \346\227\245\346\234\254"
                                       "\350\252\236\346\227\245"),
              "wide first row missing");
  ASSERT_TRUE(contains_bytes(terminal, "   \346\227\245\346\234\254\350\252\236"
                                       "\360\237\231\202x"),
              "wide continuation row missing");
  PASS();
}

static void test_unicode_backspace_deletes_clusters(void) {
  char terminal[8192];
  char result[256];
  int status;

  TEST("Unicode backspace deletes combining and emoji clusters");
  ASSERT_TRUE(run_pty_readline_case("ze\314\201x\177\177\r", 20, 0, NULL,
                                    terminal, sizeof(terminal), result,
                                    sizeof(result), &status) == 0,
              "combining pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "combining child editor failed");
  ASSERT_TRUE(strcmp(result, "z") == 0, "combining cluster was split");

  ASSERT_TRUE(run_pty_readline_case("z\360\237\221\251\342\200\215"
                                    "\360\237\222\273x\177\177\r",
                                    20, 0, NULL, terminal, sizeof(terminal),
                                    result, sizeof(result), &status) == 0,
              "emoji pty case failed");
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "emoji child editor failed");
  ASSERT_TRUE(strcmp(result, "z") == 0, "emoji cluster was split");
  PASS();
}
#else
static void test_pty_enter_and_ctrl_j(void) {
  TEST("pty readline is non-destructive and Ctrl-J inserts newline");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_readline_uses_default_prompt(void) {
  TEST("pty readline uses default prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_readline_stops_at_line_max(void) {
  TEST("pty readline stops at configured line max");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_ctrl_d_exits(void) {
  TEST("pty Ctrl-D on empty prompt returns NULL");
  printf("SKIP\n");
  tests_passed++;
}
static void test_normal_prompt_proceeds_after_output(void) {
  TEST("normal prompt proceeds after output");
  printf("SKIP\n");
  tests_passed++;
}
static void test_normal_wrapped_prompt_proceeds_after_output(void) {
  TEST("normal wrapped prompt proceeds after output");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_history_navigation_restores_draft(void) {
  TEST("history up recalls newest entry");
  printf("SKIP\n");
  tests_passed++;
  TEST("history down restores edited draft");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_readline_does_not_auto_add_history(void) {
  TEST("TTY readline does not auto-add submitted history");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_searches_memory_history(void) {
  TEST("Ctrl-R searches in-memory history newest first");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_repeats_and_wraps_matches(void) {
  TEST("Ctrl-R repeats cycle older matches and wrap");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_no_match_and_cancel_restore_draft(void) {
  TEST("Ctrl-R no-match submit preserves draft");
  printf("SKIP\n");
  tests_passed++;
  TEST("Ctrl-R cancel restores edited draft");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_searches_loaded_history_file(void) {
  TEST("Ctrl-R searches loaded history file");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_searches_unicode_and_multiline_history(void) {
  TEST("Ctrl-R searches Unicode and multiline history");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_r_renders_inside_bounded_prompt(void) {
  TEST("Ctrl-R renders inside bounded prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bracketed_paste_is_literal_content(void) {
  TEST("bracketed paste treats carriage return as content");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bracketed_paste_stops_at_line_max(void) {
  TEST("bracketed paste stops at configured line max");
  printf("SKIP\n");
  tests_passed++;
}
static void test_tab_render_expands_beyond_line_bytes(void) {
  TEST("tab rendering can exceed line byte count");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_tab_inserts_text(void) {
  TEST("key binding TAB inserts text");
  printf("SKIP\n");
  tests_passed++;
}
static void test_prompt_queue_dispatches_fifo_with_themes(void) {
  TEST("prompt queue previews and dispatches FIFO across themes");
  printf("SKIP\n");
  tests_passed++;
}
static void test_prompt_queue_alt_e_recalls_newest(void) {
  TEST("Alt-E recalls the newest queued prompt into the editor");
  printf("SKIP\n");
  tests_passed++;
}
static void test_prompt_themes_style_normal_readline(void) {
  TEST("prompt themes style normal readline UI");
  printf("SKIP\n");
  tests_passed++;
}
static void test_statusline_uses_palette_offset_and_truncation(void) {
  TEST("status line offsets colours and truncates after 32 elements");
  printf("SKIP\n");
  tests_passed++;
}
static void test_default_statusline_uses_ansi_palette(void) {
  TEST("default status line uses standard ANSI palette");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_alt_m_inserts_text(void) {
  TEST("key binding Alt-M inserts text");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_f1_submits(void) {
  TEST("key binding F1 submits");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_can_edit_buffer_and_submit(void) {
  TEST("key binding can edit buffer and submit");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_can_cancel(void) {
  TEST("key binding can cancel");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_enter_can_pass_to_default(void) {
  TEST("key binding Enter can pass to default");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_ctrl_enter_can_call_submit(void) {
  TEST("key binding Ctrl-Enter can call submit");
  printf("SKIP\n");
  tests_passed++;
}
static void test_key_binding_ctrl_r_overrides_history_search(void) {
  TEST("key binding Ctrl-R overrides history search");
  printf("SKIP\n");
  tests_passed++;
}
static void test_terminal_mode_is_restored(void) {
  TEST("readline restores terminal mode before returning");
  printf("SKIP\n");
  tests_passed++;
}
static void test_non_ctrl_c_signal_does_not_interrupt_readline(void) {
  TEST("non-Ctrl-C signal does not interrupt readline");
  printf("SKIP\n");
  tests_passed++;
}
static void test_pty_multiline_deletes_are_buffer_wide(void) {
  TEST("Ctrl-U deletes from buffer start across lines");
  printf("SKIP\n");
  tests_passed++;
  TEST("Ctrl-K deletes to buffer end across lines");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_print_above_uses_scroll_region(void) {
  TEST("bounded prompt prints output through scroll region");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_prompt_starts_at_bottom(void) {
  TEST("bounded prompt starts on bottom row");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_readline_leaves_output_below_prompt(void) {
  TEST("bounded readline leaves following output below prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_readline_keeps_normal_completion_with_queueing(void) {
  TEST("readline retains completion output when queueing is enabled");
  printf("SKIP\n");
  tests_passed++;
}
static void test_config_zero_bounds_start_at_terminal_bottom(void) {
  TEST("zero config bounds start on terminal bottom row");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_redraw_clears_only_box_width(void) {
  TEST("bounded redraw clears only configured width");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_wrapped_shrink_clears_continuation_tail(void) {
  TEST("bounded wrapped shrink clears continuation row tail");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_viewport_follows_cursor(void) {
  TEST("bounded viewport follows cursor above bottom page");
  printf("SKIP\n");
  tests_passed++;
}
static void test_print_above_pulls_stream_chunks(void) {
  TEST("print_above pulls streamed chunks");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_prompt_growth_scrolls_output_region(void) {
  TEST("bounded prompt growth scrolls output region");
  printf("SKIP\n");
  tests_passed++;
}
static void test_bounded_print_above_does_not_overwrite_prompt(void) {
  TEST("bounded print_above does not overwrite active prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_word_wrap_keeps_words_intact(void) {
  TEST("word wrap reflows at word boundaries");
  printf("SKIP\n");
  tests_passed++;
}
static void test_word_wrap_cursor_at_skipped_space(void) {
  TEST("word wrap preserves cursor at skipped space");
  printf("SKIP\n");
  tests_passed++;
}
static void test_active_word_wrap_does_not_duplicate_prompt(void) {
  TEST("active word wrap does not duplicate prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_active_exact_width_row_does_not_autowrap_prompt(void) {
  TEST("active exact-width row does not autowrap prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_normal_prompt_wraps_at_bottom_with_long_prompt(void) {
  TEST("normal prompt wraps at bottom with long prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_normal_prompt_growth_scrolls_at_screen_bottom(void) {
  TEST("normal prompt growth scrolls at screen bottom");
  printf("SKIP\n");
  tests_passed++;
}
static void test_exact_width_cursor_uses_explicit_wrap_row(void) {
  TEST("exact-width cursor uses explicit wrap row");
  printf("SKIP\n");
  tests_passed++;
  TEST("bounded exact-width cursor uses explicit wrap row");
  printf("SKIP\n");
  tests_passed++;
}
static void test_resize_reflows_without_keypress(void) {
  TEST("resize reflows while readline is idle");
  printf("SKIP\n");
  tests_passed++;
}
static void test_dynamic_bounded_resize_reflows_without_keypress(void) {
  TEST("dynamic bounded prompt reflows after resize");
  printf("SKIP\n");
  tests_passed++;
}
static void test_visual_up_moves_across_wrapped_rows(void) {
  TEST("up arrow moves across wrapped visual rows");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_c_interrupts_child(void) {
  TEST("Ctrl-C interrupts active readline");
  printf("SKIP\n");
  tests_passed++;
}
static void test_ctrl_c_signal_is_not_delivered_twice(void) {
  TEST("Ctrl-C is raised once after raw cleanup");
  printf("SKIP\n");
  tests_passed++;
}
static void test_idle_callback_can_submit_without_input(void) {
  TEST("idle callback can submit without input");
  printf("SKIP\n");
  tests_passed++;
}
static void test_idle_callback_can_cancel_without_input(void) {
  TEST("idle callback can cancel without input");
  printf("SKIP\n");
  tests_passed++;
}
static void test_narrow_terminal_does_not_submit_before_enter(void) {
  TEST("narrow terminal does not submit before Enter");
  printf("SKIP\n");
  tests_passed++;
}
static void test_idle_callback_prints_above_active_prompt(void) {
  TEST("idle callback prints above active prompt");
  printf("SKIP\n");
  tests_passed++;
}
static void test_normal_prompt_pins_at_bottom_for_live_output(void) {
  TEST("normal prompt pins at bottom for live output");
  printf("SKIP\n");
  tests_passed++;
}
static void test_cursor_probe_preserves_concurrent_queue_input(void) {
  TEST("cursor probe preserves concurrent queue input");
  printf("SKIP\n");
  tests_passed++;
}
static void test_utf8_input_and_backspace(void) {
  TEST("UTF-8 input is preserved and backspace is character-wide");
  printf("SKIP\n");
  tests_passed++;
}
static void test_utf8_swedish_input_is_rendered_as_full_sequences(void) {
  TEST("UTF-8 Swedish input renders as full sequences");
  printf("SKIP\n");
  tests_passed++;
}
static void test_unicode_width_wraps_japanese_and_emoji(void) {
  TEST("Unicode width wraps Japanese and emoji");
  printf("SKIP\n");
  tests_passed++;
}
static void test_unicode_backspace_deletes_clusters(void) {
  TEST("Unicode backspace deletes combining and emoji clusters");
  printf("SKIP\n");
  tests_passed++;
}
#endif

int main(void) {
  printf("softline unit tests\n");
  printf("===================\n\n");

  test_config_init();
  test_receiver_shell();
  test_free_function_wrappers_use_receiver_methods();
  test_set_cursor_clamps_to_utf8_cluster_boundary();
  test_destroy_null();
  test_invalid_config_is_rejected();
  test_invalid_receiver_arguments();
  test_plain_readline();
  test_plain_readline_uses_default_prompt();
  test_plain_readline_consumes_crlf_once();
  test_plain_readline_empty_eof_returns_null();
  test_plain_readline_retries_eintr();
  test_plain_readline_stops_at_line_max();
  test_readline_status_is_per_instance();
  test_history_is_per_instance();
  test_history_io_failures_are_reported();
  test_history_save_uses_private_permissions();
  test_history_round_trips_multiline_entries();
  test_history_rejects_oversized_entries();
  test_stream_failures_are_reported();
  test_print_above_uses_lf_for_non_tty_output();
  test_bounded_print_above_without_space_is_error();
  test_pty_enter_and_ctrl_j();
  test_pty_readline_uses_default_prompt();
  test_pty_readline_stops_at_line_max();
  test_normal_prompt_proceeds_after_output();
  test_normal_wrapped_prompt_proceeds_after_output();
  test_pty_ctrl_d_exits();
  test_pty_history_navigation_restores_draft();
  test_pty_readline_does_not_auto_add_history();
  test_ctrl_r_searches_memory_history();
  test_ctrl_r_repeats_and_wraps_matches();
  test_ctrl_r_no_match_and_cancel_restore_draft();
  test_ctrl_r_searches_loaded_history_file();
  test_ctrl_r_searches_unicode_and_multiline_history();
  test_ctrl_r_renders_inside_bounded_prompt();
  test_bracketed_paste_is_literal_content();
  test_bracketed_paste_stops_at_line_max();
  test_tab_render_expands_beyond_line_bytes();
  test_key_binding_tab_inserts_text();
  test_prompt_queue_dispatches_fifo_with_themes();
  test_prompt_queue_dispatches_fifo_in_normal_scrollback();
  test_prompt_queue_rejects_reduced_capacity();
  test_prompt_queue_alt_e_recalls_newest();
  test_prompt_themes_style_normal_readline();
  test_statusline_uses_palette_offset_and_truncation();
  test_default_statusline_uses_ansi_palette();
  test_key_binding_alt_m_inserts_text();
  test_key_binding_f1_submits();
  test_key_binding_can_edit_buffer_and_submit();
  test_key_binding_can_cancel();
  test_key_binding_enter_can_pass_to_default();
  test_key_binding_ctrl_enter_can_call_submit();
  test_key_binding_ctrl_r_overrides_history_search();
  test_terminal_mode_is_restored();
  test_non_ctrl_c_signal_does_not_interrupt_readline();
  test_pty_multiline_deletes_are_buffer_wide();
  test_bounded_print_above_uses_scroll_region();
  test_bounded_print_above_rejects_narrow_scroll();
  test_bounded_prompt_starts_at_bottom();
  test_bounded_readline_leaves_output_below_prompt();
  test_readline_keeps_normal_completion_with_queueing();
  test_config_zero_bounds_start_at_terminal_bottom();
  test_dynamic_bounds_origin_uses_remaining_terminal_area();
  test_bounded_redraw_clears_only_box_width();
  test_bounded_wrapped_shrink_clears_continuation_tail();
  test_bounded_viewport_follows_cursor();
  test_print_above_pulls_stream_chunks();
  test_bounded_prompt_growth_scrolls_output_region();
  test_bounded_print_above_does_not_overwrite_prompt();
  test_word_wrap_keeps_words_intact();
  test_word_wrap_cursor_at_skipped_space();
  test_active_word_wrap_does_not_duplicate_prompt();
  test_active_exact_width_row_does_not_autowrap_prompt();
  test_normal_prompt_wraps_at_bottom_with_long_prompt();
  test_normal_prompt_growth_scrolls_at_screen_bottom();
  test_exact_width_cursor_uses_explicit_wrap_row();
  test_resize_reflows_without_keypress();
  test_dynamic_bounded_resize_reflows_without_keypress();
  test_visual_up_moves_across_wrapped_rows();
  test_ctrl_c_interrupts_child();
  test_ctrl_c_signal_is_not_delivered_twice();
  test_idle_callback_can_submit_without_input();
  test_idle_callback_can_cancel_without_input();
  test_final_render_failure_reports_error();
  test_narrow_terminal_does_not_submit_before_enter();
  test_idle_callback_prints_above_active_prompt();
  test_normal_prompt_pins_at_bottom_for_live_output();
  test_cursor_probe_preserves_concurrent_queue_input();
  test_utf8_input_and_backspace();
  test_utf8_swedish_input_is_rendered_as_full_sequences();
  test_unicode_width_wraps_japanese_and_emoji();
  test_unicode_backspace_deletes_clusters();

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
