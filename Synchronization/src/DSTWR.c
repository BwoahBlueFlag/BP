#include <dw3000_hw.h>
#include <dw3000_spi.h>
#include <deca_probe_interface.h>
#include <logging/log.h>

#include "UWBFrame.h"
#include "DSTWR.h"

#define INITIATOR_ADDRESS 0x4556
#define RESPONDER_ADDRESS 0x4157

#define DUMMY_ANTENNA_DELAY 16385

#define UUS_TO_DWT_TIME 63898

#define RX_DELAY 700
#define TX_DELAY 3400
#define RX_TIME_OUT 0
#define PREAMBLE_TIME_OUT 65000

LOG_MODULE_REGISTER(DSTWR);

static dwt_config_t config = {
	5,                /* Channel number. */
	DWT_PLEN_128,     /* Preamble length. Used in TX only. */
	DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
	9,                /* TX preamble code. Used in TX only. */
	9,                /* RX preamble code. Used in RX only. */
	1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
	DWT_BR_6M8,       /* Data rate. */
	DWT_PHRMODE_STD,  /* PHY header mode. */
	DWT_PHRRATE_STD,  /* PHY header rate. */
	(129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
	DWT_STS_MODE_OFF, /* STS disabled */
	DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
	DWT_PDOA_M0       /* PDOA mode off */
};

static dwt_txconfig_t txconfig_options = {
	0x34,
	0xfdfdfdfd,
	0x0
};

void (*initiatorDone)();
void setInitiatorDone(void (*function)()) {
	initiatorDone = function;
}

void (*resultProcessor)(struct DSTWRResult);
void setResultProcessor(void (*function)(struct DSTWRResult)) {
	resultProcessor = function;
}

uint8_t sequenceNumber;

struct UWBFrame firstTxFrame = {
	.frameControl = 0x8841,
	.panId = 0xDECA,
	.destinationAddress = RESPONDER_ADDRESS,
	.sourceAddress = INITIATOR_ADDRESS,
	.functionCode = 0x21
};

bool initializeUWB() {
	if (dw3000_hw_init() != 0) {
		LOG_ERR("Initialization of UWB chip HW failed");
		return false;
	}

	dw3000_spi_speed_fast();
	dw3000_hw_reset();

	if (dwt_probe((struct dwt_probe_s*)&dw3000_probe_interf) != DWT_SUCCESS) {
		LOG_ERR("Probe failed");
		return false;
	}

	k_msleep(2);

	while (!dwt_checkidlerc()) {};

	if (dwt_initialise(DWT_DW_INIT) != DWT_SUCCESS) {
		LOG_ERR("Initialization failed");
		return false;
	}

	if (dwt_configure(&config) != DWT_SUCCESS) {
		LOG_ERR("Configuration failed");
		return false;
	}

	dwt_configuretxrf(&txconfig_options);

	if (dw3000_hw_init_interrupt() != 0) {
		LOG_ERR("Interrupt initialization failed");
		return false;
	}
	
	dw3000_hw_interrupt_enable();

	dwt_setrxantennadelay(DUMMY_ANTENNA_DELAY);
	dwt_settxantennadelay(DUMMY_ANTENNA_DELAY);

	dwt_setrxaftertxdelay(RX_DELAY);
	dwt_setrxtimeout(RX_TIME_OUT);
	dwt_setpreambledetecttimeout(PREAMBLE_TIME_OUT);

	return true;
}

void initiator();

void initiatorTX(const dwt_cb_data_t *cb_data) {
	if (initiatorDone != NULL) {
		initiatorDone();
	}
}

void initiatorRX(const dwt_cb_data_t *cb_data) {
	uint64_t tx1TimeStamp;
	uint64_t rxTimeStamp;
	uint64_t tx2TimeStamp;

	struct UWBDelayDataFrame secondTxFrame = { .baseFrame = firstTxFrame };
	secondTxFrame.baseFrame.functionCode = 0x23;

	struct UWBResponseFrame rxFrame;

	uint32_t tx2Time;

	if (dwt_getframelength() >= sizeof(rxFrame)) {
		dwt_readrxdata(&rxFrame, sizeof(rxFrame), 0);

		if (
			rxFrame.baseFrame.frameControl == 0x8841 &&
			rxFrame.baseFrame.panId == 0xDECA &&
			rxFrame.baseFrame.destinationAddress == INITIATOR_ADDRESS &&
			rxFrame.baseFrame.sourceAddress == RESPONDER_ADDRESS &&
			rxFrame.baseFrame.functionCode == 0x10 &&
			rxFrame.activityCode == 0x02
		) {
			tx1TimeStamp = 0;
			tx1TimeStamp |= dwt_readtxtimestamphi32();
			tx1TimeStamp <<= 8;
			tx1TimeStamp |= dwt_readtxtimestamplo32();

			rxTimeStamp = 0;
			rxTimeStamp |= dwt_readrxtimestamphi32();
			rxTimeStamp <<= 8;
			rxTimeStamp |= dwt_readrxtimestamplo32();

			tx2Time = (rxTimeStamp + (TX_DELAY * UUS_TO_DWT_TIME)) >> 8;

			dwt_setdelayedtrxtime(tx2Time);

			tx2TimeStamp = (((uint64_t)(tx2Time & 0xFFFFFFFEUL)) << 8) + DUMMY_ANTENNA_DELAY;

			secondTxFrame.tx1TimeStamp = (uint32_t)tx1TimeStamp;
			secondTxFrame.rxTimeStamp = (uint32_t)rxTimeStamp;
			secondTxFrame.tx2TimeStamp = (uint32_t)tx2TimeStamp;

			secondTxFrame.baseFrame.sequenceNumber = sequenceNumber++;

			dwt_writetxdata(sizeof(secondTxFrame), &secondTxFrame, 0);
			dwt_writetxfctrl(sizeof(secondTxFrame) + FCS_LEN, 0, 1);

			dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK, 0, DWT_ENABLE_INT_ONLY);

			if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
				return;
			}
		}
	}

	initiator();
}

