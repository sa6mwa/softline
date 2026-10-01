#include "softline_clipboard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
static char *sl_clipboard_expand_template(const char *path_template);
#endif

static const char *sl_clipboard_token_value(const char *token, size_t length,
                                            const char *home,
                                            const char *cache) {
  if ((length == 4 && memcmp(token, "HOME", 4) == 0) ||
      (length == 4 && memcmp(token, "home", 4) == 0))
    return home;
  if ((length == 14 && memcmp(token, "XDG_CACHE_HOME", 14) == 0) ||
      (length == 14 && memcmp(token, "xdg_cache_home", 14) == 0))
    return cache;
  return NULL;
}

int sl_clipboard_valid_path_template(const char *path_template) {
  const char *cursor, *end, *star;
  if (!path_template)
    return 1;
  star = strchr(path_template, '*');
  if (!star || strchr(star + 1, '*') ||
      path_template[strlen(path_template) - 1] == '/')
    return 0;
  if (path_template[0] != '/' &&
      !(path_template[0] == '~' && path_template[1] == '/') &&
      strncmp(path_template, "{{", 2) != 0)
    return 0;
  for (cursor = path_template; *cursor;) {
    if (cursor[0] == '}' && cursor[1] == '}')
      return 0;
    if (cursor[0] == '{' && cursor[1] == '{') {
      end = strstr(cursor + 2, "}}");
      if (!end || !sl_clipboard_token_value(
                      cursor + 2, (size_t)(end - cursor - 2), "home", "cache"))
        return 0;
      cursor = end + 2;
    } else {
      cursor++;
    }
  }
  return 1;
}

void sl_clipboard_discard_image(const char *path, const char *path_template) {
  char *directory;
  char *expanded = NULL;
  char *slash;
  const char *star;
  size_t id_end;
  if (!path)
    return;
  (void)unlink(path);
#if defined(__linux__)
  if (path_template) {
    expanded = sl_clipboard_expand_template(path_template);
    path_template = expanded;
  }
#endif
  if (!path_template || !(star = strchr(path_template, '*')) ||
      !strchr(star, '/')) {
    free(expanded);
    return;
  }
  id_end = (size_t)(star - path_template) + 20;
  directory = strdup(path);
  if (!directory) {
    free(expanded);
    return;
  }
  slash = strrchr(directory, '/');
  while (slash && (size_t)(slash - directory) >= id_end) {
    *slash = '\0';
    if (rmdir(directory) != 0)
      break;
    slash = strrchr(directory, '/');
  }
  free(directory);
  free(expanded);
}

#if defined(__linux__)

#include "softline_resolver.h"
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#define SL_IMAGE_MAX_BYTES (32u * 1024u * 1024u)
#define SL_CLIPBOARD_TIMEOUT_MS 10000
#define SL_X11_MAX_PACKET (1024u * 1024u + 32u)
#define SL_X11_PROPERTY_NOTIFY 28
#define SL_X11_SELECTION_NOTIFY 31
#define SL_X11_PROPERTY_CHANGE_MASK (1u << 22)
#define SL_X11_WINDOW_EVENT_MASK (1u << 11)
#define SL_X11_ATOM_STRING 31

typedef struct {
  int fd;
  uint16_t sequence;
  uint32_t resource_base, resource_mask, root;
  int64_t deadline;
  int peer_family;
  unsigned char peer_address[16];
} sl_x11_t;

typedef struct {
  char host[256];
  unsigned int display, screen;
  int local;
} sl_x11_display_t;

static void sl_clipboard_error(char *error, size_t size, const char *message) {
  if (error && size)
    (void)snprintf(error, size, "%s", message);
}

static uint16_t sl_get16(const unsigned char *p) {
  return (uint16_t)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

static uint32_t sl_get32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void sl_put16(unsigned char *p, uint16_t value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
}

static void sl_put32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
  p[2] = (unsigned char)(value >> 16);
  p[3] = (unsigned char)(value >> 24);
}

static int64_t sl_now_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return -1;
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int sl_x11_poll(sl_x11_t *x, short events) {
  struct pollfd fd;
  int64_t remaining;
  int result;
  fd.fd = x->fd;
  fd.events = events;
  for (;;) {
    remaining = x->deadline - sl_now_ms();
    if (remaining <= 0 || remaining > SL_CLIPBOARD_TIMEOUT_MS)
      return -1;
    fd.revents = 0;
    result = poll(&fd, 1, (int)remaining);
    if (result < 0 && errno == EINTR)
      continue;
    return result > 0 && (fd.revents & events) &&
                   !(fd.revents & (POLLERR | POLLHUP | POLLNVAL))
               ? 0
               : -1;
  }
}

