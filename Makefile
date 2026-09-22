CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?= -Iinclude -Iexamples
BUILD := build

CORE_SRC := kernel/interpreter.c kernel/maps_array.c kernel/net_hook.c
EXAMPLE_SRC := examples/static_programs.c
CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)
EXAMPLE_OBJ := $(EXAMPLE_SRC:%.c=$(BUILD)/%.o)
LIB := $(BUILD)/librtbpf_net.a
TEST := $(BUILD)/test_phase1

.PHONY: all test test-sanitize clean

all: $(LIB) $(TEST)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(TEST): tests/test_phase1.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

test: $(TEST)
	./$(TEST)

test-sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_SRC) $(EXAMPLE_SRC) tests/test_phase1.c -o $(BUILD)/test_phase1_sanitize
	./$(BUILD)/test_phase1_sanitize

clean:
	rm -rf $(BUILD)
