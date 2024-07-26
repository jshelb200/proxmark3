#ifndef PYCLIENT_HANDLER_H
#define PYCLIENT_HANDLER_H

#include "common.h"
// Pour le client Python
#define TX_COMMANDNG_PREAMBLE_MAGIC_PY 0x50796D33 // b"Pym3"
#define TX_COMMANDNG_POSTAMBLE_MAGIC_PY 0x7933 // b"y3"  
#define PYCLIENT_MAX_MSG_SIZE 256
void handlePyClientSim(uint8_t* pupi);
int notify_middleware(PacketCommandNG* packet);

typedef struct  {
    uint8_t* data;
    uint16_t len;
    bool ok;
}pycliresp_t;

extern pycliresp_t pyresp;



#endif // PYCLIENT_HANDLER_H
