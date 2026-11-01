#!/bin/sh
# Teste automatico no computador (sem PSP): um robo joga ~600 mil frames
# com AddressSanitizer/UBSan ligados e confere invariantes do jogo.
set -e
cd "$(dirname "$0")"
gcc -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinc -Wall -Wno-unused-function -Wno-unused-variable test.c -o /tmp/wg_test -lm
ASAN_OPTIONS=detect_leaks=0 /tmp/wg_test
