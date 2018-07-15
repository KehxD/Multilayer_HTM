#ifndef PROCESS_COMMUNICATION_H_
#define PROCESS_COMMUNICATION_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "struct_utils.h"
#include "cortex.h"

char* lowerRegionDone;

// support function to reverse String
void strreverse(char* begin, char* end) {
	char aux;
	while (end > begin)
		aux = *end, *end-- = *begin, *begin++ = aux;
}

//support function to turn integer to String array
void itoa(int value, char* str, int base) {
	static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	char* wstr = str;
	int sign;

	// Validate base
	if (base < 2 || base > 35) {
		*wstr = '\0';
		return;
	}

	// Take care of sign	
	if ((sign = value) < 0)
		value = -value;

	// Conversion. Number is reversed.
	do
		*wstr++ = num[value % base];
	while (value /= base);
	if (sign < 0)
		*wstr++ = '-';
	*wstr = '\0';

	// Reverse string
	strreverse(str, wstr - 1);
}

/* Makes and returns a graph representation of all regions (Adjacency Matrix)
 This is required for functions involving the named pipes (setup and destruct)*/
int** set_multilayer_hierarchy(int number_regions) {
	// Allocate Space. This could be changed to calloc.
	int** hierarchy = malloc(sizeof(int*) * number_regions);
	for (int i = 0; i < number_regions; i++) {
		hierarchy[i] = malloc(sizeof(int*) * number_regions);
	}
	for (int i = 0; i < number_regions; i++) {
		for (int j = 0; j < number_regions; j++) {
			hierarchy[i][j] = 0;
		}
	}

	// File containing the region edges
	FILE* config;
	if ((config = fopen("./config/region_hierarchy", "r+")) == NULL) {
		printf("No such file \"region_hierarchy\"");
		exit(1);
	}
	int edge[2];

	// read individual edges
	char dummy[256];
	fscanf(config, "%s", dummy);
	while (fscanf(config, "%i %i", edge, edge + 1) > 0) {
		hierarchy[edge[0]][edge[1]] = 1;
	}

	fclose(config);

	return hierarchy;
}

// Destruct function for the above matrix
void destroy_hierarchy_matrix(int** hierarchy, int number_regions) {
	for (int i = 0; i < number_regions; i++) {
		free(hierarchy[i]);
	}
	free(hierarchy);
}

// Opens the write side of the pipea. Invoked by every lower level region.
List* open_write_pipes(int** hierarchy, int number_regions, int region_id) {
	List* write_pipes = NULL;
	for (int i = 0; i < number_regions; i++) {
		if (hierarchy[region_id][i]) {
			char* buffer1 = malloc(10 * sizeof(char));
			char* buffer2 = malloc(10 * sizeof(char));
			itoa(i, buffer1, 10);
			itoa(region_id, buffer2, 10);
			char* pipe_name = malloc(sizeof("/tmp/region_id_") + sizeof(buffer1) + sizeof(char) + sizeof(buffer2));
			char* prename = "/tmp/region_id_";
			pipe_name[0] = '\0';
			strcat(pipe_name, prename);
			strcat(pipe_name, buffer2);
			strcat(pipe_name, "_");
			strcat(pipe_name, buffer1);

			mkfifo(pipe_name, 0666);

			printf("Pipe linked: %s\n", pipe_name);
			int pipe = open(pipe_name, O_WRONLY);
			printf("Write pipe opened: %s\n", pipe_name);

			write_pipes = add_elem(pipe, write_pipes);
			free(buffer1);
			free(buffer2);
		}
	}
	return write_pipes;
}

// Open read end of the pipes. Invoked by the higher level regions
List* open_read_pipes(int** hierarchy, int number_regions, int region_id) {
	List* read_pipes = NULL;
	for (int i = 0; i < number_regions; i++) {
		if (hierarchy[i][region_id]) {
			char* buffer1 = malloc(10 * sizeof(char));
			char* buffer2 = malloc(10 * sizeof(char));
			itoa(i, buffer1, 10);
			itoa(region_id, buffer2, 10);
			char* pipe_name = malloc(sizeof("/tmp/region_id_") + sizeof(buffer1) + sizeof(char) + sizeof(buffer2));
			char* prename = "/tmp/region_id_";
			pipe_name[0] = '\0';
			strcat(pipe_name, prename);
			strcat(pipe_name, buffer1);
			strcat(pipe_name, "_");
			strcat(pipe_name, buffer2);

			mkfifo(pipe_name, 0666);

			int pipe = open(pipe_name, O_RDONLY);
			printf("Read pipe opened: %s\n", pipe_name);
			read_pipes = add_elem(pipe, read_pipes);

			free(buffer1);
			free(buffer2);
		}
	}

	if (read_pipes) {
		lowerRegionDone = calloc(read_pipes->len, sizeof(char));
	}
	return read_pipes;
}

