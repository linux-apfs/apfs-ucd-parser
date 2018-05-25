unicode.c: mktrie
	./mktrie

mktrie: mktrie.c
	gcc -o mktrie mktrie.c

clean:
	rm -f mktrie
	rm -f unicode.c
