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
ELF_LIBS := $(shell $(PKG_CONFIG) --libs libelf 2>/dev/null)
ELF_CFLAGS := $(shell $(PKG_CONFIG) --cflags libelf 2>/dev/null)
JSON_LIBS := $(shell $(PKG_CONFIG) --libs json-c 2>/dev/null)
JSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags json-c 2>/dev/null)
PROJECT_CPPFLAGS := -Iinclude -Isrc/tui -Isrc/layout -Isrc/gdb -Isrc/session -Isrc/model $(NCURSES_CFLAGS) $(ELF_CFLAGS) $(JSON_CFLAGS)
PROJECT_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror \
	-Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes
SOURCES := src/main.c src/tui/app.c src/tui/demo.c src/tui/render.c src/tui/theme.c src/tui/preferences.c src/tui/keybinds.c src/tui/editor.c src/tui/terminal.c src/tui/setup_capture.c src/tui/setup_restore.c src/layout/layout.c src/gdb/mi.c src/gdb/controller.c src/session/native.c src/session/session.c src/session/openocd.c src/session/profiles.c src/session/setup.c src/model/context.c src/model/code.c src/model/browse.c src/model/monitor.c src/model/mmio.c src/model/images.c src/model/breakpoints.c src/model/registers.c src/model/memory.c src/model/variables.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
BINARY := $(BUILD_DIR)/the-bugger

.PHONY: all test test-ci test-native fixtures test-mi test-controller test-gdb-startup controller-demo mi-demo test-tmux sanitize sanitize-native analyze clean check-deps

# Shared host regression list for local preflight and the CI Tests job.
test-ci: test test-native test-tmux test-m15-native test-m16-server test-m16-tui test-m16-openocd

all: $(BINARY)

