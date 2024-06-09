#define WINDOWS

#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#ifdef WIN32

#include <Windows.h>
#include <conio.h> // _kbhit

#else

#include <stdlib.h>
#include <unistd.h> 
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

#endif


// Registers
enum {
	R_R0 = 0,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC, // program counter
	R_COND,
	R_COUNT
};

enum {
	MR_KBSR = 0xFE00,	// keyboard status
	MR_KBDR = 0xFE02	// keyboard data	
};

// TRAP codes
enum {
	TRAP_GETC 	= 0x20,
	TRAP_OUT 	= 0x21,
	TRAP_PUTS 	= 0x22,
	TRAP_IN 	= 0x23,
	TRAP_PUTSP 	= 0x24,
	TRAP_HALT 	= 0x25
};

// Condition flags
enum {
	FL_POS = 1 << 0,
	FL_ZRO = 1 << 1,
	FL_NEG = 1 << 2
};

// Opcodes
enum {
	OP_BR = 0, /* branch */
	OP_ADD,	   /* add  */
	OP_LD,	   /* load */
	OP_ST,	   /* store */
	OP_JSR,	   /* jump register */
	OP_AND,	   /* bitwise and */
	OP_LDR,	   /* load register */
	OP_STR,	   /* store register */
	OP_RTI,	   /* unused */
	OP_NOT,	   /* bitwise not */
	OP_LDI,	   /* load indirect */
	OP_STI,	   /* store indirect */
	OP_JMP,	   /* jump */
	OP_RES,	   /* reserved (unused) */
	OP_LEA,	   /* load effective address */
	OP_TRAP	   /* execute trap */
};


// Memory storage
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // 65536 locations

// Register storage
uint16_t reg[R_COUNT];

uint16_t sign_extend(uint16_t x, int bit_count);
uint16_t swap16(uint16_t x);
void update_flags(uint16_t r);
int read_image_file(FILE* file);
int read_image(const char* image_path);
void mem_write(uint16_t address, uint16_t val);
uint16_t mem_read(uint16_t address);

#ifdef WIN32

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering() {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(hStdin, &fdwOldMode);
	fdwMode = fdwOldMode ^ ENABLE_ECHO_INPUT ^ ENABLE_LINE_INPUT;
	SetConsoleMode(hStdin, fdwMode);
	FlushConsoleInputBuffer(hStdin);
}

void restore_input_buffering() {
	SetConsoleMode(hStdin, fdwOdlMode);
}

