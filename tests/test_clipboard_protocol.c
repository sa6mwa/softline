#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "protocol assertion failed: %s (%s, sequence %u)\n", #c, \
              mode, sequence);                                                 \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)

static void le16(unsigned char *p, unsigned value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
}
static void le32(unsigned char *p, size_t value) {
  le16(p, (unsigned)value);
  le16(p + 2, (unsigned)(value >> 16));
}
/* Return zero only for an ordinary boundary EOF, never a partial message. */
static int receive(int fd, unsigned char *data, size_t length) {
  size_t offset = 0;
  ssize_t count;
  while (offset < length) {
    count = read(fd, data + offset, length - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count == 0)
      return offset ? -1 : 0;
    if (count < 0 && errno == ECONNRESET && offset == 0)
      return 0;
    if (count < 0)
      return -1;
    offset += (size_t)count;
  }
  return 1;
}
static int send_all(int fd, const unsigned char *data, size_t length) {
  ssize_t count;
  while (length) {
    count = write(fd, data, length);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return -1;
    data += count;
    length -= (size_t)count;
  }
  return 0;
}
static unsigned char *load(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  long size;
  unsigned char *bytes = NULL;
  if (!file)
    return NULL;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET))
    goto done;
  bytes = (unsigned char *)malloc((size_t)size + 1);
  if (!bytes)
    goto done;
  if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    bytes = NULL;
  } else
    *length = (size_t)size;
done:
  fclose(file);
  return bytes;
}
static int probe(const char *fixture, const char *directory, const char *mode,
                 unsigned sequence, const unsigned char *payload, size_t length,
                 size_t chunk_size, int broken, unsigned wrapped_opcode,
                 int natural_wrap) {
  int pair[2] = {-1, -1}, result = 1, status, received, initial = 1;
  pid_t child = -1;
  struct timeval timeout;
  unsigned char head[4], request[262144], reply[32], size_bytes[4];
  unsigned char padding[3] = {0, 0, 0}, *actual = NULL;
  const unsigned char *data;
  size_t served = 0, count, actual_length = 0;
  unsigned requests = 0, opcode, serial, wraps = 0;
  char fd_text[32], sequence_text[32], output[4096];
  snprintf(output, sizeof(output), "%s/image.png", directory);
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  timeout.tv_sec = 12;
  timeout.tv_usec = 0;
  CHECK(setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) == 0);
  CHECK(setsockopt(pair[1], SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) == 0);
  snprintf(fd_text, sizeof(fd_text), "%d", pair[0]);
  snprintf(sequence_text, sizeof(sequence_text), "%u", sequence);
  if (broken) {
    close(pair[1]);
    pair[1] = -1;
  }
  child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    if (pair[1] >= 0)
      close(pair[1]);
    execl(fixture, fixture, mode, fd_text, sequence_text, output, (char *)NULL);
    _exit(127);
  }
  close(pair[0]);
  pair[0] = -1;
  while (!broken && (received = receive(pair[1], head, sizeof(head))) != 0) {
    CHECK(received == 1);
    count = ((unsigned)head[2] | (unsigned)head[3] << 8) * 4;
    CHECK(count >= 4 && count - 4 <= sizeof(request));
    if (count > 4)
      CHECK(receive(pair[1], request, count - 4) == 1);
    ++requests;
    serial = (sequence + requests) & 65535;
    opcode = head[0];
    if (serial == 0) {
      CHECK(opcode == wrapped_opcode);
      ++wraps;
    }
    memset(reply, 0, sizeof(reply));
    reply[0] = 1;
    le16(reply + 2, serial);
    if (opcode == 16) {
      le32(reply + 8, 30);
      CHECK(send_all(pair[1], reply, sizeof(reply)) == 0);
    } else if (opcode == 20) {
      if (initial) {
        le32(size_bytes, length);
        data = size_bytes;
        count = 4;
        reply[1] = 32;
        le32(reply + 8, 40);
        le32(reply + 16, 1);
        initial = 0;
      } else {
        count = length - served;
        if (count > chunk_size)
          count = chunk_size;
        data = payload + served;
        served += count;
        reply[1] = 8;
        le32(reply + 8, 30);
        le32(reply + 16, count);
      }
      le32(reply + 4, (count + 3) / 4);
      CHECK(send_all(pair[1], reply, sizeof(reply)) == 0);
      CHECK(send_all(pair[1], data, count) == 0);
      CHECK(send_all(pair[1], padding, (4 - count % 4) % 4) == 0);
    } else if (opcode == 18 || opcode == 19 || opcode == 24) {
      reply[0] = (unsigned char)(opcode == 24 ? 31 : 28);
      if (opcode == 24) {
        le32(reply + 4, 123);
        le32(reply + 8, 10);
        le32(reply + 12, 25);
        le32(reply + 16, 30);
        le32(reply + 20, 20);
      } else {
        le32(reply + 4, 10);
        le32(reply + 8, 20);
        le32(reply + 12, 123);
      }
      received = send_all(pair[1], reply, sizeof(reply));
      CHECK(received == 0 ||
            (opcode == 19 && (errno == EPIPE || errno == ECONNRESET)));
    } else
      CHECK(opcode == 1);
  }
  CHECK(waitpid(child, &status, 0) == child);
  child = -1;
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == (broken ? 1 : 0));
  if (!broken && wrapped_opcode)
    CHECK(wraps == 1);
  if (natural_wrap)
    CHECK(requests > 65536);
  if (!broken && length) {
    actual = load(output, &actual_length);
    CHECK(actual && actual_length == length &&
          memcmp(actual, payload, length) == 0);
    CHECK(served == length);
  }
  result = 0;
cleanup:
  if (child > 0) {
    kill(child, SIGTERM);
    waitpid(child, &status, 0);
  }
  if (pair[0] >= 0)
    close(pair[0]);
  if (pair[1] >= 0)
    close(pair[1]);
  free(actual);
  unlink(output);
  return result;
}
int main(int argc, char **argv) {
  const char *modes[] = {"atom", "window", "delete", "timestamp", "select"};
  const unsigned opcodes[] = {16, 1, 19, 18, 24};
  char directory[4096];
  size_t small_length = 0, large_length = 0, i;
  unsigned char *small = NULL, *large = NULL;
  int result = 1;
  if (argc != 5)
    return 2;
  signal(SIGPIPE, SIG_IGN);
  snprintf(directory, sizeof(directory), "%s/clipboard-protocol.XXXXXX",
           argv[2]);
  if (!mkdtemp(directory))
    return 1;
  small = load(argv[3], &small_length);
  large = load(argv[4], &large_length);
  if (!small || !large || large_length < 17 * 1024 * 1024)
    goto done;
  for (i = 0; i < 5; ++i) {
    unsigned sequence = i == 4 ? 65534 : 65535;
    if (probe(argv[1], directory, modes[i], sequence, NULL, 0, 512, 0,
              opcodes[i], 0) ||
        probe(argv[1], directory, modes[i], sequence, NULL, 0, 512, 1, 0, 0))
      goto done;
  }
  if (probe(argv[1], directory, "transfer", 65534, small, small_length, 512, 0,
            19, 0) ||
      probe(argv[1], directory, "transfer", 65535, small, small_length, 512, 0,
            20, 0) ||
      probe(argv[1], directory, "transfer", 0, large, large_length, 512, 0, 19,
            1) ||
      probe(argv[1], directory, "transfer", 0, large, large_length, 4096, 0, 0,
            0))
    goto done;
  result = 0;
done:
  free(small);
  free(large);
  rmdir(directory);
  return result;
}
