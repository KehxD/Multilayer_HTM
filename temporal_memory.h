#ifndef TEMPORAL_MEMORY_H
#define TEMPORAL_MEMORY_H

#include <stdlib.h>
#include <stdio.h>
#include "struct_utils.h"
#include "cortex.h"

//based on the following sources:
//http://numenta.com/biological-and-machine-intelligence/
//http://numenta.com/assets/pdf/whitepapers/hierarchical-temporal-memory-cortical-learning-algorithm-0.2.1-en.pdf
//http://numenta.com/assets/pdf/biological-and-machine-intelligence/0.4/BaMI-Temporal-Memory.pdf

//removes old and unused updates inside a column

void temporal_column_forget_updates(Column* column, long cycle, long cycles) {
	int a;
	for (a = 0; a < CELL_COUNT; a++) {
		Cell* cell = &(column->cells[a]);
		List* segment_updates = cell->segment_updates; //current list of updates
		List* new_segment_updates = NULL; //new list of updates
		while (segment_updates != NULL) {
			Update* update = (Update*) segment_updates->elem;
			if (cycle - update->active_cycle >= cycles) { //if update is too old
				if (update->segment != NULL) {
					update->segment->update--; //decrement the segment's pending updates marker
				}
				List* connections = update->new_connections;
				while (connections != NULL) { //remove all newly allocated connections in the update
					Connection* connection = (Connection*) connections->elem;
					free(connection);
					connections = connections->next;
				}
				free_list(update->active_connections);
				free_list(update->new_connections);
				free_list(update->inactive_connections);
				free(update);
			} else {
				new_segment_updates = add_elem((long) update, new_segment_updates); //else: add update to new list of updates
			}
			segment_updates = segment_updates->next;
		}
		free_list(cell->segment_updates); //free now old list of updates
		cell->segment_updates = new_segment_updates; //assign new list of updates as current
	}
}

//removes old and unused updates inside columns between index "from" and "to"

void temporal_region_forget_updates(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		temporal_column_forget_updates(column, region->cycle, FORGET_INTERVAL);
	}
}

//removes old and unused segments and connections inside a column

void temporal_column_forget_segments(Column* column, long cycle, long cycles) {
	int a;
	for (a = 0; a < CELL_COUNT; a++) {
		Cell* cell = &(column->cells[a]);
		List* segments = cell->segments; //current list of segments
		List* new_segments = NULL; //new list of segments
		while (segments != NULL) {
			Segment* segment = (Segment*) segments->elem;
			char rem = 0; //segment to be removed variable
			if (segment->update == 0) { //if segment has no pending updates
				if (cycle - segment->active_cycle >= cycles) { //if segment inactive for too long
					rem = 1; //mark segment for removal
				}
				List* connections = segment->connections; //current list of connections
				List* new_connections = NULL; //new list of connections
				while (connections != NULL) {
					Connection* connection = (Connection*) connections->elem;
					if (rem || cycle - connection->active_cycle >= cycles || connection->perm == 0.0) { //if segment marked for removal OR connection inactive for too long OR connection permanence equals 0
						free(connection); //free connection
					} else {
						new_connections = add_elem((long) connection, new_connections); //add connection to new list of connections
					}
					connections = connections->next;
				}
				free_list(segment->connections); //free now old list of connections
				segment->connections = new_connections; //assign new list of connections as current
			}
			if (rem) { //if segment marked for removal
				free(segment); //free segment
			} else {
				new_segments = add_elem((long) segment, new_segments); //add segment to new list of segments
			}
			segments = segments->next;
		}
		free_list(cell->segments); //free now old list of segments
		cell->segments = new_segments; //assign new list of segments as current
	}
}

//removes old and unused segments and connections inside columns between index "from" and "to"

void temporal_region_forget_segments(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		temporal_column_forget_segments(column, region->cycle, FORGET_INTERVAL);
	}
}

//resets the burst count

void temporal_reset_region(Region* region) {
	region->cycle++;
	region->bursts = 0;
}
void temporal_reset_prediction(Region* region) {
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			if (cell->predictive) {
				cell->predictive = 1;
			}
		}
	}
}

