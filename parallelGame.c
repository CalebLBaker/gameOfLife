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
char *curr_frame_end;
char *next_frame_end;

void printMatrix(char *m, size_t height, size_t width) {
	size_t real_width = width + 2;
	size_t num_local_rows = BLOCK_SIZE(id,p,height);
	char *matrix_end = m + (num_local_rows + 1) * real_width;
	char prompt;
	static char *buffer = NULL;
	MPI_Status status;
	if (id == 0) {
		if (buffer == NULL) {
			buffer = malloc(BLOCK_SIZE(p-1,p,height) * width * sizeof(char));
		}
		char *buffer_end = buffer + BLOCK_SIZE(p-1,p,height) * width;
		// Print processor 0's section of the matrix
		for (char *i = START(m,real_width); i < matrix_end; i += real_width) {
			char *row_end = i + width;
			for (char *j = i; j < row_end; j++) {
				putc(*j + '0', stderr);
			}
			putc('\n', stderr);
		}

		// Print the rest of the matrix
		for (unsigned i = 1; i < p; i++) {
			size_t size = BLOCK_SIZE(i,p,height) * width;
			MPI_Send(&prompt, 1, MPI_CHAR, i, PROMPT, MPI_COMM_WORLD);
			MPI_Recv(buffer, size, MPI_CHAR, i, PRINT, MPI_COMM_WORLD, &status);
			char *end = buffer + size;
			for (char *i = buffer; i < end; i += width) {
				char *row_end = i + width;
				for (char *j = i; j < row_end; j++) {
					putc(*j + '0', stderr);
				}
				putc('\n', stderr);
			}
		}
		putc('\n', stderr);
	}
	// All other processors send data to processor 0
	else {
		if (buffer == NULL) {
			buffer = malloc(num_local_rows * width * sizeof(char));
		}
		char *buffer_end = buffer + num_local_rows * width;
		char *dest = buffer;
		for(char *src = START(m,real_width); src < matrix_end; src+=real_width){
			memcpy(dest, src, width);
			dest += width;
		}
		MPI_Recv(&prompt, 1, MPI_CHAR, 0, PROMPT, MPI_COMM_WORLD, &status);
		MPI_Send(buffer, num_local_rows*width, MPI_CHAR,0,PRINT,MPI_COMM_WORLD);
	}
}

