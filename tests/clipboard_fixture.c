#include "softline/softline.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int override_paste(sl_t *sl, sl_key_t key, void *userdata,
                          sl_key_action_t *action) {
  (void)key;
  (void)userdata;
  if (sl_insert(sl, "OVERRIDE") != SL_OK)
    return SL_ERROR;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int write_all(int fd, const char *bytes, size_t length) {
  while (length > 0) {
    ssize_t written = write(fd, bytes, length);
    if (written <= 0)
      return -1;
    bytes += written;
    length -= (size_t)written;
  }
  return 0;
}

int main(int argc, char **argv) {
  sl_config_t config;
  sl_t *sl;
  char *line;
  const char *error;
  int result_fd;
  if (argc < 2 || argc > 3)
    return 2;
  result_fd = atoi(argv[1]);
  sl_config_init(&config);
  if (argc >= 3 && strcmp(argv[2], "limit") == 0)
    config.line_max_len = 16;
  config.image_paste_path_template = getenv("SOFTLINE_TEST_IMAGE_TEMPLATE");
  if (getenv("SOFTLINE_TEST_DISABLE_IMAGE_PASTE"))
    config.disable_image_paste = 1;
  sl = sl_create_with_config(&config);
  if (!sl)
    return 2;
  if (argc >= 3 && strcmp(argv[2], "setter") == 0 &&
      sl_set_image_paste_path_template(
          sl, getenv("SOFTLINE_TEST_IMAGE_SETTER")) != SL_OK)
    return 2;
  if (argc >= 3 && strcmp(argv[2], "override") == 0 &&
      sl_bind_key(sl, SL_KEY_CTRL_V, override_paste, NULL) != SL_OK)
    return 2;
  line = sl_readline(sl, "> ");
  if (!line)
    return 2;
  error = sl_last_error(sl);
  if (write_all(result_fd, line, strlen(line)) != 0 ||
      write_all(result_fd, "\n", 1) != 0 ||
      write_all(result_fd, error ? error : "", error ? strlen(error) : 0) !=
          0 ||
      write_all(result_fd, "\n", 1) != 0)
    return 2;
  sl_free_string(sl, line);
  sl_destroy(sl);
  return 0;
}
