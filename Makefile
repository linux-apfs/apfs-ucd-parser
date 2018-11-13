OUT_DIR = build
SCR_DIR = $(OUT_DIR)/scripts

all: $(SCR_DIR) $(OUT_DIR)/unicode.c $(OUT_DIR)/unicode.h $(OUT_DIR)/test.out

$(SCR_DIR):
	mkdir -p $(SCR_DIR)

# Compile and run the tests
$(OUT_DIR)/test.out: $(SCR_DIR)/unitest
	$(SCR_DIR)/unitest > $(OUT_DIR)/test.out
$(SCR_DIR)/unitest: $(SCR_DIR)/unicode.c $(SCR_DIR)/unicode.h
	gcc $(CFLAGS) -o $(SCR_DIR)/unitest $(SCR_DIR)/unicode.c

# We want to patch together two different versions of the generated source code:
# one for the kernel module, and another for running tests in user space
$(OUT_DIR)/unicode.c: $(SCR_DIR)/mktrie code/unicode.c code/bld_head.c
	$(SCR_DIR)/mktrie
	cat code/bld_head.c code/unicode.c unicode.c.tmp > $(OUT_DIR)/unicode.c
	rm -f unicode.c.tmp
$(SCR_DIR)/unicode.c: $(SCR_DIR)/mktrie code/unicode.c code/test_head.c
	$(SCR_DIR)/mktrie
	cat code/test_head.c code/unicode.c unicode.c.tmp > $(SCR_DIR)/unicode.c
	rm -f unicode.c.tmp

$(OUT_DIR)/unicode.h: code/unicode.h code/bld_head.h
	cat code/bld_head.h code/unicode.h > $(OUT_DIR)/unicode.h
$(SCR_DIR)/unicode.h: code/unicode.h code/test_head.h
	cat code/test_head.h code/unicode.h > $(SCR_DIR)/unicode.h

$(SCR_DIR)/mktrie: mktrie.c
	gcc $(CFLAGS) -o $(SCR_DIR)/mktrie mktrie.c

clean:
	rm -Rf $(OUT_DIR)
	rm -f unicode.c.tmp
