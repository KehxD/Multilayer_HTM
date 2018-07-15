#ifndef SDR_UTILS_H
#define SDR_UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include "struct_utils.h"

//based on the following sources:
//http://numenta.com/biological-and-machine-intelligence/
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-SDR.pdf
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-Encoders.pdf

int SDR_BASE; //amount of different values that can be represented
int SDR_SET; //amount of set bits

typedef struct SDR {
	int len;
	char* bits;
} SDR;

//deallocates the input SDR

void free_sdr(SDR* sdr) {
	free(sdr->bits);
	free(sdr);
}

//returns a new SDR representing the input integer, integer must be >= 0 and < SDR_BASE

SDR* int_to_sdr(int i) {
	if (i < 0 || i >= SDR_BASE) {
		printf("INVALID INTEGER: %d\n", i);
		return NULL;
	}
	SDR* sdr = malloc(sizeof(SDR));
	sdr->len = SDR_BASE + SDR_SET;
	sdr->bits = malloc(sdr->len * sizeof(char));
	int a;
	for (a = 0; a < sdr->len; a++) {
		sdr->bits[a] = a < i || a >= i + SDR_SET ? 0 : 1; //sets bits at indices between i and i + SDR_SET to 1, rest to 0
	}
	return sdr;
}

SDR* bits_to_sdr(char* bits, int len) {
	SDR* sdr = malloc(sizeof(SDR));
	sdr->len = len;
	sdr->bits = bits;
	return sdr;
}

void print_sdr(SDR* sdr) {
	int a;
	for (a = 0; a < sdr->len; a++) {
		printf("%d", sdr->bits[a]);
	}
	printf("\n");
}

//returns 1 if bit at index is set, otherwise 0, index must be >= 0 and < SDR_BASE + SDR_SET

char is_set(SDR* sdr, int index) {
	if (index < 0 || index >= SDR_BASE + SDR_SET) {
		printf("INVALID INDEX: %d\n", index);
		fflush(stdout);
		return 0;
	}
	return sdr->bits[index];
}

//returns the overlap of both input SDRs

int sdr_overlap(SDR* sdr1, SDR* sdr2) {
	int overlap = 0;
	int a;
	for (a = 0; a < sdr1->len && a < sdr2->len; a++) {
		if (sdr1->bits[a] == 1 && sdr2->bits[a] == 1) {
			overlap++;
		}
	}
	return overlap;
}

//returns a new SDR that is the union of input SDRs

SDR* sdr_union(SDR* sdr1, SDR* sdr2) {
	SDR* sdr = malloc(sizeof(SDR));
	sdr->len = sdr1->len >= sdr2->len ? sdr1->len : sdr2->len;
	sdr->bits = malloc(sdr->len * sizeof(char));
	int a;
	for (a = 0; a < sdr->len; a++) {
		sdr->bits[a] = sdr1->bits[a] == 1 || sdr2->bits[a] == 1 ? 1 : 0;
	}
	return sdr;
}
#endif // SDR_UTILS_H
