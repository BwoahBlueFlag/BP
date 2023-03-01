#include <logging/log.h>
#include <deca_probe_interface.h>

#include "DSTWR.h"

LOG_MODULE_REGISTER(main);

#define RANGING_INTERVAL 1000

//#define INITIATOR

void printResult(struct DSTWRResult result) {
	double firstLoopDuration = (double)(result.rx2 - result.tx1);
	double firstProcessingDuration = (double)(result.tx2 - result.rx1);
	double secondLoopDuration = (double)(result.rx3 - result.tx2);
	double secondProcessingDuration = (double)(result.tx3 - result.rx2);

	double timeOfFlight = (firstLoopDuration * secondLoopDuration - firstProcessingDuration * secondProcessingDuration) /
				(firstLoopDuration + secondLoopDuration + firstProcessingDuration + secondProcessingDuration) * DWT_TIME_UNITS;

	double distance = timeOfFlight * SPEED_OF_LIGHT;

	printf("2Distance = %3.2f m\n", distance);

	responder();
}

void repeatRanging() {
	k_msleep(RANGING_INTERVAL);
	initiator();
}

void main(void) {
#ifdef INITIATOR
	setInitiatorDone(repeatRanging);
#else
	setResultProcessor(printResult);
#endif

	if (initializeUWB()) {
#ifdef INITIATOR
		initiatorStart();
#else
		responderStart();
#endif
	} else {
		LOG_ERR("Initialization failed");
	}
}
