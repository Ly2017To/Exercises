//Note: this is single producer single consumer lock free queue implemented as a ring buffer
//the producer thread only modifies the write index
//the consumer thread only modifies the read index
//reference:https://www.cs.fsu.edu/~baker/realtime/restricted/examples/prodcons/prodcons1.c

#define BUFFER_SIZE 128
#define NUM_MESSAGES 10

typedef struct {
	struct MessageInCirBuf * buffer;
	volatile size_t head;
	volatile size_t tail;
	size_t size; //2^n
	size_t numElements; 
}CircularBufferLockFree;

int circularBufInitLockFree(CircularBufferLockFree *cbuf, int size) {
	int r = -1;
	cbuf->size = size;
	cbuf->numElements = size-1;
	cbuf->buffer = malloc(cbuf->size * sizeof(struct MessageInCirBuf));
	if(cbuf->buffer == NULL){
		printf("[Error] CircularBuf - circularBufInit_lockfree - Not enough memory, ptrInfoMsg\n");
		return r;
	}
    if(circularBufResetLockFree(cbuf) == -1) {
        r = -1;
    }
    else {
        r = 0;
    }
    return r;
}

void circularBufFreeLockFree(CircularBufferLockFree *cbuf) {
    free(cbuf->buffer);
}

int circularBufResetLockFree(CircularBufferLockFree *cbuf) {
	int r = -1;
	if(cbuf){
		cbuf->head = 0;
		cbuf->tail = 0;
		r = 0;
	}
	return r;
}

int isCircularBufEmptyLockFree(CircularBufferLockFree cbuf) {
	return (cbuf.head == cbuf.tail);
}

int isCircularBufFullLockFree(CircularBufferLockFree cbuf) {
	return ((cbuf.head + 1) & cbuf.numElements) == cbuf.tail;
}

int circularBufPutLockFree(CircularBufferLockFree * cbuf, struct MessageInCirBuf mMsg) {
	if(cbuf && !isCircularBufFullLockFree(*cbuf)) {
		cbuf->buffer[cbuf->head]  = mMsg;
		cbuf->head = (cbuf->head + 1) & cbuf->numElements;
		return 0;
	}
	return -1;
}

int circularBufGetLockFree(CircularBufferLockFree * cbuf, struct MessageInCirBuf * mMsg) {
	//int r = -1;
	if(cbuf && mMsg && !isCircularBufEmptyLockFree(*cbuf)) {
		*mMsg = cbuf->buffer[cbuf->tail];
		cbuf->tail = (cbuf->tail + 1) & cbuf->numElements;
		return 0;
	}
	return -1;
}

// Producer thread function
void* producer(void* arg) {
    CircularBufferLockFree* cbuf = (CircularBufferLockFree*) arg;
    struct MessageInCirBuf msg;
    for (int i = 0; i < NUM_MESSAGES; i++) {
        msg.data = i;
        while (circularBufPutLockFree(cbuf, msg) == -1) {
            // If buffer is full, wait for consumer to consume some data
            usleep(100);
        }
        printf("Produced: %d\n", msg.data);
    }
    return NULL;
}

// Consumer thread function
void* consumer(void* arg) {
    CircularBufferLockFree* cbuf = (CircularBufferLockFree*) arg;
    struct MessageInCirBuf msg;
    for (int i = 0; i < NUM_MESSAGES; i++) {
        while (circularBufGetLockFree(cbuf, &msg) == -1) {
            // If buffer is empty, wait for producer to produce some data
            usleep(100);
        }
        printf("Consumed: %d\n", msg.data);
    }
    return NULL;
}

int main() {
    CircularBufferLockFree cbuf;

    // Initialize circular buffer
    if (circularBufInitLockFree(&cbuf, BUFFER_SIZE) != 0) {
        printf("Error initializing buffer\n");
        return -1;
    }

    pthread_t prod_thread, cons_thread;

    // Create producer and consumer threads
    pthread_create(&prod_thread, NULL, producer, (void*)&cbuf);
    pthread_create(&cons_thread, NULL, consumer, (void*)&cbuf);

    // Wait for both threads to finish
    pthread_join(prod_thread, NULL);
    pthread_join(cons_thread, NULL);

    // Free allocated memory for the buffer
    circularBufFreeLockFree(&cbuf);

    return 0;
}
