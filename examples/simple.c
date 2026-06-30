#include "softline/softline.h"

#include <stdio.h>
#include <string.h>

int main(void) {
  sl_t *sl;
  char *line;

  sl = sl_create();
  if (!sl) {
    fprintf(stderr, "failed to create softline\n");
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
