#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  FILE *maps;
  char line[4096];
  if (argc == 2 && strcmp(argv[1], "self") == 0) {
    execl("/proc/self/exe", "runtime_probe", (char *)NULL);
    return 2;
  }
  if (argc == 2 && strcmp(argv[1], "host") == 0) {
    execl("/bin/sh", "sh", "-c", "cat /proc/self/maps", (char *)NULL);
    return 3;
  }
  maps = fopen("/proc/self/maps", "r");
  if (!maps)
    return 4;
  while (fgets(line, sizeof(line), maps))
    fputs(line, stdout);
  return fclose(maps) == 0 ? 0 : 5;
}