static int sl_x11_io(sl_x11_t *x, void *data, size_t length, int writing) {
  unsigned char *bytes = data;
  ssize_t count;
  while (length) {
    /* A responsive peer must not extend the paste's overall deadline. */
    if (sl_now_ms() >= x->deadline)
      return -1;
    count = writing ? send(x->fd, bytes, length, MSG_NOSIGNAL)
                    : recv(x->fd, bytes, length, 0);
    if (count > 0) {
      bytes += count;
      length -= (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
        sl_x11_poll(x, writing ? POLLOUT : POLLIN) == 0)
      continue;
    return -1;
  }
  return 0;
}

static int sl_x11_parse_display(const char *value, sl_x11_display_t *display) {
  const char *colon, *end;
  size_t host_len;
  unsigned long number;
  char *parsed;
  if (!value || !*value)
    return -1;
  memset(display, 0, sizeof(*display));
  colon = strrchr(value, ':');
  if (!colon || !colon[1])
    return -1;
  host_len = (size_t)(colon - value);
  if (host_len >= sizeof(display->host))
    return -1;
  memcpy(display->host, value, host_len);
  display->host[host_len] = '\0';
  if (host_len >= 2 && display->host[0] == '[' &&
      display->host[host_len - 1] == ']') {
    memmove(display->host, display->host + 1, host_len - 2);
    display->host[host_len - 2] = '\0';
  }
  if (strcmp(display->host, "unix") == 0 ||
      strcmp(display->host, "unix/") == 0 || !display->host[0])
    display->local = 1;
  number = strtoul(colon + 1, &parsed, 10);
  if (parsed == colon + 1 || number > 59535)
    return -1;
  display->display = (unsigned int)number;
  end = parsed;
  if (*end == '.') {
    number = strtoul(end + 1, &parsed, 10);
    if (parsed == end + 1 || number > 255)
      return -1;
    display->screen = (unsigned int)number;
    end = parsed;
  }
  return *end == '\0' ? 0 : -1;
}

static int sl_x11_connect(sl_x11_t *x, const sl_x11_display_t *display) {
  int fd = -1, result, flags;
  socklen_t result_size;
  struct sockaddr_storage peer;
  struct sockaddr_un unix_addr;
  struct ares_addrinfo *addresses = NULL;
  struct ares_addrinfo_node *item;
  char port[8];
  if (display->local) {
    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    (void)snprintf(unix_addr.sun_path, sizeof(unix_addr.sun_path),
                   "/tmp/.X11-unix/X%u", display->display);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
      return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
      goto fail;
    result = connect(fd, (struct sockaddr *)&unix_addr, sizeof(unix_addr));
    if (result < 0 && errno != EINPROGRESS)
      goto fail;
    x->fd = fd;
    if (result < 0 && sl_x11_poll(x, POLLOUT) != 0)
      goto fail;
  } else {
    (void)snprintf(port, sizeof(port), "%u", 6000 + display->display);
    if (sl_resolve(display->host, port, x->deadline, NULL, &addresses) != 0)
      return -1;
    for (item = addresses->nodes; item; item = item->ai_next) {
      fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
      if (fd < 0)
        continue;
      flags = fcntl(fd, F_GETFL, 0);
      if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        fd = -1;
        continue;
      }
      result = connect(fd, item->ai_addr, item->ai_addrlen);
      if (result < 0 && errno != EINPROGRESS) {
        close(fd);
        fd = -1;
        continue;
      }
      x->fd = fd;
      if (result == 0 || sl_x11_poll(x, POLLOUT) == 0) {
        result_size = sizeof(result);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &result_size) == 0 &&
            result == 0)
          break;
      }
      close(fd);
      fd = -1;
      x->fd = -1;
    }
    sl_resolve_free(addresses);
    if (fd < 0)
      return -1;
  }
  result_size = sizeof(result);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &result_size) != 0 ||
      result != 0)
    goto fail;
  result_size = sizeof(peer);
  if (getpeername(fd, (struct sockaddr *)&peer, &result_size) != 0)
    goto fail;
  x->peer_family = peer.ss_family;
  if (peer.ss_family == AF_INET)
    memcpy(x->peer_address, &((const struct sockaddr_in *)&peer)->sin_addr, 4);
  else if (peer.ss_family == AF_INET6)
    memcpy(x->peer_address, &((const struct sockaddr_in6 *)&peer)->sin6_addr,
           16);
  else if (peer.ss_family != AF_UNIX)
    goto fail;
  return 0;
