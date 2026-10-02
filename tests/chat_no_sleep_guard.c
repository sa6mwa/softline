#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <unistd.h>

/* A zero-delay example must complete even when any nanosleep aborts its
 * producer. This test-only interposer is never installed or shipped. */
int nanosleep(const struct timespec *request, struct timespec *remaining) {
  (void)request;
  (void)remaining;
  _exit(97);
}
