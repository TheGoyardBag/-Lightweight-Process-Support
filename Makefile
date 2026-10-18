CC ?= gcc
AR ?= ar
CPPFLAGS := -Iinclude -Isrc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=
BUILD := build
OBJECTS := $(BUILD)/lwp.o $(BUILD)/stack.o

.PHONY: all test demo benchmark ubsan clean
all: $(BUILD)/liblwp.a $(BUILD)/round_robin
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/%.o: src/%.c include/lwp.h src/stack.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/liblwp.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/round_robin: examples/round_robin.c $(BUILD)/liblwp.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/liblwp.a $(LDFLAGS) -o $@
$(BUILD)/test_lwp: tests/test_lwp.c $(BUILD)/liblwp.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/liblwp.a $(LDFLAGS) -o $@
$(BUILD)/benchmark: examples/benchmark.c $(BUILD)/liblwp.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/liblwp.a $(LDFLAGS) -o $@
test: $(BUILD)/test_lwp
	./$(BUILD)/test_lwp
demo: $(BUILD)/round_robin
	./$(BUILD)/round_robin
benchmark: $(BUILD)/benchmark
	./$(BUILD)/benchmark
ubsan:
	$(MAKE) clean
	$(MAKE) test CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -fno-sanitize-recover=all' LDFLAGS='-fsanitize=undefined'
clean:
	rm -rf $(BUILD)
