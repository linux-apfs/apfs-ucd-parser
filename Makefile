all: build build/unicode.c

build:
	mkdir build
build/unicode.c: build/mktrie
	build/mktrie
build/mktrie: mktrie.c
	gcc -o build/mktrie mktrie.c

clean:
	rm -Rf build
