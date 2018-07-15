#ifndef SPATIAL_POOLER_H
#define SPATIAL_POOLER_H

#include <stdlib.h>
#include <stdio.h>
#include "struct_utils.h"
#include "sdr_utils.h"
#include "cortex.h"

//based on the following sources:
//http://numenta.com/biological-and-machine-intelligence/
//http://numenta.com/assets/pdf/whitepapers/hierarchical-temporal-memory-cortical-learning-algorithm-0.2.1-en.pdf
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-Spatial-Pooler.pdf

//initializes a column, requires the length of the input SDRs to initialize the input connections

void spatial_init_column(Column* column, int input_len) {
	column->boost = 1;
	column->center_index = rand() % input_len; //pick a random index as the column's center
	column->average_active = 0;
	column->average_overlap = 0;
	char* used = malloc(input_len * sizeof(char)); //array for remembering to which indices an input connection has already been created
	int a;
	for (a = 0; a < input_len; a++) {
		used[a] = 0;
	}
	int count = input_len; //number of available distinct indices
	int rest = INPUT_COUNT; //number of input connections still to be created
	for (a = 0; a < INPUT_COUNT; a++) {
		Input* input = &(column->inputs[a]);
		int i = -1;
		while (count > 0 && rest > 0) { //do as long as new input connections to be created are left AND there are still unused indices left
			i = rand() % input_len; //pick a random index
			if (!used[i]) {
				used[i] = 1;
				count--;
				rest--;
				break;
			}
		}
		input->bit_index = i != -1 ? i : rand() % input_len; //if no unused index was found use a random index instead
		//following code calculates the initial permanence for the input connection
		int sign = rand() % 2 == 0 ? 1 : -1;
		double dif = sign * (rand() % 100 + 1) / 1000.0;
		int distance = column->center_index - input->bit_index;
		distance = distance >= 0 ? distance : -distance;
		double bias = 0.1 - 0.1 * distance / ((double) input_len);
		input->perm = INPUT_PERMANENCE_THRESHOLD - 0.15 + dif + bias;
		input->perm = input->perm < 0.0 ? 0.0 : input->perm;
		input->perm = input->perm > 1.0 ? 1.0 : input->perm;
	}
	free(used);
}

//initializes the region

void spatial_init_region(Region* region, int input_len) {
	region->cycle = 0;
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		spatial_init_column(column, input_len);
	}
}

//finds the max average activation rate of all columns

double spatial_max_activity(Region* region) {
	double max = 0;
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		if (column->average_active > max) {
			max = column->average_active;
		}
	}
	return max;
}

//computes the average activation rate for a column, uses a moving average approximation

void spatial_column_averages(Column* column, int window) {
	column->average_active -= column->average_active / window;
	if (column->active) {
		column->average_active += 1.0 / window;
	}
	column->average_overlap -= column->average_overlap / window;
	if (column->overlap > 0) {
		column->average_overlap += 1.0 / window;
	}
}

//computes the average activation rate for each column and sets the max average activation rate

void spatial_region_averages(Region* region) {
	int a;
	int window = region->cycle <= COLUMN_AVERAGE_WINDOW ? region->cycle : COLUMN_AVERAGE_WINDOW;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		spatial_column_averages(column, window);
	}
	region->average_max = spatial_max_activity(region);
}

//boosts a column if necessary

void spatial_boost_column(Column* column, double max) {
	if (column->average_active < 0.01 * max) { //if boosting is required
		column->boost = column->boost < COLUMN_MAX_BOOST ? column->boost + 1 : COLUMN_MAX_BOOST; //increment boost value if maximum value not yet reached
	} else {
		column->boost = 1; //reset boost value to 1
	}
	if (column->average_overlap < 0.01 * max) { //if column's overlap is too low
		int b;
		for (b = 0; b < INPUT_COUNT; b++) { //increase permanences of all input connections
			Input* input = &(column->inputs[b]);
			input->perm += 0.1 * INPUT_PERMANENCE_THRESHOLD;
			input->perm = input->perm > 1.0 ? 1.0 : input->perm;
		}
	}
}

