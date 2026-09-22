CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?= -Iinclude -Iexamples
BUILD := build

CORE_SRC := kernel/interpreter.c kernel/verifier.c kernel/maps_array.c \
	kernel/net_hook.c kernel/rx_adapter.c adapters/st67w61/rtbpf_st67_adapter.c
EXAMPLE_SRC := examples/static_programs.c
CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)
EXAMPLE_OBJ := $(EXAMPLE_SRC:%.c=$(BUILD)/%.o)
LIB := $(BUILD)/librtbpf_net.a
TEST_PHASE1 := $(BUILD)/test_phase1
TEST_VERIFIER := $(BUILD)/test_verifier
TEST_RX_ADAPTER := $(BUILD)/test_rx_adapter
TEST_ST67_GLUE := $(BUILD)/test_st67_glue
FUZZ_VERIFIER := $(BUILD)/fuzz_verifier

.PHONY: all test test-sanitize fuzz-smoke clean

all: $(LIB) $(TEST_PHASE1) $(TEST_VERIFIER) $(TEST_RX_ADAPTER) $(TEST_ST67_GLUE) $(FUZZ_VERIFIER)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(TEST_PHASE1): tests/test_phase1.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

$(TEST_VERIFIER): tests/test_verifier.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

$(TEST_RX_ADAPTER): tests/test_rx_adapter.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EXAMPLE_OBJ) $(LIB) -o $@

$(TEST_ST67_GLUE): tests/test_st67_glue.c integration/x-cube-st67w61/rtbpf_st67_lwip_glue.c $(LIB) $(EXAMPLE_OBJ)
	$(CC) $(CPPFLAGS) -Itests/stubs -Iports/stm32h5 -Iintegration/x-cube-st67w61 \
		$(CFLAGS) $^ -o $@

$(FUZZ_VERIFIER): tests/fuzz_verifier.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

test: $(TEST_PHASE1) $(TEST_VERIFIER) $(TEST_RX_ADAPTER) $(TEST_ST67_GLUE)
	./$(TEST_PHASE1)
	./$(TEST_VERIFIER)
	./$(TEST_RX_ADAPTER)
	./$(TEST_ST67_GLUE)
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
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_SRC) $(EXAMPLE_SRC) tests/test_rx_adapter.c -o $(BUILD)/test_rx_adapter_sanitize
	./$(BUILD)/test_rx_adapter_sanitize
	$(CC) $(CPPFLAGS) -Itests/stubs -Iports/stm32h5 -Iintegration/x-cube-st67w61 \
		-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_SRC) $(EXAMPLE_SRC) integration/x-cube-st67w61/rtbpf_st67_lwip_glue.c \
		tests/test_st67_glue.c -o $(BUILD)/test_st67_glue_sanitize
	./$(BUILD)/test_st67_glue_sanitize

clean:
	rm -rf $(BUILD)