//updates the state of a column and its substructures for the next timestep

void temporal_column_cycle(Column* column) {
	int a;
	for (a = 0; a < CELL_COUNT; a++) {
		Cell* cell = &(column->cells[a]);
		cell->prev_active = cell->active;
		cell->prev_predictive = cell->predictive;
		cell->prev_learning = cell->learning;
		cell->active = cell->active == 0 ? 0 : cell->active - 1;
		cell->predictive = cell->predictive == 0 ? 0 : cell->predictive - 1;
		cell->learning = cell->learning == 0 ? 0 : cell->learning - 1;
		List* segments = cell->segments;
		while (segments != NULL) {
			Segment* segment = (Segment*) segments->elem;
			segment->activity = 0;
			segment->prev_active = segment->active;
			segment->prev_learning = segment->learning;
			segment->active = 0;
			segment->learning = 0;
			segments = segments->next;
		}
	}
}

//proceeds to the next timestep for all columns between index "from" and "to"

void temporal_region_cycle(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		temporal_column_cycle(column);
	}
}

//initializes a column

void temporal_init_column(Column* column, int column_index) {
	int a;
	for (a = 0; a < CELL_COUNT; a++) {
		Cell* cell = &(column->cells[a]);
		cell->active = 0;
		cell->prev_active = 0;
		cell->predictive = 0;
		cell->prev_predictive = 0;
		cell->learning = 0;
		cell->prev_learning = 0;
		cell->index = a + column_index * CELL_COUNT;
		if (CELL_REMAIN_RANDOM) {
			cell->remain_active = (rand() % CELL_REMAIN_ACTIVE) + 1;
			cell->remain_predictive = (rand() % CELL_REMAIN_PREDICTIVE) + 1;
			cell->remain_learning = (rand() % CELL_REMAIN_LEARNING) + 1;
		} else {
			cell->remain_active = CELL_REMAIN_ACTIVE;
			cell->remain_predictive = CELL_REMAIN_PREDICTIVE;
			cell->remain_learning = CELL_REMAIN_LEARNING;
		}
	}
}

//initializes the region

void temporal_init_region(Region* region) {
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		temporal_init_column(column, a);
	}
	region->bursts = 0;
}

//updates the state of all segments inside a cell

void temporal_activate_segments(Cell* cell, long cycle) {
	List* segments = cell->segments;
	while (segments != NULL) {
		Segment* segment = (Segment*) segments->elem;
		int active = 0; //number of connections pointing to cells active in previous timestep
		int learning = 0; //number of connections pointing to cells learning in previous timestep
		List* connections = segment->connections;
		while (connections != NULL) {
			Connection* connection = (Connection*) connections->elem;
			if (connection->perm >= CONNECTION_PERMANENCE_THRESHOLD) { //if connection's permanence reaches threshold
				if (connection->cell->prev_active) { //if connection points to cell active in previous timestep
					active++;
					connection->active_cycle = cycle; //set activity timestamp
				}
				if (connection->cell->prev_learning) { //if connection points to cell learning in previous timestep
					learning++;
				}
			}
			connections = connections->next;
		}
		segment->activity = active; //set segment's activity
		if (active >= SEGMENT_ACTIVATION_THRESHOLD) { //if segment's activity reaches activation threshold
			segment->active = 1; //activate segment
			segment->active_cycle = cycle; //set activity timestamp
			if (learning == active) { //if all active connections point to learning cells
				segment->learning = 1;
			} else {
				segment->learning = 0;
			}
		} else {
			segment->active = 0;
			segment->learning = 0;
		}
		segments = segments->next;
	}
}

//searches the learning area of a cell for other learning cells to form new connections to