fail:
  close(fd);
  x->fd = -1;
  return -1;
}

/* Xauthority records use network-order lengths even for little-endian X11. */
static int sl_auth_field(FILE *file, unsigned char *value, size_t capacity,
                         size_t *length) {
  unsigned char size[2];
  if (fread(size, 1, 2, file) != 2)
    return -1;
  *length = ((size_t)size[0] << 8) | size[1];
  if (*length > capacity || fread(value, 1, *length, file) != *length)
    return -1;
  return 0;
}

/* A hostname such as localhost may connect over either address family. Match
 * the credential to the connected peer, not to the DISPLAY spelling. */
static int sl_x11_authority(const sl_x11_display_t *display, const sl_x11_t *x,
                            unsigned char *cookie, size_t *cookie_len) {
  static const unsigned char ipv6_loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 0, 0, 1};
  const char *path = getenv("XAUTHORITY"), *home;
  char fallback[512], hostname[256], number[16];
  unsigned char family_bytes[2], address[256], index[32], name[64], data[256];
  size_t address_len, index_len, name_len, data_len;
  int family, score, best = 0, loopback;
  FILE *file;
  if (!path || !*path) {
    home = getenv("HOME");
    if (!home || strlen(home) + sizeof("/.Xauthority") > sizeof(fallback))
      return 0;
    (void)snprintf(fallback, sizeof(fallback), "%s/.Xauthority", home);
    path = fallback;
  }
  file = fopen(path, "rb");
  if (!file)
    return 0;
  if (gethostname(hostname, sizeof(hostname)) != 0)
    hostname[0] = '\0';
  hostname[sizeof(hostname) - 1] = '\0';
  (void)snprintf(number, sizeof(number), "%u", display->display);
  loopback = (x->peer_family == AF_INET && x->peer_address[0] == 127) ||
             (x->peer_family == AF_INET6 &&
              memcmp(x->peer_address, ipv6_loopback, 16) == 0);
  while (fread(family_bytes, 1, 2, file) == 2) {
    family = ((int)family_bytes[0] << 8) | family_bytes[1];
    if (sl_auth_field(file, address, sizeof(address), &address_len) != 0 ||
        sl_auth_field(file, index, sizeof(index), &index_len) != 0 ||
        sl_auth_field(file, name, sizeof(name), &name_len) != 0 ||
        sl_auth_field(file, data, sizeof(data), &data_len) != 0)
      break;
    if (index_len != strlen(number) || memcmp(index, number, index_len) ||
        name_len != 18 || memcmp(name, "MIT-MAGIC-COOKIE-1", 18) || !data_len)
      continue;
    score = 0;
    if (family == 65535)
      score = 1;
    if ((x->peer_family == AF_UNIX || loopback) && family == 256 &&
        address_len == strlen(hostname) &&
        memcmp(address, hostname, address_len) == 0)
      score = 3;
    if (x->peer_family == AF_INET && family == 0 && address_len == 4 &&
        memcmp(address, x->peer_address, 4) == 0)
      score = 4;
    if (x->peer_family == AF_INET6 && family == 6 && address_len == 16 &&
        memcmp(address, x->peer_address, 16) == 0)
      score = 4;
    if (score > best) {
      memcpy(cookie, data, data_len);
      *cookie_len = data_len;
      best = score;
    }
  }
  fclose(file);
  return best;
}

