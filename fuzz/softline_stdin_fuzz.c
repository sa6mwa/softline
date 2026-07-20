#include "softline/softline.h"

#include <stdio.h>
#include <unistd.h>

int main(void) {
  int input[2];
  sl_config_t config;
  sl_t *sl;
  char *line;
  char buffer[4096];
  ssize_t count;
  size_t total;

  if (pipe(input) != 0)
    return 2;
  total = 0;
  while (total < sizeof(buffer) &&
         (count = read(STDIN_FILENO, buffer + total, sizeof(buffer) - total)) > 0) {
    total += (size_t)count;
  }
  if (total > 0) {
    if (write(input[1], buffer, total) != (ssize_t)total)
      return 3;
  }
  close(input[1]);
  sl_config_init(&config);
  config.input_fd = input[0];
  config.output_fd = STDERR_FILENO;
  sl = sl_create_with_config(&config);
  if (!sl)
    return 4;
  line = sl->readline(sl, NULL);
  sl->free_string(sl, line);
  sl->destroy(sl);
  close(input[0]);
  return 0;
}
