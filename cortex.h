#ifndef CORTEX_H
#define CORTEX_H

#include <stdlib.h>
#include <stdio.h>
#include "struct_utils.h"
#include "sdr_utils.h"

//based on the following sources:
//http://numenta.com/biological-and-machine-intelligence/
//http://numenta.com/assets/pdf/whitepapers/hierarchical-temporal-memory-cortical-learning-algorithm-0.2.1-en.pdf
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-Spatial-Pooler.pdf
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-Temporal-Memory.pdf

//HTM parameters
double INPUT_PERMANENCE_THRESHOLD;
double INPUT_PERMANENCE_INC;
double INPUT_PERMANENCE_DEC;
char INPUT_PERMANENCE_CHECK; //check input permanence threshold
int COLUMN_STIMULUS_THRESHOLD;
int COLUMN_MAX_BOOST; //maximum boost value
int COLUMN_START_BOOST; //cycle at which boosting starts
int COLUMN_AVERAGE_WINDOW; //moving average window
int REGION_ACTIVE_COLUMNS; //amount of active columns (not exact)
int CELL_REMAIN_ACTIVE;
int CELL_REMAIN_PREDICTIVE;
int CELL_REMAIN_LEARNING;
char CELL_REMAIN_RANDOM;
int SEGMENT_ACTIVATION_THRESHOLD;
int SEGMENT_LEARNING_THRESHOLD;
int SEGMENT_NEW_CONNECTIONS; //amount of new connections
int CONNECTION_LEARNING_HORIZONTAL; //horizontal search space for new connections
int CONNECTION_LEARNING_VERTICAL; //vertical search space for new connections
double CONNECTION_PERMANENCE_THRESHOLD;
double CONNECTION_INITIAL_PERMANENCE;
double CONNECTION_PERMANENCE_INC;
double CONNECTION_PERMANENCE_DEC;
int FORGET_INTERVAL; //cycle interval between garbage collector calls
double DETECTION_THRESHOLD;
double OVERLAP_THRESHOLD;
char ENABLE_LEARNING;
char LOAD;
char SAVE;

int COLUMN_COUNT;
int INPUT_COUNT;
int CELL_COUNT;

typedef struct Region Region;
typedef struct Column Column;
typedef struct Cell Cell;
typedef struct Segment Segment;
typedef struct Connection Connection;
typedef struct Input Input;
typedef struct Update Update;

typedef struct Region {
	long cycle;
	int bursts;
	double average_max;
	double overlap;
	List* active_columns;
	Column* columns;
	SDR* sdr;
} Region;

typedef struct Column {
	char active;
	int overlap;
	int boost;
	int center_index;
	double average_active;
	double average_overlap;
	Input* inputs;
	Cell* cells;
} Column;

typedef struct Cell {
	char active;
	int remain_active;
	char prev_active;
	char predictive;
	int remain_predictive;
	char prev_predictive;
	char learning;
	int remain_learning;
	char prev_learning;
	int index;
	List* segment_updates;
	List* segments;
} Cell;

typedef struct Segment {
	long active_cycle;
	char update;
	int activity;
	char active;
	char prev_active;
	char learning;
	char prev_learning;
	List* connections;
} Segment;

typedef struct Connection {
	long active_cycle;
	Cell* cell;
	double perm;
} Connection;

typedef struct Input {
	int bit_index;
	char active;
	double perm;
} Input;

typedef struct Update {
	long active_cycle;
	Segment* segment;
	List* active_connections;
	List* new_connections;
	List* inactive_connections;
} Update;

//allocates new region
Region* new_region() {
	Region* region = malloc(sizeof(Region));
	region->active_columns = NULL;
	region->columns = malloc(COLUMN_COUNT * sizeof(Column));
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		column->inputs = malloc(INPUT_COUNT * sizeof(Input));
		column->cells = malloc(CELL_COUNT * sizeof(Cell));
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			cell->segment_updates = NULL;
			cell->segments = NULL;
		}
	}
	return region;
}

//frees region
void free_region(Region* region) {
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			List* segments = cell->segments;
			while (segments != NULL) {
				Segment* segment = (Segment*) segments->elem;
				List* connections = segment->connections;
				while (connections != NULL) {
					Connection* connection = (Connection*) connections->elem;
					free(connection);
					connections = connections->next;
				}
				free_list(segment->connections);
				free(segment);
				segments = segments->next;
			}
			free_list(cell->segments);
			List* segment_updates = cell->segment_updates;
			while (segment_updates != NULL) {
				Update* update = (Update*) segment_updates->elem;
				List* new_connections = update->new_connections;
				while (new_connections != NULL) {
					Connection* connection = (Connection*) new_connections->elem;
					free(connection);
					new_connections = new_connections->next;
				}
				free_list(update->new_connections);
				free(update);
				segment_updates = segment_updates->next;
			}
			free_list(cell->segment_updates);
		}
		free(column->cells);
	}
	free(region->columns);
	free_list(region->active_columns);
	free(region);
}

//debug function, prints predictive state of all cells
void print_prediction(Region* region) {
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			if (cell->predictive) {
				printf("%d:%d\n", a, b);
			}
		}
	}
}

#endif // CORTEX_H
