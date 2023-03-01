#ifndef MJ_DSTWR
#define MJ_DSTWR

#include <stdio.h>
#include <zephyr.h>

#define SPEED_OF_LIGHT 299702547

struct DSTWRResult {
	uint64_t tx1;
	uint64_t rx1;
	uint64_t tx2;
	uint64_t rx2;
	uint64_t tx3;
	uint64_t rx3;
} __attribute__((packed));

bool initializeUWB();
void responderStart();
void responder();
void initiatorStart();
void setResultProcessor(void (*function)(struct DSTWRResult));
void setInitiatorDone(void (*function)());

#endif