static int sl_x11_setup(sl_x11_t *x, const sl_x11_display_t *display,
                        int *rejected) {
  unsigned char cookie[256], request[8 + 20 + 256 + 4], head[8];
  unsigned char *reply;
  size_t cookie_len = 0, length, cursor;
  uint16_t vendor_len;
  unsigned int formats, roots, index;
  uint32_t depths, visuals, j;
  (void)sl_x11_authority(display, x, cookie, &cookie_len);
  memset(request, 0, sizeof(request));
  request[0] = 'l';
  sl_put16(request + 2, 11);
  if (cookie_len) {
    sl_put16(request + 6, 18);
    sl_put16(request + 8, (uint16_t)cookie_len);
    memcpy(request + 12, "MIT-MAGIC-COOKIE-1", 18);
    memcpy(request + 12 + 20, cookie, cookie_len);
  }
  length = 12 + (cookie_len ? 20 + ((cookie_len + 3) & ~(size_t)3) : 0);
  if (sl_x11_io(x, request, length, 1) != 0 ||
      sl_x11_io(x, head, sizeof(head), 0) != 0)
    return -1;
  length = (size_t)sl_get16(head + 6) * 4;
  if (head[0] != 1) {
    *rejected = head[0] == 0 || head[0] == 2;
    return -1;
  }
  if (length < 32 || length > SL_X11_MAX_PACKET)
    return -1;
  reply = malloc(length);
  if (!reply)
    return -1;
  if (sl_x11_io(x, reply, length, 0) != 0)
    goto fail;
  x->resource_base = sl_get32(reply + 4);
  x->resource_mask = sl_get32(reply + 8);
  vendor_len = sl_get16(reply + 16);
  roots = reply[20];
  formats = reply[21];
  cursor = 32 + ((vendor_len + 3u) & ~3u) + formats * 8u;
  for (index = 0; index < roots; ++index) {
    if (cursor + 40 > length)
      goto fail;
    if (index == display->screen) {
      x->root = sl_get32(reply + cursor);
      free(reply);
      return x->resource_mask && x->root ? 0 : -1;
    }
    depths = reply[cursor + 39];
    cursor += 40;
    for (j = 0; j < depths; ++j) {
      if (cursor + 8 > length)
        goto fail;
      visuals = sl_get16(reply + cursor + 2);
      cursor += 8 + (size_t)visuals * 24;
      if (cursor > length)
        goto fail;
    }
  }
fail:
  free(reply);
  return -1;
}

/* Status is separate from the wire sequence: zero is valid after wraparound.
 * Requests are synchronous, so reply callers use x->sequence after success. */
static int sl_x11_request(sl_x11_t *x, const unsigned char *data,
                          size_t length) {
  if (sl_x11_io(x, (void *)data, length, 1) != 0)
    return -1;
  ++x->sequence;
  return 0;
}

/* The only subscribed events are PropertyNotify and SelectionNotify. */
static unsigned char *sl_x11_packet(sl_x11_t *x, size_t *size) {
  unsigned char head[32], *packet;
  size_t extra = 0;
  uint32_t units;
  if (sl_x11_io(x, head, sizeof(head), 0) != 0)
    return NULL;
  if (head[0] == 1 || (head[0] & 127) == 35) {
    units = sl_get32(head + 4);
    if (units > (SL_X11_MAX_PACKET - 32) / 4)
      return NULL;
    extra = (size_t)units * 4;
  }
  packet = malloc(32 + extra);
  if (!packet)
    return NULL;
  memcpy(packet, head, 32);
  if (extra && sl_x11_io(x, packet + 32, extra, 0) != 0) {
    free(packet);
    return NULL;
  }
  *size = 32 + extra;
  return packet;
}

static unsigned char *sl_x11_reply(sl_x11_t *x, uint16_t sequence,
                                   size_t *size) {
  unsigned char *packet;
  for (;;) {
    packet = sl_x11_packet(x, size);
    if (!packet)
      return NULL;
    if (packet[0] == 0) {
      free(packet);
      return NULL;
    }
    if (packet[0] == 1 && sl_get16(packet + 2) == sequence)
      return packet;
    free(packet);
  }
}

static unsigned char *sl_x11_event(sl_x11_t *x) {
  unsigned char *packet;
  size_t size;
  for (;;) {
    packet = sl_x11_packet(x, &size);
    if (!packet)
      return NULL;
    if (packet[0] == 0) {
      free(packet);
      return NULL;
    }
    if (packet[0] != 1)
      return packet;
    free(packet);
  }
}

static uint32_t sl_x11_atom(sl_x11_t *x, const char *name) {
  unsigned char request[64], *reply;
  size_t size, length = strlen(name);
  uint32_t atom;
  if (length > sizeof(request) - 8)
    return 0;
  memset(request, 0, sizeof(request));
  request[0] = 16;
  sl_put16(request + 2, (uint16_t)(2 + (length + 3) / 4));
  sl_put16(request + 4, (uint16_t)length);
  memcpy(request + 8, name, length);
  if (sl_x11_request(x, request, 8 + ((length + 3) & ~(size_t)3)) != 0)
    return 0;
  reply = sl_x11_reply(x, x->sequence, &size);
  if (!reply)
    return 0;
  atom = sl_get32(reply + 8);
  free(reply);
  return atom;
}

static int sl_x11_create_window(sl_x11_t *x, uint32_t window) {
  unsigned char request[36];
  memset(request, 0, sizeof(request));
  request[0] = 1;
  sl_put16(request + 2, 9);
  sl_put32(request + 4, window);
  sl_put32(request + 8, x->root);
  sl_put16(request + 16, 1);
  sl_put16(request + 18, 1);
  sl_put16(request + 22, 2);
  sl_put32(request + 28, SL_X11_WINDOW_EVENT_MASK);
  sl_put32(request + 32, SL_X11_PROPERTY_CHANGE_MASK);
  return sl_x11_request(x, request, sizeof(request));
}

