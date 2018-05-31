all: build build/unicode.c build/unicode.h

build:
	mkdir build
build/unicode.c: build/mktrie code/unicode.c
	build/mktrie
	cat code/unicode.c build/unicode.c.tmp > build/unicode.c
	rm -f build/unicode.c.tmp
build/unicode.h: code/unicode.h
	cp code/unicode.h build/unicode.h
build/mktrie: mktrie.c
	gcc -o build/mktrie mktrie.c

clean:
	rm -Rf build
