CC      = cc
CXX     = c++
CFLAGS  = -Wall -Wextra -std=c11 -g -I src
CXXFLAGS= -Wall -Wextra -std=c++17 -g -I src
LDLIBS  = -lm

# Every module is part of the library.
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

TEST_SRCS = $(wildcard tests/*.c)
TESTS     = $(TEST_SRCS:.c=)

TOOL_SRCS = $(wildcard tools/*.c)
TOOLS     = $(TOOL_SRCS:.c=)

.PHONY: all clean test tools asan seeds

all: libconfigbin.a tools

libconfigbin.a: $(OBJS)
	ar rcs $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Tests and tools each link against the whole library archive.
tests/%: tests/%.c libconfigbin.a
	$(CC) $(CFLAGS) $< libconfigbin.a $(LDLIBS) -o $@

tools/%: tools/%.c libconfigbin.a
	$(CC) $(CFLAGS) $< libconfigbin.a $(LDLIBS) -o $@

tools: $(TOOLS)

test: $(TESTS)
	@fail=0; for t in $(TESTS); do \
	    echo "== $$t"; ./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "all tests passed"

# ASAN build for local PoC verification: make asan && /tmp/bcfg_asan_driver poc.bin
asan:
	clang -fsanitize=address,undefined \
	      -fno-omit-frame-pointer -g \
	      -I src \
	      src/configbin.c src/value.c src/frame.c \
	      -o /tmp/bcfg_asan_driver \
	      -x c - << 'EOF'
	#include <stdio.h>
	#include <stdlib.h>
	#include "src/configbin.h"
	int main(int argc, char **argv){
	    FILE *f=fopen(argv[1],"rb");
	    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
	    unsigned char *buf=malloc(n); fread(buf,1,n,f); fclose(f);
	    bcfg_doc *doc=NULL;
	    bcfg_parse(buf,n,&doc);
	    bcfg_free(doc); free(buf);
	}
	EOF

seeds:
	python3 gen_seeds.py

clean:
	rm -f src/*.o libconfigbin.a $(TESTS) $(TOOLS)
