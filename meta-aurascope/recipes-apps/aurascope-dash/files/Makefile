CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
LDLIBS   := -lasound -lpthread -lm -lutil

SRCS := main.c audio.c http.c fft.c health.c
OBJS := $(SRCS:.c=.o)
BIN  := aurascope-dash

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