void initiatorRXFault(const dwt_cb_data_t *cb_data) {
	initiator();
}

void initiatorStart() {
	dwt_setcallbacks(initiatorTX, initiatorRX, initiatorRXFault, initiatorRXFault, NULL, NULL, NULL);
	
	dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

	initiator();
}

void initiator() {
	dwt_setinterrupt(DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR, 0, DWT_ENABLE_INT_ONLY);

	do {
		firstTxFrame.sequenceNumber = sequenceNumber++;

		dwt_writetxdata(sizeof(firstTxFrame), &firstTxFrame, 0);
		dwt_writetxfctrl(sizeof(firstTxFrame) + FCS_LEN, 0, 1);
	} while(dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) == DWT_ERROR);
}

void responderRX(const dwt_cb_data_t *cb_data);
void responderRXFault(const dwt_cb_data_t *cb_data);

void responder() {
	dwt_setcallbacks(NULL, responderRX, responderRXFault, responderRXFault, NULL, NULL, NULL);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

uint64_t firstRxTimeStamp;

void responderSecondRX(const dwt_cb_data_t *cb_data) {
	struct UWBDelayDataFrame secondRxFrame;
	uint64_t txTimeStamp;
	uint64_t secondRxTimeStamp;

	sequenceNumber++;

	if (dwt_getframelength() >= sizeof(secondRxFrame)) {
		dwt_readrxdata(&secondRxFrame, sizeof(secondRxFrame), 0);
		if (
			secondRxFrame.baseFrame.frameControl == 0x8841 &&
			secondRxFrame.baseFrame.panId == 0xDECA &&
			secondRxFrame.baseFrame.destinationAddress == RESPONDER_ADDRESS &&
			secondRxFrame.baseFrame.sourceAddress == INITIATOR_ADDRESS &&
			secondRxFrame.baseFrame.functionCode == 0x23
		) {
			txTimeStamp = 0;
			txTimeStamp |= dwt_readtxtimestamphi32();
			txTimeStamp <<= 8;
			txTimeStamp |= dwt_readtxtimestamplo32();

			secondRxTimeStamp = 0;
			secondRxTimeStamp |= dwt_readrxtimestamphi32();
			secondRxTimeStamp <<= 8;
			secondRxTimeStamp |= dwt_readrxtimestamplo32();

			if (resultProcessor != NULL) {
				resultProcessor((struct DSTWRResult){secondRxFrame.tx1TimeStamp, firstRxTimeStamp, txTimeStamp, secondRxFrame.rxTimeStamp, secondRxFrame.tx2TimeStamp, secondRxTimeStamp});
			}

			return;
		}
	}

	responder();
}

void responderRX(const dwt_cb_data_t *cb_data) {
	struct UWBFrame firstRxFrame;
	uint64_t txTime;

	struct UWBResponseFrame txFrame = {
		.baseFrame = {
			.frameControl = 0x8841,
			.panId = 0xDECA,
			.destinationAddress = INITIATOR_ADDRESS,
			.sourceAddress = RESPONDER_ADDRESS,
			.functionCode = 0x10
		},
		.activityCode = 0x02
	};

	if (dwt_getframelength() >= sizeof(firstRxFrame)) {
		dwt_readrxdata(&firstRxFrame, sizeof(firstRxFrame), 0);
		if (
			firstRxFrame.frameControl == 0x8841 &&
			firstRxFrame.panId == 0xDECA &&
			firstRxFrame.destinationAddress == RESPONDER_ADDRESS &&
			firstRxFrame.sourceAddress == INITIATOR_ADDRESS &&
			firstRxFrame.functionCode == 0x21
		) {
			firstRxTimeStamp = 0;
			firstRxTimeStamp |= dwt_readrxtimestamphi32();
			firstRxTimeStamp <<= 8;
			firstRxTimeStamp |= dwt_readrxtimestamplo32();

			txTime = (firstRxTimeStamp + (TX_DELAY * UUS_TO_DWT_TIME)) >> 8;

			dwt_setdelayedtrxtime(txTime);

			txFrame.baseFrame.sequenceNumber = sequenceNumber;

			dwt_writetxdata(sizeof(txFrame), &txFrame, 0);
			dwt_writetxfctrl(sizeof(txFrame) + FCS_LEN, 0, 1);

			dwt_setcallbacks(NULL, responderSecondRX, responderRXFault, responderRXFault, NULL, NULL, NULL);

			if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_ERROR) {
				return;
			}
		}
	}

	responder();
}

void responderRXFault(const dwt_cb_data_t *cb_data) {
	responder();
}

void responderStart() {
	dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setinterrupt(DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR, 0, DWT_ENABLE_INT_ONLY);
	responder();
}