$(BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(NCURSES_LIBS) $(ELF_LIBS) $(JSON_LIBS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c Makefile | check-deps
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(PROJECT_CFLAGS) -MMD -MP -c $< -o $@

check-deps:
	@$(PKG_CONFIG) --exists ncursesw || { echo 'ncursesw development files and pkg-config are required.' >&2; exit 1; }
	@$(PKG_CONFIG) --exists libelf || { echo 'libelf development files are required.' >&2; exit 1; }
	@$(PKG_CONFIG) --exists json-c || { echo 'json-c development files are required.' >&2; exit 1; }

$(BUILD_DIR)/test-unit: $(BUILD_DIR)/tests/unit/test_demo.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-cli: $(BUILD_DIR)/tests/integration/test_cli.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-tui: $(BUILD_DIR)/tests/integration/test_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-render: $(BUILD_DIR)/tests/unit/test_render.o $(BUILD_DIR)/src/tui/render.o $(BUILD_DIR)/src/tui/theme.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(ELF_LIBS) -lm $(LDLIBS)

$(BUILD_DIR)/test-layout: $(BUILD_DIR)/tests/unit/test_layout.o $(BUILD_DIR)/src/layout/layout.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-workspace: $(BUILD_DIR)/tests/integration/test_workspace.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-tmux: $(BUILD_DIR)/tests/integration/test_tmux.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-mi: $(BUILD_DIR)/tests/unit/test_mi.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/mi-dump: $(BUILD_DIR)/tests/support/mi_dump.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-mi-alloc: $(BUILD_DIR)/tests/unit/test_mi_alloc.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-mi: $(BUILD_DIR)/test-mi $(BUILD_DIR)/test-mi-alloc $(BUILD_DIR)/mi-dump
	$(BUILD_DIR)/test-mi tests/fixtures/mi/valid.mi tests/fixtures/mi/invalid.mi
	$(BUILD_DIR)/test-mi-alloc

mi-demo: $(BUILD_DIR)/mi-dump
	$(BUILD_DIR)/mi-dump --chunk 1 tests/fixtures/mi/valid.mi

$(BUILD_DIR)/fake-mi: $(BUILD_DIR)/tests/support/fake_mi.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-controller: $(BUILD_DIR)/tests/integration/test_controller.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-controller: $(BUILD_DIR)/test-controller $(BUILD_DIR)/fake-mi $(BUILD_DIR)/controller-demo $(BUILD_DIR)/test-controller-demo
	$(BUILD_DIR)/test-controller $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-controller-demo $(abspath $(BUILD_DIR)/controller-demo) $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/controller-demo: $(BUILD_DIR)/tests/support/controller_demo.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-controller-demo: $(BUILD_DIR)/tests/integration/test_controller_demo.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

controller-demo: $(BUILD_DIR)/controller-demo $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/controller-demo $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/test-gdb-startup: $(BUILD_DIR)/tests/integration/test_gdb_startup.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-gdb-startup: $(BUILD_DIR)/test-gdb-startup
	$(BUILD_DIR)/test-gdb-startup gdb

$(BUILD_DIR)/debug-fixture: tests/fixtures/native/session.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/debug-fixture-cpp: tests/fixtures/native/session.cpp
	mkdir -p $(dir $@)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

fixtures: $(BUILD_DIR)/debug-fixture $(BUILD_DIR)/debug-fixture-cpp

$(BUILD_DIR)/context-fixture: tests/fixtures/native/context.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 -pthread $< -o $@

$(BUILD_DIR)/context-fixture-optimized: tests/fixtures/native/context.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O2 -pthread $< -o $@

$(BUILD_DIR)/context-fixture-stripped: tests/fixtures/native/context.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -s -pthread $< -o $@

fixtures: $(BUILD_DIR)/context-fixture $(BUILD_DIR)/context-fixture-optimized $(BUILD_DIR)/context-fixture-stripped

$(BUILD_DIR)/code-fixture: tests/fixtures/native/code.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/code-fixture-optimized: tests/fixtures/native/code.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O2 $< -o $@

$(BUILD_DIR)/code-fixture-stripped: tests/fixtures/native/code.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -s $< -o $@

$(BUILD_DIR)/code-fixture-cpp: tests/fixtures/native/code.cpp
	mkdir -p $(dir $@)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/code-fixture-relocated: tests/fixtures/native/relocated/main.c tests/fixtures/native/relocated/left/duplicate.c tests/fixtures/native/relocated/right/duplicate.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 '-fdebug-prefix-map=$(CURDIR)/tests/fixtures/native/relocated=/the bugger build tree' $(abspath $^) -o $@

fixtures: $(BUILD_DIR)/code-fixture $(BUILD_DIR)/code-fixture-optimized $(BUILD_DIR)/code-fixture-stripped $(BUILD_DIR)/code-fixture-cpp $(BUILD_DIR)/code-fixture-relocated

NATIVE_OBJECTS = $(BUILD_DIR)/src/session/native.o $(BUILD_DIR)/src/session/session.o $(BUILD_DIR)/src/session/openocd.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/breakpoints.o $(BUILD_DIR)/src/model/registers.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/variables.o

$(BUILD_DIR)/test-native-words: $(BUILD_DIR)/tests/unit/test_native_words.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-native: $(BUILD_DIR)/tests/integration/test_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-native-tui: $(BUILD_DIR)/tests/integration/test_native_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-native: all $(BUILD_DIR)/test-acceptance-native $(BUILD_DIR)/test-native $(BUILD_DIR)/test-native-tui $(BUILD_DIR)/test-context-native $(BUILD_DIR)/test-context-tui $(BUILD_DIR)/test-code-native $(BUILD_DIR)/test-code-tui fixtures
	$(BUILD_DIR)/test-acceptance-native $(abspath $(BUILD_DIR)/variables-fixture) $(abspath $(BUILD_DIR)/variables-fixture-cpp)
	$(BUILD_DIR)/test-native $(abspath $(BUILD_DIR)/debug-fixture) $(abspath $(BUILD_DIR)/debug-fixture-cpp)
	$(BUILD_DIR)/test-native-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)
	TB_CONSOLE_TEST_TMUX=1 $(BUILD_DIR)/test-native-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)
	$(BUILD_DIR)/test-keybinds-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)
	TB_KEYBINDS_TEST_TMUX=1 $(BUILD_DIR)/test-keybinds-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)
	$(BUILD_DIR)/test-context-native $(abspath $(BUILD_DIR)/context-fixture) $(abspath $(BUILD_DIR)/context-fixture-optimized) $(abspath $(BUILD_DIR)/context-fixture-stripped)
	$(BUILD_DIR)/test-context-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/context-fixture)
	$(BUILD_DIR)/test-code-native $(abspath $(BUILD_DIR)/code-fixture) $(abspath $(BUILD_DIR)/code-fixture-optimized) $(abspath $(BUILD_DIR)/code-fixture-stripped) $(abspath $(BUILD_DIR)/code-fixture-cpp) $(abspath $(BUILD_DIR)/code-fixture-relocated) $(abspath tests/fixtures/native/relocated)
	$(BUILD_DIR)/test-code-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/code-fixture) $(abspath $(BUILD_DIR)/debug-fixture)

$(BUILD_DIR)/test-context-native: $(BUILD_DIR)/tests/integration/test_context_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-context-tui: $(BUILD_DIR)/tests/integration/test_context_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-code-native: $(BUILD_DIR)/tests/integration/test_code_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-code: $(BUILD_DIR)/tests/integration/test_code.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-code-tui: $(BUILD_DIR)/tests/integration/test_code_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-tmux: all $(BUILD_DIR)/test-tmux
	$(BUILD_DIR)/test-tmux $(abspath $(BINARY))

