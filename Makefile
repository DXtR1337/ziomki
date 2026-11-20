CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS += -Isrc -D_CRT_SECURE_NO_WARNINGS

LIB_SRC := $(filter-out src/interface/web.c,$(wildcard src/*/*.c))

all: ziomki ziomki-web test_ziomki

ziomki: $(LIB_SRC) src/main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

ziomki-web: $(LIB_SRC) src/interface/web.c src/main_web.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_ziomki: $(LIB_SRC) tests/test_ziomki.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: test_ziomki
	./test_ziomki

clean:
	rm -f ziomki ziomki.exe test_ziomki test_ziomki.exe

.PHONY: all test clean
