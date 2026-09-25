#define _POSIX_C_SOURCE 200809L

#include "softline/softline.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* The caller owns the large input. Formatting it must not allocate another
 * buffer proportional to its length. Run under a child-only address-space
 * limit so the other unit tests retain their normal allocation budget. */
static int check_bounded_quote(void) {
  const size_t prompt_len = 4u * 1024u * 1024u;
  sl_config_t config;
  sl_t *softline;
  struct rlimit limit;
  FILE *statm;
  unsigned long pages;
  long page_size;
  char *prompt;
  int output_fd;
  int ok;

  output_fd = open("/dev/null", O_WRONLY);
  prompt = (char *)malloc(prompt_len + 1);
  if (output_fd < 0 || !prompt)
    return 1;
  memset(prompt, 'x', prompt_len);
  prompt[prompt_len] = '\0';
  sl_config_init(&config);
  config.output_fd = output_fd;
  config.screen_width = 32;
  softline = sl_create_with_config(&config);
  if (!softline || sl_output_stream_begin(softline) != SL_OK)
    return 2;

  statm = fopen("/proc/self/statm", "r");
  page_size = sysconf(_SC_PAGESIZE);
  if (!statm || page_size <= 0 || fscanf(statm, "%lu", &pages) != 1)
    return 3;
  fclose(statm);
  if (getrlimit(RLIMIT_AS, &limit) != 0)
    return 4;
  limit.rlim_cur = (rlim_t)pages * (rlim_t)page_size + 2u * 1024u * 1024u;
  if (setrlimit(RLIMIT_AS, &limit) != 0)
    return 5;

  ok = sl_output_stream_write_quoted_prompt(softline, prompt) == SL_OK &&
       sl_output_stream_end(softline) == SL_OK;
  sl_destroy(softline);
  free(prompt);
  close(output_fd);
  return ok ? 0 : 6;
}

int main(void) {
  pid_t child;
  int status;
  child = fork();
  if (child < 0) {
    perror("fork");
    return 1;
  }
  if (child == 0)
    _exit(check_bounded_quote());
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    fprintf(stderr,
            "quoted prompt exceeded its bounded memory budget (status %d)\n",
            status);
    return 1;
  }
  return 0;
}
