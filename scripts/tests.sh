#!/usr/bin/env bash
cmake -S . -B build && cmake --build build &&\
mkdir -p tests/include tests/lib && cp build/libtat.a tests/lib/ && cp include/tat.h tests/include/ &&\
gcc tests/test_lifecycle.c -I tests/include tests/lib/libtat.a -o ./tests/main && ./tests/main
