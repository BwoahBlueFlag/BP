#include <zephyr.h>
#include <dw3000_hw.h>
#include <dw3000_spi.h>
#include <deca_probe_interface.h>
#include "UWBFrame.h"

//#define INITIATOR

#define SPEED_OF_LIGHT 299702547

#define INITIATOR_ADDRESS 0x4556
#define RESPONDER_ADDRESS 0x4157

#define DUMMY_ANTENNA_DELAY 16385

#define UUS_TO_DWT_TIME 63898

#define RX_DELAY 700
#define TX_DELAY 3400
#define RX_TIME_OUT 0
#define PREAMBLE_TIME_OUT 65000

#define RANGING_INTERVAL 1000

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

void main(void) {
	dw3000_hw_init();
	dw3000_spi_speed_fast();
	dw3000_hw_reset();
	k_msleep(2);

	if (dwt_probe((struct dwt_probe_s*)&dw3000_probe_interf) != DWT_SUCCESS) {
		printk("Probe failed");
		return;
	}

	while (!dwt_checkidlerc()) {};

	if (dwt_initialise(DWT_DW_INIT) != DWT_SUCCESS) {
        printk("Initialisation Failed");
		return;
    }

	if (dwt_configure(&config) != DWT_SUCCESS) {
        printk("Configuration Failed");
		return;
    }

    dwt_configuretxrf(&txconfig_options);

	dwt_setrxantennadelay(DUMMY_ANTENNA_DELAY);
    dwt_settxantennadelay(DUMMY_ANTENNA_DELAY);

	dwt_setrxaftertxdelay(RX_DELAY);
    dwt_setrxtimeout(RX_TIME_OUT);
    dwt_setpreambledetecttimeout(PREAMBLE_TIME_OUT);

    uint32_t status;
    uint8_t sequenceNumber = 0;

#ifdef INITIATOR
	uint64_t tx1TimeStamp;
	uint64_t rxTimeStamp;
	uint64_t tx2TimeStamp;

    struct UWBFrame firstTxFrame = {
        .frameControl = 0x8841,
        .panId = 0xDECA,
        .destinationAddress = RESPONDER_ADDRESS,
        .sourceAddress = INITIATOR_ADRRESS,
        .functionCode = 0x21
    };

    struct UWBDelayDataFrame secondTxFrame = { .baseFrame = firstTxFrame };
    secondTxFrame.baseFrame.functionCode = 0x23;

    struct UWBResponseFrame rxFrame;

    uint32_t tx2Time;

	while (true) {
        firstTxFrame.sequenceNumber = sequenceNumber++;
        
        dwt_writetxdata(sizeof(firstTxFrame), &firstTxFrame, 0);
        dwt_writetxfctrl(sizeof(firstTxFrame) + FCS_LEN, 0, 1);

		dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

		while (!((status = dwt_readsysstatuslo()) & (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {}

		if (status & DWT_INT_RXFCG_BIT_MASK) {
			dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

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

                    if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
                        while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) {}

                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                    }
                }
            }
		} else
			dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_TXFRS_BIT_MASK);

		k_msleep(RANGING_INTERVAL);
	}
#else
    struct UWBFrame firstRxFrame;
    struct UWBDelayDataFrame secondRxFrame;
    uint64_t firstRxTimeStamp;
    uint64_t txTimeStamp;
    uint64_t secondRxTimeStamp;
    uint64_t txTime;
    double firstLoopDuration;
    double firstProcessingDuration;
    double secondLoopDuration;
    double secondProcessingDuration;
    double timeOfFlight;
    double distance;

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

    while (true) {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        while (!((status = dwt_readsysstatuslo()) & (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {}
        
        if (status & DWT_INT_RXFCG_BIT_MASK) {
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

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
                    
                    if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_ERROR) {
                        while (!((status = dwt_readsysstatuslo()) & (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {}

                        sequenceNumber++;

                        if (status & DWT_INT_RXFCG_BIT_MASK) {
                            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

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

                                    firstLoopDuration = (double)(secondRxFrame.rxTimeStamp - secondRxFrame.tx1TimeStamp);
                                    firstProcessingDuration = (double)(((uint32_t)txTimeStamp) - ((uint32_t)firstRxTimeStamp));
                                    secondLoopDuration = (double)(((uint32_t)secondRxTimeStamp) - ((uint32_t)txTimeStamp));
                                    secondProcessingDuration = (double)(secondRxFrame.tx2TimeStamp - secondRxFrame.rxTimeStamp);

                                    timeOfFlight = (firstLoopDuration * secondLoopDuration - firstProcessingDuration * secondProcessingDuration) /
                                                   (firstLoopDuration + secondLoopDuration + firstProcessingDuration + secondProcessingDuration) *
                                                   DWT_TIME_UNITS;

                                    distance = timeOfFlight * SPEED_OF_LIGHT;

                                    printf("Distance = %3.2f m\n", distance);
                                }
                            }
                        } else
                            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                    }
                }
            }
        } else
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    }
#endif
}