$(BUILD_DIR)/test-context: $(BUILD_DIR)/tests/integration/test_context.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: all test-mi test-controller $(BUILD_DIR)/test-code $(BUILD_DIR)/test-context $(BUILD_DIR)/test-native-words $(BUILD_DIR)/test-unit $(BUILD_DIR)/test-layout $(BUILD_DIR)/test-render $(BUILD_DIR)/test-cli $(BUILD_DIR)/test-tui $(BUILD_DIR)/test-workspace
	$(BUILD_DIR)/test-images
	$(BUILD_DIR)/test-browse
	$(BUILD_DIR)/test-monitor tests/fixtures/mi/openocd-monitor.mi tests/fixtures/mi/openocd-widths.mi
	$(BUILD_DIR)/test-mmio $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-mmio-memory $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-mmio-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-image-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-registers $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-memory $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-variables $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-breakpoints $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-code $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-context $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-native-words
	$(BUILD_DIR)/test-unit
	$(BUILD_DIR)/test-layout
	$(BUILD_DIR)/test-render
	$(BUILD_DIR)/test-cli $(abspath $(BINARY))
	$(BUILD_DIR)/test-tui $(abspath $(BINARY))
	$(BUILD_DIR)/test-workspace $(abspath $(BINARY))

$(BUILD_DIR)/test-images: $(BUILD_DIR)/tests/unit/test_images.o $(BUILD_DIR)/src/model/images.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-monitor: $(BUILD_DIR)/tests/unit/test_monitor.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Physical, text-only framing probe: built on request, never run by test-ci.
$(BUILD_DIR)/test-monitor-hardware: $(BUILD_DIR)/tests/integration/test_monitor_hardware.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test-browse: $(BUILD_DIR)/tests/unit/test_browse.o $(BUILD_DIR)/src/model/browse.o
	$(CC) $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=free -o $@ $^ $(LDLIBS)

test: $(BUILD_DIR)/test-browse $(BUILD_DIR)/test-monitor

test: $(BUILD_DIR)/test-images

$(BUILD_DIR)/test-breakpoints: $(BUILD_DIR)/tests/integration/test_breakpoints.o $(BUILD_DIR)/src/model/breakpoints.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-breakpoints

$(BUILD_DIR)/test-registers: $(BUILD_DIR)/tests/integration/test_registers.o $(BUILD_DIR)/src/model/registers.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-registers

.PHONY: test-m09-model
test-m09-model: $(BUILD_DIR)/test-registers $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-registers $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/test-registers-native: $(BUILD_DIR)/tests/integration/test_registers_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-registers-tui: $(BUILD_DIR)/tests/integration/test_registers_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m09-native
test-m09-native: all $(BUILD_DIR)/test-registers-native $(BUILD_DIR)/test-registers-tui $(BUILD_DIR)/code-fixture
	$(BUILD_DIR)/test-registers-native $(abspath $(BUILD_DIR)/code-fixture)
	$(BUILD_DIR)/test-registers-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/code-fixture)

test-native: test-m09-native

-include $(BUILD_DIR)/tests/integration/test_registers.d $(BUILD_DIR)/tests/integration/test_registers_native.d $(BUILD_DIR)/tests/integration/test_registers_tui.d

$(BUILD_DIR)/test-memory: $(BUILD_DIR)/tests/integration/test_memory.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-memory

.PHONY: test-m10-model
test-m10-model: $(BUILD_DIR)/test-memory $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-memory $(abspath $(BUILD_DIR)/fake-mi)

-include $(BUILD_DIR)/tests/integration/test_memory.d

$(BUILD_DIR)/memory-fixture: tests/fixtures/native/memory.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

fixtures: $(BUILD_DIR)/memory-fixture

$(BUILD_DIR)/test-memory-native: $(BUILD_DIR)/tests/integration/test_memory_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-memory-tui: $(BUILD_DIR)/tests/integration/test_memory_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m10-native
test-m10-native: all $(BUILD_DIR)/test-memory-native $(BUILD_DIR)/test-memory-tui $(BUILD_DIR)/memory-fixture
	$(BUILD_DIR)/test-memory-native $(abspath $(BUILD_DIR)/memory-fixture)
	$(BUILD_DIR)/test-memory-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/memory-fixture)

test-native: test-m10-native

$(BUILD_DIR)/test-variables: $(BUILD_DIR)/tests/integration/test_variables.o $(BUILD_DIR)/src/model/variables.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m11-model
test-m11-model: $(BUILD_DIR)/test-variables $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-variables $(abspath $(BUILD_DIR)/fake-mi)

test: $(BUILD_DIR)/test-variables

-include $(BUILD_DIR)/tests/integration/test_variables.d

$(BUILD_DIR)/variables-fixture: tests/fixtures/native/variables.c
	mkdir -p $(dir $@)
	$(CC) -D_POSIX_C_SOURCE=200809L -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/variables-fixture-cpp: tests/fixtures/native/variables.cpp tests/fixtures/native/variables.c
	mkdir -p $(dir $@)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/variables-fixture-optimized: tests/fixtures/native/variables.c
	mkdir -p $(dir $@)
	$(CC) -D_POSIX_C_SOURCE=200809L -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O2 $< -o $@

