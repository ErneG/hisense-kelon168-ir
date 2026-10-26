SRC := ../flipper/hisense_ac
COMP := components/hisense_kelon168
CXX ?= c++
CC ?= cc
ESPHOME ?= esphome

.PHONY: check-sync test compile clean

check-sync:
	@diff -u $(SRC)/hisense_codec.c $(COMP)/hisense_codec.c
	@diff -u $(SRC)/hisense_codec.h $(COMP)/hisense_codec.h
	@echo "codec copies in sync"

tests/test_decode: tests/test_decode.cpp $(COMP)/hisense_decode.h $(COMP)/hisense_codec.c $(COMP)/hisense_codec.h
	$(CC) -std=c11 -Wall -Wextra -Werror -c -o tests/hisense_codec.o $(COMP)/hisense_codec.c
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I$(COMP) -o $@ tests/test_decode.cpp tests/hisense_codec.o

test: check-sync tests/test_decode
	./tests/test_decode

compile: check-sync
	$(ESPHOME) compile hisense-heatpump.yaml

clean:
	rm -rf tests/test_decode tests/hisense_codec.o .esphome
