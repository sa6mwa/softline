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

int main(void) {
  sl_t *sl = sl_create();
  unsigned long serial = 0;
  char *line;
  if (!sl || sl_set_prompt_theme(sl, SL_PROMPT_THEME_PLAIN) != SL_OK ||
      sl_bind_key(sl, SL_KEY_ALT_M, barrier, &serial) != SL_OK)
    return 1;
  while ((line = sl_readline(sl, "softline> ")) != NULL) {
    printf("submitted: %s\n", line);
    sl_free_string(sl, line);
  }
  sl_destroy(sl);
  return 0;
}
