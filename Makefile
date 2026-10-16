.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CC ?= cc
PYTHON ?= python3
BUILD_DIR ?= build
CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
LDLIBS ?=

PROJECT_CPPFLAGS := -Iinclude
PROJECT_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror \
	-Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes
SOURCES := src/main.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
BINARY := $(BUILD_DIR)/the-bugger

.PHONY: all test sanitize analyze clean

all: $(BINARY)

$(BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(PROJECT_CFLAGS) -MMD -MP -c $< -o $@

test: all
	$(PYTHON) tests/integration/test_cli.py $(BINARY)

# Separate artifacts keep instrumentation out of the normal executable.
sanitize:
	$(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=address,undefined' test

analyze:
	clang-tidy $(SOURCES) --checks='-*,clang-analyzer-*,bugprone-*' \
		--warnings-as-errors='*' -- \
		$(PROJECT_CPPFLAGS) $(PROJECT_CFLAGS)

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
