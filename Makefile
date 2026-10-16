.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CC ?= cc
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build
CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
LDLIBS ?=

NCURSES_CFLAGS := $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null)
NCURSES_LIBS := $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null)
PROJECT_CPPFLAGS := -Iinclude -Isrc/tui -Isrc/layout -Isrc/gdb $(NCURSES_CFLAGS)
PROJECT_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror \
	-Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes
SOURCES := src/main.c src/tui/app.c src/tui/demo.c src/tui/render.c src/tui/theme.c src/layout/layout.c src/gdb/mi.c src/gdb/controller.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
BINARY := $(BUILD_DIR)/the-bugger

.PHONY: all test test-mi test-controller test-gdb-startup controller-demo mi-demo test-tmux sanitize analyze clean check-deps

all: $(BINARY)

$(BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(NCURSES_LIBS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c Makefile | check-deps
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(PROJECT_CFLAGS) -MMD -MP -c $< -o $@

check-deps:
	@$(PKG_CONFIG) --exists ncursesw || { echo 'ncursesw development files and pkg-config are required.' >&2; exit 1; }

$(BUILD_DIR)/test-unit: $(BUILD_DIR)/tests/unit/test_demo.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-cli: $(BUILD_DIR)/tests/integration/test_cli.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-tui: $(BUILD_DIR)/tests/integration/test_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-render: $(BUILD_DIR)/tests/unit/test_render.o $(BUILD_DIR)/src/tui/render.o $(BUILD_DIR)/src/tui/theme.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-layout: $(BUILD_DIR)/tests/unit/test_layout.o $(BUILD_DIR)/src/layout/layout.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-workspace: $(BUILD_DIR)/tests/integration/test_workspace.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-tmux: $(BUILD_DIR)/tests/integration/test_tmux.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-mi: $(BUILD_DIR)/tests/unit/test_mi.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/mi-dump: $(BUILD_DIR)/tests/support/mi_dump.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-mi-alloc: $(BUILD_DIR)/tests/unit/test_mi_alloc.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@ $^ $(LDLIBS)

test-mi: $(BUILD_DIR)/test-mi $(BUILD_DIR)/test-mi-alloc $(BUILD_DIR)/mi-dump
	$(BUILD_DIR)/test-mi tests/fixtures/mi/valid.mi tests/fixtures/mi/invalid.mi
	$(BUILD_DIR)/test-mi-alloc

mi-demo: $(BUILD_DIR)/mi-dump
	$(BUILD_DIR)/mi-dump --chunk 1 tests/fixtures/mi/valid.mi

$(BUILD_DIR)/fake-mi: $(BUILD_DIR)/tests/support/fake_mi.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-controller: $(BUILD_DIR)/tests/integration/test_controller.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-controller: $(BUILD_DIR)/test-controller $(BUILD_DIR)/fake-mi $(BUILD_DIR)/controller-demo $(BUILD_DIR)/test-controller-demo
	$(BUILD_DIR)/test-controller $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-controller-demo $(abspath $(BUILD_DIR)/controller-demo) $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/controller-demo: $(BUILD_DIR)/tests/support/controller_demo.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-controller-demo: $(BUILD_DIR)/tests/integration/test_controller_demo.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

controller-demo: $(BUILD_DIR)/controller-demo $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/controller-demo $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/test-gdb-startup: $(BUILD_DIR)/tests/integration/test_gdb_startup.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-gdb-startup: $(BUILD_DIR)/test-gdb-startup
	$(BUILD_DIR)/test-gdb-startup gdb

test-tmux: all $(BUILD_DIR)/test-tmux
	$(BUILD_DIR)/test-tmux $(abspath $(BINARY))

test: all test-mi test-controller $(BUILD_DIR)/test-unit $(BUILD_DIR)/test-layout $(BUILD_DIR)/test-render $(BUILD_DIR)/test-cli $(BUILD_DIR)/test-tui $(BUILD_DIR)/test-workspace
	$(BUILD_DIR)/test-unit
	$(BUILD_DIR)/test-layout
	$(BUILD_DIR)/test-render
	$(BUILD_DIR)/test-cli $(abspath $(BINARY))
	$(BUILD_DIR)/test-tui $(abspath $(BINARY))
	$(BUILD_DIR)/test-workspace $(abspath $(BINARY))

# Separate artifacts keep instrumentation out of the normal executable.
sanitize:
	UBSAN_OPTIONS=halt_on_error=1 $(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=address,undefined' test

analyze:
# Annex K replacements such as snprintf_s are not provided by Linux libc.
	clang-tidy $(SOURCES) --checks='-*,clang-analyzer-*,bugprone-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		--warnings-as-errors='*' -- \
		$(PROJECT_CPPFLAGS) $(PROJECT_CFLAGS)

clean:
	rm -rf build

-include $(OBJECTS:.o=.d) $(BUILD_DIR)/tests/unit/test_demo.d \
	$(BUILD_DIR)/tests/integration/test_cli.d $(BUILD_DIR)/tests/integration/test_tui.d \
	$(BUILD_DIR)/tests/support/terminal.d $(BUILD_DIR)/tests/unit/test_render.d \
	$(BUILD_DIR)/tests/unit/test_layout.d $(BUILD_DIR)/tests/integration/test_workspace.d \
	$(BUILD_DIR)/tests/integration/test_tmux.d $(BUILD_DIR)/tests/unit/test_mi.d \
	$(BUILD_DIR)/tests/unit/test_mi_alloc.d $(BUILD_DIR)/tests/support/mi_dump.d \
	$(BUILD_DIR)/tests/integration/test_controller.d $(BUILD_DIR)/tests/support/fake_mi.d

-include $(BUILD_DIR)/tests/support/controller_demo.d $(BUILD_DIR)/tests/integration/test_controller_demo.d \
	$(BUILD_DIR)/tests/integration/test_gdb_startup.d
