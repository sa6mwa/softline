#include "softline_resolver.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
  sl_resolve_config_t config;
  struct ares_addrinfo *addresses;
  struct ares_addrinfo_node *item;
  struct timespec now;
  char ip[INET6_ADDRSTRLEN];
  const void *address;
  unsigned int port;
  int64_t deadline;
  if (argc != 6 || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return 2;
  deadline = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 +
             strtol(argv[5], NULL, 10);
  config.hosts_path = argv[2];
  config.resolvconf_path = argv[3];
  config.servers = strcmp(argv[4], "-") == 0 ? NULL : argv[4];
  if (sl_resolve(argv[1], "6010", deadline, &config, &addresses) != 0)
    return 1;
  for (item = addresses->nodes; item; item = item->ai_next) {
    if (item->ai_family == AF_INET) {
      address = &((struct sockaddr_in *)item->ai_addr)->sin_addr;
      port = ntohs(((struct sockaddr_in *)item->ai_addr)->sin_port);
    } else if (item->ai_family == AF_INET6) {
      address = &((struct sockaddr_in6 *)item->ai_addr)->sin6_addr;
      port = ntohs(((struct sockaddr_in6 *)item->ai_addr)->sin6_port);
    } else {
      sl_resolve_free(addresses);
      return 3;
    }
    if (!inet_ntop(item->ai_family, address, ip, sizeof(ip))) {
      sl_resolve_free(addresses);
      return 4;
    }
    if (item->ai_family == AF_INET6 &&
        ((struct sockaddr_in6 *)item->ai_addr)->sin6_scope_id)
      printf("%s%%%u %u\n", ip,
             ((struct sockaddr_in6 *)item->ai_addr)->sin6_scope_id, port);
    else
      printf("%s %u\n", ip, port);
  }
  sl_resolve_free(addresses);
  return 0;
}
