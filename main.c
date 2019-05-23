#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCK_SIZE ((size_t)127)
#define BUF_SIZE 4096

typedef enum __honk_compress_state_t__
{
	HONK_COMPRESS_STATE_RLE,
	HONK_COMPRESS_STATE_BLOCK
} honk_compress_state_t;

typedef enum __honk_decompress_state_t__
{
	HONK_DECOMPRESS_STATE_STATUS,
	HONK_DECOMPRESS_STATE_RLE,
	HONK_DECOMPRESS_STATE_BLOCK
} honk_decompress_state_t;

typedef union __honk_state_t__
{
	//Compression:
	struct
	{
		//The compression state:
		honk_compress_state_t compress_state;

		//The number of bytes in the current RLE run / block:
		size_t compress_bytes_count;

		//The last byte we have seen:
		uint8_t last_byte;

		//The current block:
		uint8_t block[MAX_BLOCK_SIZE];
	};

	//Decompression:
	struct
	{
		//The decompression state:
		honk_decompress_state_t decompress_state;

		//The number of bytes in the current RLE run / block:
		size_t decompress_bytes_count;
	};
} honk_state_t;

typedef struct __honk_funcs_t__
{
	void (*init_func)(honk_state_t*);
	void (*process_byte_func)(honk_state_t*, FILE*, uint8_t);
	void (*finalize_func)(honk_state_t*, FILE*);
} honk_funcs_t;

//Get stdin, opened in binary mode:
FILE* get_stdin_binary(void);

//Get stdout, opened in binary mode:
FILE* get_stdout_binary(void);

//Write a single byte to the output:
void write_byte(FILE* output, uint8_t new_byte);

//Write a status byte to the output:
void write_status_byte(FILE* output, bool is_rle, size_t bytes_count);

//Write a RLE run (status byte + content byte):
void write_rle_run(FILE* output, uint8_t byte, size_t count);

//Write a block (status byte + block bytes):
void write_block(FILE* output, const uint8_t* block, size_t count);

//Compression:
void honk_compress_init(honk_state_t* state);
void honk_compress_process_byte(honk_state_t* state, FILE* output, uint8_t new_byte);
void honk_compress_finalize(honk_state_t* state, FILE* output);

//Decompression:
void honk_decompress_init(honk_state_t* state);
void honk_decompress_process_byte(honk_state_t* state, FILE* output, uint8_t new_byte);
void honk_decompress_finalize(honk_state_t* state, FILE* output);

FILE* get_stdin_binary(void)
{
	//For our dearest Windows users ... binary != text for you!
#ifdef WIN32
	_setmode(STDIN_FILENO, _O_BINARY);
#endif
	return stdin;
}

FILE* get_stdout_binary(void)
{
	//See get_stdin_binary() ...
#ifdef WIN32
	_setmode(STDOUT_FILENO, _O_BINARY);
#endif
	return stdout;
}

void write_byte(FILE* output, uint8_t new_byte)
{
	if (fputc((int)new_byte, output) == EOF)
	{
		fprintf(stderr, "Error while writing to output file descriptor.\n");
		exit(EXIT_FAILURE);
	}
}

void write_status_byte(FILE* output, bool is_rle, size_t bytes_count)
{
	uint8_t status_byte = (uint8_t)bytes_count;

	if (is_rle)
	{
		status_byte |= (1 << 7);
	}

	write_byte(output, status_byte);
}

void write_rle_run(FILE* output, uint8_t byte, size_t count)
{
	//Write the status byte:
	write_status_byte(output, true, count);

	//Write the RLE content once:
	write_byte(output, byte);
}

void write_block(FILE* output, const uint8_t* block, size_t count)
{
	//Write the status byte:
	write_status_byte(output, false, count);

	//Flush the block via fwrite():
	if (fwrite(block, 1, count, output) != count)
	{
		fprintf(stderr, "Error while writing to output file descriptor.\n");
	}
}

void honk_compress_init(honk_state_t* state)
{
	//Start in the (empty) block state:
	state->compress_state = HONK_COMPRESS_STATE_BLOCK;
	state->compress_bytes_count = 0;
}

void honk_compress_process_byte(honk_state_t* state, FILE* output, uint8_t new_byte)
{
	//State machine:
	switch (state->compress_state)
	{
	case HONK_COMPRESS_STATE_RLE:

		//If we see another byte, the RLE must be closed and we move to the block state:
		if (new_byte != state->last_byte)
		{
			//Write run:
			write_rle_run(output, state->last_byte, state->compress_bytes_count);

			//Change state:
			state->last_byte = new_byte;
			state->block[0] = new_byte;
			state->compress_bytes_count = 1;
			state->compress_state = HONK_COMPRESS_STATE_BLOCK;
		}
		else
		{
			//Increment the number of bytes.
			//Is the RLE full?
			if (++state->compress_bytes_count == MAX_BLOCK_SIZE)
			{
				//Write run:
				write_rle_run(output, state->last_byte, MAX_BLOCK_SIZE);

				//Move to the (empty) block state:
				state->compress_bytes_count = 0;
				state->compress_state = HONK_COMPRESS_STATE_BLOCK;
			}
		}

		break;

	case HONK_COMPRESS_STATE_BLOCK:

		//If we see the same byte twice, the block must be closed and we move to RLE:
		if ((state->compress_bytes_count > 0) && (new_byte == state->last_byte))
		{
			//The last byte is *not* part of the block:
			size_t actual_bytes_count = state->compress_bytes_count - 1;

			//Write block:
			if (actual_bytes_count > 0)
			{
				write_block(output, state->block, actual_bytes_count);
			}

			//Change state:
			state->compress_bytes_count = 2;
			state->compress_state = HONK_COMPRESS_STATE_RLE;
		}
		else
		{
			//Add the new byte to the block and increment the number of bytes:
			state->block[state->compress_bytes_count] = new_byte;

			//Is the block full?
			if (++state->compress_bytes_count == MAX_BLOCK_SIZE)
			{
				//Write block:
				write_block(output, state->block, MAX_BLOCK_SIZE);

				//Stay in the (empty) block state:
				state->compress_bytes_count = 0;
			}
			else
			{
				//Remember the new byte:
				state->last_byte = new_byte;
			}
		}

		break;
	}
}

