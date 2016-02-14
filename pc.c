#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>

#define DH_CONSUMER	1

int packet_data = 0;
typedef struct {
	bool dh_flag;
	int data;
} packet_t;

typedef struct {
	packet_t* buf;
	size_t len;
	size_t capacity;
	int producer_index;
	int consumer_index;
	pthread_mutex_t mutex;
	pthread_cond_t empty;
	pthread_cond_t full;
} buffer_t;

typedef struct {
	int id;
	int max_packet;
	buffer_t *bptr;
} thread_info;

bool dh_flag_in_use = false;

void __core_consume(buffer_t *buffer, int id)
{
	printf("\tConsumed by %d ----> %d and flag is %d\n",id,
		buffer->buf[buffer->consumer_index].data,
		buffer->buf[buffer->consumer_index].dh_flag);
	buffer->consumer_index = (buffer->consumer_index + 1) %
		buffer->capacity;
	buffer->len = buffer->len - 1;
}

void core_consume(buffer_t *buffer, int id, int max)
{

	int i;
	for (i = 0; i < max; i++) {
		if ((buffer->buf[buffer->consumer_index].dh_flag == true) &&
			dh_flag_in_use) {
			if (id == DH_CONSUMER) {
				/* Consume if DH FLAG is set only for Consumer 1
				 * and for any other consumer signal and
				 * yield.
				 */
				__core_consume(buffer, id);
			}
			else
				return;
		}
		else {
			/* Process non flagged packets regularly for
			 * all consumers
			 */
			__core_consume(buffer, id);
		}
		if (buffer->len == 0)
			return;
	}
}

void core_produce(buffer_t *buffer, int id, int max)
{
	int i;
	for (i = 0; i < max; i++) {
		buffer->buf[buffer->producer_index].data = packet_data++;
			buffer->buf[buffer->producer_index].dh_flag = rand()%2;
		printf("Produced by %d ----> %d and flag is %d\n",id,
			buffer->buf[buffer->producer_index].data,
			buffer->buf[buffer->producer_index].dh_flag);
		buffer->producer_index = (buffer->producer_index + 1) %
			buffer->capacity;
		buffer->len++;
		if (buffer->len == buffer->capacity)
			break;
	}
}

void* producer(void *arg) {

	int id, max;
	thread_info *prod = (thread_info*)arg;
	buffer_t *buffer = prod->bptr;
	id = prod->id;
	max = prod->max_packet;

	while(1) {

		pthread_mutex_lock(&buffer->mutex);

		while(buffer->len == buffer->capacity)
			pthread_cond_wait(&buffer->empty, &buffer->mutex);
		core_produce(buffer, id, max);
		pthread_cond_broadcast(&buffer->full);
		pthread_mutex_unlock(&buffer->mutex);
		sleep(1);
	}

	return NULL;
}

void* consumer(void *arg) {

	int id, max;
	thread_info *cons = (thread_info*)arg;
	buffer_t *buffer = cons->bptr;
	id = cons->id;
	max = cons->max_packet;

	while(1) {
		pthread_mutex_lock(&buffer->mutex);
		while(buffer->len == 0)
			pthread_cond_wait(&buffer->full, &buffer->mutex);

		if ((buffer->buf[buffer->consumer_index].dh_flag == true) &&
			dh_flag_in_use) {
			if (id == 1) {
				/* Consume if DH FLAG is set only for Consumer 1
				 * and for any other consumer we just pass on
				 * and ignore the packet until consumer 1 is
				 * able to process
				 */
				core_consume(buffer, id, max);
				pthread_cond_broadcast(&buffer->empty);
				pthread_mutex_unlock(&buffer->mutex);
			}
			else {
				pthread_cond_broadcast(&buffer->empty);
				pthread_mutex_unlock(&buffer->mutex);
			}
		}
		else {
				/* Regular packet can be processed by any
				 * consumer regardless of its ID
				 */
				core_consume(buffer, id, max);
				pthread_cond_broadcast(&buffer->empty);
				pthread_mutex_unlock(&buffer->mutex);
		}
		sleep(1);
	}

	return NULL;
}

int main(int argc, char *argv[]) {

	int i, num_prod, num_cons;
	int max_packet_per_sec_prod = 10;
	int max_packet_per_sec_cons = 5;
	int ret = 0;
	num_prod = 2;
	num_cons = 4;
	pthread_t *prod_threads = NULL;
	pthread_t *cons_threads = NULL;
	thread_info *cons_info = NULL;
	thread_info *prod_info = NULL;

	buffer_t buffer = {
        .len = 0,
	.producer_index = 0,
	.consumer_index = 0,
	.capacity = 40,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .empty = PTHREAD_COND_INITIALIZER,
        .full = PTHREAD_COND_INITIALIZER
	};

	dh_flag_in_use = true;

	buffer.buf = malloc(buffer.capacity*sizeof(packet_t));
	if (!buffer.buf) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto exit;
	}

	prod_threads = malloc(num_prod*sizeof(pthread_t));
	if (!prod_threads) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_buf;
	}

	cons_threads = malloc(num_cons*sizeof(pthread_t));
	if (!cons_threads) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_prod;
	}

	prod_info = malloc(num_prod * sizeof(thread_info));
	if (!prod_info) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_cons;
	}

	cons_info = malloc(num_cons * sizeof(thread_info));
	if (!cons_info) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_prod_info;
	}

	/* Init random seq generator for packets
	 * and for dh_flag
	 */
	srand(0);

	for (i = 0; i < num_prod; i++) {
		prod_info[i].bptr = &buffer;
		prod_info[i].id = i+1;
		prod_info[i].max_packet = max_packet_per_sec_prod;
	}

	for (i = 0; i < num_cons; i++) {
		cons_info[i].bptr = &buffer;
		cons_info[i].id = i+1;
		cons_info[i].max_packet = max_packet_per_sec_cons;
	}

	for (i = 0; i < num_prod; i++) {
		if (pthread_create(&prod_threads[i], NULL, producer,
				   (void*)&prod_info[i])){
			  printf("Unable to procure memory\n");
			  ret = -1;
			  goto free_all;
		}
	}

	for (i = 0; i < num_cons; i++) {
		if (pthread_create(&cons_threads[i], NULL, consumer,
				   (void*)&cons_info[i])){
			  printf("Unable to procure memory\n");
			  ret = -1;
			  goto free_all;
		}
	}

	for (i = 0; i < num_prod; i++)
		pthread_join(prod_threads[i], NULL);
	for (i = 0; i < num_cons; i++)
		pthread_join(cons_threads[i], NULL);

free_all:
	free(cons_info);
free_prod_info:
	free(prod_info);
free_cons:
	free(cons_threads);
free_prod:
	free(prod_threads);
free_buf:
	free(buffer.buf);
exit:
	return ret;
}