$(BUILD_DIR)/test-variables-native: $(BUILD_DIR)/tests/integration/test_variables_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-variables-tui: $(BUILD_DIR)/tests/integration/test_variables_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m11-native test-m11-tui
test-m11-tui: all $(BUILD_DIR)/test-variables-tui $(BUILD_DIR)/variables-fixture
	$(BUILD_DIR)/test-variables-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/variables-fixture)

test-m11-native: all $(BUILD_DIR)/test-variables-native $(BUILD_DIR)/test-variables-tui $(BUILD_DIR)/variables-fixture $(BUILD_DIR)/variables-fixture-cpp $(BUILD_DIR)/variables-fixture-optimized
	$(BUILD_DIR)/test-variables-native $(abspath $(BUILD_DIR)/variables-fixture) $(abspath $(BUILD_DIR)/variables-fixture-cpp) $(abspath $(BUILD_DIR)/variables-fixture-optimized)
	$(BUILD_DIR)/test-variables-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/variables-fixture)

fixtures: $(BUILD_DIR)/variables-fixture $(BUILD_DIR)/variables-fixture-cpp $(BUILD_DIR)/variables-fixture-optimized
test-native: test-m11-native
-include $(BUILD_DIR)/tests/integration/test_variables_native.d $(BUILD_DIR)/tests/integration/test_variables_tui.d

-include $(BUILD_DIR)/tests/integration/test_memory_native.d $(BUILD_DIR)/tests/integration/test_memory_tui.d

$(BUILD_DIR)/break-fixture: tests/fixtures/native/breakpoints.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@ -ldl

$(BUILD_DIR)/break-fixture-cpp: tests/fixtures/native/breakpoints.cpp
	mkdir -p $(dir $@)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/break-fixture-library.so: tests/fixtures/native/breakpoints_library.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 -fPIC -shared $< -o $@

fixtures: $(BUILD_DIR)/break-fixture $(BUILD_DIR)/break-fixture-cpp $(BUILD_DIR)/break-fixture-library.so

$(BUILD_DIR)/test-break-native: $(BUILD_DIR)/tests/integration/test_break_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m08-native
test-m08-native: all $(BUILD_DIR)/test-break-native $(BUILD_DIR)/break-fixture $(BUILD_DIR)/break-fixture-cpp $(BUILD_DIR)/break-fixture-library.so
	$(BUILD_DIR)/test-break-native $(abspath $(BUILD_DIR)/break-fixture) $(abspath $(BUILD_DIR)/break-fixture-cpp) $(abspath $(BUILD_DIR)/break-fixture-library.so)
	$(BUILD_DIR)/test-break-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/break-fixture)

$(BUILD_DIR)/test-break-tui: $(BUILD_DIR)/tests/integration/test_break_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-m08-native: $(BUILD_DIR)/test-break-tui

test-native: test-m08-native

# Separate artifacts keep instrumentation out of the normal executable.
$(BUILD_DIR)/test-watchpoints: $(BUILD_DIR)/tests/integration/test_watchpoints.o $(BUILD_DIR)/src/model/breakpoints.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m12-model
test-m12-model: $(BUILD_DIR)/test-watchpoints $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-watchpoints $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/test-profiles: $(BUILD_DIR)/tests/unit/test_profiles.o $(BUILD_DIR)/src/session/profiles.o $(BUILD_DIR)/src/session/setup.o
	$(CC) $(LDFLAGS) -o $@ $^ $(JSON_LIBS) $(LDLIBS)

.PHONY: test-m13-model
$(BUILD_DIR)/test-profile-sources: $(BUILD_DIR)/tests/integration/test_profile_sources.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-m13-model: $(BUILD_DIR)/test-profiles $(BUILD_DIR)/test-profile-sources $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-profiles
	$(BUILD_DIR)/test-profile-sources $(abspath $(BUILD_DIR)/fake-mi)

test: test-m13-model
$(BUILD_DIR)/test-setup-capture: $(BUILD_DIR)/tests/unit/test_setup_capture.o $(BUILD_DIR)/src/tui/setup_capture.o $(BUILD_DIR)/src/session/setup.o $(BUILD_DIR)/src/session/profiles.o $(BUILD_DIR)/src/layout/layout.o $(BUILD_DIR)/src/tui/demo.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(JSON_LIBS) $(ELF_LIBS) $(LDLIBS)

.PHONY: test-setup-capture
test-setup-capture: $(BUILD_DIR)/test-setup-capture
	$(BUILD_DIR)/test-setup-capture

test: test-setup-capture
.PHONY: test-setup-tmux
test-setup-tmux: all $(BUILD_DIR)/test-profiles-tui $(BUILD_DIR)/debug-fixture
	TB_PROFILE_SETUP_TMUX=1 $(BUILD_DIR)/test-profiles-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture) --native

test-tmux: test-setup-tmux
-include $(BUILD_DIR)/tests/unit/test_setup_capture.d $(BUILD_DIR)/tests/unit/test_setup_restore.d $(BUILD_DIR)/tests/unit/test_profiles.d $(BUILD_DIR)/tests/integration/test_profile_sources.d