void temporal_add_new_connections(Region* region, int column_index, int cell_index, char prev, Update* update,
		int count) {
	List* available_cells = NULL; //list of available cells to form new connections to
	int column_from = column_index - CONNECTION_LEARNING_HORIZONTAL; //start searching at column index == column_from
	int column_to = column_index + CONNECTION_LEARNING_HORIZONTAL; //stop searching at column index == column_to
	column_from = column_from < 0 ? 0 : column_from;
	column_to = column_to >= COLUMN_COUNT ? COLUMN_COUNT - 1 : column_to;
	int cell_from = cell_index - CONNECTION_LEARNING_VERTICAL; //start searching at cell index == cell_from
	int cell_to = cell_index + CONNECTION_LEARNING_VERTICAL; //stop searching at cell index == cell_from
	cell_from = cell_from < 0 ? 0 : cell_from;
	cell_to = cell_to >= CELL_COUNT ? CELL_COUNT - 1 : cell_to;
	int a;
	for (a = column_from; a <= column_to; a++) { //iterate through columns in search space
		Column* column = &(region->columns[a]);
		int b;
		for (b = cell_from; b <= cell_to; b++) { //iterate through cells in search space
			Cell* cell = &(column->cells[b]);
			if (prev && cell->prev_learning) { //if cell was learning in previous timestep
				available_cells = add_elem((long) cell, available_cells);
			}
			if (!prev && cell->learning) { //if cell is learning
				available_cells = add_elem((long) cell, available_cells);
			}
		}
	}
	if (available_cells != NULL) { //if available cells were found
		int len = available_cells->len;
		int rest = len; //number of cells still available
		long* array = list_to_array(available_cells);
		free_list(available_cells);
		while (count > 0 && rest > 0) { //as long as new connections are needed AND available cells are left
			int i = rand() % len; //pick an available cell at random
			if (array[i] != -1) { //if cell not yet used
				Connection* new_connection = malloc(sizeof(Connection)); //allocate new connection
				new_connection->active_cycle = region->cycle; //set timestamp
				new_connection->cell = (Cell*) array[i]; //point to chosen cell
				new_connection->perm = CONNECTION_INITIAL_PERMANENCE; //set initial permanence
				update->new_connections = add_elem((long) new_connection, update->new_connections); //add new connection to update
				array[i] = -1; //mark cell as used
				count--;
				rest--;
			}
		}
		free(array);
	}
}

//marks the connections as active or inactive

void temporal_add_active_connections(char prev, Update* update) {
	if (update->segment == NULL) {
		return;
	}
	List* connections = update->segment->connections;
	while (connections != NULL) {
		Connection* connection = (Connection*) connections->elem;
		if (prev && connection->cell->prev_active) {
			update->active_connections = add_elem((long) connection, update->active_connections);
		}
		if (prev && !connection->cell->prev_active) {
			update->inactive_connections = add_elem((long) connection, update->inactive_connections);
		}
		if (!prev && connection->cell->active) {
			update->active_connections = add_elem((long) connection, update->active_connections);
		}
		if (!prev && !connection->cell->active) {
			update->inactive_connections = add_elem((long) connection, update->inactive_connections);
		}
		connections = connections->next;
	}
}

//finds the cell inside a column best suitable for becoming the learning cell
//forms new connections being added to the learning cell when the new update is applied

