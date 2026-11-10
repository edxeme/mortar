# mortar — build recipes
#
#   make            build bin/mortar and bin/bench
#   make test       build and run the unit tests
#   make asan       rebuild everything under ASan + UBSan and run tests
#   make debug      -O0 -g3 client build meant for gdb sessions
#   make bench      build and run the micro-benchmark

CC       ?= cc
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -O2 -g
LDFLAGS  ?=

BIN      := bin
BUILD    := build

LIB_SRCS := src/util.c src/arena.c src/mem.c src/htab.c src/store.c
LIB_OBJS := $(LIB_SRCS:src/%.c=$(BUILD)/%.o)

TESTS    := test_arena test_mem test_htab test_store
TEST_BINS := $(TESTS:%=$(BIN)/%)

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all test asan debug bench clean

all: $(BIN)/mortar $(BIN)/bench

$(BIN) $(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_%.o: tests/test_%.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -Itests -c $< -o $@

$(BUILD)/bench.o: bench/bench.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BIN)/mortar: $(BUILD)/main.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/main.o $(LIB_OBJS) $(LDFLAGS)

$(BIN)/bench: $(BUILD)/bench.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/bench.o $(LIB_OBJS) $(LDFLAGS)

$(BIN)/test_arena: $(BUILD)/test_arena.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_arena.o $(LIB_OBJS) $(LDFLAGS)

$(BIN)/test_mem: $(BUILD)/test_mem.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_mem.o $(LIB_OBJS) $(LDFLAGS)

$(BIN)/test_htab: $(BUILD)/test_htab.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_htab.o $(LIB_OBJS) $(LDFLAGS)

$(BIN)/test_store: $(BUILD)/test_store.o $(LIB_OBJS) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_store.o $(LIB_OBJS) $(LDFLAGS)

test: $(TEST_BINS)
	@set -e; for t in $(TESTS); do echo "== $$t =="; $(BIN)/$$t; done

asan:
	@set -e; \
	for t in $(TESTS); do \
		echo "== $$t (asan) =="; \
		$(CC) $(CFLAGS) $(ASAN_FLAGS) -Isrc -Itests -o $(BIN)/$$t-asan tests/$$t.c $(LIB_SRCS); \
		$(BIN)/$$t-asan; \
	done; \
	echo "== mortar client (asan) =="; \
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(BIN)/mortar-asan src/main.c $(LIB_SRCS); \
	echo OK

debug:
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L \
		-O0 -g3 -fno-omit-frame-pointer -o $(BIN)/mortar-dbg \
		src/main.c $(LIB_SRCS)

bench: $(BIN)/bench
	./$(BIN)/bench

clean:
	rm -rf $(BUILD) $(BIN)
