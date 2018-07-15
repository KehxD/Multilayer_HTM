#ifndef SAVE_LOAD_H_
#define SAVE_LOAD_H_

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
#include "process_communication.h"

FILE* file;
int debugprint = 0;
//The functions save the state of the region in clear text recursively going from region -> column -> cell -> segment -> connection. The name of the methods correspond to their save/load of the respective structure. save_region and load_region additionally initialize the respective file to save to or load from.

long index_of_Column(Region* r, Column* co, int num_Columns, int num_Cells) {
	long col_off = co - r->columns;
	long index = col_off / sizeof(Column);
	return col_off;
}

void save_connection(Region* r, Segment* s, Column* co, Cell* ce, Connection* con, int num_Columns, int num_Cells) {
	fprintf(file, "1 %ld %d %lf\n", con->active_cycle, con->cell->index, con->perm);
}

void save_segment(Region* r, Segment* s, Column* co, Cell* ce, int num_Columns, int num_Cells) {
	fprintf(file, "1 %ld %d %d %d %d %d %d\n", s->active_cycle, s->update, s->activity, s->active, s->prev_active,
			s->learning, s->prev_learning);
	for (List* i = s->connections; i; i = i->next) {
		save_connection(r, s, co, ce, (Connection*) i->elem, num_Columns, num_Cells);
	}
	fprintf(file, "-1 \n");
}

void save_cell(Region* r, Cell* ce, Column* co, int num_Columns, int num_Cells) {
	fprintf(file, "%d %d %d %d %d %d %d %d %d\n", ce->active, ce->remain_active, ce->prev_active, ce->predictive,
			ce->remain_predictive, ce->prev_predictive, ce->learning, ce->remain_learning, ce->prev_learning);
	for (List* i = ce->segments; i; i = i->next) {
		save_segment(r, (Segment*) i->elem, co, ce, num_Columns, num_Cells);
	}
	fprintf(file, "-1 \n");
}

void save_input(Input* in) {
	fprintf(file, "%d %d %lf\n", in->bit_index, in->active, in->perm);
}

void save_column(Region* r, Column* co, int num_Columns, int num_Cells, int current_col) {
	fprintf(file, "%d %d %d %d %d %lf %lf\n", current_col, co->active, co->overlap, co->boost, co->center_index,
			co->average_active, co->average_overlap);
	for (int i = 0; i < num_Cells / num_Columns; i++) {
		save_cell(r, &co->cells[i], co, num_Columns, num_Cells);
	}
	for (int i = 0; i < INPUT_COUNT; i++) {
		save_input(&co->inputs[i]);
	}
}

void save_region(Region* region, int region_id, int num_Columns, int num_Cells) {
	mkdir("./saves", 0700);
	num_Cells = num_Cells * num_Columns;
	char* buffer1 = malloc(10 * sizeof(char));
	itoa(region_id, buffer1, 10);
	char* save_name = malloc(sizeof("./saves/region_id_") + sizeof(buffer1) * sizeof(".dat"));
	char* prename = "./saves/region_id_";
	char* postname = ".dat";
	save_name[0] = '\0';
	strcat(save_name, prename);
	strcat(save_name, buffer1);
	strcat(save_name, postname);
	remove(save_name);
	file = fopen(save_name, "ab+");
	printf("\nSaving Region: %s\n", save_name);

	fprintf(file, "%ld %d %lf %lf\n", region->cycle, region->bursts, region->average_max, region->overlap);
	for (List* i = region->active_columns; i; i = i->next) {
		fprintf(file, "%ld ", i->elem);
	}
	fprintf(file, "-1\n");
	for (int i = 0; i < num_Columns; i++) {
		save_column(region, &region->columns[i], num_Columns, num_Cells, i);
	}

	printf("Region saved\n");
}

void load_connection(Region* r, Segment* s, Column* co, Cell* ce, Connection* con, int num_Columns, int num_Cells) {
	long index;
	fscanf(file, "%ld %ld %lf\n", &con->active_cycle, &index, &con->perm);
	con->cell = &(&r->columns[index / num_Cells])->cells[index % num_Cells];
}