int main(int argc, char **argv) {

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &p);
	MPI_Comm_rank(MPI_COMM_WORLD, &id);
	MPI_Barrier(MPI_COMM_WORLD);
	double start_time = MPI_Wtime();

	// Verify that command line arguments are valid
	// print help if they are not
	if (argc < 3) {
		if (id == 0) {
			printf("Usage: %s <input> <duration> [output_frequency]\n",argv[0]);
		}
		return 0;
	}

	// Open input file
	size_t height, width;
	FILE *in;
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

	// Make sure that there isn't too many processors for the problem size
	if (p > height) {
		if (id == 0) {
			printf(
			"Please do not use more processors than your matrix has rows!\n");
		}
		MPI_Finalize();
		return 0;
	}

	// Allocate memory for matrix. Matrix is represented as a single array, and
	// we use pointer arithmetic to access individual elements. Note that the
	// array is larger than the matrix being loaded because we want to leave
	// space for "sentinal" elements.
	size_t num_local_rows = BLOCK_SIZE(id,p,height);
	size_t real_width = width + 2;
	size_t real_size = (num_local_rows + 2) * real_width;
	char *next_frame = malloc(real_size * sizeof(char));
	char *curr_frame = calloc(real_size, sizeof(char));
	curr_frame_end = curr_frame + real_size;
	next_frame_end = next_frame + real_size;
	size_t start_index = real_width + 1;
	size_t last_row_index = num_local_rows * real_width + 1;
	size_t end_index = last_row_index + real_width;

	// Read file and distribute data
	if (id == reader) {

		// Send everyone else their data
		for (unsigned i = 0; i < reader; i++) {
			size_t size = BLOCK_SIZE(i,p,height) * width;
			char *read_end = next_frame + size;
			for (char *j = next_frame; j < read_end; j += width) {
				fread(j, sizeof(char), width, in);
				getc(in);
			}
			MPI_Send(next_frame,size,MPI_CHAR,i,INITIAL_READ,MPI_COMM_WORLD);
		}

		// Read the last processor's data from the file
		char *read_end = curr_frame + end_index;
		for (char *j = curr_frame + start_index; j < read_end; j += real_width){
			fread(j, sizeof(char), width, in);
			getc(in);
		}
	}
	else {

		// Receive data from last processor
		MPI_Status status;
		size_t local_size = num_local_rows * width;
		MPI_Recv(next_frame, local_size, MPI_CHAR, reader,
				 INITIAL_READ, MPI_COMM_WORLD, &status);
		char *read_end = next_frame + local_size;
		char *dest = curr_frame + start_index;

		// Move data into its proper place
		for (char *src = next_frame; src < read_end; src += width) {
			memcpy(dest, src, width * sizeof(char));
			dest += real_width;
		}
	}

	// Convert ASCII characters to 1's and 0's
	char *end = curr_frame + end_index;
	for (char *i = curr_frame + start_index; i < end; i += real_width) {
		char *row_end = i + width;
		for (char *j = i; j < row_end; j++) {
			*j -= '0';
		}
	}

	// Clear Sentinals
	memset(next_frame, 0, real_width);
	char *end_sentinals = next_frame + end_index - 1;
	size_t right_sentinal_index = real_width - 1;
	for (char *i = next_frame + real_width; i < end_sentinals; i += real_width){
		*i = 0;
		i[right_sentinal_index] = 0;
	}
	memset(end_sentinals, 0, real_width);


	// Begin printing with the initial matrix.

	// duration: how many total game of life cycles get computed
	// period: the number of cycles between each print
	unsigned long duration = (size_t) atoi(argv[2]);
	unsigned long period = (argc == 3) ? -1 : (size_t) atoi(argv[3]);
	bool should_print = argc > 3;
	bool odd = id & 1 == 1;
	unsigned next_guy = id + 1;
	unsigned prev_guy = id - 1;

	for (unsigned long turn = 0; turn < duration; turn++) {

		// Print matrix after "period" turns
		if (should_print && turn % period == 0) {
			printMatrix(curr_frame, height, width);
		}

		// Distribute first and last rows to neighboring processors
		end = curr_frame + end_index;
		char *curr_frame_start = curr_frame + start_index;
		MPI_Status status;
		if (odd) {
			if (id != reader) {
				MPI_Send(curr_frame + last_row_index, width, MPI_CHAR,
						 next_guy, SEND_BELOW, MPI_COMM_WORLD);
				MPI_Recv(end, width, MPI_CHAR, next_guy,
						 SEND_ABOVE, MPI_COMM_WORLD, &status);
			}
			MPI_Send(curr_frame_start, width, MPI_CHAR,
					 prev_guy, SEND_ABOVE, MPI_COMM_WORLD);
			MPI_Recv(curr_frame + 1, width, MPI_CHAR,
					 prev_guy, SEND_BELOW, MPI_COMM_WORLD, &status);
		}
		else {
			if (id != 0) {
				MPI_Recv(curr_frame + 1, width, MPI_CHAR,
						 prev_guy, SEND_BELOW, MPI_COMM_WORLD, &status);
				MPI_Send(curr_frame_start, width, MPI_CHAR,
						 prev_guy, SEND_ABOVE, MPI_COMM_WORLD);
			}
			if (id != reader) {
				MPI_Recv(end, width, MPI_CHAR, next_guy,
						 SEND_ABOVE, MPI_COMM_WORLD, &status);
				MPI_Send(curr_frame + last_row_index, width, MPI_CHAR,
						 next_guy, SEND_BELOW, MPI_COMM_WORLD);
			}
		}

		// Fill "next_frame" with values for the next cycle of the game of life
		char *dest_row = next_frame + start_index;
		for (char *row = curr_frame_start; row < end; row += real_width) {
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
		tmp = curr_frame_end;
		curr_frame_end = next_frame_end;
		next_frame_end = tmp;
	}
	if (should_print) {
		printMatrix(curr_frame, height, width);
	}
	double time = MPI_Wtime() - start_time;
	if (id == 0) {
		printf("%lf", time);
	}
	MPI_Finalize();
	return 0;
}