static int sl_x11_delete_property(sl_x11_t *x, uint32_t window,
                                  uint32_t property) {
  unsigned char request[12] = {0};
  request[0] = 19;
  sl_put16(request + 2, 3);
  sl_put32(request + 4, window);
  sl_put32(request + 8, property);
  return sl_x11_request(x, request, sizeof(request));
}

static int sl_x11_wait_property(sl_x11_t *x, uint32_t window, uint32_t property,
                                uint32_t *timestamp) {
  unsigned char *event;
  for (;;) {
    event = sl_x11_event(x);
    if (!event)
      return -1;
    if ((event[0] & 127) == SL_X11_PROPERTY_NOTIFY &&
        sl_get32(event + 4) == window && sl_get32(event + 8) == property &&
        event[16] == 0) {
      if (timestamp)
        *timestamp = sl_get32(event + 12);
      free(event);
      return 0;
    }
    free(event);
  }
}

static int sl_x11_timestamp(sl_x11_t *x, uint32_t window, uint32_t property,
                            uint32_t *timestamp) {
  unsigned char request[28] = {0};
  request[0] = 18;
  sl_put16(request + 2, 7);
  sl_put32(request + 4, window);
  sl_put32(request + 8, property);
  sl_put32(request + 12, SL_X11_ATOM_STRING);
  request[16] = 8;
  sl_put32(request + 20, 1);
  request[24] = 'x';
  if (sl_x11_request(x, request, sizeof(request)) != 0)
    return -1;
  return sl_x11_wait_property(x, window, property, timestamp);
}

static int sl_x11_select_image(sl_x11_t *x, uint32_t window, uint32_t selection,
                               uint32_t property, uint32_t target,
                               uint32_t timestamp) {
  unsigned char request[24] = {0}, *event;
  if (sl_x11_delete_property(x, window, property) != 0)
    return -1;
  request[0] = 24;
  sl_put16(request + 2, 6);
  sl_put32(request + 4, window);
  sl_put32(request + 8, selection);
  sl_put32(request + 12, target);
  sl_put32(request + 16, property);
  sl_put32(request + 20, timestamp);
  if (sl_x11_request(x, request, sizeof(request)) != 0)
    return -1;
  for (;;) {
    event = sl_x11_event(x);
    if (!event)
      return -1;
    if ((event[0] & 127) == SL_X11_SELECTION_NOTIFY &&
        sl_get32(event + 8) == window && sl_get32(event + 12) == selection &&
        sl_get32(event + 16) == target) {
      int accepted = sl_get32(event + 20) == property;
      free(event);
      return accepted;
    }
    free(event);
  }
}

static int sl_clipboard_write(int fd, const void *data, size_t length) {
  const unsigned char *bytes = data;
  while (length) {
    ssize_t written = write(fd, bytes, length);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      return -1;
    bytes += written;
    length -= (size_t)written;
  }
  return 0;
}

static int sl_x11_read_value(sl_x11_t *x, uint32_t window, uint32_t property,
                             uint32_t target, int fd, size_t *size,
                             unsigned char *head, size_t *head_len,
                             uint32_t incremental) {
  uint32_t offset = 0;
  int first = 1;
  for (;;) {
    unsigned char request[24] = {0}, *reply;
    size_t packet_size, length, take;
    uint32_t after;
    request[0] = 20;
    sl_put16(request + 2, 6);
    sl_put32(request + 4, window);
    sl_put32(request + 8, property);
    sl_put32(request + 16, offset);
    sl_put32(request + 20, 262144);
    if (sl_x11_request(x, request, sizeof(request)) != 0)
      return -1;
    reply = sl_x11_reply(x, x->sequence, &packet_size);
    if (!reply)
      return -1;
    if (first && sl_get32(reply + 8) == incremental && reply[1] == 32) {
      free(reply);
      return 1;
    }
    first = 0;
    after = sl_get32(reply + 12);
    length = sl_get32(reply + 16);
    if (sl_get32(reply + 8) != target || reply[1] != 8 ||
        length > packet_size - 32 || length > SL_IMAGE_MAX_BYTES - *size ||
        (after && (!length || (length & 3)))) {
      free(reply);
      return -1;
    }
    if (*head_len < 8 && length) {
      take = length;
      if (take > 8 - *head_len)
        take = 8 - *head_len;
      memcpy(head + *head_len, reply + 32, take);
      *head_len += take;
    }
    if (sl_clipboard_write(fd, reply + 32, length) != 0) {
      free(reply);
      return -1;
    }
    *size += length;
    free(reply);
    if (!after)
      return length ? 0 : 2;
    offset += (uint32_t)length / 4;
  }
}

