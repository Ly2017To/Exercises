//Note: this is single producer single consumer lock free queue implemented as a ring buffer
//the producer thread only modifies the write index
//the consumer thread only modifies the read index
//reference:https://www.cs.fsu.edu/~baker/realtime/restricted/examples/prodcons/prodcons1.c

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
