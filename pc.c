#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>


#define BUF_SIZE 40

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
	buffer_t *bptr;
} thread_info;

bool dh_flag_in_use = false;

void core_consume(buffer_t *buffer, int id)
{

	printf("\tConsumed by %d ----> %d and flag is %d\n",id,
		buffer->buf[buffer->consumer_index].data,
		buffer->buf[buffer->consumer_index].dh_flag);
	buffer->consumer_index = (buffer->consumer_index + 1) %
				buffer->capacity;
	buffer->len = buffer->len - 1;
}

void core_produce(buffer_t *buffer, int id)
{
	buffer->buf[buffer->producer_index].data = rand();
	buffer->buf[buffer->producer_index].dh_flag = rand()%2;
	printf("Produced by %u ----> %d and flag is %d\n",id,
		buffer->buf[buffer->producer_index].data,
		buffer->buf[buffer->producer_index].dh_flag);
	buffer->producer_index = (buffer->producer_index + 1) %
		buffer->capacity;
	buffer->len++;
}

void* producer(void *arg) {

	int id;
	thread_info *prod = (thread_info*)arg;
	buffer_t *buffer = prod->bptr;
	id = prod->id;

	while(1) {

		usleep(1000);
		pthread_mutex_lock(&buffer->mutex);

		while(buffer->len == BUF_SIZE)
			pthread_cond_wait(&buffer->empty, &buffer->mutex);
		core_produce(buffer, id);
		pthread_cond_broadcast(&buffer->full);
		pthread_mutex_unlock(&buffer->mutex);
	}

	return NULL;
}

void* consumer(void *arg) {

	int id;
	thread_info *cons = (thread_info*)arg;
	buffer_t *buffer = cons->bptr;
	id = cons->id;

	while(1) {
		usleep(100000);
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
				core_consume(buffer, id);
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
				core_consume(buffer, id);
				pthread_cond_broadcast(&buffer->empty);
				pthread_mutex_unlock(&buffer->mutex);
		}
	}

	return NULL;
}

int main(int argc, char *argv[]) {

	int i, num_prod, num_cons;
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
	.capacity = BUF_SIZE,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .empty = PTHREAD_COND_INITIALIZER,
        .full = PTHREAD_COND_INITIALIZER
	};

	dh_flag_in_use = true;

	buffer.buf = malloc(buffer.capacity*sizeof(packet_t));
	if (!buffer.buf) {
		printf("Unable to procure memory\n");
		return -1;
	}

	prod_threads = malloc(num_prod*sizeof(pthread_t));
	if (!prod_threads) {
		printf("Unable to procure memory\n");
		return -1;
	}

	cons_threads = malloc(num_cons*sizeof(pthread_t));
	if (!cons_threads) {
		printf("Unable to procure memory\n");
		return -1;
	}

	prod_info = malloc(num_prod * sizeof(thread_info));
	if (!prod_info) {
		printf("Unable to procure memory\n");
		return -1;
	}

	cons_info = malloc(num_cons * sizeof(thread_info));
	if (!cons_info) {
		printf("Unable to procure memory\n");
		return -1;
	}

	/* Init random seq generator for packets
	 * and for dh_flag
	 */
	srand(0);

	for (i = 0; i < num_prod; i++) {
		prod_info[i].bptr = &buffer;
		prod_info[i].id = i+1;
	}

	for (i = 0; i < num_cons; i++) {
		cons_info[i].bptr = &buffer;
		cons_info[i].id = i+1;
	}

	for (i = 0; i < num_prod; i++) {
		if (pthread_create(&prod_threads[i], NULL, producer,
				   (void*)&prod_info[i])){
			  printf("Unable to procure memory\n");
			  return -1;
		}
	}

	for (i = 0; i < num_cons; i++) {
		if (pthread_create(&cons_threads[i], NULL, consumer,
				   (void*)&cons_info[i])){
			  printf("Unable to procure memory\n");
			  return -1;
		}
	}

	for (i = 0; i < num_prod; i++)
		pthread_join(prod_threads[i], NULL);
	for (i = 0; i < num_cons; i++)
		pthread_join(cons_threads[i], NULL);

exit:	free(prod_threads);
	free(cons_info);
	free(cons_threads);
	free(buffer.buf);
	return 0;
}