static int sl_x11_transfer(sl_x11_t *x, uint32_t window, uint32_t property,
                           uint32_t target, uint32_t incr, int is_png, int fd) {
  size_t size = 0, head_len = 0;
  unsigned char head[8];
  int result;
  static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
  result = sl_x11_read_value(x, window, property, target, fd, &size, head,
                             &head_len, incr);
  if (result == 1) {
    if (sl_x11_delete_property(x, window, property) != 0)
      return -1;
    do {
      if (sl_x11_wait_property(x, window, property, NULL) != 0)
        return -1;
      result = sl_x11_read_value(x, window, property, target, fd, &size, head,
                                 &head_len, incr);
      if (sl_x11_delete_property(x, window, property) != 0)
        return -1;
    } while (result == 0);
    if (result != 2)
      return -1;
  } else if (result != 0) {
    return -1;
  }
  if (sl_x11_delete_property(x, window, property) != 0 || !size)
    return -1;
  if (is_png)
    return head_len >= 8 && memcmp(head, png, sizeof(png)) == 0 ? 0 : -1;
  return head_len >= 3 && head[0] == 255 && head[1] == 216 && head[2] == 255
             ? 0
             : -1;
}

static int sl_clipboard_random(unsigned char *bytes, size_t count) {
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t read_count;
  if (fd < 0)
    return -1;
  while (count) {
    read_count = read(fd, bytes, count);
    if (read_count < 0 && errno == EINTR)
      continue;
    if (read_count <= 0) {
      (void)close(fd);
      return -1;
    }
    bytes += read_count;
    count -= (size_t)read_count;
  }
  return close(fd);
}

/* xid's 12-byte layout: Unix seconds, machine id, PID, random-seeded atomic
 * counter. Base32hex is lowercase, unpadded, and exactly 20 characters. */
static int sl_clipboard_xid(char id[21]) {
  static volatile unsigned int counter;
  unsigned char raw[12], seed[3];
  char machine[256];
  const char *alphabet = "0123456789abcdefghijklmnopqrstuv";
  unsigned int hash = 2166136261u;
  unsigned int value, bits = 0, accumulator = 0;
  size_t index, out = 0;
  time_t now = time(NULL);
  int fd;
  ssize_t n;
  if (now == (time_t)-1)
    return -1;
  if (__sync_fetch_and_add(&counter, 0u) == 0) {
    if (sl_clipboard_random(seed, sizeof(seed)) != 0)
      return -1;
    value =
        ((unsigned int)seed[0] << 16) | ((unsigned int)seed[1] << 8) | seed[2];
    (void)__sync_bool_compare_and_swap(&counter, 0u, value ? value : 1u);
  }
  raw[0] = (unsigned char)((unsigned long)now >> 24);
  raw[1] = (unsigned char)((unsigned long)now >> 16);
  raw[2] = (unsigned char)((unsigned long)now >> 8);
  raw[3] = (unsigned char)now;
  fd = open("/etc/machine-id", O_RDONLY);
  n = fd >= 0 ? read(fd, machine, sizeof(machine)) : -1;
  if (fd >= 0)
    (void)close(fd);
  if (n <= 0) {
    if (gethostname(machine, sizeof(machine)) != 0)
      return -1;
    machine[sizeof(machine) - 1] = '\0';
    n = (ssize_t)strlen(machine);
  }
  for (index = 0; index < (size_t)n; index++)
    hash = (hash ^ (unsigned char)machine[index]) * 16777619u;
  raw[4] = (unsigned char)(hash >> 16);
  raw[5] = (unsigned char)(hash >> 8);
  raw[6] = (unsigned char)hash;
  value = (unsigned int)getpid();
  raw[7] = (unsigned char)(value >> 8);
  raw[8] = (unsigned char)value;
  value = __sync_add_and_fetch(&counter, 1u);
  raw[9] = (unsigned char)(value >> 16);
  raw[10] = (unsigned char)(value >> 8);
  raw[11] = (unsigned char)value;
  for (index = 0; index < sizeof(raw); index++) {
    accumulator = (accumulator << 8) | raw[index];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      id[out++] = alphabet[(accumulator >> bits) & 31u];
    }
  }
  if (bits)
    id[out++] = alphabet[(accumulator << (5 - bits)) & 31u];
  id[out] = '\0';
  return out == 20 ? 0 : -1;
}

