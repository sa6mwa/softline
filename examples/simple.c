#include "softline/softline.h"
#include <stdio.h>

int main(void) {
  sl_t *sl;
  char *line;
  int count;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "Failed to create softline handle\n");
    return 1;
  }

  sl_set_prompt_symbol(sl, "> ");

  printf("softline interactive example\n");
  printf("Type lines, Ctrl+D or empty Ctrl+D to quit.\n\n");

  count = 0;
  for (;;) {
    line = sl_readline(sl, count == 0 ? "first> " : "softline> ");
    if (!line)
      break;
    if (line[0] == '\0')
      printf("(empty line)\n");
    else
      printf("you typed: '%s'\n", line);
    sl_free(line);
    count++;
  }

  sl_destroy(sl);
  printf("\nGoodbye.\n");
  return 0;
}