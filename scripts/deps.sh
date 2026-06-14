#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "softline has no external dependencies. Vendored linenoise is built-in."
exit 0