void load_segment(Region* r, Segment* s, Column* co, Cell* ce, int num_Columns, int num_Cells) {
	fscanf(file, "%ld %d %d %d %d %d %d\n", &s->active_cycle, &s->update, &s->activity, &s->active, &s->prev_active,
			&s->learning, &s->prev_learning);
	if (debugprint)
		printf("1 %ld %c %d %c %c %c %c\n", s->active_cycle, s->update, s->activity, s->active, s->prev_active,
				s->learning, s->prev_learning);
	int i;
	for (fscanf(file, "%d ", &i); i > -1; fscanf(file, "%d ", &i)) {
		Connection* con = malloc(sizeof(Connection));
		load_connection(r, s, co, ce, con, num_Columns, num_Cells);
		s->connections = add_elem((long) con, s->connections);
	}
}

void load_cell(Region* r, Cell* ce, Column* co, int num_Columns, int num_Cells) {
	fscanf(file, "%d %d %d %d %d %d %d %d %d\n", &ce->active, &ce->remain_active, &ce->prev_active, &ce->predictive,
			&ce->remain_predictive, &ce->prev_predictive, &ce->learning, &ce->remain_learning, &ce->prev_learning);
	int i;
	if (debugprint)
		printf("%c %d %c %c %d %c %c %d %c\n", ce->active, ce->remain_active, ce->prev_active, ce->predictive,
				ce->remain_predictive, ce->prev_predictive, ce->learning, ce->remain_learning, ce->prev_learning);
	for (fscanf(file, "%d ", &i); i > -1; fscanf(file, "%d ", &i)) {
		if (debugprint)
			printf("%d ", i);
		Segment* s = malloc(sizeof(Segment));
		s->connections = NULL;
		load_segment(r, s, co, ce, num_Columns, num_Cells);
		ce->segments = add_elem((long) s, ce->segments);
	}
}

void load_input(Input* in) {
	fscanf(file, "%d %d %lf\n", &in->bit_index, &in->active, &in->perm);
}

void load_column(Region* r, Column* co, int num_Columns, int num_Cells) {
	int colnum;
	fscanf(file, "%d %d %d %d %d %lf %lf\n", &colnum, &co->active, &co->overlap, &co->boost, &co->center_index,
			&co->average_active, &co->average_overlap);
	for (int i = 0; i < num_Cells / num_Columns; i++) {
		load_cell(r, &co->cells[i], co, num_Columns, num_Cells);
	}
	for (int i = 0; i < INPUT_COUNT; i++) {
		load_input(&co->inputs[i]);
	}
}

void load_region(Region* region, int region_id, int num_Columns, int num_Cells) {
	num_Cells = num_Cells * num_Columns;
	char* buffer1 = malloc(10 * sizeof(char));
	itoa(region_id, buffer1, 10);
	char* save_name = malloc(sizeof("./saves/region_id_") + sizeof(buffer1) * sizeof(".dat"));
	char* prename = "./saves/region_id_";
	char* postname = ".dat";
	save_name[0] = '\0';
	strcat(save_name, prename);
	strcat(save_name, buffer1);
	strcat(save_name, postname);
	file = fopen(save_name, "ab+");
	printf("\nLoading Region: %s\n", save_name);
	if (!file) {
		printf("No such file found. Running without loading\n");
		return;
	}

	fscanf(file, "%ld %d %lf %lf\n", &region->cycle, &region->bursts, &region->average_max, &region->overlap);
	long i;
	for (fscanf(file, "%ld ", &i); i > -1; fscanf(file, "%ld ", &i)) {
		region->active_columns = add_elem(i, region->active_columns);
	}
	for (int i = 0; i < num_Columns; i++) {
		load_column(region, &region->columns[i], num_Columns, num_Cells);
	}

	printf("Region loaded\n");
}

#endif