$(BUILD_DIR)/test-profiles-tui: $(BUILD_DIR)/tests/integration/test_profiles_tui.o $(BUILD_DIR)/tests/support/terminal.o $(BUILD_DIR)/src/session/profiles.o $(BUILD_DIR)/src/session/setup.o $(BUILD_DIR)/src/tui/keybinds.o
	$(CC) $(LDFLAGS) -o $@ $^ $(JSON_LIBS) $(LDLIBS)

.PHONY: test-m13-tui test-m13-native
test-m13-tui: all $(BUILD_DIR)/test-profiles-tui $(BUILD_DIR)/debug-fixture
	$(BUILD_DIR)/test-profiles-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)

$(BUILD_DIR)/test-profiles-native: $(BUILD_DIR)/tests/integration/test_profiles_native.o $(BUILD_DIR)/src/session/profiles.o $(BUILD_DIR)/src/session/setup.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(JSON_LIBS) $(ELF_LIBS) $(LDLIBS)

test-m13-native: all $(BUILD_DIR)/test-profiles-native $(BUILD_DIR)/test-profiles-tui $(BUILD_DIR)/debug-fixture $(BUILD_DIR)/code-fixture-relocated
	$(BUILD_DIR)/test-profiles-native $(abspath $(BUILD_DIR)/code-fixture-relocated) $(abspath tests/fixtures/native/relocated)
	$(BUILD_DIR)/test-profiles-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture) --native

$(BUILD_DIR)/test-setup-restore: $(BUILD_DIR)/tests/integration/test_setup_restore.o $(BUILD_DIR)/src/tui/setup_restore.o $(BUILD_DIR)/src/tui/setup_capture.o $(BUILD_DIR)/src/session/setup.o $(BUILD_DIR)/src/layout/layout.o $(BUILD_DIR)/src/tui/demo.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(JSON_LIBS) $(ELF_LIBS) $(LDLIBS)

.PHONY: test-setup-restore
test-setup-restore: all $(BUILD_DIR)/test-setup-restore $(BUILD_DIR)/debug-fixture
	$(BUILD_DIR)/test-setup-restore $(abspath $(BUILD_DIR)/debug-fixture) $(abspath tests/fixtures/native/session.c)

test-native: test-setup-restore
-include $(BUILD_DIR)/tests/integration/test_setup_restore.d

test-native: test-m13-native
-include $(BUILD_DIR)/tests/integration/test_profiles_tui.d $(BUILD_DIR)/tests/integration/test_profiles_native.d

.PHONY: test-m12-slow
test-m12-slow: $(BUILD_DIR)/test-watchpoints $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-watchpoints $(abspath $(BUILD_DIR)/fake-mi) --slow

test: test-m12-model
-include $(BUILD_DIR)/tests/integration/test_watchpoints.d

$(BUILD_DIR)/watch-fixture: tests/fixtures/native/watchpoints.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0 $< -o $@

$(BUILD_DIR)/test-watchpoints-native: $(BUILD_DIR)/tests/integration/test_watchpoints_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m12-native
$(BUILD_DIR)/test-watchpoints-tui: $(BUILD_DIR)/tests/integration/test_watchpoints_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-m12-native: all $(BUILD_DIR)/test-watchpoints-native $(BUILD_DIR)/test-watchpoints-tui $(BUILD_DIR)/watch-fixture
	$(BUILD_DIR)/test-watchpoints-native $(abspath $(BUILD_DIR)/watch-fixture)
	$(BUILD_DIR)/test-watchpoints-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/watch-fixture)

.PHONY: test-m12-tui
test-m12-tui: all $(BUILD_DIR)/test-watchpoints-tui $(BUILD_DIR)/watch-fixture
	$(BUILD_DIR)/test-watchpoints-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/watch-fixture)

fixtures: $(BUILD_DIR)/watch-fixture
test-native: test-m12-native
-include $(BUILD_DIR)/tests/integration/test_watchpoints_native.d $(BUILD_DIR)/tests/integration/test_watchpoints_tui.d

