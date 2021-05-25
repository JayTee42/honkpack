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

//Get stdin, opened in binary mode:
static FILE* get_stdin_binary(void);

//Get stdout, opened in binary mode:
static FILE* get_stdout_binary(void);

//Write a single byte to the output:
static void write_byte(FILE* output, uint8_t new_byte);

//Write a status byte to the output:
static void write_status_byte(FILE* output, bool is_rle, size_t bytes_count);

//Write a RLE run (status byte + content byte):
static void write_rle_run(FILE* output, uint8_t byte, size_t count);

//Write a block (status byte + block bytes):
static void write_block(FILE* output, const uint8_t* block, size_t count);

static FILE* get_stdin_binary(void)
{
	//For our dearest Windows users ... binary != text for you!
#ifdef WIN32
	_setmode(STDIN_FILENO, _O_BINARY);
#endif
	return stdin;
}

static FILE* get_stdout_binary(void)
{
	//See get_stdin_binary() ...
#ifdef WIN32
	_setmode(STDOUT_FILENO, _O_BINARY);
#endif
	return stdout;
}

static void write_byte(FILE* output, uint8_t new_byte)
{
	if (fputc((int)new_byte, output) == EOF)
	{
		fprintf(stderr, "Error while writing to output file descriptor.\n");
		exit(EXIT_FAILURE);
	}
}

static void write_status_byte(FILE* output, bool is_rle, size_t bytes_count)
{
	uint8_t status_byte = (uint8_t)bytes_count;

	if (is_rle)
	{
		status_byte |= (1 << 7);
	}

	write_byte(output, status_byte);
}

static void write_rle_run(FILE* output, uint8_t byte, size_t count)
{
	//Write the status byte:
	write_status_byte(output, true, count);

	//Write the RLE content once:
	write_byte(output, byte);
}

static void write_block(FILE* output, const uint8_t* block, size_t count)
{
	//Write the status byte:
	write_status_byte(output, false, count);

	//Flush the block via fwrite():
	if (fwrite(block, 1, count, output) != count)
	{
		fprintf(stderr, "Error while writing to output file descriptor.\n");
		exit(EXIT_FAILURE);
	}
}

static void honk_compress(FILE* input, FILE* output)
{
	//Start in the (empty) block state:
	honk_compress_state_t state = HONK_COMPRESS_STATE_BLOCK;

	size_t count = 0;
	uint8_t last_byte = 0;
	uint8_t block[MAX_BLOCK_SIZE];

	//Read the input file block-wise and process each byte:
	uint8_t buf[BUF_SIZE];
	size_t bytes_count;

	while ((bytes_count = fread(buf, 1, BUF_SIZE, input)) > 0)
	{
		//Process the new bytes:
		for (size_t i = 0; i < bytes_count; i++)
		{
			uint8_t new_byte = buf[i];

			switch (state)
			{
			case HONK_COMPRESS_STATE_RLE:

				//If we see another byte, the RLE must be closed and we move to the block state:
				if (new_byte != last_byte)
				{
					//Write run:
					write_rle_run(output, last_byte, count);

					//Change state:
					last_byte = new_byte;
					block[0] = new_byte;
					count = 1;
					state = HONK_COMPRESS_STATE_BLOCK;
				}
				else
				{
					//Increment the number of bytes.
					//Is the RLE full?
					if (++count == MAX_BLOCK_SIZE)
					{
						//Write run:
						write_rle_run(output, last_byte, MAX_BLOCK_SIZE);

						//Move to the (empty) block state:
						count = 0;
						state = HONK_COMPRESS_STATE_BLOCK;
					}
				}

				break;

			case HONK_COMPRESS_STATE_BLOCK:

				//If we see the same byte twice, the block must be closed and we move to RLE:
				if ((count > 0) && (new_byte == last_byte))
				{
					//The last byte is *not* part of the block:
					size_t actual_bytes_count = count - 1;

					//Write block:
					if (actual_bytes_count > 0)
					{
						write_block(output, block, actual_bytes_count);
					}

					//Change state:
					count = 2;
					state = HONK_COMPRESS_STATE_RLE;
				}
				else
				{
					//Add the new byte to the block and increment the number of bytes:
					block[count] = new_byte;

					//Is the block full?
					if (++count == MAX_BLOCK_SIZE)
					{
						//Write block:
						write_block(output, block, MAX_BLOCK_SIZE);

						//Stay in the (empty) block state:
						count = 0;
					}
					else
					{
						//Remember the new byte:
						last_byte = new_byte;
					}
				}

				break;
			}
		}
	}

	//Write the last block if necessary:
	switch (state)
	{
	case HONK_COMPRESS_STATE_RLE:

		//Write run:
		write_rle_run(output, last_byte, count);
		break;

	case HONK_COMPRESS_STATE_BLOCK:

		//Write block:
		if (count > 0)
		{
			write_block(output, block, count);
		}

		break;
	}
}

static void honk_decompress(FILE* input, FILE* output)
{
	//Start in the status state:
	honk_decompress_state_t state = HONK_DECOMPRESS_STATE_STATUS;
	size_t count = 0;

	//Read the input file block-wise and process each byte:
	uint8_t buf[BUF_SIZE];
	size_t bytes_count;

	while ((bytes_count = fread(buf, 1, BUF_SIZE, input)) > 0)
	{
		//Process the new bytes:
		for (size_t i = 0; i < bytes_count; i++)
		{
			uint8_t new_byte = buf[i];

			switch (state)
			{
			case HONK_DECOMPRESS_STATE_STATUS:

				//Read the block count:
				count = (size_t)(new_byte & 0x7F);

				//RLE or block?
				if (new_byte & (1 << 7))
				{
					//If the length of the RLE would be 0, we read one byte that will be repeated 0 times (quite pointless).
					state = HONK_DECOMPRESS_STATE_RLE;
				}
				else
				{
					//If the length of the block would be 0, we stay in HONK_DECOMPRESS_STATE_STATUS.
					if (count > 0)
					{
						state = HONK_DECOMPRESS_STATE_BLOCK;
					}
				}

				break;

			case HONK_DECOMPRESS_STATE_RLE:

				//Write n instances of our byte and move back to status state:
				for (size_t i = 0; i < count; i++)
				{
					write_byte(output, new_byte);
				}

				state = HONK_DECOMPRESS_STATE_STATUS;

				break;

			case HONK_DECOMPRESS_STATE_BLOCK:

				//Write the new byte and reduce the count.
				//If all bytes of the block are read / written, we move back to status state.
				write_byte(output, new_byte);

				if (--count == 0)
				{
					state = HONK_DECOMPRESS_STATE_STATUS;
				}

				break;
			}
		}
	}

	//Validate the state:
	if (state != HONK_DECOMPRESS_STATE_STATUS)
	{
		fprintf(stderr, "Error while decompressing: Bad format\n");
		exit(EXIT_FAILURE);
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

	//Compress / Decompress:
	if (is_compress_mode)
	{
		honk_compress(input, output);
	}
	else
	{
		honk_decompress(input, output);
	}

	//Did we leave the loop because of a read error?
	if (ferror(input))
	{
		fprintf(stderr, "Error while reading from input file descriptor.\n");
		exit(EXIT_FAILURE);
	}

	//Close the streams:
	fclose(input);
	fclose(output);

	return 0;
}
