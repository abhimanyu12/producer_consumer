#ifndef PROC_COM_H
#define PROC_COM_H

/* Packet type
 * boolean dh_flag: used for DH feature and is randomly produced
 * intger data
 */
typedef struct {
	bool dh_flag;
	int data;
} packet_t;

/* Buffer type
 * packet_t * buf: Array of packet_t with size len and max size capacity
 * integer producer_index: current index of producer for given buffer
 * integer consumer_index: current index of consumer for given buffer
 * mutex : Simple lock for sync access to buffer array
 * empty: condition variable used to resolve bounded buffer problem on buf
 * full: condition variable used to resolve bounded buffer problem on buf
 */
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

/* Thread info type
 * integer id:  Producer/Consumer thread id
 * int max_packet: maximum packet production/consumption per second
 * bptr: Handle to the shared buffer
 */
typedef struct {
	int id;
	int max_packet;
	buffer_t *bptr;
} thread_info;

#endif