sanitize:
	UBSAN_OPTIONS=halt_on_error=1 $(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=address,undefined' test

sanitize-native:
	UBSAN_OPTIONS=halt_on_error=1 $(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=address,undefined' test-native

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
	$(BUILD_DIR)/tests/integration/test_gdb_startup.d $(BUILD_DIR)/tests/unit/test_native_words.d \
	$(BUILD_DIR)/tests/integration/test_native.d $(BUILD_DIR)/tests/integration/test_native_tui.d

-include $(BUILD_DIR)/tests/integration/test_context.d
-include $(BUILD_DIR)/tests/integration/test_context_native.d
-include $(BUILD_DIR)/tests/integration/test_context_tui.d
-include $(BUILD_DIR)/tests/integration/test_code.d $(BUILD_DIR)/tests/integration/test_code_native.d $(BUILD_DIR)/tests/integration/test_code_tui.d
-include $(BUILD_DIR)/tests/unit/test_images.d $(BUILD_DIR)/tests/integration/test_breakpoints.d
-include $(BUILD_DIR)/tests/integration/test_break_native.d $(BUILD_DIR)/tests/integration/test_break_tui.d

.PHONY: test-m14-model test-m14-native
test-m14-model: $(BUILD_DIR)/test-context $(BUILD_DIR)/test-native-words $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-context $(abspath $(BUILD_DIR)/fake-mi)
	$(BUILD_DIR)/test-native-words

$(BUILD_DIR)/test-acceptance-native: $(BUILD_DIR)/tests/integration/test_acceptance_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-m14-native: all $(BUILD_DIR)/test-acceptance-native $(BUILD_DIR)/test-context-native $(BUILD_DIR)/test-context-tui fixtures
	$(BUILD_DIR)/test-acceptance-native $(abspath $(BUILD_DIR)/variables-fixture) $(abspath $(BUILD_DIR)/variables-fixture-cpp)
	$(BUILD_DIR)/test-context-native $(abspath $(BUILD_DIR)/context-fixture) $(abspath $(BUILD_DIR)/context-fixture-optimized) $(abspath $(BUILD_DIR)/context-fixture-stripped)
	$(BUILD_DIR)/test-context-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/context-fixture)

-include $(BUILD_DIR)/tests/integration/test_acceptance_native.d

.PHONY: test-m15-model
test-m15-model: $(BUILD_DIR)/test-remote $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-remote $(abspath $(BUILD_DIR)/fake-mi)

$(BUILD_DIR)/test-remote: $(BUILD_DIR)/tests/integration/test_remote.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

-include $(BUILD_DIR)/tests/integration/test_remote.d

.PHONY: test-m15-native
$(BUILD_DIR)/remote-fixture: tests/fixtures/native/remote.c
	mkdir -p $(dir $@)
	$(CC) -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 -g $< -o $@

fixtures: $(BUILD_DIR)/remote-fixture

$(BUILD_DIR)/test-remote-native: $(BUILD_DIR)/tests/integration/test_remote_native.o $(BUILD_DIR)/tests/support/terminal.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-m15-native: all $(BUILD_DIR)/test-remote-native $(BUILD_DIR)/remote-fixture
	$(BUILD_DIR)/test-remote-native $(abspath $(BUILD_DIR)/remote-fixture) --tui $(abspath $(BINARY))

test: test-m15-model
-include $(BUILD_DIR)/tests/integration/test_remote_native.d

$(BUILD_DIR)/test-openocd: $(BUILD_DIR)/tests/unit/test_openocd.o $(BUILD_DIR)/src/session/openocd.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/fake-openocd: $(BUILD_DIR)/tests/support/fake_openocd.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-m16-model
test-m16-model: $(BUILD_DIR)/test-openocd
	$(BUILD_DIR)/test-openocd

test: test-m16-model
-include $(BUILD_DIR)/tests/unit/test_openocd.d $(BUILD_DIR)/tests/support/fake_openocd.d

$(BUILD_DIR)/test-openocd-server: $(BUILD_DIR)/tests/integration/test_openocd_server.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: test-m16-server test-m16-slow
test-m16-server: $(BUILD_DIR)/test-openocd-server $(BUILD_DIR)/fake-openocd $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-openocd-server $(abspath $(BUILD_DIR)/fake-openocd) $(abspath $(BUILD_DIR)/fake-mi)

test-m16-slow: $(BUILD_DIR)/test-openocd-server $(BUILD_DIR)/fake-openocd $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-openocd-server $(abspath $(BUILD_DIR)/fake-openocd) $(abspath $(BUILD_DIR)/fake-mi) --timeout

-include $(BUILD_DIR)/tests/integration/test_openocd_server.d

$(BUILD_DIR)/test-openocd-tui: $(BUILD_DIR)/tests/integration/test_openocd_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-m16-tui
test-m16-tui: all $(BUILD_DIR)/test-openocd-tui $(BUILD_DIR)/fake-openocd $(BUILD_DIR)/fake-mi
	$(BUILD_DIR)/test-openocd-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/fake-openocd) $(abspath $(BUILD_DIR)/fake-mi)

-include $(BUILD_DIR)/tests/integration/test_openocd_tui.d

$(BUILD_DIR)/test-openocd-real: $(BUILD_DIR)/tests/integration/test_openocd_real.o $(BUILD_DIR)/src/session/openocd.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-m16-openocd
test-m16-openocd: $(BUILD_DIR)/test-openocd-real
	$(BUILD_DIR)/test-openocd-real openocd

-include $(BUILD_DIR)/tests/integration/test_openocd_real.d

# Optional physical-acceptance fixtures. Building never connects or programs.
ARM_CC ?= arm-none-eabi-gcc
ARM_GDB ?= arm-none-eabi-gdb
STM32_FIXTURE := tests/fixtures/embedded/stm32f446re
M17_WORKLOAD := tests/fixtures/embedded/workload.c
M17_WORKLOAD_HEADER := tests/fixtures/embedded/workload.h
STM32_FLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Og -g3 \
	-ffreestanding -fno-builtin -fno-omit-frame-pointer -fno-optimize-sibling-calls \
	-ffunction-sections -fdata-sections -nostdlib $(PROJECT_CFLAGS)

