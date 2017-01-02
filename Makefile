CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -Werror -D_GNU_SOURCE -I. -g -ansi
LDFLAGS =
TARGET = latcheck

# begin generic

SRC = $(wildcard *.c) $(wildcard */*.c)
HDR = $(wildcard *.h) $(wildcard */*.h)
OBJ = $(SRC:.c=.o)

$(TARGET): $(OBJ)
	@echo $@
	@$(CC) $(LDFLAGS) $(OBJ) -o$@

%.o: %.c $(HDR)
	@echo $@
	@$(CC) $(CFLAGS) -c -o$@ $<

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: clean
