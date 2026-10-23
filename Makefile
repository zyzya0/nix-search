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
SRC := src/main.c src/util.c src/channel.c
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

TEST_CHANNEL := $(BUILD)/test_channel

.PHONY: all clean test install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_CHANNEL): tests/test_channel.c src/channel.c src/util.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_channel.c src/channel.c src/util.c

test: $(BIN) $(TEST_CHANNEL)
	./$(TEST_CHANNEL)

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:
	rm -rf $(BUILD) $(BIN)
