#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "struct_utils.h"
#include "sdr_utils.h"
#include "cortex.h"
#include "spatial_pooler.h"
#include "temporal_memory.h"
#include "thread_pool.h"
#include "process_communication.h"
#include "save_load.h"

char give_data;
int pattern_len; //pattern length
int** pattern; //pattern
int pattern_count;
int current_pattern;
int terminate; //termination cycle
int var; //noise level
int random_prob; //1 / random_prob = probability of random value instead of pattern in each cycle
int warmup; //number of warmup cycles
int cooldown; //cooldown period after random value
int random_last; //last cycle with random value
int random_count; //number of random values given
int random_detected; //number of random values detected
int pattern_failed; //number of unrecognized patterns
double avg_act_columns; //average number of active columns
int last_overlap;
int region_id;
List* read_pipes;
List* write_pipes;
int number_regions;
int unioned_sdr;
int** connected_region_sizes;

void set_parameter(char* param, char* val);
void read_region_config();
void create_jobs(Region* region, thread_pool* tp, int thread_count, char cmd);
void init();
int generate_input(Region* region);
void generate_stats(Region* region);
void print_stats();
void finalize();
void init_connected_region_sizes(int** hierarchy);
List** read_data(int fc);

int main(int argc, char* argv[]) {

	give_data = 1;

	if (argc < 2) {
		printf("error: no argument");
		exit(0);
	} else if (argc < 3) {
		give_data = 0;
	}
	region_id = atoi(argv[1]);

	time_t t;
	time(&t);
	srand((unsigned int) t);
	int thread_count = 8;
	thread_pool* tp = new_thread_pool(thread_count);
	init();

	printf("STARTED\n");
	Region* region = new_region();
	printf("region allocated\n");

	spatial_init_region(region, SDR_BASE + SDR_SET);
	temporal_init_region(region);
	if (LOAD) {
		load_region(region, region_id, COLUMN_COUNT, CELL_COUNT);
	}
	printf("region initialized\n");
	int file_count = 0;
	List** data_list = NULL;
	if (give_data) {
		file_count = atoi(argv[2]);
		data_list = read_data(file_count);
	}
	int cf = 0;
	int ci = 0;
	List* l = NULL;
	int a;
	//main loop
	for (a = 0; a < terminate; a++) {
		if (give_data) {
			if (l == NULL) {
				cf = rand() % file_count;
				l = data_list[cf];
				ci = 0;
			}
			region->sdr = (SDR*) l->elem;
			l = l->next;
		} else if (read_pipes) {
			SDR* region_sdr = read_input_from_pipes(read_pipes, connected_region_sizes);
			if (region_sdr == NULL) {
				break;
			}
			region->sdr = region_sdr;
		} else {
			int input = generate_input(region);
			region->sdr = int_to_sdr(input);
		}

		//spatial_give_input(region, 0, COLUMN_COUNT - 1);
		create_jobs(region, tp, thread_count, 0);
		spatial_activate_region(region);
		if (ENABLE_LEARNING) {
			spatial_reinforce_region(region);
			spatial_region_averages(region);
			//spatial_boost_region(region, 0, COLUMN_COUNT - 1);
			create_jobs(region, tp, thread_count, 1);
		}
		temporal_activate_region(region);
		if (give_data) {
			printf("input: %d:%d\n", cf, ci);
			ci++;
		}
		generate_stats(region);
		double ratio = region->bursts / ((double) (region->active_columns != NULL ? region->active_columns->len : 0)); //ratio of bursting columns to active columns
		printf("columns activated: (%d/%d) = %f\n", region->active_columns != NULL ? region->active_columns->len : 0,
				COLUMN_COUNT,
				(region->active_columns != NULL ? region->active_columns->len : 0) / ((double) COLUMN_COUNT));
		printf("columns bursted: (%d/%d) = %f\n", region->bursts,
				region->active_columns != NULL ? region->active_columns->len : 0,
				((double) region->bursts) / (region->active_columns != NULL ? region->active_columns->len : 0));
		//temporal_predict_cells(region, 0, COLUMN_COUNT - 1);
		create_jobs(region, tp, thread_count, 2);
		if (ratio > DETECTION_THRESHOLD) { //if ratio exceeds detection threshold
			printf("ANOMALY DETECTED\n");
			temporal_reset_prediction(region);
		}
		temporal_overlap(region);
		if (region->overlap < OVERLAP_THRESHOLD) {
			last_overlap = region->cycle;
		}
		if (ENABLE_LEARNING) {
			//temporal_apply_updates(region, 0, COLUMN_COUNT - 1);
			create_jobs(region, tp, thread_count, 3);
		}
		//temporal_region_cycle(region, 0, COLUMN_COUNT - 1);
		create_jobs(region, tp, thread_count, 4);
		temporal_reset_region(region);
		spatial_reset_region(region);
		if (ENABLE_LEARNING && region->cycle > 0 && region->cycle % FORGET_INTERVAL == 0) { //run garbage collector
		//temporal_region_forget_updates(region, 0, COLUMN_COUNT - 1);
			create_jobs(region, tp, thread_count, 5);
			//temporal_region_forget_segments(region, 0, COLUMN_COUNT - 1);
			create_jobs(region, tp, thread_count, 6);
		}
		if (!give_data) {
			free_sdr(region->sdr);
		}
		if (last_overlap == region->cycle - 2) {
			write_output_to_pipes(region, write_pipes);
			printf("SDR written\n");
		}
		printf("cycle %ld done\n\n", region->cycle);
	}
	print_stats();
	if (SAVE) {
		save_region(region, region_id, COLUMN_COUNT, CELL_COUNT);
	}
	finalize();
	printf("TERMINATED\n");
}

