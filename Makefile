cax: src/cax.c
	$(CC) $< -o $@ -Wall -Wextra -pedantic -std=c99

.PHONY: clean
clean:
	rm -rf cax
