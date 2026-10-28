#include <softline/softline.h>
#include <stdio.h>
#include <string.h>

/* A public-API editor with a render barrier. The title is outside the cell
 * transcript and follows a synchronous redraw of the preceding input/resize. */
static int barrier(sl_t *sl, sl_key_t key, void *data,
                   sl_key_action_t *action) {
  unsigned long *serial = data;
  (void)key;
  if (sl_set_status_message(sl, NULL) != SL_OK)
    return SL_ERROR_IO;
  printf("\033]2;rendered-%lu\007", ++*serial);
  if (fflush(stdout))
    return SL_ERROR_IO;
  *action = SL_KEY_ACTION_HANDLED;
  return SL_OK;
}

static int theme(sl_t *sl, sl_key_t key, void *data, sl_key_action_t *action) {
  (void)data;
  *action = SL_KEY_ACTION_HANDLED;
  return sl_set_prompt_theme(sl, (sl_prompt_theme_t)(key - SL_KEY_F1));
}

int main(int argc, char **argv) {
  sl_t *sl = sl_create();
  unsigned long serial = 0;
  int index;
  char *line;
  if (!sl || sl_set_prompt_theme(sl, SL_PROMPT_THEME_PLAIN) != SL_OK ||
      sl_bind_key(sl, SL_KEY_ALT_M, barrier, &serial) != SL_OK)
    return 1;
  if (argc == 2 && !strcmp(argv[1], "--themes")) {
    for (index = 0; index <= SL_PROMPT_THEME_DEFAULT; ++index)
      if (sl_bind_key(sl, SL_KEY_F1 + index, theme, NULL) != SL_OK)
        return 1;
    if (sl_set_prompt_theme(sl, SL_PROMPT_THEME_DEFAULT) != SL_OK ||
        sl_set_statusline(sl, 1, 0) != SL_OK ||
        sl_set_status_element(sl, 0, "theme probe") != SL_OK ||
        sl_output_stream_begin(sl) != SL_OK)
      return 1;
    line = sl_next_prompt(sl, "> ", NULL);
    if (!line || sl_output_stream_end(sl) != SL_OK)
      return 1;
    printf("submitted: %s\n", line);
    sl_free_string(sl, line);
    sl_destroy(sl);
    return 0;
  }
  while ((line = sl_readline(sl, "softline> ")) != NULL) {
    printf("submitted: %s\n", line);
    sl_free_string(sl, line);
  }
  sl_destroy(sl);
  return 0;
}
