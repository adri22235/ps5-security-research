CC     := gcc
CFLAGS := -Wall -Wextra -O2 -std=gnu11

all: test_prospero_bridge test_webkit_1422_geometry

test_prospero_bridge: test_prospero_bridge.c
	$(CC) $(CFLAGS) $< -o $@

test_webkit_1422_geometry: test_webkit_1422_geometry.c
	$(CC) $(CFLAGS) $< -o $@

run: all
	./test_prospero_bridge
	./test_webkit_1422_geometry

clean:
	rm -f test_prospero_bridge test_webkit_1422_geometry

.PHONY: all run clean
