PROG := nlex
CFLAGS := -fsanitize=address,undefined -Wall -g -Og
LDFLAGS :=
CC := clang

.PHONY: all
all: $(PROG)

$(PROG): main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf *.o $(PROG)

