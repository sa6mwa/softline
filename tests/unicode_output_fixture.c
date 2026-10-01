#define _POSIX_C_SOURCE 200809L
#include "softline/softline.h"
#include <libmdf/mdf.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct producer {
  sl_t *sl;
  mdf *renderer;
  const char *source;
  size_t chunk;
  int trace, result, started;
};

static int write_all(int fd, const char *bytes, size_t length) {
  while (length > 0) {
    ssize_t n = write(fd, bytes, length);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    bytes += n;
    length -= (size_t)n;
  }
  return 0;
}

static int sink(void *userdata, const char *bytes, size_t length) {
  struct producer *producer = (struct producer *)userdata;
  /* Test evidence only: the production path forwards each emission at once. */
  if (write_all(producer->trace, bytes, length) != 0)
    return -1;
  return producer->sl
             ? (sl_output_stream_write(producer->sl, bytes, length) == SL_OK
                    ? 0
                    : -1)
             : write_all(STDOUT_FILENO, bytes, length);
}

static void produce(struct producer *producer) {
  size_t offset, length = strlen(producer->source);
  producer->result = MDF_OK;
  for (offset = 0; offset < length && producer->result == MDF_OK;) {
    size_t take = length - offset;
    if (take > producer->chunk)
      take = producer->chunk;
    if (producer->renderer)
      producer->result = producer->renderer->feed(
          producer->renderer, producer->source + offset, take);
    else
      producer->result = sink(producer, producer->source + offset, take);
    offset += take;
  }
  if (producer->renderer && producer->result == MDF_OK)
    producer->result = producer->renderer->finish_document(producer->renderer);
}

static void idle(sl_t *sl, void *userdata) {
  struct producer *producer = (struct producer *)userdata;
  if (producer->started)
    return;
  producer->started = 1;
  produce(producer);
  (void)sl_cancel(sl);
}

static int same_mode(const struct termios *before,
                     const struct termios *after) {
  return before->c_iflag == after->c_iflag &&
         before->c_oflag == after->c_oflag &&
         before->c_cflag == after->c_cflag &&
         before->c_lflag == after->c_lflag &&
         memcmp(before->c_cc, after->c_cc, sizeof(before->c_cc)) == 0;
}

int main(int argc, char **argv) {
  struct producer producer;
  struct termios before, after;
  mdf_options options;
  mdf_sink output;
  sl_config_t config;
  int interrupted;
  char *line;
  if (argc != 6 || tcgetattr(STDIN_FILENO, &before) != 0)
    return 2;
  memset(&producer, 0, sizeof(producer));
  producer.chunk = (size_t)strtoul(argv[3], NULL, 10);
  producer.source = argv[4];
  producer.trace = open(argv[5], O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (producer.chunk == 0 || producer.trace < 0)
    return 2;
  if (strcmp(argv[2], "mdf") == 0) {
    mdf_options_init(&options);
    options.width = 40;
    options.output_fd = STDOUT_FILENO;
    if (mdf_create(MDF_FORMAT_ANSI, &options, &producer.renderer) != MDF_OK)
      return 2;
    output.userdata = &producer;
    output.write = sink;
    if (producer.renderer->set_sink(producer.renderer, &output) != MDF_OK)
      return 2;
  } else if (strcmp(argv[2], "raw") != 0) {
    return 2;
  }
  interrupted = strcmp(argv[1], "interrupted") == 0;
  if (strcmp(argv[1], "direct") == 0) {
    produce(&producer);
  } else {
    if (!interrupted && strcmp(argv[1], "active") != 0)
      return 2;
    if (interrupted && signal(SIGINT, SIG_IGN) == SIG_ERR)
      return 2;
    sl_config_init(&config);
    config.prompt_theme = SL_PROMPT_THEME_PLAIN;
    producer.sl = sl_create_with_config(&config);
    if (!producer.sl || sl_output_stream_begin(producer.sl) != SL_OK ||
        (!interrupted &&
         sl_set_idle_callback(producer.sl, idle, &producer) != SL_OK))
      return 2;
    line = sl_readline(producer.sl, "> ");
    if (line ||
        sl_last_readline_status(producer.sl) !=
            (interrupted ? SL_READLINE_INTERRUPTED : SL_READLINE_CANCELLED))
      return 2;
    sl_free_string(producer.sl, line);
    if (interrupted) {
      if (tcgetattr(STDIN_FILENO, &after) != 0 || !same_mode(&before, &after))
        return 2;
      produce(&producer);
    }
    sl_destroy(producer.sl);
  }
  if (producer.renderer)
    producer.renderer->destroy(producer.renderer);
  close(producer.trace);
  if (tcgetattr(STDIN_FILENO, &after) != 0 || !same_mode(&before, &after))
    return 2;
  return producer.result == MDF_OK ? 0 : 1;
}
