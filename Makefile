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
PROJECT_CPPFLAGS := -Iinclude -Isrc/tui -Isrc/layout -Isrc/gdb -Isrc/session -Isrc/model $(NCURSES_CFLAGS) $(ELF_CFLAGS)
PROJECT_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror \
	-Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes
SOURCES := src/main.c src/tui/app.c src/tui/demo.c src/tui/render.c src/tui/theme.c src/layout/layout.c src/gdb/mi.c src/gdb/controller.c src/session/native.c src/model/context.c src/model/code.c src/model/images.c src/model/breakpoints.c src/model/registers.c src/model/memory.c src/model/variables.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
BINARY := $(BUILD_DIR)/the-bugger

.PHONY: all test test-native fixtures test-mi test-controller test-gdb-startup controller-demo mi-demo test-tmux sanitize sanitize-native analyze clean check-deps

all: $(BINARY)

$(BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(NCURSES_LIBS) $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c Makefile | check-deps
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(PROJECT_CFLAGS) -MMD -MP -c $< -o $@

check-deps:
	@$(PKG_CONFIG) --exists ncursesw || { echo 'ncursesw development files and pkg-config are required.' >&2; exit 1; }
	@$(PKG_CONFIG) --exists libelf || { echo 'libelf development files are required.' >&2; exit 1; }

$(BUILD_DIR)/test-unit: $(BUILD_DIR)/tests/unit/test_demo.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-cli: $(BUILD_DIR)/tests/integration/test_cli.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-tui: $(BUILD_DIR)/tests/integration/test_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-render: $(BUILD_DIR)/tests/unit/test_render.o $(BUILD_DIR)/src/tui/render.o $(BUILD_DIR)/src/tui/theme.o $(BUILD_DIR)/src/tui/demo.o
	$(CC) $(LDFLAGS) -o $@ $^ $(NCURSES_LIBS) $(ELF_LIBS) $(LDLIBS)

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

NATIVE_OBJECTS = $(BUILD_DIR)/src/session/native.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/breakpoints.o $(BUILD_DIR)/src/model/registers.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/variables.o

$(BUILD_DIR)/test-native-words: $(BUILD_DIR)/tests/unit/test_native_words.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-native: $(BUILD_DIR)/tests/integration/test_native.o $(NATIVE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-native-tui: $(BUILD_DIR)/tests/integration/test_native_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-native: all $(BUILD_DIR)/test-native $(BUILD_DIR)/test-native-tui $(BUILD_DIR)/test-context-native $(BUILD_DIR)/test-context-tui $(BUILD_DIR)/test-code-native $(BUILD_DIR)/test-code-tui fixtures
	$(BUILD_DIR)/test-native $(abspath $(BUILD_DIR)/debug-fixture) $(abspath $(BUILD_DIR)/debug-fixture-cpp)
	$(BUILD_DIR)/test-native-tui $(abspath $(BINARY)) $(abspath $(BUILD_DIR)/debug-fixture)
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

$(BUILD_DIR)/test-code: $(BUILD_DIR)/tests/integration/test_code.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

$(BUILD_DIR)/test-code-tui: $(BUILD_DIR)/tests/integration/test_code_tui.o $(BUILD_DIR)/tests/support/terminal.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test-tmux: all $(BUILD_DIR)/test-tmux
	$(BUILD_DIR)/test-tmux $(abspath $(BINARY))

$(BUILD_DIR)/test-context: $(BUILD_DIR)/tests/integration/test_context.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: all test-mi test-controller $(BUILD_DIR)/test-code $(BUILD_DIR)/test-context $(BUILD_DIR)/test-native-words $(BUILD_DIR)/test-unit $(BUILD_DIR)/test-layout $(BUILD_DIR)/test-render $(BUILD_DIR)/test-cli $(BUILD_DIR)/test-tui $(BUILD_DIR)/test-workspace
	$(BUILD_DIR)/test-images
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

test: $(BUILD_DIR)/test-images

$(BUILD_DIR)/test-breakpoints: $(BUILD_DIR)/tests/integration/test_breakpoints.o $(BUILD_DIR)/src/model/breakpoints.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
	$(CC) $(LDFLAGS) -o $@ $^ $(ELF_LIBS) $(LDLIBS)

test: $(BUILD_DIR)/test-breakpoints

$(BUILD_DIR)/test-registers: $(BUILD_DIR)/tests/integration/test_registers.o $(BUILD_DIR)/src/model/registers.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
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

$(BUILD_DIR)/test-memory: $(BUILD_DIR)/tests/integration/test_memory.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
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

$(BUILD_DIR)/test-variables: $(BUILD_DIR)/tests/integration/test_variables.o $(BUILD_DIR)/src/model/variables.o $(BUILD_DIR)/src/model/memory.o $(BUILD_DIR)/src/model/context.o $(BUILD_DIR)/src/model/code.o $(BUILD_DIR)/src/model/images.o $(BUILD_DIR)/src/gdb/controller.o $(BUILD_DIR)/src/gdb/mi.o
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
