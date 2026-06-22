TARGET  = disk-analysis
CC      = gcc
CFLAGS  = -W -Wall -Wextra -g
LDFLAGS = -lncurses -lpng
CFLAGS += -I/usr/local/include

SRCS = disk-analysis.c \
       disk-analysis-log.c \
       mfm.c \
       fluxstream.c \
       input.c \
       kf-info.c \
       kf-oob.c \
       atari-fs.c \
       visualise.c

OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
