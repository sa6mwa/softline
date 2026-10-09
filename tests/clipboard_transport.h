#ifndef SOFTLINE_TEST_CLIPBOARD_TRANSPORT_H
#define SOFTLINE_TEST_CLIPBOARD_TRANSPORT_H
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

static int send_bytes(int fd, const unsigned char *bytes, size_t length) {
  ssize_t count;
  while (length) {
    count = send(fd, bytes, length, MSG_NOSIGNAL);
    if (count <= 0)
      return 1;
    bytes += count;
    length -= (size_t)count;
  }
  return 0;
}
static int receive(int fd, unsigned char *bytes, size_t length) {
  ssize_t count;
  while (length) {
    count = read(fd, bytes, length);
    if (count <= 0)
      return 1;
    bytes += count;
    length -= (size_t)count;
  }
  return 0;
}
static int listen_display(int family, int first, int last, int *number) {
  struct sockaddr_in v4;
  struct sockaddr_in6 v6;
  int listener = socket(family, SOCK_STREAM, 0), one = 1;
  if (listener < 0)
    return -1;
  if (family == AF_INET6 &&
      setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one))) {
    close(listener);
    return -1;
  }
  memset(&v4, 0, sizeof(v4));
  memset(&v6, 0, sizeof(v6));
  v4.sin_family = AF_INET;
  v4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  v6.sin6_family = AF_INET6;
  v6.sin6_addr = in6addr_loopback;
  for (*number = first; *number < last; ++*number) {
    v4.sin_port = htons((unsigned short)(6000 + *number));
    v6.sin6_port = v4.sin_port;
    if (!bind(listener,
              family == AF_INET6 ? (struct sockaddr *)&v6
                                 : (struct sockaddr *)&v4,
              family == AF_INET6 ? sizeof(v6) : sizeof(v4))) {
      if (!listen(listener, 1))
        return listener;
      break;
    }
  }
  close(listener);
  return -1;
}
static int connect_original(const char *display) {
  const char *colon = strrchr(display, ':');
  struct sockaddr_un unix_address;
  int fd;
  if (!colon)
    return -1;
  if (colon == display) {
    memset(&unix_address, 0, sizeof(unix_address));
    unix_address.sun_family = AF_UNIX;
    snprintf(unix_address.sun_path, sizeof(unix_address.sun_path),
             "/tmp/.X11-unix/X%d", atoi(colon + 1));
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0 &&
        connect(fd, (struct sockaddr *)&unix_address, sizeof(unix_address))) {
      close(fd);
      return -1;
    }
    return fd;
  } else {
    struct addrinfo hints, *addresses, *at;
    char host[256], port[32];
    size_t length = (size_t)(colon - display);
    if (length >= sizeof(host))
      return -1;
    memcpy(host, display, length);
    host[length] = 0;
    snprintf(port, sizeof(port), "%d", 6000 + atoi(colon + 1));
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses))
      return -1;
    fd = -1;
    for (at = addresses; at; at = at->ai_next) {
      fd = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
      if (fd >= 0 && !connect(fd, at->ai_addr, at->ai_addrlen))
        break;
      if (fd >= 0)
        close(fd);
      fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
  }
}
static int auth_record(FILE *file, unsigned family,
                       const unsigned char *address, size_t length, int number,
                       const unsigned char cookie[16]) {
  const char *name = "MIT-MAGIC-COOKIE-1";
  char display[32];
  const unsigned char *fields[4];
  size_t sizes[4], index;
  unsigned char encoded[2];
  snprintf(display, sizeof(display), "%d", number);
  fields[0] = address;
  sizes[0] = length;
  fields[1] = (const unsigned char *)display;
  sizes[1] = strlen(display);
  fields[2] = (const unsigned char *)name;
  sizes[2] = strlen(name);
  fields[3] = cookie;
  sizes[3] = 16;
  encoded[0] = (unsigned char)(family >> 8);
  encoded[1] = (unsigned char)family;
  if (fwrite(encoded, 1, 2, file) != 2)
    return 1;
  for (index = 0; index < 4; ++index) {
    encoded[0] = (unsigned char)(sizes[index] >> 8);
    encoded[1] = (unsigned char)sizes[index];
    if (fwrite(encoded, 1, 2, file) != 2 ||
        fwrite(fields[index], 1, sizes[index], file) != sizes[index])
      return 1;
  }
  return 0;
}
static int write_auth(const char *path, int number, int family,
                      const unsigned char cookie[16],
                      const unsigned char *decoy) {
  unsigned char address[256];
  size_t length = 0;
  FILE *file;
  int failed = 0;
  if (family == 256) {
    if (gethostname((char *)address, sizeof(address)))
      return 1;
    length = strlen((char *)address);
  } else if (family == 0) {
    inet_pton(AF_INET, "127.0.0.1", address);
    length = 4;
  } else if (family == 6) {
    inet_pton(AF_INET6, "::1", address);
    length = 16;
  }
  file = fopen(path, "wb");
  if (!file)
    return 1;
  if (decoy) {
    int decoy_family = family == 256 ? 65535 : family == 0 ? 6 : 0;
    unsigned char other[16];
    size_t other_length = decoy_family == 65535 ? 0
                          : decoy_family == 6   ? 16
                                                : 4;
    memset(other, 0, sizeof(other));
    if (decoy_family == 6)
      inet_pton(AF_INET6, "::1", other);
    else if (decoy_family == 0)
      inet_pton(AF_INET, "127.0.0.1", other);
    failed = auth_record(file, (unsigned)decoy_family, other, other_length,
                         number, decoy);
  }
  if (!failed)
    failed =
        auth_record(file, (unsigned)family, address, length, number, cookie);
  if (fclose(file) || chmod(path, 0600))
    failed = 1;
  return failed;
}
static int read_cookie(const char *path, int number, unsigned char cookie[16]) {
  FILE *file = path ? fopen(path, "rb") : NULL;
  unsigned char encoded[2], fields[4][256];
  unsigned family, lengths[4], index;
  char display[32];
  if (!file)
    return 1;
  snprintf(display, sizeof(display), "%d", number);
  while (fread(encoded, 1, 2, file) == 2) {
    family = encoded[0] * 256u + encoded[1];
    for (index = 0; index < 4; ++index) {
      if (fread(encoded, 1, 2, file) != 2)
        goto missing;
      lengths[index] = encoded[0] * 256u + encoded[1];
      if (lengths[index] >= 256 ||
          fread(fields[index], 1, lengths[index], file) != lengths[index])
        goto missing;
      fields[index][lengths[index]] = 0;
    }
    if ((family == 0 || family == 6 || family == 256 || family == 65535) &&
        !strcmp((char *)fields[1], display) &&
        !strcmp((char *)fields[2], "MIT-MAGIC-COOKIE-1") && lengths[3] == 16) {
      memcpy(cookie, fields[3], 16);
      fclose(file);
      return 0;
    }
  }
missing:
  fclose(file);
  return 1;
}
static int relay_display(int listener, const char *original,
                         const unsigned char cookie[16]) {
  int client = -1, server = -1, failed = 1, index;
  struct pollfd pollers[2];
  unsigned char header[12], body[512], bytes[8192];
  size_t name_length, data_length, data_offset, length;
  ssize_t count;
  struct timeval timeout;
  timeout.tv_sec = 15;
  timeout.tv_usec = 0;
  pollers[0].fd = listener;
  pollers[0].events = POLLIN;
  if (poll(pollers, 1, 15000) <= 0)
    goto cleanup;
  client = accept(listener, NULL, NULL);
  if (client < 0)
    goto cleanup;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  server = connect_original(original);
  if (server < 0)
    goto cleanup;
  setsockopt(server, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  if (receive(client, header, sizeof(header)) || header[0] != 'l')
    goto cleanup;
  name_length = header[6] + header[7] * 256u;
  data_length = header[8] + header[9] * 256u;
  data_offset = (name_length + 3) / 4 * 4;
  length = data_offset + (data_length + 3) / 4 * 4;
  if (length > sizeof(body) || name_length != strlen("MIT-MAGIC-COOKIE-1") ||
      data_length != 16 || receive(client, body, length) ||
      memcmp(body, "MIT-MAGIC-COOKIE-1", name_length) ||
      memcmp(body + data_offset, cookie, 16))
    goto cleanup;
  if (send_bytes(server, header, sizeof(header)) ||
      send_bytes(server, body, length))
    goto cleanup;
  pollers[0].fd = client;
  pollers[1].fd = server;
  pollers[0].events = pollers[1].events = POLLIN;
  for (;;) {
    if (poll(pollers, 2, 15000) <= 0)
      goto cleanup;
    for (index = 0; index < 2; ++index)
      if (pollers[index].revents & (POLLIN | POLLHUP)) {
        count = read(pollers[index].fd, bytes, sizeof(bytes));
        if (count == 0 || (count < 0 && errno == ECONNRESET)) {
          failed = 0;
          goto cleanup;
        }
        if (count < 0 ||
            send_bytes(pollers[1 - index].fd, bytes, (size_t)count))
          goto cleanup;
      }
  }
cleanup:
  if (client >= 0)
    close(client);
  if (server >= 0)
    close(server);
  close(listener);
  return failed;
}
#endif