uint16_t check_key() {
	return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

#else
// Input buffering unix

struct termios original_tio;
void disable_input_buffering() {
	tcgetattr(STDIN_FILENO, &original_tio);
	struct termios new_tio = original_tio;
	new_tio.c_lflag &= ~ICANON & ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
	tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key() {
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

#endif

// Handle interrupt
void handle_interrupt(int signal) {
	restore_input_buffering();
	printf("\n");
	exit(-2); 
}


int main(int argc, const char *argv[]) {
	// Handle user input
	if (argc < 2) {
		printf("vm [image-file1] ...\n");
		exit(2);
	}

	for (int j = 1; j < argc; ++j) {
		if (!read_image(argv[j])) {
			printf("Failed to load image: %s\n", argv[j]);
			exit(1);
		}
	}

	signal(SIGINT, handle_interrupt);
	disable_input_buffering();

	reg[R_PC] = FL_ZRO;
	
	enum { PC_START = 0x3000 };
	reg[R_PC] = PC_START;

	int running = 1;
	while (running) {
		// Fetch
		uint16_t instr = mem_read(reg[R_PC]++);
		uint16_t op = instr >> 12;

		switch (op) {
		case OP_ADD:
			// destination register (DR)
			uint16_t r0_add = (instr >> 9) & 0x7;
			// first operand (SR1)
			uint16_t r1_add = (instr >> 6) & 0x7;
			// whether we are in immediate mode
			uint16_t imm_flag = (instr >> 5) & 0x1;

			if (imm_flag) {
				uint16_t imm5 = sign_extend(instr & 0x1F, 5);
				reg[r0_add] = reg[r1_add] + imm5;
			} else {
				uint16_t r2 = instr & 0x7;
				reg[r0_add] = reg[r1_add] + reg[r2];
			}

			update_flags(r0);

			break;
		case OP_AND:
			// destination register (DR)
			uint16_t r0 = (instr >> 9) & 0x7;
			// first operand (SR1)
			uint16_t r1 = (instr >> 6) & 0x7;
			uint16_t and_flag = (instr >> 5) & 0x1;

			if (and_flag) {
				uint16_t imm5 = sign_extend(instr & 0x1F, 5);
				reg[r0] = reg[r1] & imm5;
			} else {
				uint16_t r2 = instr & 0x7;
				reg[r0] = reg[r1] & reg[r2];
			}
			update_flags(r0);

			break;
		case OP_NOT:
			// (DR)
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t r1 = (instr >> 6) & 0x7;
			reg[r0] = ~reg[r1];
			update_flags(r0);

			break;
		case OP_BR:
			uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
			uint16_t cond_flag = (instr >> 9) & 0x7;
			if (cond_flag & reg[R_COND]) 
				reg[R_PC] += pc_offset;
			break;
		case OP_JMP:
			uint16_t base_r = (instr >> 6) & 0x7;
			reg[R_PC] = reg[base_r];
			break;
		case OP_JSR:
			uint16_t long_flag = (instr >> 11) & 1;

			reg[R_R7] = reg[R_PC];
			if (long_flag) {
				uint16_t long_offset = sign_extend(instr & 0x7FF, 11);
				reg[R_PC] += long_offset;
			} else {
				uint16_t base_r = (instr >> 6) & 0x7;
				reg[R_PC] = reg[base_r];
			}

			break;
		case OP_LD:
			// DR
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t ld_offset = sign_extend(instr & 0x1FF, 9);
			reg[r0] += mem_read(reg[R_PC] + ld_offset);
			update_flags(r0);

			break;
		case OP_LDI:
			// Destination register (DR)
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t ldi_offset = sign_extend(instr & 0x1FF, 9);
			reg[r0] = mem_read(mem_read(reg[R_PC] + ldi_offset));
			update_flags(r0);

			break;
		case OP_LDR:
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t base_ldr = (instr >> 6) & 0x7;
			uint16_t ldr_offset = sign_extend(instr & 0x3F, 6);
			reg[r0] = mem_read(reg[base_ldr] + ldr_offset);
			update_flags(r0);

			break;
		case OP_LEA:
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t lea_offset = sign_extend(instr & 0x1FF, 9);
			reg[r0] = reg[R_PC] + lea_offset;
			update_flags(r0);

			break;
		case OP_ST:
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t st_offset = sign_extend(instr & 0x1FF, 9);
			mem_write(reg[R_PC] + st_offset, reg[r0]); 
			break;
		case OP_STI:
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t sti_offset = sign_extend(instr & 0x1FF, 9);
			mem_write(mem_read(reg[R_PC] + sti_offset), reg[r0]);
			break;
		case OP_STR:
			uint16_t r0 = (instr >> 9) & 0x7;
			uint16_t base_str = (instr >> 6) & 0x7;
			uint16_t str_offset = sign_extend(instr & 0x3F, 6);
			mem_write(reg[base_str] + str_offset, reg[r0]);
			break;
		case OP_TRAP:
			reg[R_R7] = reg[R_PC];

			switch (instr & 0xFF) {
			case TRAP_GETC:
				reg[R_R0] = (uint16_t)getchar();
				update_flags(R_R0);
				break;
			case TRAP_OUT:
				putc((char)reg[R_R0], stdout);
				fflush(stdout);
				break;
			case TRAP_PUTS:
				uint16_t *c = memory + reg[R_R0];
				while (*c) {
					putc((char)*c, stdout);
					++c;
				}
				fflush(stdout);
				break;
			case TRAP_IN:
				printf("Enter a character: ");
				char ch = getchar();
				putc(c, stdout);
				fflush(stdout);
				reg[R_R0] = (uint16_t)c;
				update_flags(R_R0);
				break;
			case TRAP_PUTSP:
				uint16_t *ch_t = memory + reg[R_R0];
				while (*ch_t) {
					char char1 = (*ch_t) & 0xFF;
					putc(char1, stdout);
					char char2 = (*ch_t) >> 8;
					if (char2) putc(char2, stdout);
					++c;
				}
				fflush(stdout);
				break;
			case TRAP_HALT:
				puts("HALT");
				fflush(stdout);
				running = 0;
				break;
			}

			break;
		case OP_RES:
		case OP_RTI:
		default:
			abort();
			break;
		}
	}
	restore_input_buffering();
}

// Sign Extend
uint16_t sign_extend(uint16_t x, int bit_count) {
	if ((x >> (bit_count - 1)) & 1) {
		x |= (0xFFFF << bit_count);
	}

	return x;
}

uint16_t swap16(uint16_t x) {
	return (x << 8) | (x >> 8);
}


void update_flags(uint16_t r) {
	if (reg[r] == 0) {
		reg[R_COND] = FL_ZRO;
	} else if (reg[r] >> 15) {
		reg[R_COND] = FL_NEG;
	} else {
		reg[R_COND] = FL_POS;
	}
}

// Read Image File
int read_image_file(FILE* file) {
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin);

	uint16_t max_read = MEMORY_MAX - origin;
	uint16_t *p = memory + origin;
	size_t read = fread(p, sizeof(uint16_t), max_read, file);

	while (read-- > 0) {
		*p = swap16(*p);
		++p;
	}
}

// Read Image
int read_image(const char* image_path) {
	FILE* file = fopen(image_path, "rb");
	if (!file) return 0;
	read_image_file(image_path);
	fclose(file);
	return 1;
}

void mem_write(uint16_t address, uint16_t val) {
	memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
	if (address == MR_KBSR) {
		if (check_key()) {
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		} else {
			memory[MR_KBSR] = 0;
		}
	}

	return memory[address];
}