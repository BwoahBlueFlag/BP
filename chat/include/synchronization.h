#ifndef MJ_SYNCHRONIZATION
#define MJ_SYNCHRONIZATION

#include <stdint.h>

void broadcastMaster();
void getClockDelta(uint16_t firstNodeAddress, uint16_t secondNodeAddress);
void messageHandler(const uint8_t* message, uint16_t senderAddress);

#endif