$(BUILD_DIR)/m17/stm32f446re/firmware.elf: $(STM32_FIXTURE)/firmware.c $(STM32_FIXTURE)/startup.S $(STM32_FIXTURE)/firmware.ld $(M17_WORKLOAD) $(M17_WORKLOAD_HEADER) Makefile
	mkdir -p $(dir $@)
	$(ARM_CC) $(STM32_FLAGS) -Wl,--gc-sections,--build-id=none,-Map,$(@:.elf=.map) \
		-T$(STM32_FIXTURE)/firmware.ld $(STM32_FIXTURE)/startup.S $(STM32_FIXTURE)/firmware.c $(M17_WORKLOAD) -o $@

$(BUILD_DIR)/m17/stm32f446re/firmware-led.elf: $(STM32_FIXTURE)/firmware.c $(STM32_FIXTURE)/startup.S $(STM32_FIXTURE)/firmware.ld $(M17_WORKLOAD) $(M17_WORKLOAD_HEADER) Makefile
	mkdir -p $(dir $@)
	$(ARM_CC) $(STM32_FLAGS) -DTB_FIXTURE_LED=1 -Wl,--gc-sections,--build-id=none,-Map,$(@:.elf=.map) \
		-T$(STM32_FIXTURE)/firmware.ld $(STM32_FIXTURE)/startup.S $(STM32_FIXTURE)/firmware.c $(M17_WORKLOAD) -o $@

$(BUILD_DIR)/test-firmware: $(BUILD_DIR)/tests/unit/test_firmware.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

.PHONY: fixture-m17-stm32 fixture-m17-stm32-led test-m17-fixture
fixture-m17-stm32: $(BUILD_DIR)/m17/stm32f446re/firmware.elf
fixture-m17-stm32-led: $(BUILD_DIR)/m17/stm32f446re/firmware-led.elf
test-m17-fixture: fixture-m17-stm32 fixture-m17-stm32-led $(BUILD_DIR)/test-firmware
	$(BUILD_DIR)/test-firmware $(BUILD_DIR)/m17/stm32f446re/firmware.elf $(BUILD_DIR)/m17/stm32f446re/firmware-led.elf
	$(ARM_GDB) --nx --batch $(BUILD_DIR)/m17/stm32f446re/firmware.elf -ex 'info line fixture_checkpoint' -ex 'disassemble fixture_callee'

-include $(BUILD_DIR)/tests/unit/test_firmware.d

# Opt-in runner: requires an externally started OpenOCD and programmed fixture.
$(BUILD_DIR)/test-board: $(BUILD_DIR)/tests/integration/test_board.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

-include $(BUILD_DIR)/tests/integration/test_board.d

$(BUILD_DIR)/test-board-tui: $(BUILD_DIR)/tests/integration/test_board_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(BUILD_DIR)/tests/integration/test_board_tui.d

.PHONY: test-m17-offline
test-m17-offline: test-m17-fixture $(BUILD_DIR)/test-board $(BUILD_DIR)/test-board-tui

# Linux x86-64 distribution; target GDB/OpenOCD and shared libraries stay external.
RELEASE_VERSION := $(shell sed -n 's/.*THE_BUGGER_VERSION "\([0-9.]*\)"/\1/p' include/the_bugger/version.h)
RELEASE_DIST_DIR ?= $(BUILD_DIR)/dist/v$(RELEASE_VERSION)
RELEASE_CFLAGS ?= -O2 -g -fPIE -fstack-protector-strong
RELEASE_LDFLAGS ?= -pie -Wl,-z,relro,-z,now
.PHONY: release
release:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/release-build CPPFLAGS='$(CPPFLAGS) -D_FORTIFY_SOURCE=3' CFLAGS='$(RELEASE_CFLAGS)' LDFLAGS='$(RELEASE_LDFLAGS)' all
	bash tools/package-release.sh $(BUILD_DIR)/release-build/the-bugger $(RELEASE_DIST_DIR)