static int sl_clipboard_mkdir_parents(const char *path) {
  char *directory = strdup(path);
  char *slash, *cursor;
  struct stat info;
  int result = 0;
  if (!directory)
    return -1;
  slash = strrchr(directory, '/');
  if (!slash || slash == directory) {
    result = slash ? 0 : -1;
    free(directory);
    return result;
  }
  *slash = '\0';
  for (cursor = directory + 1;; cursor++) {
    if (*cursor != '/' && *cursor != '\0')
      continue;
    {
      char saved = *cursor;
      *cursor = '\0';
      if (mkdir(directory, 0700) != 0 &&
          (errno != EEXIST || stat(directory, &info) != 0 ||
           !S_ISDIR(info.st_mode))) {
        result = -1;
        *cursor = saved;
        break;
      }
      *cursor = saved;
      if (saved == '\0')
        break;
    }
  }
  free(directory);
  return result;
}

static char *sl_clipboard_expand_template(const char *path_template) {
  const char *home = getenv("HOME");
  const char *cache = getenv("XDG_CACHE_HOME");
  const char *cursor, *start, *end, *replacement;
  char *fallback = NULL, *result, *write;
  size_t capacity, length;
  if (!home || home[0] != '/')
    home = NULL;
  if (!cache || cache[0] != '/') {
    if (home) {
      fallback = malloc(strlen(home) + sizeof("/.cache"));
      if (fallback)
        (void)snprintf(fallback, strlen(home) + sizeof("/.cache"), "%s/.cache",
                       home);
    }
    cache = fallback;
  }
  if (!path_template)
    path_template = "{{XDG_CACHE_HOME}}/softline/*";
  capacity = strlen(path_template) + 1;
  cursor = path_template;
  if (cursor[0] == '~' && cursor[1] == '/') {
    if (!home || strlen(home) > (size_t)-1 - capacity)
      goto fail;
    capacity += strlen(home);
    cursor++;
  }
  while ((start = strstr(cursor, "{{")) != NULL) {
    end = strstr(start + 2, "}}");
    if (!end)
      goto fail;
    replacement = sl_clipboard_token_value(start + 2, (size_t)(end - start - 2),
                                           home, cache);
    if (!replacement || strlen(replacement) > (size_t)-1 - capacity)
      goto fail;
    capacity += strlen(replacement);
    cursor = end + 2;
  }
  result = malloc(capacity);
  if (!result)
    goto fail;
  cursor = path_template;
  write = result;
  if (cursor[0] == '~' && cursor[1] == '/') {
    if (!home)
      goto fail_result;
    length = strlen(home);
    memcpy(write, home, length);
    write += length;
    cursor++;
  }
  while ((start = strstr(cursor, "{{")) != NULL) {
    length = (size_t)(start - cursor);
    memcpy(write, cursor, length);
    write += length;
    end = strstr(start + 2, "}}");
    if (!end)
      goto fail_result;
    replacement = sl_clipboard_token_value(start + 2, (size_t)(end - start - 2),
                                           home, cache);
    if (!replacement)
      goto fail_result;
    length = strlen(replacement);
    memcpy(write, replacement, length);
    write += length;
    cursor = end + 2;
  }
  length = strlen(cursor);
  memcpy(write, cursor, length + 1);
  free(fallback);
  return result;
fail_result:
  free(result);
fail:
  free(fallback);
  return NULL;
}

