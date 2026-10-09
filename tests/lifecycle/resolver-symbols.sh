#!/usr/bin/env bash
set -euo pipefail
"$1" -g "$2" | awk '
  /sl_cares_ares_getaddrinfo/ { found=1 }
  $NF ~ /^ares_[A-Za-z0-9_]+$/ { bad=1; print "unprefixed upstream symbol " $NF > "/dev/stderr" }
  $0 ~ /[[:space:]]U[[:space:]]/ && $NF ~ /^(getaddrinfo|freeaddrinfo|getnameinfo|gethostbyname2?|gethostbyaddr|getservbyname(_r)?|getservbyport(_r)?|dlopen|dlsym)$/ {
    bad=1; print "libc resolver/module dependency " $NF > "/dev/stderr"
  }
  END { exit bad || !found }
'
echo 'Static archive embeds private resolver without libc NSS/module loading.'