//allocates, starts and waits for jobs running the parallel functions (which function is indicated by 'cmd', see thread_pool.h)
void create_jobs(Region* region, thread_pool* tp, int thread_count, char cmd) {
	int count = COLUMN_COUNT / thread_count + (COLUMN_COUNT % thread_count != 0 ? 1 : 0);
	tp_job** jobs = malloc(thread_count * sizeof(tp_job*));
	int a;
	for (a = 0; a < thread_count; a++) {
		int from = a * count;
		int to = ((a + 1) * count > COLUMN_COUNT ? COLUMN_COUNT : (a + 1) * count) - 1;
		jobs[a] = new_job();
		jobs[a]->region = region;
		jobs[a]->cmd = cmd;
		jobs[a]->from = from;
		jobs[a]->to = to;
		schedule_job(tp, jobs[a]);
	}
	for (a = 0; a < thread_count; a++) {
		join_job(tp, jobs[a]);
		free_job(jobs[a]);
	}
	free(jobs);
}

//sets the region parameter with name 'param' to the value 'val'
void set_parameter(char* param, char* val) {
	if (strcmp(param, "COLUMN_COUNT\n") == 0) {
		COLUMN_COUNT = atoi(val);
	} else if (strcmp(param, "CELL_COUNT\n") == 0) {
		CELL_COUNT = atoi(val);
	} else if (strcmp(param, "INPUT_COUNT\n") == 0) {
		INPUT_COUNT = atoi(val);
	} else if (strcmp(param, "INPUT_PERMANENCE_THRESHOLD\n") == 0) {
		INPUT_PERMANENCE_THRESHOLD = atof(val);
	} else if (strcmp(param, "INPUT_PERMANENCE_INC\n") == 0) {
		INPUT_PERMANENCE_INC = atof(val);
	} else if (strcmp(param, "INPUT_PERMANENCE_DEC\n") == 0) {
		INPUT_PERMANENCE_DEC = atof(val);
	} else if (strcmp(param, "INPUT_PERMANENCE_CHECK\n") == 0) {
		INPUT_PERMANENCE_CHECK = atoi(val);
	} else if (strcmp(param, "COLUMN_STIMULUS_THRESHOLD\n") == 0) {
		COLUMN_STIMULUS_THRESHOLD = atoi(val);
	} else if (strcmp(param, "COLUMN_MAX_BOOST\n") == 0) {
		COLUMN_MAX_BOOST = atoi(val);
	} else if (strcmp(param, "COLUMN_START_BOOST\n") == 0) {
		COLUMN_START_BOOST = atoi(val);
	} else if (strcmp(param, "COLUMN_AVERAGE_WINDOW\n") == 0) {
		COLUMN_AVERAGE_WINDOW = atoi(val);
	} else if (strcmp(param, "REGION_ACTIVE_COLUMNS\n") == 0) {
		REGION_ACTIVE_COLUMNS = atoi(val);
	} else if (strcmp(param, "CELL_REMAIN_ACTIVE\n") == 0) {
		CELL_REMAIN_ACTIVE = atoi(val);
	} else if (strcmp(param, "CELL_REMAIN_PREDICTIVE\n") == 0) {
		CELL_REMAIN_PREDICTIVE = atoi(val);
	} else if (strcmp(param, "CELL_REMAIN_LEARNING\n") == 0) {
		CELL_REMAIN_LEARNING = atoi(val);
	} else if (strcmp(param, "CELL_REMAIN_RANDOM\n") == 0) {
		CELL_REMAIN_RANDOM = atoi(val);
	} else if (strcmp(param, "SEGMENT_ACTIVATION_THRESHOLD\n") == 0) {
		SEGMENT_ACTIVATION_THRESHOLD = atof(val);
	} else if (strcmp(param, "SEGMENT_LEARNING_THRESHOLD\n") == 0) {
		SEGMENT_LEARNING_THRESHOLD = atof(val);
	} else if (strcmp(param, "SEGMENT_NEW_CONNECTIONS\n") == 0) {
		SEGMENT_NEW_CONNECTIONS = atoi(val);
	} else if (strcmp(param, "CONNECTION_LEARNING_HORIZONTAL\n") == 0) {
		CONNECTION_LEARNING_HORIZONTAL = atoi(val);
	} else if (strcmp(param, "CONNECTION_LEARNING_VERTICAL\n") == 0) {
		CONNECTION_LEARNING_VERTICAL = atoi(val);
	} else if (strcmp(param, "CONNECTION_PERMANENCE_THRESHOLD\n") == 0) {
		CONNECTION_PERMANENCE_THRESHOLD = atof(val);
	} else if (strcmp(param, "CONNECTION_INITIAL_PERMANENCE\n") == 0) {
		CONNECTION_INITIAL_PERMANENCE = atof(val);
	} else if (strcmp(param, "CONNECTION_PERMANENCE_INC\n") == 0) {
		CONNECTION_PERMANENCE_INC = atof(val);
	} else if (strcmp(param, "CONNECTION_PERMANENCE_DEC\n") == 0) {
		CONNECTION_PERMANENCE_DEC = atof(val);
	} else if (strcmp(param, "FORGET_INTERVAL\n") == 0) {
		FORGET_INTERVAL = atoi(val);
	} else if (strcmp(param, "DETECTION_THRESHOLD\n") == 0) {
		DETECTION_THRESHOLD = atof(val);
	} else if (strcmp(param, "OVERLAP_THRESHOLD\n") == 0) {
		OVERLAP_THRESHOLD = atof(val);
	} else if (strcmp(param, "ENABLE_LEARNING\n") == 0) {
		ENABLE_LEARNING = atoi(val);
	} else if (strcmp(param, "LOAD\n") == 0) {
		LOAD = atoi(val);
	} else if (strcmp(param, "SAVE\n") == 0) {
		SAVE = atoi(val);
	} else if (strcmp(param, "var\n") == 0) {
		var = atoi(val);
	} else if (strcmp(param, "random_prob\n") == 0) {
		random_prob = atoi(val);
	} else if (strcmp(param, "warmup\n") == 0) {
		warmup = atoi(val);
	} else if (strcmp(param, "cooldown\n") == 0) {
		cooldown = atoi(val);
	}
}