void honk_compress_finalize(honk_state_t* state, FILE* output)
{
	//State machine:
	switch (state->compress_state)
	{
	case HONK_COMPRESS_STATE_RLE:

		//Write run:
		write_rle_run(output, state->last_byte, state->compress_bytes_count);

		break;

	case HONK_COMPRESS_STATE_BLOCK:

		//Write block:
		if (state->compress_bytes_count > 0)
		{
			write_block(output, state->block, state->compress_bytes_count);
		}

		break;
	}
}

void honk_decompress_init(honk_state_t* state)
{
	//Start in the status state:
	state->decompress_state = HONK_DECOMPRESS_STATE_STATUS;
}

void honk_decompress_process_byte(honk_state_t* state, FILE* output, uint8_t new_byte)
{
	//State machine:
	switch (state->decompress_state)
	{
	case HONK_DECOMPRESS_STATE_STATUS:

		//Read the block count:
		state->decompress_bytes_count = (size_t)(new_byte & 0x7F);

		//Special case (should not happen):
		if (state->decompress_bytes_count == 0)
		{
			state->decompress_state = HONK_DECOMPRESS_STATE_STATUS;
		}
		else
		{
			//RLE or block?
			if (new_byte & (1 << 7))
			{
				state->decompress_state = HONK_DECOMPRESS_STATE_RLE;
			}
			else
			{
				state->decompress_state = HONK_DECOMPRESS_STATE_BLOCK;
			}
		}

		break;

	case HONK_DECOMPRESS_STATE_RLE:

		//Write n instances of our byte and move back to status state:
		for (size_t i = 0; i < state->decompress_bytes_count; i++)
		{
			write_byte(output, new_byte);
		}

		state->decompress_state = HONK_DECOMPRESS_STATE_STATUS;

		break;

	case HONK_DECOMPRESS_STATE_BLOCK:

		//Write the new byte and reduce the count.
		//If all bytes of the block are read / written, we move back to status state.
		write_byte(output, new_byte);

		if (--state->decompress_bytes_count == 0)
		{
			state->decompress_state = HONK_DECOMPRESS_STATE_STATUS;
		}

		break;
	}
}

void honk_decompress_finalize(honk_state_t* state, FILE* output)
{
	//We do not need the "output" parameter here.
	(void)(output);

	//Validate the state:
	if (state->decompress_state != HONK_DECOMPRESS_STATE_STATUS)
	{
		fprintf(stderr, "Error while decompressing: Bad format\n");
	}
}

int main(int argc, char** argv)
{
	//Compression / Decompression?
	bool is_compress_mode = true;

	//Check parameters:
	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];

		if (strcmp(arg, "-d") == 0)
		{
			is_compress_mode = false;
			break;
		}
	}

	//Get file pointers to stdin and stdout:
	FILE* input = get_stdin_binary();
	FILE* output = get_stdout_binary();

	//Initialize a func struct:
	honk_funcs_t funcs;

	if (is_compress_mode)
	{
		funcs.init_func = honk_compress_init;
		funcs.process_byte_func = honk_compress_process_byte;
		funcs.finalize_func = honk_compress_finalize;
	}
	else
	{
		funcs.init_func = honk_decompress_init;
		funcs.process_byte_func = honk_decompress_process_byte;
		funcs.finalize_func = honk_decompress_finalize;
	}

	//Initialize a state struct:
	honk_state_t state;
	funcs.init_func(&state);

	//Read the input file block-wise and process each byte:
	uint8_t buf[BUF_SIZE];
	size_t bytes_count;

	while ((bytes_count = fread(buf, 1, BUF_SIZE, input)) > 0)
	{
		//Process the new bytes:
		for (size_t i = 0; i < bytes_count; i++)
		{
			funcs.process_byte_func(&state, output, buf[i]);
		}
	}

	//Did we leave the loop because of a read error?
	if (ferror(input))
	{
		fprintf(stderr, "Error while reading from input file descriptor.\n");
		exit(EXIT_FAILURE);
	}

	//Finalize:
	funcs.finalize_func(&state, output);

	//Close the streams:
	fclose(input);
	fclose(output);

	return 0;
}
