#!/usr/bin/env bash
# The FEXCore fiber path on an x86-64 host (tests/test_fiber_stubs.c). Needs SDL3 headers (SDL_INCLUDE).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p out
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -no-pie -I. -Isrc ${SDL_INCLUDE:+-I"$SDL_INCLUDE"} \
    -DBB_FORCE_GUEST_CPU tests/test_fiber_stubs.c src/runtime_fiber.c -o out/fiber-stubs-test
out/fiber-stubs-test