//reads the region parameter settings from local config file and sets their values
void read_region_config() {
	printf("reading region config from 'region_id_%d'... ", region_id);
	char fn[256] = "./config/region_id_";
	char fm[256];
	itoa(region_id, fm, 10);
	strcat(fn, fm);
	FILE* fp;
	size_t n = 0;
	char* line1 = NULL;
	char* line2 = NULL;
	char* skip = NULL;
	ssize_t len1 = 0;
	ssize_t len2 = 0;
	if ((fp = fopen(fn, "r")) == NULL) {
		printf("No such file \"%s\"\n", fn);
		exit(1);
	}
	while (1) {
		len1 = getline(&line1, &n, fp);
		len2 = getline(&line2, &n, fp);
		if (len1 == -1 || len2 == -1) {
			break;
		} else {
			set_parameter(line1, line2);
			getline(&skip, &n, fp);
		}
	}
	free(line1);
	free(line2);
	free(skip);
	fclose(fp);
	printf("done\n");
}

void init() {
	//region config
	read_region_config();

	//global config
	FILE* global_config;
	if ((global_config = fopen("./config/global_config", "r+")) == NULL) {
		printf("No such file \"global_config\"");
		exit(1);
	}
	char dummy[256];
	fscanf(global_config, "%s", dummy);
	fscanf(global_config, "%i", &number_regions);
	fscanf(global_config, "%s", dummy);
	fscanf(global_config, "%i", &terminate);

	fclose(global_config);

	//setup pipes
	int** hierarchy = set_multilayer_hierarchy(number_regions);
	write_pipes = open_write_pipes(hierarchy, number_regions, region_id);
	read_pipes = open_read_pipes(hierarchy, number_regions, region_id);
	if (read_pipes) {
		init_connected_region_sizes(hierarchy);
	}
	destroy_hierarchy_matrix(hierarchy, number_regions);

	if (give_data) {
		SDR_BASE = 10000;
		SDR_SET = 5;
	} else {
		SDR_BASE = 1000;
		SDR_SET = 20;
	}

	pattern_count = 10;
	pattern_len = 25;

	if (!read_pipes) {
		pattern = malloc(pattern_count * sizeof(int*));
		int a = 0;
		for (a = 0; a < pattern_count; a++) {
			pattern[a] = malloc(pattern_len * sizeof(int));
			int b = 0;
			for (b = 0; b < pattern_len; b++) {
				pattern[a][b] = rand() % (SDR_BASE - 2 * var) + var;
			}
		}
	} else {
		SDR_SET = 0;
		SDR_BASE = COLUMN_COUNT * CELL_COUNT * (read_pipes->len);
		INPUT_COUNT = SDR_BASE / 4;
	}

	random_last = -1;
	random_count = 0;
	random_detected = 0;
	pattern_failed = 0;
	avg_act_columns = 0;
}

