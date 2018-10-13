#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>

#define START(arr,width) (arr)+(width)+1
#define END(arr,height,real_width) (arr)+((height)+1)*(real_width)
#define DEAD 0
#define ALIVE 1
#define INVISIBLE 0
#define NORMAL 1

void printMatrix(char *m, size_t height, size_t width) {
	size_t real_width = width + 2;
	size_t matrix_end = real_width * (height + 1);
	for (size_t i = real_width + 1; i < matrix_end; i += real_width) {
		for (size_t j = 0; j < width; j++) {
			if (m[i+j]) {
				mvaddch(i / real_width, j, '*');
			}
		}
	}
}

void updateMatrix(char *m, char *old, size_t height, size_t width) {
	size_t real_width = width + 2;
	size_t matrix_end = real_width * (height + 1);
	for (size_t i = real_width + 1; i < matrix_end; i += real_width) {
		for (size_t j = 0; j < width; j++) {
			size_t index = i + j;
			if (m[index] != old[index]) {
				mvaddch(i / real_width, j, (m[index]) ? '*' : ' ');
			}
		}
	}
}

int main(int argc, char **argv) {
	initscr();
	if (argc < 4) {
		printw("Usage: %s <input> <duration> <output_frequency>", argv[0]);
		refresh();
		return 0;
	}
	cbreak();
	noecho();
	curs_set(INVISIBLE);
	size_t height, width;
	FILE *in = fopen(argv[1], "r");
	fscanf(in, "%lu %lu\n", &height, &width);
	size_t real_width = width + 2;
	size_t real_height = height + 2;
	size_t real_size = (height + 2) * real_width;
	char *m = calloc(real_size, sizeof(char));
	size_t end_index = (height + 1) * real_width;
	char *read_end = m + end_index;
	for (char *dest = START(m,real_width); dest < read_end; dest += real_width) {
		fgets(dest, width + 1, in);
		char *row_end = dest + width;
		for (char *i = dest; i < row_end; i++) {
			*i = (*i == ' ' || *i == '0' || *i == '.') ? DEAD : ALIVE;
		}
		getc(in);
	}
	fclose(in);
	printMatrix(m, height, width);
	move(0, 0);
	printw("Initial State:");
	refresh();
	unsigned long duration = (size_t) atoi(argv[2]);
	unsigned long freq = (size_t) atoi(argv[3]);
	unsigned clock = (argc == 5) ? (unsigned)(atof(argv[4]) * 1000000) : 0;
	char *backup = calloc(real_size, sizeof(char));
	for (unsigned long turn = 1; turn <= duration; turn++) {
		char *end = m + end_index;
		char *dest_row = START(backup,real_width);
		for (char *row = START(m,real_width); row < end; row += real_width) {
			char *row_end = row + width;
			char *dest = dest_row;
			for (char *i = row; i < row_end; i++) {
				char *up = i - real_width;
				char *down = i + real_width;
				unsigned char neighbors =	*(up-1) +	*up +	*(up+1)
										  + *(i-1) +			*(i+1)
										  + *(down-1) +	*down +	*(down+1);
				*dest = (neighbors == 3 || neighbors == 2 && *i) ? ALIVE : DEAD;
				dest++;
			}
			dest_row += real_width;
		}
		char *tmp = m;
		m = backup;
		backup = tmp;
		if (turn % freq == 0) {
			if (clock) {
				usleep(clock);
			}
			else {
				getch();
			}
			updateMatrix(m, backup, height, width);
			move(0, 0);
			printw("Turn %lu:       ", turn);
			refresh();
		}
	}
	if (clock) {
		usleep(clock);
	}
	else {
		getch();
	}
	updateMatrix(m, backup, height, width);
	move(0, 0);
	printw("Finished (%lu turns):", duration);
	move(real_height, 0);
	refresh();
	endwin();
	return 0;
}
