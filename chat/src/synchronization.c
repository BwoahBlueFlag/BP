#include "synchronization.h"
#include "model_handler.h"
#include <dk_buttons_and_leds.h>

enum messageType { SetMaster, SetupSynchronization, StartSynchronization, SynchronizationResult };

volatile uint16_t masterAddress;

struct addressMessage {
	const uint8_t type;
	uint16_t address;
} __attribute__((packed));

struct resultMessage {
	const uint8_t type;
	uint64_t clockDelta;
} __attribute__((packed));

void broadcastMaster() {
	uint8_t message = SetMaster;
	sendBroadcast(&message, sizeof(message));
}

void getClockDelta(uint16_t firstNodeAddress, uint16_t secondNodeAddress) {
	struct addressMessage message = { .type = SetupSynchronization, .address = secondNodeAddress };
	sendUnicast(&message, sizeof(message), firstNodeAddress);
}

void setupSynchronization(uint16_t initiatorAddress) {
	// TODO setup UWB listening

	uint8_t message = StartSynchronization;
	sendUnicast(&message, sizeof(message), initiatorAddress);
}

void startSynchronization(uint16_t responderAddress) {
	// TODO setup UWB initiator

	struct resultMessage message = { .type = SynchronizationResult, .clockDelta = 420 };
	sendUnicast(&message, sizeof(message), masterAddress);
}

void messageHandler(const uint8_t* message, uint16_t senderAddress) {
	switch (*message) {
	case SetMaster:
		masterAddress = senderAddress;
		dk_set_led(0, 1); //DEBUG
		break;
	case SetupSynchronization:
		setupSynchronization(((struct addressMessage*)message)->address);
		break;
	case StartSynchronization:
		startSynchronization(senderAddress);
		break;
	case SynchronizationResult:
		printk("Result: %llu\n", ((struct resultMessage*)message)->clockDelta);
		break;
	}
}