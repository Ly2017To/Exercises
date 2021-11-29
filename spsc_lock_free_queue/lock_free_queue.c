//Note: this is single producer single consumer lock free queue implemented as a ring buffer
//the producer thread only modifies the write index
//the consumer thread only modifies the read index
//reference:https://www.cs.fsu.edu/~baker/realtime/restricted/examples/prodcons/prodcons1.c

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <values.h>
#include <errno.h>
#include <sched.h>

typedef struct sharebuff{
	int size;
	int numElements;
	int *b;
	int in;
	int out;
}sharebuff_t;

pthread_t consumer;
pthread_t producer;

sharebuff_t cirbuff;

sharebuff_t ini_sharebuff(){
	sharebuff_t cirbuff;

	cirbuff.size=8; //should be a power of 2
	cirbuff.numElements=cirbuff.size-1;
	cirbuff.b=(int *)malloc(cirbuff.size*sizeof(int));
	if(cirbuff.b==NULL){
		printf("malloc error\n");
		exit(-1);
	}

	cirbuff.in=0;
	cirbuff.out=0;

	return cirbuff;
}

void * consumer_body (void *arg) {

	int tmp = 0; //variable to take out from the queue

	printf("consumer thread starts\n");

	for (;;) {
		printf("consumer: in=%d, out=%d\n", cirbuff.in, cirbuff.out);
		while (cirbuff.in == cirbuff.out){
			//printf("here consumer_body\n");
			//printf("consumer: in=%d\n, out=%d\n", cirbuff.in, cirbuff.out);
			sched_yield();
		}
		tmp = cirbuff.b[cirbuff.out];
		//cirbuff.out = (cirbuff.out + 1) % cirbuff.size;
		cirbuff.out = (cirbuff.out + 1) & cirbuff.numElements;
	}
	printf("consumer thread exits\n");

	return NULL;
}

void * producer_body (void * arg) {

	int val=0; //variable to write into the queue

	printf("producer thread starts\n");

	for (;;){

		//if(((cirbuff.in+1)% cirbuff.size) == cirbuff.out){
		if(((cirbuff.in+1) & cirbuff.numElements) == cirbuff.out){
			//printf("here producer_body\n");
			//printf("producer: in=%d\n, out=%d\n", cirbuff.in, cirbuff.out);
			sched_yield();
		}

		printf("producer : in=%d, out=%d\n", cirbuff.in, cirbuff.out);
		cirbuff.b[cirbuff.in] = val;
		//cirbuff.in = (cirbuff.in + 1) % cirbuff.size;
		cirbuff.in = (cirbuff.in + 1) & cirbuff.numElements;

		printf("producer increment index: in=%d, out=%d\n", cirbuff.in, cirbuff.out);
	}

	return NULL;
}

int main () {

	//initialize the circular buffer
	cirbuff=ini_sharebuff();

	int result;
	pthread_attr_t attrs;

	void *retval;

	/* use default attributes */
	pthread_attr_init (&attrs);

	/* create producer thread */
	if ((result = pthread_create (&producer, &attrs,producer_body,NULL))) {
		printf ("pthread_create: %d\n", result);
		exit (-1);
	}
	printf("producer thread created\n");

	/* create consumer thread */
	if ((result = pthread_create (&consumer,&attrs,consumer_body,NULL))) {
		printf ("pthread_create: %d\n", result);
		exit (-1);
	}
	printf("consumer threads created\n");

	printf("before sleep\n");

	sleep (10);

	printf("after sleep\n");

	//pthread_join(producer,&retval);
	//pthread_join(consumer,&retval);

	int ret=0;

	ret=pthread_join(producer,&retval);

	if (retval == PTHREAD_CANCELED){
		printf("The thread was canceled - ");
	}else{
		printf("Returned value %d - ", (int)retval);
	}

	switch (ret) {
	case 0:
		printf("The producer thread joined successfully\n");
		break;
	case EDEADLK:
		printf("Deadlock detected\n");
		break;
	case EINVAL:
		printf("The thread is not joinable\n");
		break;
	case ESRCH:
		printf("No thread with given ID is found\n");
		break;
	default:
		printf("Error occurred when joining the thread\n");
	}

	pthread_join(consumer,&retval);

	if (retval == PTHREAD_CANCELED){
		printf("The thread was canceled - ");
	}else{
		printf("Returned value %d - ", (int)retval);
	}

	switch (ret) {
	case 0:
		printf("The consumer thread joined successfully\n");
		break;
	case EDEADLK:
		printf("Deadlock detected\n");
		break;
	case EINVAL:
		printf("The thread is not joinable\n");
		break;
	case ESRCH:
		printf("No thread with given ID is found\n");
		break;
	default:
		printf("Error occurred when joining the thread\n");
	}

	pthread_exit(NULL);

	return 0;
}