//boosts the columns between index "from" and "to"

void spatial_boost_region(Region* region, int from, int to) {
	int a;
	if (region->cycle >= COLUMN_START_BOOST) {
		double max = region->average_max;
		for (a = from; a <= to; a++) {
			Column* column = &(region->columns[a]);
			spatial_boost_column(column, max);
		}
	}
}

//resets the winning columns

void spatial_reset_region(Region* region) {
	free_list(region->active_columns);
	region->active_columns = NULL;
}

//reinforces a column

void spatial_reinforce_column(Column* column) {
	int a;
	for (a = 0; a < INPUT_COUNT; a++) {
		Input* input = &(column->inputs[a]);
		if (input->active) { //if input connection is active: positive reinforcement
			input->perm += INPUT_PERMANENCE_INC;
			input->perm = input->perm > 1.0 ? 1.0 : input->perm;
		} else { //otherwise negative reinforcement
			input->perm -= INPUT_PERMANENCE_DEC;
			input->perm = input->perm < 0.0 ? 0.0 : input->perm;
		}
	}
}

//reinforces the winning columns

void spatial_reinforce_region(Region* region) {
	List* active_columns = region->active_columns;
	while (active_columns != NULL) {
		Column* column = &(region->columns[active_columns->elem]);
		spatial_reinforce_column(column);
		active_columns = active_columns->next;
	}
}

//finds the overlap threshold a column must exceed to win

int spatial_activation_threshold(Region* region) {
	int* overlaps = malloc(COLUMN_COUNT * sizeof(int)); //array with overlap values of all columns
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) { //fill array with overlap values
		Column* column = &(region->columns[a]);
		overlaps[a] = column->overlap;
	}
	qsort(overlaps, COLUMN_COUNT, sizeof(int), comp_ints); //sort array by overlap value
	int val = overlaps[COLUMN_COUNT - 1]; //highest overlap value
	int i = 1; //i-th highest overlap value
	for (a = COLUMN_COUNT - 2; a > 0 && i < REGION_ACTIVE_COLUMNS; a--) { //find the i-th highest overlap value required by REGION_ACTIVE_COLUMNS
		if (overlaps[a] != val) {
			val = overlaps[a];
			i++;
		}
	}
	free(overlaps);
	return val;
}

//activates the winning columns

void spatial_activate_region(Region* region) {
	int val = spatial_activation_threshold(region); //overlap threshold to be reached for column activation
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) { //find the winning columns
		Column* column = &(region->columns[a]);
		column->active = column->overlap > 0 && column->overlap >= val ? 1 : 0;
		if (column->active) {
			region->active_columns = add_elem(a, region->active_columns);
		}
	}
}

//computes the overlap of the column and the input SDR

void spatial_column_overlap(Column* column, SDR* sdr) {
	int overlap = 0;
	int a;
	for (a = 0; a < INPUT_COUNT; a++) {
		Input* input = &(column->inputs[a]);
		if (INPUT_PERMANENCE_CHECK) { //if permanence check is enabled
			if (input->perm >= INPUT_PERMANENCE_THRESHOLD && is_set(sdr, input->bit_index)) { //if permanence threshold is reached and SDR bit is set
				input->active = 1;
			} else {
				input->active = 0;
			}
		} else {
			if (is_set(sdr, input->bit_index)) { //if SDR bit is set
				input->active = 1;
			} else {
				input->active = 0;
			}
		}
		overlap += input->active;
	}
	column->overlap = overlap >= COLUMN_STIMULUS_THRESHOLD ? overlap * column->boost : 0; //if overlap * boost does not reach stimulus threshold, set overlap to 0
}

//gives the columns between index "from" and "to" the input SDR

void spatial_give_input(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		spatial_column_overlap(column, region->sdr);
	}
}

#endif // SPATIAL_POOLER_H