// closes read end of the pipes
void close_read_pipes(List* pipe_list) {
	for (; pipe_list; pipe_list = pipe_list->next) {
		printf("Read pipe closed\n");
		close((int) (pipe_list->elem));
	}
	free_list(pipe_list);
}

// closes write end of the pipes and unlinks both read and write
void close_write_pipes(List* pipe_list, int** hierarchy, int number_regions, int region_id) {
	for (; pipe_list; pipe_list = pipe_list->next) {
		printf("Write pipe closed\n");
		close((int) (pipe_list->elem));
	}
	for (int i = 0; i < number_regions; i++) {
		if (hierarchy[region_id][i]) {
			char* buffer1 = malloc(10 * sizeof(char));
			char* buffer2 = malloc(10 * sizeof(char));
			itoa(i, buffer1, 10);
			itoa(region_id, buffer2, 10);
			char* pipe_name = malloc(sizeof("/tmp/region_id_") + sizeof(buffer1) + sizeof(char) + sizeof(buffer2));
			char* prename = "/tmp/region_id_";
			pipe_name[0] = '\0';
			strcat(pipe_name, prename);
			strcat(pipe_name, buffer2);
			strcat(pipe_name, "_");
			strcat(pipe_name, buffer1);

			unlink(pipe_name);
			printf("Pipe unlinked: %s\n", pipe_name);
			free(buffer1);
			free(buffer2);
		}
	}

	free_list(pipe_list);
}

/* Invoked by lower level regions. Writes the cycle and predictive states of the lower region into the pipe */
void write_output_to_pipes(Region* region, List* write_pipes) {
	char* region_sdr = malloc(sizeof(char) * CELL_COUNT * COLUMN_COUNT);
	for (int i = 0; i < COLUMN_COUNT; i++) {
		for (int j = 0; j < CELL_COUNT; j++) {
			region_sdr[i * CELL_COUNT + j] = (region->columns)[i].cells[j].prev_predictive;
		}
	}

	for (List* pipes = write_pipes; pipes; pipes = pipes->next) {
		write(pipes->elem, &region->cycle, sizeof(int));
		write(pipes->elem, region_sdr, sizeof(char) * CELL_COUNT * COLUMN_COUNT);
	}
	free(region_sdr);
}

// reads the input from all incoming pipes and concats them to an SDR
SDR* read_input_from_pipes(List* read_pipes, int** crs) { //crs = connected_region_sizes
	int len = read_pipes->len;
	int full_size = 0;
	int max_size = 0;
	int cycle;
	for (int i = 0; i < len; i++) {
		full_size += crs[i][0] * crs[i][1]; // Size for concat
		max_size = max_size < crs[i][0] * crs[i][1] ? crs[i][0] * crs[i][1] : max_size; // Size for union
	}

	char* region_bits = malloc(sizeof(char) * full_size);
	int current_position = 0;
	int i = 0;
	char flag = 1;
	for (int i = 0; i < len; i++) {
		if (!lowerRegionDone[i]) {
			// For multihierarchical setup, if 1 lower region is done, end termination. Indicated by transmitting cycle number -1 from lower region
			flag = 0;
		}
	}
	if (flag) {
		return NULL;
		free(region_bits);
	}

	//reads cycle and predictive states from read pipes and writes them to array -> then converted to SDR
	for (List* pipes = read_pipes; pipes; pipes = pipes->next) {
		if (!lowerRegionDone[i]) {
			read(pipes->elem, &cycle, sizeof(int));
			printf("region: %i            cycle: %i\n", i, cycle);
			read(pipes->elem, region_bits + sizeof(char) * current_position, sizeof(char) * crs[i][0] * crs[i][1]);
			if (*(region_bits + sizeof(char) + current_position) == -1) {
				lowerRegionDone[i] = 1;
				for (int j = 0; j < crs[i][0] * crs[i][1]; j++) {
					*(region_bits + current_position + j * sizeof(char)) = 0;
				}
			}
		}
		current_position += crs[i][0] * crs[i][1];
		i++;
	}
	SDR* region_sdr = bits_to_sdr(region_bits, sizeof(char) * full_size);
	return region_sdr;
}

// writes a signal of the finished region upwards (-1) and thus stops termination of higher region.
void write_end_signal(List* write_pipes) {
	char* region_sdr = malloc(sizeof(char) * CELL_COUNT * COLUMN_COUNT);
	for (int i = 0; i < COLUMN_COUNT; i++) {
		for (int j = 0; j < CELL_COUNT; j++) {
			region_sdr[i * CELL_COUNT + j] = -1;
		}
	}
	int cycle = -1;
	for (List* pipes = write_pipes; pipes; pipes = pipes->next) {
		write(pipes->elem, &cycle, sizeof(int));
		write(pipes->elem, region_sdr, sizeof(char) * CELL_COUNT * COLUMN_COUNT);
	}
	free(region_sdr);
	printf("Wrote end signals\n");
}
#endif

