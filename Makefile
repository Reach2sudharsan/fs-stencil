CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -I.

SRCS = src/mobj.c \
       src/namev.c \
       src/slab_allocator.c \
       src/svfs.c \
       src/vnode.c \
       sys/disk/disk.c \
       sys/mem/mem.c \
       util/list.c \
       util/spinlock.c \
       util/string.c

TEST1 = bin/test6-1
TEST2 = bin/test6-2

.PHONY: all empty-iso clean

all: $(TEST1) $(TEST2)

$(TEST1): test/test6-1.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

$(TEST2): test/test6-2.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

empty-iso:
	dd if=/dev/zero bs=4096 count=161 > sys/empty-fs.iso

clean:
	rm -f bin/*