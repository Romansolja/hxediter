CC      = gcc
CFLAGS  = -Wall -Wextra -O2
TARGET  = hxediter
SRCS    = main.c display.c fileops.c undo.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c platform.h editor.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
