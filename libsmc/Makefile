CC = clang
CFLAGS = -mmacosx-version-min=10.4 -Wall -DCMD_TOOL_BUILD

all: libsmc.a

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

lib%.a: %.o
	ar rc $@ $<
	ranlib $@

clean:
	rm -f *.a *.o
