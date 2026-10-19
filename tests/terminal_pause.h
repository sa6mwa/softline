#ifndef SOFTLINE_TEST_TERMINAL_PAUSE_H
#define SOFTLINE_TEST_TERMINAL_PAUSE_H
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Linux-only coordination of this test's own editor/producer. No lifecycle
 * supervision: the caller owns the fixture job and resumes it after resizing.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static double pause_clock(void) {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (double)time.tv_sec + time.tv_nsec / 1000000000.0;
}

static int proc_text(pid_t pid, const char *member, char *bytes,
                     size_t capacity) {
  char path[128];
  FILE *file;
  size_t count;
  snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid, member);
  file = fopen(path, "r");
  if (!file)
    return 1;
  count = fread(bytes, 1, capacity - 1, file);
  bytes[count] = 0;
  if (ferror(file) || count == capacity - 1) {
    fclose(file);
    return 1;
  }
  fclose(file);
  return 0;
}
static int pause_editor(pid_t pid, int (*drain)(void *), void *context) {
  char waiting[128], before[512], after[512], path[128];
  double deadline = pause_clock() + 5;
  int status, stopped = 0, input = -1, pending;
  pid_t event;
  while (pause_clock() < deadline) {
    if (proc_text(pid, "wchan", waiting, sizeof(waiting)) ||
        proc_text(pid, "syscall", before, sizeof(before)))
      goto failure;
    if (!strstr(waiting, "poll") || !strncmp(before, "running", 7)) {
      if (drain(context))
        goto failure;
      continue;
    }
    if (kill(pid, SIGSTOP))
      goto failure;
    stopped = 1;
    do {
      event = waitpid(pid, &status, WNOHANG | WUNTRACED);
      if (event == pid) {
        if (!WIFSTOPPED(status))
          goto failure;
        break;
      }
      if (event < 0 || pause_clock() >= deadline || drain(context))
        goto failure;
    } while (1);
    if (proc_text(pid, "syscall", after, sizeof(after)) || drain(context))
      goto failure;
    snprintf(path, sizeof(path), "/proc/%ld/fd/0", (long)pid);
    input = open(path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
    if (input < 0 || ioctl(input, FIONREAD, &pending))
      goto failure;
    close(input);
    input = -1;
    if (!strcmp(before, after) && pending == 0)
      return 0;
    if (kill(pid, SIGCONT))
      goto failure;
    stopped = 0;
    if (drain(context))
      goto failure;
  }
failure:
  if (input >= 0)
    close(input);
  if (stopped)
    kill(pid, SIGCONT);
  fprintf(stderr, "Owned editor did not reach a drained poll boundary\n");
  return 1;
}
static int pause_worker(pid_t parent, int (*drain)(void *), void *context,
                        pid_t *worker) {
  char member[128], children[128], *end;
  long value;
  int index;
  *worker = -1;
  snprintf(member, sizeof(member), "task/%ld/children", (long)parent);
  if (proc_text(parent, member, children, sizeof(children)))
    return 1;
  value = strtol(children, &end, 10);
  if (end == children)
    return 0;
  while (*end == ' ' || *end == '\n')
    ++end;
  if (*end || value <= 0) {
    fprintf(stderr, "Unexpected chat workers\n");
    return 1;
  }
  if (kill((pid_t)value, SIGSTOP))
    return errno == ESRCH ? 0 : 1;
  *worker = (pid_t)value;
  for (index = 0; index < 3; ++index)
    if (drain(context)) {
      kill(*worker, SIGCONT);
      *worker = -1;
      return 1;
    }
  return 0;
}
#endif
