#include <zephyr.h>
#include <dw3000_hw.h>
#include <dw3000_spi.h>
#include <deca_probe_interface.h>

#define DUMMY_ANTENNA_DELAY 16385

#define UUS_TO_DWT_TIME 63898

#define RX_DELAY 700
#define TX_DELAY 3400
#define RX_TIME_OUT 0
#define PREAMBLE_TIME_OUT 65000

#define RANGING_INTERVAL 1000

#define BUFFER_SIZE 20
static uint8_t buffer[BUFFER_SIZE];

static uint8_t tx_poll_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21 };
static uint8_t rx_resp_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0 };
static uint8_t tx_final_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

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

uint64_t get_tx_timestamp_u64() {
    uint8_t ts_tab[5];
    uint64_t ts = 0;
    int8_t i;
    dwt_readtxtimestamp(ts_tab);
    for (i = 4; i >= 0; i--)
    {
        ts <<= 8;
        ts |= ts_tab[i];
    }
    return ts;
}

uint64_t get_rx_timestamp_u64() {
    uint8_t ts_tab[5];
    uint64_t ts = 0;
    int8_t i;
    dwt_readrxtimestamp(ts_tab);
    for (i = 4; i >= 0; i--)
    {
        ts <<= 8;
        ts |= ts_tab[i];
    }
    return ts;
}

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

	uint8_t sequenceNumber = 0;
	uint16_t frameSize;
	uint32_t status;
	uint64_t tx1TimeStamp;
	uint64_t rxTimeStamp;
	uint64_t tx2TimeStamp;

	while (true) {
		tx_poll_msg[2] = sequenceNumber;
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);  /* Zero offset in TX buffer. */
        dwt_writetxfctrl(sizeof(tx_poll_msg) + FCS_LEN, 0, 1); /* Zero offset in TX buffer, ranging. */

		dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

		while (!((status = dwt_readsysstatuslo()) & (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {}

		sequenceNumber++;

		if (status & DWT_INT_RXFCG_BIT_MASK) {
			dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

			frameSize = dwt_getframelength();
            if (frameSize <= BUFFER_SIZE)
                dwt_readrxdata(buffer, frameSize, 0);

			buffer[2] = 0;
            if (memcmp(buffer, rx_resp_msg, 10) == 0) {
				uint32_t final_tx_time;

                tx1TimeStamp = get_tx_timestamp_u64();
                rxTimeStamp = get_rx_timestamp_u64();

                final_tx_time = (rxTimeStamp + (TX_DELAY * UUS_TO_DWT_TIME)) >> 8;
                dwt_setdelayedtrxtime(final_tx_time);

                tx2TimeStamp = (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + DUMMY_ANTENNA_DELAY;

				uint32_t* assigner = &(tx_final_msg[10]);
				*assigner = (uint32_t)tx1TimeStamp;
				assigner = &(tx_final_msg[14]);
				*assigner = (uint32_t)rxTimeStamp;
				assigner = &(tx_final_msg[18]);
				*assigner = (uint32_t)tx2TimeStamp;

                tx_final_msg[2] = sequenceNumber;
                dwt_writetxdata(sizeof(tx_final_msg), tx_final_msg, 0);
                dwt_writetxfctrl(sizeof(tx_final_msg) + FCS_LEN, 0, 1);

                if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS)
                {
					while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) {}

                    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                    sequenceNumber++;
                }
			}
		} else {
			dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_TXFRS_BIT_MASK);
		}

		k_msleep(RANGING_INTERVAL);
	}
}
