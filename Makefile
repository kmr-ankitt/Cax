cax: src/cax.c
	$(CC) $< -o $@ -Wall -Wextra -pedantic -std=c99
run: run
	./cax

.PHONY: clean
clean:
	rm -rf cax
