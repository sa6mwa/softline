#define _POSIX_C_SOURCE 200809L
#include "softline/softline.h"
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t interrupt_count;

static void returned_interrupt(int signum) {
  (void)signum;
  interrupt_count++;
}

static int same_input_mode(const struct termios *before,
                           const struct termios *after) {
  return before->c_iflag == after->c_iflag &&
         before->c_oflag == after->c_oflag &&
         before->c_cflag == after->c_cflag &&
         before->c_lflag == after->c_lflag &&
         memcmp(before->c_cc, after->c_cc, sizeof(before->c_cc)) == 0;
}

static int interrupted(int handler) {
  sl_t *sl;
  char *input;
  int result;
  struct termios before, after;
  if (tcgetattr(STDIN_FILENO, &before) != 0 ||
      signal(SIGINT, handler ? returned_interrupt : SIG_IGN) == SIG_ERR)
    return 2;
  sl = sl_create();
  if (!sl || sl_output_stream_begin(sl) != SL_OK)
    return 2;
  input = sl_readline(sl, "> ");
  result = !input && sl_last_readline_status(sl) == SL_READLINE_INTERRUPTED &&
                   tcgetattr(STDIN_FILENO, &after) == 0 &&
                   same_input_mode(&before, &after) &&
                   interrupt_count == (handler ? 1 : 0)
               ? SL_OK
               : SL_ERROR;
  sl_free_string(sl, input);
  if (result == SL_OK)
    result = sl_output_stream_write(sl, "AFTER INTERRUPT", 15);
  if (result == SL_OK)
    result = sl_output_stream_end(sl);
  sl_destroy(sl);
  if (tcgetattr(STDIN_FILENO, &after) != 0 || !same_input_mode(&before, &after))
    return 2;
  return result == SL_OK ? 0 : 1;
}

static int stream_chunks(const char *text, const char *chunk_text,
                         int handoff) {
  sl_t *sl;
  size_t offset, length, chunk;
  int result;
  chunk = (size_t)strtoul(chunk_text, NULL, 10);
  if (chunk == 0)
    return 2;
  sl = sl_create();
  if (!sl)
    return 2;
  length = strlen(text);
  result = sl_output_stream_begin(sl);
  for (offset = 0; result == SL_OK && offset < length;) {
    size_t take = length - offset;
    if (take > chunk)
      take = chunk;
    result = sl_output_stream_write(sl, text + offset, take);
    offset += take;
  }
  if (result == SL_OK && handoff) {
    char *input = sl_readline(sl, "> ");
    result = input && !input[0] ? SL_OK : SL_ERROR;
    sl_free_string(sl, input);
    if (result == SL_OK)
      result = sl_output_stream_write(sl, "Y", 1);
  }
  if (result == SL_OK)
    result = sl_output_stream_end(sl);
  sl_destroy(sl);
  return result == SL_OK ? 0 : 1;
}

struct finite_source {
  const char *source;
  size_t offset;
};

static int finite_chunk(sl_t *sl, void *userdata, const char **bytes,
                        size_t *length) {
  struct finite_source *producer = (struct finite_source *)userdata;
  (void)sl;
  if (producer->offset < 3) {
    *bytes = "F: " + producer->offset;
    *length = 1;
  } else {
    *bytes = producer->source + producer->offset - 3;
    *length = **bytes ? 1 : 0;
  }
  producer->offset++;
  return SL_OK;
}

static int gated(const char *command_path, const char *ack_path,
                 const char *text) {
  sl_t *sl;
  int command_fd, ack_fd, result = SL_OK;
  char command = 'r';
  command_fd = open(command_path, O_RDWR);
  ack_fd = open(ack_path, O_RDWR);
  if (command_fd < 0 || ack_fd < 0)
    return 2;
  sl = sl_create();
  if (!sl || sl_output_stream_begin(sl) != SL_OK)
    return 2;
  for (;;) {
    struct winsize geometry;
    unsigned short reply[2];
    if (command == 's') {
      size_t i;
      for (i = 0; result == SL_OK && text[i]; i++)
        result = sl_output_stream_write(sl, text + i, 1);
    } else if (command == 'p') {
      result = sl_output_stream_write(sl, "Y", 1);
    } else if (command == 'w') {
      result = sl_set_screen_width(sl, 12);
    } else if (command == 'f') {
      struct finite_source producer;
      producer.source = text;
      producer.offset = 0;
      result = sl_output_stream_end(sl);
      if (result == SL_OK)
        result = sl_print_above(sl, finite_chunk, &producer);
      if (result == SL_OK)
        result = sl_output_stream_begin(sl);
    } else if (command == 'i') {
      char *input = sl_readline(sl, "> ");
      result = input && !input[0] ? SL_OK : SL_ERROR;
      sl_free_string(sl, input);
    } else if (command == 'x') {
      result = sl_output_stream_end(sl);
      break;
    }
    if (result != SL_OK || ioctl(STDOUT_FILENO, TIOCGWINSZ, &geometry) != 0)
      break;
    reply[0] = geometry.ws_col;
    reply[1] = geometry.ws_row;
    if (write(ack_fd, reply, sizeof(reply)) != (ssize_t)sizeof(reply) ||
        read(command_fd, &command, 1) != 1) {
      result = SL_ERROR;
      break;
    }
  }
  sl_destroy(sl);
  close(command_fd);
  close(ack_fd);
  return result == SL_OK ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "--interrupt") == 0)
    return interrupted(strcmp(argv[2], "handler") == 0);
  if (argc == 4 && strcmp(argv[1], "--chunks") == 0)
    return stream_chunks(argv[2], argv[3], 0);
  if (argc == 4 && strcmp(argv[1], "--handoff") == 0)
    return stream_chunks(argv[2], argv[3], 1);
  if (argc == 5 && strcmp(argv[1], "--gated") == 0)
    return gated(argv[2], argv[3], argv[4]);
  return 2;
}