$(BUILD_DIR)/test-preferences: $(BUILD_DIR)/tests/unit/test_preferences.o $(BUILD_DIR)/src/tui/preferences.o $(BUILD_DIR)/src/tui/theme.o $(BUILD_DIR)/src/tui/keybinds.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(JSON_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-themes-tui: $(BUILD_DIR)/tests/integration/test_themes_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-themes
test-themes: all $(BUILD_DIR)/test-preferences $(BUILD_DIR)/test-render $(BUILD_DIR)/test-themes-tui
	$(BUILD_DIR)/test-preferences
	$(BUILD_DIR)/test-render
	$(BUILD_DIR)/test-themes-tui $(abspath $(BINARY))

test: test-themes

$(BUILD_DIR)/test-keybinds: $(BUILD_DIR)/tests/unit/test_keybinds.o $(BUILD_DIR)/src/tui/keybinds.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-keybinds
$(BUILD_DIR)/test-keybinds-tui: $(BUILD_DIR)/tests/integration/test_keybinds_tui.o $(BUILD_DIR)/tests/support/terminal.o $(BUILD_DIR)/src/tui/preferences.o $(BUILD_DIR)/src/tui/keybinds.o $(BUILD_DIR)/src/tui/theme.o $(BUILD_DIR)/src/session/profiles.o $(BUILD_DIR)/src/session/setup.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(JSON_LIBS) $(LDLIBS)

test-keybinds: all $(BUILD_DIR)/test-keybinds $(BUILD_DIR)/test-keybinds-tui
	$(BUILD_DIR)/test-keybinds
	$(BUILD_DIR)/test-keybinds-tui $(abspath $(BINARY))

test: test-keybinds
test-native: $(BUILD_DIR)/test-keybinds-tui

-include $(BUILD_DIR)/tests/unit/test_keybinds.d $(BUILD_DIR)/tests/integration/test_keybinds_tui.d $(BUILD_DIR)/src/tui/keybinds.d

-include $(BUILD_DIR)/tests/unit/test_preferences.d $(BUILD_DIR)/tests/integration/test_themes_tui.d

$(BUILD_DIR)/test-navigation: $(BUILD_DIR)/tests/integration/test_navigation.o $(BUILD_DIR)/tests/support/terminal.o $(BUILD_DIR)/src/tui/preferences.o $(BUILD_DIR)/src/tui/keybinds.o $(BUILD_DIR)/src/tui/theme.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(JSON_LIBS) $(LDLIBS)

.PHONY: test-navigation
test-navigation: all $(BUILD_DIR)/test-navigation
	$(BUILD_DIR)/test-navigation $(abspath $(BINARY))

test: test-navigation
-include $(BUILD_DIR)/tests/integration/test_navigation.d

$(BUILD_DIR)/test-fullscreen: $(BUILD_DIR)/tests/integration/test_fullscreen.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-fullscreen test-fullscreen-native
test-fullscreen: all $(BUILD_DIR)/test-fullscreen
	$(BUILD_DIR)/test-fullscreen $(abspath $(BINARY))
test-fullscreen-native: all $(BUILD_DIR)/test-fullscreen $(BUILD_DIR)/code-fixture
	$(BUILD_DIR)/test-fullscreen $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/code-fixture)

test: test-fullscreen
test-native: test-fullscreen-native
-include $(BUILD_DIR)/tests/integration/test_fullscreen.d

$(BUILD_DIR)/test-editor: $(BUILD_DIR)/tests/unit/test_editor.o $(BUILD_DIR)/src/tui/editor.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-editor
test-editor: $(BUILD_DIR)/test-editor
	$(BUILD_DIR)/test-editor

test: test-editor

-include $(BUILD_DIR)/tests/unit/test_editor.d

$(BUILD_DIR)/test-terminal-description: $(BUILD_DIR)/tests/unit/test_terminal_description.o $(BUILD_DIR)/src/tui/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-tmux-colors: $(BUILD_DIR)/tests/integration/test_tmux_colors.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: test-terminal-description test-tmux-colors
test-terminal-description: $(BUILD_DIR)/test-terminal-description
	$(BUILD_DIR)/test-terminal-description

test-tmux-colors: all $(BUILD_DIR)/test-tmux-colors
	$(BUILD_DIR)/test-tmux-colors $(abspath $(BINARY))

test: test-terminal-description
test-tmux: test-tmux-colors
-include $(BUILD_DIR)/tests/unit/test_terminal_description.d $(BUILD_DIR)/tests/integration/test_tmux_colors.d

$(BUILD_DIR)/test-mmio: $(BUILD_DIR)/tests/integration/test_mmio.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD_DIR)/test-mmio

$(BUILD_DIR)/test-mmio-memory: $(BUILD_DIR)/tests/integration/test_mmio_memory.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/browse.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-mmio-memory

$(BUILD_DIR)/test-mmio-tui: $(BUILD_DIR)/tests/integration/test_mmio_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD_DIR)/test-mmio-tui

$(BUILD_DIR)/test-image-tui: $(BUILD_DIR)/tests/integration/test_image_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-image-tui

-include $(BUILD_DIR)/tests/integration/test_image_tui.d

# Physical, explicitly named GPIOA_ODR probe: never executed by test-ci.
$(BUILD_DIR)/test-mmio-hardware: $(BUILD_DIR)/tests/integration/test_mmio_hardware.o $(BUILD_DIR)/src/model/mmio.o $(BUILD_DIR)/src/model/monitor.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(BUILD_DIR)/tests/integration/test_mmio.d $(BUILD_DIR)/tests/integration/test_mmio_memory.d $(BUILD_DIR)/tests/integration/test_mmio_tui.d
