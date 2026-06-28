CC      = cc
CXX     = c++
CFLAGS  = -Wall -Wextra -std=c11 -g -I src
CXXFLAGS= -Wall -Wextra -std=c++17 -g -I src

SRCS = src/configbin.c src/value.c src/frame.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean test asan seeds

all: libconfigbin.a

libconfigbin.a: $(OBJS)
	ar rcs $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_basic: tests/test_basic.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

test: tests/test_basic
	./tests/test_basic

# ASAN build for local PoC verification
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

poc:
	python3 gen_poc.py

clean:
	rm -f src/*.o libconfigbin.a tests/test_basic poc.bin
