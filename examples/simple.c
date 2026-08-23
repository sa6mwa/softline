#include "softline/softline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int set_prompt_theme_from_environment(sl_t *sl,
                                             sl_prompt_theme_t fallback) {
  const char *name;
  sl_prompt_theme_t theme;
  name = getenv("SOFTLINE_PROMPT_THEME");
  theme = fallback;
  if (name && name[0] != '\0') {
    if (strcmp(name, "plain") == 0)
      theme = SL_PROMPT_THEME_PLAIN;
    else if (strcmp(name, "accent") == 0)
      theme = SL_PROMPT_THEME_ACCENT;
    else if (strcmp(name, "riced") == 0)
      theme = SL_PROMPT_THEME_RICED;
    else {
      fprintf(
          stderr,
          "invalid SOFTLINE_PROMPT_THEME: %s (use plain, accent, or riced)\n",
          name);
      return -1;
    }
  }
  return sl_set_prompt_theme(sl, theme) == SL_OK ? 0 : -1;
}

int main(void) {
  sl_t *sl;
  char *line;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
    return 1;
  }
  if (set_prompt_theme_from_environment(sl, SL_PROMPT_THEME_PLAIN) != 0) {
    fprintf(stderr, "failed to set prompt theme\n");
    sl->destroy(sl);
    return 1;
  }

  for (;;) {
    line = sl->readline(sl, "softline> ");
    if (!line)
      break;
    if (strcmp(line, "exit") == 0) {
      sl->free_string(sl, line);
      break;
    }
    printf("submitted: %s\n", line);
    sl->free_string(sl, line);
  }

  sl->destroy(sl);
  return 0;
}
