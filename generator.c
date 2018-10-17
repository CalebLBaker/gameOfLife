#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv) {
	srand((unsigned)time(0));
	unsigned height = atoi(argv[2]);
	unsigned width = atoi(argv[3]);
	FILE *f = fopen(argv[1], "w");
	fprintf(f, "%s %s\n", argv[2], argv[3]);
	for (unsigned i = 0; i < height; i++) {
		for (unsigned j = 0; j < width; j++) {
			fprintf(f, "%u", rand() & 1);
		}
		fprintf(f, "\n");
	}
	return 0;
}
