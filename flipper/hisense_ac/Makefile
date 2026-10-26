CC ?= cc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O1

.PHONY: test vectors clean app

test: tests/test_codec
	./tests/test_codec

vectors:
	python3 -I tests/gen_vectors.py tests/test_vectors.h

tests/test_codec: tests/test_codec.c hisense_codec.c hisense_codec.h tests/test_vectors.h
	$(CC) $(CFLAGS) -o $@ tests/test_codec.c hisense_codec.c

app:
	ufbt

clean:
	rm -f tests/test_codec