void temporal_find_learning_cell(Region* region, Column* column, int column_index) {
	Cell* best_cell = NULL; //cell with the most active segment
	int best_index = -1;
	Segment* best_segment = NULL; //most active segment
	int best_active = 0; //highest activity
	Cell* smallest_cell = NULL; //cell with the least amount of segments
	int smallest_index = -1;
	int smallest_len = 0; //lowest amount of segments
	int a;
	for (a = 0; a < CELL_COUNT; a++) {
		Cell* cell = &(column->cells[a]);
		int len = cell->segments != NULL ? cell->segments->len : 0; //cell's amount of segments
		List* segments = cell->segments;
		while (segments != NULL) {
			Segment* segment = (Segment*) segments->elem;
			int active = 0; //segment's activity
			List* connections = segment->connections;
			while (connections != NULL) {
				Connection* connection = (Connection*) connections->elem;
				if (connection->cell->prev_active) {
					active++;
				}
				connections = connections->next;
			}
			if (active >= SEGMENT_LEARNING_THRESHOLD && active > best_active) { //if activity reaches learning threshold AND activity is the highest
				best_cell = cell;
				best_index = a;
				best_segment = segment;
				best_active = active;
			}
			segments = segments->next;
		}
		if (smallest_cell == NULL || len < smallest_len || (len == smallest_len && rand() % 2 == 0)) { //if cell is the smallest cell
			smallest_cell = cell;
			smallest_index = a;
			smallest_len = len;
		}
	}
	Cell* chosen_cell;
	int chosen_index;
	if (best_cell != NULL) { //use cell with most active segment if possible
		chosen_cell = best_cell;
		chosen_index = best_index;
	} else { //use cell with least amount of segments otherwise
		chosen_cell = smallest_cell;
		chosen_index = smallest_index;
	}
	chosen_cell->learning = chosen_cell->remain_learning;
	Update* update = malloc(sizeof(Update));
	update->active_cycle = region->cycle;
	update->segment = NULL;
	update->active_connections = NULL;
	update->new_connections = NULL;
	update->inactive_connections = NULL;
	if (best_segment != NULL) {
		best_segment->update++;
		update->segment = best_segment;
		temporal_add_active_connections(1, update); //remember which connections were active
	}
	int count =
			update->active_connections != NULL ?
					SEGMENT_NEW_CONNECTIONS - update->active_connections->len : SEGMENT_NEW_CONNECTIONS; //number of new connections to be formed
	temporal_add_new_connections(region, column_index, chosen_index, 1, update, count); //form new connections
	chosen_cell->segment_updates = add_elem((long) update, chosen_cell->segment_updates); //submit update
}

//finds the most active segment inside a cell

Segment* temporal_best_segment(Cell* cell, char learning) {
	Segment* best = NULL;
	List* segments = cell->segments;
	while (segments != NULL) {
		Segment* segment = (Segment*) segments->elem;
		if ((best == NULL || segment->activity > best->activity) && segment->active
				&& !(learning && !segment->learning)) {
			best = segment;
		}
		segments = segments->next;
	}
	return best;
}

//activates the cells of the winning columns and finds a learning cell in each winning column

void temporal_activate_region(Region* region) {
	List* active_columns = region->active_columns;
	while (active_columns != NULL) { //iterate through active columns
		Column* column = &(region->columns[active_columns->elem]); //get column from index
		char predicted = 0; //column predicted its activation
		char chosen = 0; //learning cell chosen
		int a;
		for (a = 0; a < CELL_COUNT; a++) {
			Cell* cell = &(column->cells[a]);
			if (cell->prev_predictive) { //if cell was predictive in previous timestep
				predicted = 1;
				cell->active = cell->remain_active; //activate cell
				temporal_activate_segments(cell, region->cycle); //activate its segments
				Segment* segment = temporal_best_segment(cell, 0); //find its best segment
				if (segment != NULL && segment->prev_learning) { //continue learning
					chosen = 1;
					cell->learning = cell->remain_learning;
				}
			}
		}
		if (!predicted) { //burst column if no predictive cell
			region->bursts++;
			for (a = 0; a < CELL_COUNT; a++) {
				Cell* cell = &(column->cells[a]);
				cell->active = cell->remain_active;
			}
		}
		if (!chosen) { //find learning cell if none yet chosen
			temporal_find_learning_cell(region, column, active_columns->elem);
		}
		active_columns = active_columns->next;
	}
}

//computes the predictive state of cells inside the columns between index "from" and "to", schedules an update for active segments inside predictive cells

void temporal_predict_cells(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			List* segments = cell->segments;
			while (segments != NULL) {
				Segment* segment = (Segment*) segments->elem;
				int active = 0; //segment's activity
				List* connections = segment->connections;
				while (connections != NULL) {
					Connection* connection = (Connection*) connections->elem;
					if (connection->perm >= CONNECTION_PERMANENCE_THRESHOLD && connection->cell->active) { //if connection permanence reaches threshold AND connection points to active cell
						active++;
					}
					connections = connections->next;
				}
				if (active >= SEGMENT_ACTIVATION_THRESHOLD) { //if segment's activity reaches activation threshold
					cell->predictive = cell->remain_predictive; //set parent cell to predictive state
					if (ENABLE_LEARNING) {
						//schedule reinforcement update
						segment->update++;
						Update* update = malloc(sizeof(Update));
						update->active_cycle = region->cycle;
						update->segment = segment;
						update->active_connections = NULL;
						update->new_connections = NULL;
						update->inactive_connections = NULL;
						temporal_add_active_connections(0, update);
						cell->segment_updates = add_elem((long) update, cell->segment_updates);
					}
				}
				segments = segments->next;
			}
		}
	}
}