static int sl_clipboard_image_file(const char *path_template,
                                   const char *suffix, char **path) {
  char *expanded_template;
  const char *star;
  size_t before, after, length;
  char id[21];
  int fd, attempt;
  expanded_template = sl_clipboard_expand_template(path_template);
  if (!expanded_template)
    return -1;
  path_template = expanded_template;
  if (!sl_clipboard_valid_path_template(path_template)) {
    free(expanded_template);
    return -1;
  }
  star = strchr(path_template, '*');
  before = (size_t)(star - path_template);
  after = strlen(star + 1);
  length = before + 20 + after + 1 + strlen(suffix) + 1;
  *path = malloc(length);
  if (!*path) {
    free(expanded_template);
    return -1;
  }
  for (attempt = 0; attempt < 8; attempt++) {
    if (sl_clipboard_xid(id) != 0)
      break;
    (void)snprintf(*path, length, "%.*s%s%s.%s", (int)before, path_template, id,
                   star + 1, suffix);
    if (sl_clipboard_mkdir_parents(*path) != 0)
      break;
    fd = open(*path, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    if (fd >= 0) {
      free(expanded_template);
      return fd;
    }
    if (errno != EEXIST)
      break;
  }
  free(*path);
  *path = NULL;
  free(expanded_template);
  return -1;
}

int sl_clipboard_save_image(const char *path_template, char **path, char *error,
                            size_t error_size) {
  sl_x11_t x;
  sl_x11_display_t display;
  uint32_t window = 0, clipboard, png, jpeg, property, incr, timestamp;
  uint32_t target = 0;
  char *saved = NULL;
  int fd = -1, selected, result = -1, rejected = 0;
  const char *display_name = getenv("DISPLAY");
  if (!path)
    return -1;
  *path = NULL;
  sl_clipboard_error(error, error_size, "X11 clipboard image paste failed");
  if (!display_name || !*display_name) {
    sl_clipboard_error(error, error_size, "X11 DISPLAY is unavailable");
    return -1;
  }
  if (sl_x11_parse_display(display_name, &display) != 0) {
    sl_clipboard_error(error, error_size, "invalid X11 DISPLAY");
    return -1;
  }
  memset(&x, 0, sizeof(x));
  x.fd = -1;
  x.deadline = sl_now_ms() + SL_CLIPBOARD_TIMEOUT_MS;
  if (sl_x11_connect(&x, &display) != 0) {
    sl_clipboard_error(error, error_size, "cannot connect to X11 DISPLAY");
    goto cleanup;
  }
  if (sl_x11_setup(&x, &display, &rejected) != 0) {
    sl_clipboard_error(
        error, error_size,
        rejected ? "X11 server rejected connection setup; check XAUTHORITY"
                 : "X11 connection setup failed");
    goto cleanup;
  }
  window = x.resource_base | (x.resource_mask & (~x.resource_mask + 1));
  if (sl_x11_create_window(&x, window) != 0)
    goto cleanup;
  clipboard = sl_x11_atom(&x, "CLIPBOARD");
  png = sl_x11_atom(&x, "image/png");
  jpeg = sl_x11_atom(&x, "image/jpeg");
  property = sl_x11_atom(&x, "SOFTLINE_IMAGE_PASTE");
  incr = sl_x11_atom(&x, "INCR");
  if (!clipboard || !png || !jpeg || !property || !incr)
    goto cleanup;
  if (sl_x11_timestamp(&x, window, property, &timestamp) != 0)
    goto cleanup;
  selected =
      sl_x11_select_image(&x, window, clipboard, property, png, timestamp);
  if (selected < 0) {
    sl_clipboard_error(error, error_size, "X11 clipboard request timed out");
    goto cleanup;
  }
  if (selected == 1)
    target = png;
  else {
    selected =
        sl_x11_select_image(&x, window, clipboard, property, jpeg, timestamp);
    if (selected < 0) {
      sl_clipboard_error(error, error_size, "X11 clipboard request timed out");
      goto cleanup;
    }
    if (selected == 1)
      target = jpeg;
  }
  if (!target) {
    sl_clipboard_error(error, error_size,
                       "no PNG or JPEG image on the X11 clipboard");
    goto cleanup;
  }
  fd = sl_clipboard_image_file(path_template, target == png ? "png" : "jpeg",
                               &saved);
  if (fd < 0) {
    sl_clipboard_error(error, error_size, "cannot create clipboard image file");
    goto cleanup;
  }
  if (sl_x11_transfer(&x, window, property, target, incr, target == png, fd) !=
      0) {
    sl_clipboard_error(error, error_size,
                       "clipboard image transfer failed or exceeded 32 MiB");
    goto cleanup;
  }
  if (close(fd) != 0) {
    fd = -1;
    sl_clipboard_error(error, error_size, "cannot finish clipboard image file");
    goto cleanup;
  }
  fd = -1;
  *path = saved;
  saved = NULL;
  result = 0;
cleanup:
  if (fd >= 0)
    (void)close(fd);
  if (saved)
    sl_clipboard_discard_image(saved, path_template);
  free(saved);
  if (x.fd >= 0)
    (void)close(x.fd);
  return result;
}

#else

int sl_clipboard_save_image(const char *path_template, char **path, char *error,
                            size_t error_size) {
  (void)path_template;
  if (path)
    *path = NULL;
  if (error && error_size)
    (void)snprintf(error, error_size,
                   "X11 clipboard image paste is unavailable on this platform");
  return -1;
}

#endif
