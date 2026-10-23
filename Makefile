CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wshadow \
          -Wformat=2 -Wundef -Wpointer-arith
CPPFLAGS += -Isrc
LDFLAGS ?=
LDLIBS ?=

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BUILD := build

BIN := nix-search
SRC := src/main.c src/util.c src/channel.c src/json.c src/package.c
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

TEST_JSON := $(BUILD)/test_json
TEST_PACKAGE := $(BUILD)/test_package
TEST_CHANNEL := $(BUILD)/test_channel

.PHONY: all clean test install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_JSON): tests/test_json.c src/json.c src/util.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_json.c src/json.c src/util.c

$(TEST_PACKAGE): tests/test_package.c src/package.c src/json.c src/util.c \
                 src/channel.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_package.c src/package.c \
	      src/json.c src/util.c src/channel.c

$(TEST_CHANNEL): tests/test_channel.c src/channel.c src/util.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_channel.c src/channel.c src/util.c

test: $(BIN) $(TEST_JSON) $(TEST_PACKAGE) $(TEST_CHANNEL)
	./$(TEST_JSON)
	./$(TEST_PACKAGE)
	./$(TEST_CHANNEL)

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:
	rm -rf $(BUILD) $(BIN)