//generates test input data
int generate_input(Region* region) {
	if (region->cycle % pattern_len == 0) {
		if (rand() % 10) {
			current_pattern = rand() % pattern_count;
		} else {
			current_pattern = -1;
		}
	}
	int p;
	if (current_pattern != -1) {
		p = pattern[current_pattern][region->cycle % pattern_len]
				+ (rand() % 2 == 0 ? rand() % (var + 1) : -(rand() % (var + 1)));
	}
	if (current_pattern == -1 || rand() % random_prob == 0) {
		if (current_pattern == -1) {
			p = rand() % SDR_BASE;
		} else {
			while (p >= pattern[current_pattern][region->cycle % pattern_len] - var
					&& p <= pattern[current_pattern][region->cycle % pattern_len] + var) {
				p = rand() % SDR_BASE;
			}
		}
		random_last = region->cycle;
		if (region->cycle > warmup) {
			random_count++;
			printf("RANDOM VALUE\n");
		}
	}
	printf("input(%d:%d): %d\n", current_pattern, region->cycle % pattern_len, p);
	return p;
}

//closes pipes and frees data
void finalize() {
	int** hierarchy = set_multilayer_hierarchy(number_regions);
	write_end_signal(write_pipes);
	close_read_pipes(read_pipes);
	close_write_pipes(write_pipes, hierarchy, number_regions, region_id);
	destroy_hierarchy_matrix(hierarchy, number_regions);
}

//update test stats
void generate_stats(Region* region) {
	avg_act_columns -= avg_act_columns / (region->cycle + 1);
	avg_act_columns += (region->active_columns != NULL ? region->active_columns->len : 0)
			/ ((double) (region->cycle + 1));
	double ratio = region->bursts / ((double) (region->active_columns != NULL ? region->active_columns->len : 0));
	if (region->cycle > warmup && ratio > DETECTION_THRESHOLD && region->cycle == random_last) {
		random_detected++;
	}
	if (region->cycle > warmup && ratio > DETECTION_THRESHOLD && region->cycle - random_last > cooldown) {
		pattern_failed++;
	}
}

