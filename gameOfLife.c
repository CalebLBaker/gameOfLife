#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define START(arr,width) (arr)+(width)+1
#define END(arr,height,real_width) (arr)+((height)+1)*(real_width)
#define DEAD 0
#define ALIVE 1
#define BLOCK_LOW(id,p,n) ((id)*(n)/(p))
#define BLOCK_HIGH(id,p,n) (((id)+1)*(n)/(p)-1)
#define BLOCK_SIZE(id,p,n) (((id)+1)*(n)/(p)-(id)*(n)/(p))

#define INITIAL_READ 0

void printMatrix(char *m, size_t height, size_t width) {
	size_t real_width = width + 2;
	char *matrix_end = m + (height + 1) * real_width;
	for (char *i = START(m,real_width); i < matrix_end; i += real_width) {
		char *row_end = i + width;
		for (char *j = i; j < row_end; j++) {
			putchar(*j + '0');
		}
		putchar('\n');
	}
	putchar((int)'\n');
}

int main(int argc, char **argv) {

	unsigned id;
	unsigned p;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &p);
	MPI_Comm_rank(MPI_COMM_WORLD, &id);

	// Verify that command line arguments are valid and print help if they are not
	if (argc < 3) {
		if (id == 0) {
			printf("Usage: %s <input> <duration> [output_frequency]\n", argv[0]);
		}
		return 0;
	}

	size_t height, width;
	FILE *in;
	// Open input file
	unsigned reader = p - 1;
	if (id == reader) {
		*in = fopen(argv[1], "r");
		if (in == NULL) {
			height = 0;
		}
		else {
			// Read matrix dimensions
			fscanf(in, "%lu %lu\n", &height, &width);
		}
	}
	MPI_Bcast(&height, 1, MPI_LONG, reader, MPI_COMM_WORLD);
	MPI_Bcast(&width, 1, MPI_LONG, reader, MPI_COMM_WORLD);
	if (height == 0) {
		if (id == 0) {
			printf("Error reading file\n");
		}
		MPI_Finalize();
		return 0;
	}
	size_t num_local_rows = BLOCK_SIZE(id,p,height);

	// Allocate memory for matrix. Matrix is represented as a single array, and
	// we use pointer arithmetic to access individual elements. Note that the
	// array is larger than the matrix being loaded because we want to leave
	// space for "sentinal" elements.
	size_t real_width = width + 2;
	size_t real_size = (num_local_rows + 2) * real_width;
	char *next_frame = malloc(real_size, sizeof(char));

	if (id == reader) {
		for (unsigned i = 0; i < reader; i++) {
			size_t size = BLOCK_SIZE(i,p,height) * width;
			char *read_end = next_frame + size;
			for (char *j = next_frame; j < read_end; j += width) {
				fread(j, sizeof(char), width, in);
				getc(in);
			}
			MPI_Send(next_frame, size, MPI_CHAR, i, INITIAL_READ, MPI_COMM_WORLD);
		}
	}
	else {
		MPI_Status status;
		MPI_Recv(next_frame, num_local_rows * width, MPI_CHAR, reader, INITIAL_READ, MPI_COMM_WORLD, &status);
	}


	// Load matrix data into allocated buffer.
	size_t end_index = (height + 1) * real_width;
	char *read_end = curr_frame + end_index;
	for (char *dest = START(curr_frame,real_width); dest < read_end; dest += real_width) {
		fgets(dest, width + 1, in);
		char *row_end = dest + width;
		for (char *i = dest; i < row_end; i++) {
			*i -= '0';
		}
		getc(in);
	}
	fclose(in);

	// Begin printing with the initial matrix.

	// duration: how many total game of life cycles get computed
	// period: the number of cycles between each print
	unsigned long duration = (size_t) atoi(argv[2]);
	unsigned long period = (argc == 3) ? -1 : (size_t) atoi(argv[3]);

	bool should_print = argc > 3;

	char *next_frame = calloc(real_size, sizeof(char));
	for (unsigned long turn = 0; turn < duration; turn++) {

		// Print matrix after "period" turns
		if (should_print && turn % period == 0) {
			printMatrix(curr_frame, height, width);
		}

		// Fill "next_frame" with values for the next cycle of the game of life
		char *end = curr_frame + end_index;
		char *dest_row = START(next_frame,real_width);
		for (char *row = START(curr_frame,real_width); row < end; row += real_width) {
			char *row_end = row + width;
			char *dest = dest_row;
			for (char *i = row; i < row_end; i++) {
				char *up = i - real_width;
				char *down = i + real_width;
				unsigned char neighbors =	*(up-1)   +	*up   +	*(up+1)
										  + *(i-1)    +			*(i+1)
										  + *(down-1) +	*down +	*(down+1);
				*dest = (neighbors == 3 || neighbors == 2 && *i) ? ALIVE : DEAD;
				dest++;
			}
			dest_row += real_width;
		}

		// Swap curr_frame with next_frame
		char *tmp = curr_frame;
		curr_frame = next_frame;
		next_frame = tmp;
	}
	if (should_print) {
		printMatrix(curr_frame, height, width);
	}
	return 0;
}
