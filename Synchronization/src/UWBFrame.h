#ifndef UWB_FRAME
#define UWB_FRAME

#include <stdint.h>

struct UWBFrame {
    uint16_t frameControl;
    uint8_t  sequenceNumber;
    uint16_t panId;
    uint16_t destinationAddress;
    uint16_t sourceAddress;
    uint8_t  functionCode;
} __attribute__((packed));

struct UWBResponseFrame {
    struct UWBFrame baseFrame;
    uint8_t         activityCode;
} __attribute__((packed));

struct UWBDelayDataFrame {
    struct UWBFrame baseFrame;
    uint32_t        tx1TimeStamp;
    uint32_t        rxTimeStamp;
    uint32_t        tx2TimeStamp;
} __attribute__((packed));

#endif