//print test stats
void print_stats() {
	printf("average active columns: %d/%d = %f\n", (int) avg_act_columns, COLUMN_COUNT, avg_act_columns / COLUMN_COUNT);
	printf("anomalies detected: %d/%d = %f\n", random_detected, random_count,
			((double) random_detected) / random_count);
	printf("patterns recognized: %d/%d = %f\n", terminate - warmup - random_count - pattern_failed,
			terminate - warmup - random_count,
			((double) terminate - warmup - random_count - pattern_failed) / (terminate - warmup - random_count));
	printf("anomalies not detected: %d/%d = %f\n", random_count - random_detected, random_count,
			((double) random_count - random_detected) / random_count);
	printf("patterns not recognized: %d/%d = %f\n", pattern_failed, terminate - warmup - random_count,
			((double) pattern_failed) / (terminate - warmup - random_count));
}

void init_connected_region_sizes(int** hierarchy) {
	int number_connected = read_pipes->len;

	connected_region_sizes = malloc(sizeof(int*) * number_connected);
	for (int i = 0; i < number_connected; i++) {
		connected_region_sizes[i] = malloc(sizeof(int) * 2);
	}
	int counter = 0;
	printf("Setting connected_region_sizes\n");
	for (int i = 0; i < number_regions; i++) {
		if (hierarchy[i][region_id]) {
			printf("Setting 'region_id_%d' reading sizes\n", i);
			char fn[256] = "./config/region_id_";
			char fm[256];
			itoa(i, fm, 10);
			strcat(fn, fm);

			FILE* config;
			if ((config = fopen(fn, "r+")) == NULL) {
				printf("No such file \"%s\"", fn);
				exit(1);
			}

			char dummy[256];
			fscanf(config, "%s", dummy);
			fscanf(config, "%i", &connected_region_sizes[counter][0]);
			fscanf(config, "%s", dummy);
			fscanf(config, "%i", &connected_region_sizes[counter][1]);

			fclose(config);
			printf("COLUMN_COUNT: %i CELL_COUNT: %i added\n", connected_region_sizes[counter][0],
					connected_region_sizes[counter][1]);
			counter++;
		}
	}
}

//reads data from files and converts it to SDRs, modify to fit required data format
List** read_data(int fc) {
	mkdir("./input", 0700);
	List** l = calloc(fc, sizeof(List));
	int i;
	for (i = 0; i < fc; i++) {
		char* fn = calloc(10, sizeof(char));
		sprintf(fn, "%d", i);
		char pn[20] = "./input/";
		strcat(pn, fn);
		FILE* fp;
		size_t n = 0;
		char* line1 = NULL;
		char* line2 = NULL;
		ssize_t len1 = 0;
		ssize_t len2 = 0;
		if ((fp = fopen(pn, "r")) == NULL) {
			printf("'%s' not found\n", pn);
			exit(1);
		}
		printf("reading from '%s'...\n", pn);
		free(fn);
		while (1) {
			len1 = getline(&line1, &n, fp);
			len2 = getline(&line2, &n, fp);
			if (len1 == -1 || len2 == -1) {
				break;
			}
			int lv = (int) 100000 * atof(line1);
			int rv = (int) 100000 * atof(line2);
			lv = lv < 0 ? 0 : lv >= SDR_BASE ? SDR_BASE - 1 : lv;
			rv = rv < 0 ? 0 : rv >= SDR_BASE ? SDR_BASE - 1 : rv;
			printf("%d\n", lv);
			printf("%d\n", rv);
			SDR* sdr1 = int_to_sdr(lv);
			SDR* sdr2 = int_to_sdr(rv);
			char* bits = malloc(sdr1->len + sdr2->len);
			int a;
			for (a = 0; a < sdr1->len; a++) {
				bits[a] = sdr1->bits[a];
			}
			for (a = 0; a < sdr2->len; a++) {
				bits[a + sdr1->len] = sdr2->bits[a];
			}
			SDR* val = bits_to_sdr(bits, sdr1->len + sdr2->len);
			free_sdr(sdr1);
			free_sdr(sdr2);
			l[i] = add_elem((long) val, l[i]);
		}
		free(line1);
		free(line2);
		fclose(fp);
	}
	printf("done!\n");
	return l;
}
