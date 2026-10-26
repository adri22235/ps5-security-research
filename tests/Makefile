CC     := gcc
CFLAGS := -Wall -Wextra -O2 -std=gnu11

all: test_prospero_bridge

test_prospero_bridge: test_prospero_bridge.c
	$(CC) $(CFLAGS) $< -o $@

run: test_prospero_bridge
	./test_prospero_bridge

clean:
	rm -f test_prospero_bridge

.PHONY: all run clean
