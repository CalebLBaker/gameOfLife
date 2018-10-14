#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <mpi.h>

#define START(arr,width) (arr)+(width)+1
#define END(arr,height,real_width) (arr)+((height)+1)*(real_width)
#define DEAD 0
#define ALIVE 1
#define BLOCK_LOW(id,p,n) ((id)*(n)/(p))
#define BLOCK_HIGH(id,p,n) (((id)+1)*(n)/(p)-1)
#define BLOCK_SIZE(id,p,n) (((id)+1)*(n)/(p)-(id)*(n)/(p))

#define INITIAL_READ 0
#define SEND_BELOW 1
#define SEND_ABOVE 2
#define PROMPT 3
#define PRINT 4

unsigned id;
unsigned p;
char *buffer;

void printMatrix(char *m, size_t height, size_t width) {
	size_t real_width = width + 2;
	char *matrix_end = m + (height + 1) * real_width;
	char prompt;
	MPI_Status status;
	if (id == 0) {
		for (char *i = START(m,real_width); i < matrix_end; i += real_width) {
			char *row_end = i + width;
			for (char *j = i; j < row_end; j++) {
				putchar(*j + '0');
			}
			putchar('\n');
		}
		for (unsigned i = 1; i < p; i++) {
			MPI_Send(&prompt, 1, MPI_CHAR, i, PROMPT, MPI_COMM_WORLD);
			size_t size = BLOCK_SIZE(i,p,height) * width;
			MPI_Recv(buffer, size, MPI_CHAR, i, PRINT, MPI_COMM_WORLD, &status);
			char *end = buffer + size;
			for (char *i = buffer; i < end; i += width) {
				char *row_end = i + width;
				for (char *j = i; j < row_end; j++) {
					putchar(*j + '0');
				}
				putchar('\n');
			}
		}
		putchar('\n');
	}
	else {
		size_t size = BLOCK_SIZE(id,p,height) * width;
		char *dest = buffer;
		for (char *src = START(m,real_width); src < matrix_end; src += real_width, dest += width) {
			memcpy(dest, src, width);
		}
		MPI_Recv(&prompt, 1, MPI_CHAR, 0, PROMPT, MPI_COMM_WORLD, &status);
		MPI_Send(buffer, size, MPI_CHAR, 0, PRINT, MPI_COMM_WORLD);
	}
}

int main(int argc, char **argv) {

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
		in = fopen(argv[1], "r");
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
	char *next_frame = malloc(real_size * sizeof(char));
	char *curr_frame = calloc(real_size, sizeof(char));

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
		char *read_end = curr_frame + num_local_rows * real_width;
		for (char *j = START(curr_frame,real_width); j < read_end; j += real_width) {
			fread(j, sizeof(char), width, in);
			getc(in);
		}
	}
	else {
		MPI_Status status;
		MPI_Recv(next_frame, num_local_rows * width, MPI_CHAR, reader, INITIAL_READ, MPI_COMM_WORLD, &status);
		char *read_end = next_frame + num_local_rows * width;
		char *dest = START(curr_frame,real_width);
		for (char *src = next_frame; src < read_end; src += width, dest += real_width) {
			memcpy(dest, src, width * sizeof(char));
		}
	}

	unsigned end_index = (num_local_rows + 1) * real_width;
	size_t last_row_index = real_width * num_local_rows + 1;
	unsigned next_guy = id + 1;
	unsigned prev_guy = id - 1;
	end_index++;

	char *end = curr_frame + end_index;
	for (char *i = START(curr_frame,real_width); i < end; i += real_width) {
		char *row_end = i + width;
		for (char *j = i; j < row_end; j++) {
			*j -= '0';
		}
	}

	size_t buffer_rows = (id) ? BLOCK_SIZE(id,p,height) : BLOCK_SIZE(reader,p,height);
	buffer = malloc(buffer_rows * width * sizeof(char));
	// Clear Sentinals
	memset(next_frame, 0, real_width);
	char *end_sentinals = next_frame + end_index;
	for (char *i = next_frame + real_width; i < end_sentinals; i += real_width) {
		*i = 0;
		i[real_width - 1] = 0;
	}
	memset(end_sentinals, 0, real_width);


	// Begin printing with the initial matrix.

	// duration: how many total game of life cycles get computed
	// period: the number of cycles between each print
	unsigned long duration = (size_t) atoi(argv[2]);
	unsigned long period = (argc == 3) ? -1 : (size_t) atoi(argv[3]);

	bool should_print = argc > 3;


	for (unsigned long turn = 0; turn < duration; turn++) {

		// Print matrix after "period" turns
		if (should_print && turn % period == 0) {
			printMatrix(curr_frame, height, width);
		}

		end = curr_frame + end_index;
		MPI_Status status;
		if (id & 1 == 1) {
			if (id != reader) {
				MPI_Send(curr_frame + last_row_index, width, MPI_CHAR, next_guy, SEND_BELOW, MPI_COMM_WORLD);
				MPI_Recv(end, width, MPI_CHAR, next_guy, SEND_ABOVE, MPI_COMM_WORLD, &status);
			}
			MPI_Send(START(curr_frame,real_width), width, MPI_CHAR, prev_guy, SEND_ABOVE, MPI_COMM_WORLD);
			MPI_Recv(curr_frame + 1, width, MPI_CHAR, prev_guy, SEND_BELOW, MPI_COMM_WORLD, &status);
		}
		else {
			if (id != 0) {
				MPI_Recv(curr_frame + 1, width, MPI_CHAR, prev_guy, SEND_BELOW, MPI_COMM_WORLD, &status);
				MPI_Send(START(curr_frame,real_width), width, MPI_CHAR, prev_guy, SEND_ABOVE, MPI_COMM_WORLD);
			}
			if (id != reader) {
				MPI_Recv(end, width, MPI_CHAR, next_guy, SEND_ABOVE, MPI_COMM_WORLD, &status);
				MPI_Send(curr_frame + last_row_index, width, MPI_CHAR, next_guy, SEND_BELOW, MPI_COMM_WORLD);
			}
		}

		// Fill "next_frame" with values for the next cycle of the game of life
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
