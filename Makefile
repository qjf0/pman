CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
TARGET   = pman
SRC      = main.c

all: $(TARGET)

$(TARGET): $(SRC) config.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

config.h:
	cp config.def.h config.h

clean:
	rm -f $(TARGET)

.PHONY: all clean
