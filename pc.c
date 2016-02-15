#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <sys/syscall.h>

#include "private_data.h"

#define DH_CONSUMER	1

/*Used instead of rand() to allow easy debugging of data packets
 * and track production and consumption to ensure
 * zero packet loss
 */
unsigned int packet_data = 0;

/* Only static global variable used to
 * define state of the DH flag
 */
static int dh_flag_in_use = 0;

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

void print_usage(void)
{
	printf("Usage: ./pc \n\t-p <Num of producer threads>\n\t–c <Num of comsumer threads>\n\t--pn <Max packets produced per second>\n\t--pc <Max packets consumed per second>\n\t-q <Max size of shared buffer>\n\t-dh <Optional: Enable DH flag for consumer 1>\nExample Usage\n./pc -p 1 -c 2 --pn 20 --pc 5 -q 40 -dh\n");
}

int main(int argc, char *argv[]) {

	int i;
	int num_prod = 0;
	int num_cons = 0;
	int max_packet_per_sec_prod = 0;
	int max_packet_per_sec_cons = 0;
	int ret = 0;
	int opt;
	size_t max_buf_size = 5;
	extern char * optarg;
	int long_index = 0;

	pthread_t *prod_threads = NULL;
	pthread_t *cons_threads = NULL;
	thread_info *cons_info = NULL;
	thread_info *prod_info = NULL;

	static struct option long_options[] = {
		{"dh",          no_argument, &dh_flag_in_use,  1 },
		{"producer",	required_argument, 0,  'p' },
		{"consumer",	required_argument, 0,  'c' },
		{"pn",		required_argument, 0,  'x' },
		{"pc",		required_argument, 0,  'y' },
		{"que",		required_argument, 0,  'q' },
		{0,           0,                 0,  0   }
	};

	while ((opt = getopt_long(argc, argv,"p:c:x:y:q:d::",
				  long_options, &long_index )) != -1) {
		if (opt == -1)
			break;
		switch(opt) {
			case 'p': num_prod = atoi(optarg);
				  break;
			case 'c': num_cons = atoi(optarg);
				  break;
			case 'x': max_packet_per_sec_prod = atoi(optarg);
				  break;
			case 'y': max_packet_per_sec_cons = atoi(optarg);
				  break;
			case 'q': max_buf_size = atoi(optarg);
				  break;
			case 'd': dh_flag_in_use = true;
				  break;
			default: print_usage();
				 return -1;
		}
	}

	/* Do not accept input if any of the folowing are NULL
	 * Number of proucers
	 * Number of consumers
	 * Max size of shared Buffer
	 * Packet per second production/consumption
	 */
	if (!num_prod || !num_cons || !max_packet_per_sec_prod ||
	     !max_packet_per_sec_cons || !max_buf_size) {
		print_usage();
		return -1;
	}

	/* Static buffer */
	buffer_t buffer = {
        .len = 0,
	.producer_index = 0,
	.consumer_index = 0,
	.capacity = max_buf_size,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .empty = PTHREAD_COND_INITIALIZER,
        .full = PTHREAD_COND_INITIALIZER
	};

	/* Dynamic buffer array allocation */
	buffer.buf = malloc(buffer.capacity * sizeof(packet_t));
	if (!buffer.buf) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto exit;
	}

	/* Producer thread allocation */
	prod_threads = malloc(num_prod * sizeof(pthread_t));
	if (!prod_threads) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_buf;
	}

	/* Consumer thread allocation */
	cons_threads = malloc(num_cons * sizeof(pthread_t));
	if (!cons_threads) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_prod;
	}

	/* Producer thread info allocations */
	prod_info = malloc(num_prod * sizeof(thread_info));
	if (!prod_info) {
		printf("Unable to procure memory\n");
		ret = -1;
		goto free_cons;
	}

	/* Consumer thread info allocation */
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

	/* Init and start producers and consumers */
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

	/* Wait for threads to exit and join back
	 * In regular operation run the execution
	 * should never reach here.
	 */
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
