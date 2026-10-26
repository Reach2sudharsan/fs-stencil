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

TEST1 = bin/test_host-to-sim
TEST2 = bin/test_sim-to-host
TEST3 = bin/test_persist
TEST4 = bin/test_paths

.PHONY: all empty-iso clean

all: $(TEST1) $(TEST2) $(TEST3) $(TEST4)

$(TEST1): test/test_host-to-sim.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

$(TEST2): test/test_sim-to-host.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
$(TEST3): test/test_persist.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
$(TEST4): test/test_paths.c $(SRCS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

empty-iso:
	dd if=/dev/zero bs=4096 count=161 > sys/empty-fs.iso

clean:
	rm -f bin/*