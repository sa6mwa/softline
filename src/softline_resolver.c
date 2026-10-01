#include "softline_resolver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  struct pollfd *fds;
  size_t count;
  int failed;
  int done;
  struct ares_addrinfo *addresses;
} sl_lookup_t;

static int64_t sl_resolve_now(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return -1;
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sl_resolve_socket(void *data, ares_socket_t fd, int readable,
                              int writable) {
  sl_lookup_t *lookup = (sl_lookup_t *)data;
  struct pollfd *grown;
  size_t i;
  for (i = 0; i < lookup->count && lookup->fds[i].fd != fd; ++i)
    ;
  if (!readable && !writable) {
    if (i < lookup->count)
      lookup->fds[i] = lookup->fds[--lookup->count];
    return;
  }
  if (i == lookup->count) {
    grown = (struct pollfd *)realloc(lookup->fds,
                                     (lookup->count + 1) * sizeof(*grown));
    if (!grown) {
      lookup->failed = 1;
      return;
    }
    lookup->fds = grown;
    lookup->count++;
  }
  lookup->fds[i].fd = fd;
  lookup->fds[i].events =
      (short)((readable ? POLLIN : 0) | (writable ? POLLOUT : 0));
  lookup->fds[i].revents = 0;
}

static void sl_resolve_done(void *data, int status, int timeouts,
                            struct ares_addrinfo *addresses) {
  sl_lookup_t *lookup = (sl_lookup_t *)data;
  (void)timeouts;
  lookup->done = 1;
  if (status == ARES_SUCCESS)
    lookup->addresses = addresses;
  else
    ares_freeaddrinfo(addresses);
}

int sl_resolve(const char *host, const char *port, int64_t deadline_ms,
               const sl_resolve_config_t *config,
               struct ares_addrinfo **addresses) {
  ares_channel_t *channel = NULL;
  struct ares_options options;
  struct ares_addrinfo_hints hints;
  struct timeval maximum, timeout, *wait;
  ares_fd_events_t *events = NULL;
  sl_lookup_t lookup;
  struct ares_addrinfo_node *item;
  struct in6_addr literal_address;
  char literal[INET6_ADDRSTRLEN], *end;
  const char *zone;
  unsigned long value;
  unsigned int scope = 0;
  int64_t now, remaining;
  size_t i, count;
  int mask, delay, result = -1;
  *addresses = NULL;
  /* c-ares parses bare IPs. Preserve DISPLAY's IPv6 interface qualifiers
   * using the kernel interface index, without libc hostname/NSS APIs. */
  zone = strchr(host, '%');
  if (zone) {
    size_t length = (size_t)(zone - host);
    if (!length || length >= sizeof(literal) || !zone[1])
      return -1;
    memcpy(literal, host, length);
    literal[length] = '\0';
    if (inet_pton(AF_INET6, literal, &literal_address) != 1)
      return -1;
    errno = 0;
    value = strtoul(zone + 1, &end, 10);
    if (zone[1] >= '0' && zone[1] <= '9' && *end == '\0') {
      if (errno || value > UINT_MAX)
        return -1;
      scope = (unsigned int)value;
    } else {
      scope = if_nametoindex(zone + 1);
      if (!scope)
        return -1;
    }
    host = literal;
  }
  memset(&lookup, 0, sizeof(lookup));
  memset(&options, 0, sizeof(options));
  /* Always use hosts before DNS; arbitrary NSS modules are outside this
   * private resolver's contract. X11 services are numeric, never NSS names. */
  options.lookups = (char *)"fb";
  options.sock_state_cb = sl_resolve_socket;
  options.sock_state_cb_data = &lookup;
  mask = ARES_OPT_LOOKUPS | ARES_OPT_SOCK_STATE_CB;
  if (config) {
    if (config->hosts_path) {
      options.hosts_path = (char *)config->hosts_path;
      mask |= ARES_OPT_HOSTS_FILE;
    }
    if (config->resolvconf_path) {
      options.resolvconf_path = (char *)config->resolvconf_path;
      mask |= ARES_OPT_RESOLVCONF;
    }
  }
  now = sl_resolve_now();
  if (now < 0 || now >= deadline_ms ||
      ares_init_options(&channel, &options, mask) != ARES_SUCCESS)
    goto finish;
  if (config && config->servers &&
      ares_set_servers_ports_csv(channel, config->servers) != ARES_SUCCESS)
    goto finish;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = ARES_AI_NUMERICSERV;
  ares_getaddrinfo(channel, host, port, &hints, sl_resolve_done, &lookup);
  while (!lookup.done && !lookup.failed) {
    now = sl_resolve_now();
    remaining = deadline_ms - now;
    if (now < 0 || remaining <= 0)
      break;
    maximum.tv_sec = (long)(remaining / 1000);
    maximum.tv_usec = (long)(remaining % 1000) * 1000;
    wait = ares_timeout(channel, &maximum, &timeout);
    delay = (int)(wait->tv_sec * 1000 + (wait->tv_usec + 999) / 1000);
    count = lookup.count;
    events = count ? (ares_fd_events_t *)malloc(count * sizeof(*events)) : NULL;
    if (count && !events)
      break;
    result = poll(lookup.fds, (nfds_t)count, delay);
    if (result < 0) {
      free(events);
      events = NULL;
      if (errno == EINTR)
        continue;
      break;
    }
    /* Snapshot before processing: callbacks can change/reallocate the fd list.
     */
    for (i = 0; i < count; ++i) {
      events[i].fd = lookup.fds[i].fd;
      events[i].events = ARES_FD_EVENT_NONE;
      if (lookup.fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))
        events[i].events |= ARES_FD_EVENT_READ;
      if (lookup.fds[i].revents & POLLOUT)
        events[i].events |= ARES_FD_EVENT_WRITE;
    }
    result = ares_process_fds(channel, events, count, ARES_PROCESS_FLAG_NONE);
    free(events);
    events = NULL;
    if (result != ARES_SUCCESS)
      break;
  }
finish:
  if (channel)
    ares_destroy(channel);
  free(lookup.fds);
  if (!lookup.failed && lookup.addresses) {
    if (scope) {
      for (item = lookup.addresses->nodes; item; item = item->ai_next) {
        if (item->ai_family == AF_INET6)
          ((struct sockaddr_in6 *)item->ai_addr)->sin6_scope_id = scope;
      }
    }
    *addresses = lookup.addresses;
    return 0;
  }
  ares_freeaddrinfo(lookup.addresses);
  return -1;
}

void sl_resolve_free(struct ares_addrinfo *addresses) {
  ares_freeaddrinfo(addresses);
}
