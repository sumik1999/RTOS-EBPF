CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?= -Iinclude -Iexamples
BUILD := build

CORE_SRC := kernel/interpreter.c kernel/verifier.c kernel/maps_array.c kernel/net_hook.c
EXAMPLE_SRC := examples/static_programs.c
CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)
EXAMPLE_OBJ := $(EXAMPLE_SRC:%.c=$(BUILD)/%.o)
LIB := $(BUILD)/librtbpf_net.a
TEST_PHASE1 := $(BUILD)/test_phase1
TEST_VERIFIER := $(BUILD)/test_verifier
FUZZ_VERIFIER := $(BUILD)/fuzz_verifier

.PHONY: all test test-sanitize fuzz-smoke clean

all: $(LIB) $(TEST_PHASE1) $(TEST_VERIFIER) $(FUZZ_VERIFIER)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(TEST_PHASE1): tests/test_phase1.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

$(TEST_VERIFIER): tests/test_verifier.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

$(FUZZ_VERIFIER): tests/fuzz_verifier.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

test: $(TEST_PHASE1) $(TEST_VERIFIER)
	./$(TEST_PHASE1)
	./$(TEST_VERIFIER)
	python3 tests/test_packer.py

fuzz-smoke: $(FUZZ_VERIFIER)
	python3 -c 'import os,sys; sys.stdout.buffer.write(os.urandom(4096))' | ./$(FUZZ_VERIFIER)

test-sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_SRC) $(EXAMPLE_SRC) tests/test_phase1.c -o $(BUILD)/test_phase1_sanitize
	./$(BUILD)/test_phase1_sanitize
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_SRC) $(EXAMPLE_SRC) tests/test_verifier.c -o $(BUILD)/test_verifier_sanitize
	./$(BUILD)/test_verifier_sanitize

clean:
	rm -rf $(BUILD)
