#include "terminal_pause.h"
#include <poll.h>
#include <sys/select.h>

struct fixture {
  pid_t child;
  int input, output, finish_frame, fail_drain;
  char bytes[4096];
  size_t length;
};
static int stopped(pid_t pid) {
  char status[2048];
  return !proc_text(pid, "status", status, sizeof(status)) &&
         (strstr(status, "State:\tT") || strstr(status, "State:\tt"));
}
static int drain(void *value) {
  struct fixture *f = (struct fixture *)value;
  struct pollfd descriptor;
  ssize_t count;
  if (stopped(f->child)) {
    if (f->fail_drain)
      return 1;
    if (f->finish_frame) {
      if (write(f->input, "F", 1) != 1)
        return 1;
      f->finish_frame = 0;
    }
  }
  descriptor.fd = f->output;
  descriptor.events = POLLIN;
  if (poll(&descriptor, 1, 10) < 0)
    return 1;
  if (descriptor.revents & POLLIN) {
    count =
        read(f->output, f->bytes + f->length, sizeof(f->bytes) - f->length - 1);
    if (count <= 0)
      return 1;
    f->length += (size_t)count;
    f->bytes[f->length] = 0;
  }
  return 0;
}
static int wait_marker(struct fixture *f, const char *marker, size_t offset) {
  double deadline = pause_clock() + 5;
  while (!strstr(f->bytes + offset, marker)) {
    if (pause_clock() >= deadline || drain(f))
      return 1;
  }
  return 0;
}
static int actor(void) {
  char byte;
  int turn = 0, status;
  pid_t worker = fork();
  struct pollfd descriptor;
  fd_set input;
  if (worker < 0)
    return 2;
  if (worker == 0) {
    for (;;)
      pause();
  }
  if (write(1, "MIDFRAME\n", 9) != 9)
    return 2;
  FD_ZERO(&input);
  FD_SET(0, &input);
  if (select(1, &input, NULL, NULL, NULL) < 0 || read(0, &byte, 1) != 1 ||
      byte != 'F')
    return 2;
  if (write(1, "FRAME\n", 6) != 6)
    return 2;
  descriptor.fd = 0;
  descriptor.events = POLLIN;
  for (;;) {
    if (turn++ % 2) {
      FD_ZERO(&input);
      FD_SET(0, &input);
      if (select(1, &input, NULL, NULL, NULL) < 0)
        return 2;
    } else if (poll(&descriptor, 1, -1) < 0)
      return 2;
    if (read(0, &byte, 1) != 1)
      return 2;
    if (byte == 'Q')
      break;
    if (byte != 'I' || write(1, "INPUT\n", 6) != 6)
      return 2;
  }
  kill(worker, SIGCONT);
  kill(worker, SIGTERM);
  waitpid(worker, &status, 0);
  return 0;
}
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "Coordination assertion: %s\n%s\n", #c, f.bytes);        \
      failed = 1;                                                              \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)
int main(void) {
  struct fixture f;
  int input[2], output[2], failed = 0, status;
  size_t offset;
  pid_t worker = -1;
  memset(&f, 0, sizeof(f));
  f.child = -1;
  f.input = f.output = -1;
  CHECK(pipe(input) == 0);
  CHECK(pipe(output) == 0);
  f.child = fork();
  CHECK(f.child >= 0);
  if (f.child == 0) {
    setsid();
    close(input[1]);
    close(output[0]);
    dup2(input[0], 0);
    dup2(output[1], 1);
    close(input[0]);
    close(output[1]);
    _exit(actor());
  }
  close(input[0]);
  close(output[1]);
  f.input = input[1];
  f.output = output[0];
  CHECK(wait_marker(&f, "MIDFRAME\n", 0) == 0);
  f.finish_frame = 1;
  CHECK(pause_editor(f.child, drain, &f) == 0);
  CHECK(stopped(f.child) && strstr(f.bytes, "\nFRAME\n"));
  offset = f.length;
  CHECK(write(f.input, "I", 1) == 1);
  CHECK(drain(&f) == 0);
  CHECK(!strstr(f.bytes + offset, "INPUT\n"));
  CHECK(kill(f.child, SIGCONT) == 0);
  CHECK(wait_marker(&f, "INPUT\n", offset) == 0);
  /* Caller failure after an acquired barrier must resume its editor. */
  CHECK(pause_editor(f.child, drain, &f) == 0);
  CHECK(kill(f.child, SIGCONT) == 0);
  offset = f.length;
  CHECK(write(f.input, "I", 1) == 1);
  CHECK(wait_marker(&f, "INPUT\n", offset) == 0);
  /* A drain failure while stopped must be undone by the acquisition helper. */
  f.fail_drain = 1;
  CHECK(pause_editor(f.child, drain, &f) != 0);
  f.fail_drain = 0;
  offset = f.length;
  CHECK(write(f.input, "I", 1) == 1);
  CHECK(wait_marker(&f, "INPUT\n", offset) == 0);
  CHECK(pause_worker(f.child, drain, &f, &worker) == 0);
  CHECK(worker > 0 && stopped(worker));
  CHECK(kill(worker, SIGCONT) == 0);
  worker = -1;
  /* Unpaused diagnostics remain ordinary input; no pause API is called. */
  CHECK(!stopped(f.child));
  offset = f.length;
  CHECK(write(f.input, "I", 1) == 1);
  CHECK(wait_marker(&f, "INPUT\n", offset) == 0);
  CHECK(write(f.input, "Q", 1) == 1);
  CHECK(waitpid(f.child, &status, 0) == f.child);
  f.child = -1;
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
cleanup:
  if (worker > 0)
    kill(worker, SIGCONT);
  if (f.child > 0) {
    kill(f.child, SIGCONT);
    kill(-f.child, SIGTERM);
    waitpid(f.child, &status, 0);
  }
  if (f.input >= 0)
    close(f.input);
  if (f.output >= 0)
    close(f.output);
  return failed;
}