//positively reinforces the given List of connections if "inc" is set, otherwise negatively reinforces

void temporal_reinforce_connections(List* connections, char inc) {
	List* node = connections;
	while (node != NULL) {
		Connection* connection = (Connection*) node->elem;
		if (inc) {
			connection->perm += CONNECTION_PERMANENCE_INC;
			connection->perm = connection->perm > 1.0 ? 1.0 : connection->perm;
		} else {
			connection->perm -= CONNECTION_PERMANENCE_DEC;
			connection->perm = connection->perm < 0.0 ? 0.0 : connection->perm;
		}
		node = node->next;
	}
}

//if "positive_reinforcement" is set: positively reinforces connections marked as active and new inside the update, negatively reinforces the connections marked as inactive
//otherwise: negatively reinforces connections marked as active inside the update
//also forms new segments if required and adds new connections to their belonging segments

void temporal_adapt_segments(Cell* cell, char positive_reinforcement, long cycle) {
	List* segment_updates = cell->segment_updates;
	while (segment_updates != NULL) {
		Update* update = (Update*) segment_updates->elem;
		if (positive_reinforcement) {
			temporal_reinforce_connections(update->active_connections, 1);
			temporal_reinforce_connections(update->new_connections, 1);
			if (update->segment != NULL) {
				temporal_reinforce_connections(update->inactive_connections, 0);
			}
		} else {
			temporal_reinforce_connections(update->active_connections, 0);
		}
		if (update->segment != NULL) { //if segment already exists
			update->segment->update--;
			update->segment->connections = merge_lists(update->segment->connections, update->new_connections); //merge current connections and new connections
			update->new_connections = NULL;
		} else {
			Segment* new_segment = malloc(sizeof(Segment)); //allocate new segment
			new_segment->update = 0;
			new_segment->active_cycle = cycle;
			new_segment->activity = 0;
			new_segment->active = 0;
			new_segment->prev_active = 0;
			new_segment->learning = 0;
			new_segment->prev_learning = 0;
			new_segment->connections = update->new_connections; //add new connections to new segment
			cell->segments = add_elem((long) new_segment, cell->segments); //add new segment to parent cell
		}
		//free update
		free_list(update->active_connections);
		free_list(update->inactive_connections);
		free(update);
		segment_updates = segment_updates->next;
	}
	free_list(cell->segment_updates);
	cell->segment_updates = NULL;
}

//applies all scheduled updates for cells inside columns between index "from" and "to"

void temporal_apply_updates(Region* region, int from, int to) {
	int a;
	for (a = from; a <= to; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			if (cell->learning) { //if cell is learning
				temporal_adapt_segments(cell, 1, region->cycle);
			} else if (!cell->active && (cell->prev_predictive == 1)) { //if cell incorrectly predicted its activation
				temporal_adapt_segments(cell, 0, region->cycle);
			}
		}
	}
}

void temporal_overlap(Region* region) {
	int tcount = 0;
	int count = 0;
	int a;
	for (a = 0; a < COLUMN_COUNT; a++) {
		Column* column = &(region->columns[a]);
		int b;
		for (b = 0; b < CELL_COUNT; b++) {
			Cell* cell = &(column->cells[b]);
			if (cell->predictive) {
				tcount++;
			}
			if (cell->prev_predictive && cell->predictive) {
				count++;
			}
		}
	}
	region->overlap = count * 1.0 / tcount;
	printf("prediction overlap: %d/%d = %f\n", count, tcount, count * 1.0 / tcount);
	if (region->overlap < OVERLAP_THRESHOLD) {
		printf("OVERLAP LOW\n");
	}
}

#endif // TEMPORAL_MEMORY_H
