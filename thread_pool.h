#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <pthread.h>
#include "struct_utils.h"
#include "cortex.h"
#include "spatial_pooler.h"
#include "temporal_memory.h"

//thread pool implementation
//if additional functions are to be parallelized, add required mapping to exec_job()

//thread pool struct, allocate with new_thread_pool(), not manually
typedef struct thread_pool {
	int thread_count;
	pthread_t* threads;
	List* queue;
	pthread_mutex_t* qmut;
	pthread_cond_t* qcond;
} thread_pool;

//job struct, allocate with new_job(), not manually
typedef struct tp_job {
	char done;
	pthread_mutex_t* jmut;
	pthread_cond_t* jcond;
	Region* region;
	char cmd;
	int from;
	int to;
} tp_job;

//allocates and returns new job
tp_job* new_job() {
	tp_job* job = malloc(sizeof(tp_job));
	job->done = 0;
	job->jmut = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(job->jmut, NULL);
	job->jcond = malloc(sizeof(pthread_cond_t));
	pthread_cond_init(job->jcond, NULL);
	return job;
}

//frees job, do not free manually
void free_job(tp_job* job) {
	pthread_mutex_destroy(job->jmut);
	pthread_cond_destroy(job->jcond);
	free(job->jmut);
	free(job->jcond);
	free(job);
	return;
}

//internal function, maps 'cmd' codes of jobs to functions
void exec_job(tp_job* job) {
	switch (job->cmd) {
	case 0:
		spatial_give_input(job->region, job->from, job->to);
		break;
	case 1:
		spatial_boost_region(job->region, job->from, job->to);
		break;
	case 2:
		temporal_predict_cells(job->region, job->from, job->to);
		break;
	case 3:
		temporal_apply_updates(job->region, job->from, job->to);
		break;
	case 4:
		temporal_region_cycle(job->region, job->from, job->to);
		break;
	case 5:
		temporal_region_forget_updates(job->region, job->from, job->to);
		break;
	case 6:
		temporal_region_forget_segments(job->region, job->from, job->to);
		break;
	}
	return;
}

//internal function, main loop of thread pool
void* loop(long* p) {
	thread_pool* tp = (thread_pool*) p[0];
	free(p);
	while (1) {
		pthread_mutex_lock(tp->qmut);
		if (tp->queue) {
			tp_job* job = (tp_job*) tp->queue->elem;
			tp->queue = rem_head(tp->queue);
			pthread_mutex_unlock(tp->qmut);
			exec_job(job);
			pthread_mutex_lock(job->jmut);
			job->done = 1;
			pthread_cond_signal(job->jcond);
			pthread_mutex_unlock(job->jmut);
		} else {
			pthread_cond_wait(tp->qcond, tp->qmut);
			pthread_mutex_unlock(tp->qmut);
		}
	}
	pthread_exit(NULL);
	return NULL;
}

//allocates and returns new thread pool
thread_pool* new_thread_pool(int thread_count) {
	thread_pool* tp = malloc(sizeof(thread_pool));
	tp->thread_count = thread_count;
	tp->threads = malloc(tp->thread_count * sizeof(pthread_t));
	tp->queue = NULL;
	tp->qmut = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tp->qmut, NULL);
	tp->qcond = malloc(sizeof(pthread_cond_t));
	pthread_cond_init(tp->qcond, NULL);
	int a;
	for (a = 0; a < thread_count; a++) {
		long* p = malloc(2 * sizeof(long));
		p[0] = (long) tp;
		p[1] = (long) a;
		pthread_create(&tp->threads[a], NULL, &loop, p);
	}
	return tp;
}

//adds job to queue of thread pool
void schedule_job(thread_pool* tp, tp_job* job) {
	pthread_mutex_lock(tp->qmut);
	tp->queue = add_elem((long) job, tp->queue);
	pthread_cond_signal(tp->qcond);
	pthread_mutex_unlock(tp->qmut);
	return;
}

//wait for job
void join_job(thread_pool* tp, tp_job* job) {
	pthread_mutex_lock(job->jmut);
	if (!job->done) {
		pthread_cond_wait(job->jcond, job->jmut);
	}
	pthread_mutex_unlock(job->jmut);
	return;
}

#endif /* THREAD_POOL_H_ */
