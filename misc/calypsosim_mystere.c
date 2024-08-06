//-----------------------------------------------------------------------------
//Author (Contrib) : Jerome EVI - SpringCard SAS 2024
// 	  www.springcard.com
//
//-----------------------------------------------------------------------------
// Calypso  Card Simulation
//-----------------------------------------------------------------------------




#include "iso14443b.h"
#include "../include/proxmark3_arm.h"
#include "../include/common.h"  // access to global variable: g_dbglevel
#include "util.h"
#include "string.h"
#include "crc16.h"
#include "../include/protocols.h"
#include "appmain.h"
#include "BigBuf.h"
#include "cmd.h"
#include "fpgaloader.h"
#include "commonutil.h"
#include "dbprint.h"
#include "ticks.h"
#include "iso14b.h"       // defines for ETU conversions
#include "iclass.h"       // picopass buffer defines
#include "calypsosim.h"    // defines for ISO14443B


//-----------------------------------------------------------------------------
// The software UART that receives commands from the reader, and its state
// variables.
//-----------------------------------------------------------------------------

static struct {
    enum {
        STATE_14B_UNSYNCD,
        STATE_14B_GOT_FALLING_EDGE_OF_SOF,
        STATE_14B_AWAITING_START_BIT,
        STATE_14B_RECEIVING_DATA
    }       state;
    uint16_t shiftReg;
    int      bitCnt;
    int      byteCnt;
    int      byteCntMax;
    int      posCnt;
    uint8_t* output;
} Uart;

static void Uart14bReset(void) {
    Uart.state = STATE_14B_UNSYNCD;
    Uart.shiftReg = 0;
    Uart.bitCnt = 0;
    Uart.byteCnt = 0;
    Uart.byteCntMax = MAX_FRAME_SIZE;
    Uart.posCnt = 0;
}

static void Uart14bInit(uint8_t* data) {
    Uart.output = data;
    Uart14bReset();
}

// Attendre le temps spécifié par FWT en microsecondes
static void waitForFWT(uint32_t FWT) {
    SpinDelayUs(FWT);
}





void SimulateCalypsoTag(const uint8_t* pupi) {


    LED_A_ON();
    // Initialize Demod and Uart structs

    uint8_t ua_buf[MAX_FRAME_SIZE] = { 0 };
    Uart14bInit(ua_buf);



    // setup device.
    FpgaDownloadAndGo(FPGA_BITSTREAM_HF);

    // connect Demodulated Signal to ADC:
    SetAdcMuxFor(GPIO_MUXSEL_HIPKD);


    // Set up the synchronous serial port
    FpgaSetupSsc(FPGA_MAJOR_MODE_HF_SIMULATOR);

    // allocate command receive buffer
    BigBuf_free_keep_EM();
    BigBuf_Clear_keep_EM();


    clear_trace();
    set_tracing(true);
    //StartCountSspClk();

    // The DMA buffer, used to stream samples from the FPGA
    dmabuf16_t* dma = get_dma16();

    // Setup and start DMA.
    if (!FpgaSetupSscDma((uint8_t*)dma->buf, DMA_BUFFER_SIZE)) {
        if (g_dbglevel > DBG_ERROR) DbpString("FpgaSetupSscDma failed. Exiting");
        switch_off();
        return;
    }


    // Count of samples received so far, so that we can include timing
    //int samples = 0;

    //uint16_t* upTo = dma->buf;



    uint8_t respATQB[] = {
    0x50,                       // Code de réponse ATQB
    0xC3, 0xF7, 0x0B, 0xF7,     // PUPI/UID
    0x00, 0x00, 0x00, 0x00,	   // Application data
    0x00,                      // Bit rate capacity
    0x71,                       // Max frame size
    0x71, 					   // FWI/Coding Options
    // 0x1E,0x37                  // CRC de l'ATQB
     0x00, 0x00                  // Calculé par la fonction AddCrc14B
    };
    // response to HLTB and ATTRIB
    static const uint8_t respOK[] = { 0x00, 0x78, 0xF0 };

    uint8_t respa[] = {
        0x02, 0x6A, 0x82, 0x4B, 0x4C
    };
    uint8_t respb[] = {
    0x03, 0x6B, 0x00, 0x55, 0xA8
    };
    uint8_t respc[] = {
    0x02, 0x6A, 0x82, 0x4B, 0x4C
    };
    uint8_t respd[] = {
    0x03, 0x6A, 0x82, 0x97, 0x16
    };
    uint8_t respe[] = {
    0xA3, 0xE9, 0x67
    };
    uint8_t respf[] = {
    0x03, 0x6B, 0x00, 0x55, 0xA8
    };

    // reponse aux commandes APDU (Based on Cardpeek)
    /*
    uint8_t VOID_REP_02[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xE9, 0x3A

    };
    uint8_t VOID_REP_03[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x00, 0x8B, 0x4D
    };
    */
    uint8_t respAID_3F04[] = { 0x02, 0x33, 0x4D, 0x4, 0x52, 0x2E, 0x49, 0x43, 0x41, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x4E, 0x5E
    };
    uint8_t respICC_0002[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x71, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xF7, 0x0B, 0xF7, 0x02, 0x50, 0x14,
        0x00, 0x01, 0x2C, 0xA7, 0x3A, 0x00, 0x90, 0x00, 0xAF, 0x4E
    };
    uint8_t respID_0003[] = {0x00}
    ;
    uint8_t respDISPLAY_2F10_A[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x8B, 0x4D
    };
    /*
    uint8_t respDISPLAY_2F10_B[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xE9, 0x3A

	};
    */
    uint8_t respTICKETING_2000_ATS[] = { 0x02, 0x85, 0x17, 0x00, 0x02, 0x00, 0x00, 0x00, 0x12,
        0x12, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, 0x15, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x90, 0x00, 0xFA, 0x62
	};
    uint8_t respAID_2004[] = { 0x02, 0xA0, 0x00, 0x00, 0x04, 0x04, 0x01, 0x25, 0x09, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x33, 0xDA
    };
    uint8_t respENVIRONMENT_2001[] = { 0x03, 0x24, 0xB9, 0x28, 0x48, 0x08, 0x06, 0x3B, 0x10, 0xB4,
        0x70, 0x00, 0x13, 0xC2, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xE1, 0x7B
    };
    /*
    uint8_t respEVENT_LOG_2010_2[] = { 0x03, 0x9C, 0x0D, 0x0E, 0x90, 0x03, 0x68, 0xA0, 0x8F, 0x08,
        0x29, 0x60, 0x25, 0xF3, 0x38, 0xA7, 0x08, 0x00, 0xE8, 0x30, 0x40, 0x00, 0x30, 0x80, 0x00, 
        0x40, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x8E, 0x0D
    };
    */

    uint8_t respEVENT_LOG_2010_1[] = { 0x02, 0x9C, 0x0D, 0x09, 0x90, 0x00, 0x38, 0xA2, 0x90, 0x14,
        0x15, 0x8D, 0x93, 0xE0, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x24, 0x25

	};
    /*
	uint8_t respEVENT_LOG_2010_3[] = { 0x03, 0x9C, 0x0D, 0x03, 0x90, 0x00, 0x68, 0xA2, 0x88, 0x19, 0x23,
        0x08, 0x8A, 0x10, 0x00, 0x90, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x90, 0x00, 0x92, 0x4B
    };
    */
    uint8_t respCONTRACTS_2020[] = { 0x02, 0x5A, 0x00, 0xE0, 0x00, 0x00, 0x51, 0x3E, 0x84, 0x43, 0x12, 0x2C,
        0xD0, 0xB3, 0xF9, 0x0F, 0xEC, 0xD0, 0xC0, 0x6D, 0x99, 0x00
    };
    /*
    uint8_t respCONTRACTS_2020_1[] = { 0x00,  0x00,  0x00,  0x00
    };
    uint8_t respCONTRACTS_2020_2[] = { 0x03, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
        0xFF, 0x90, 0x00, 0x83, 0x0E

    };
    */
    uint8_t respCONTRACTS_2030[] = { 0x02, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
        0xFF, 0x90, 0x00, 0xE1, 0x79

	};
    uint8_t respCOUNTERS_202A[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x90, 0x00, 0xDE, 0x7F

	};
    uint8_t respCOUNTERS_2069[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x00, 0x8B, 0x4D
    };
    uint8_t respSPECIAL_EVENTS_2040[] = { 0x03, 0x9A, 0x19, 0x05, 0x10, 0x00, 0x68, 0xE2, 0x89, 0xA8, 0x19, 0x23,
        0x08, 0x8A, 0x10, 0x00, 0x90, 0x40, 0x88, 0xB8, 0xF0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x72, 0x7B
    };
    uint8_t respCONTRACT_LIST_2050[] = { 0x02, 0x2C, 0x1F, 0xF2, 0x1C, 0x1F, 0xFE, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xCC, 0x81
    };

    // ATS response
    uint8_t ATS_AID_3F04[] = {0x03, 0x85, 0x17, 0x04, 0x04, 0x02, 0x10, 0x01, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x48, 0x10

    };
    uint8_t ATS_ICC_0002[] = { 0x02, 0x85, 0x17, 0x02, 0x04, 0x02, 0x1D, 0x01, 0x1F, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x77, 0x25

	};
    uint8_t ATS_ID_0003[] = { 0x03, 0x85, 0x17, 0x03, 0x04, 0x02, 0x1D, 0x01, 0x01, 0x12, 0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x5B, 0xE1

	};
    uint8_t ATS_DISPLAY_2F10[] = { 0x02, 0x85, 0x17, 0x05, 0x04, 0x02, 0x1D, 0x02, 0x1F, 0x1F, 0x1F, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x3C, 0xB4

	};
    uint8_t ATS_TICKETING_2000_ATS[] = { 0x02, 0x85, 0x17, 0x00, 0x02, 0x00, 0x00, 0x00, 0x12, 0x12, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, 0x15, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xFA, 0x62

	};
    uint8_t ATS_AID_2004[] = { 0x03, 0x85, 0x17, 0x04, 0x04, 0x02, 0x10, 0x01, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x4F, 0xDE

	};
    uint8_t ATS_ENVIRONMENT_2001[] = { 0x02, 0x85, 0x17, 0x07, 0x04, 0x02, 0x1D, 0x01, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x6A, 0x62

	};
    uint8_t ATS_EVENT_LOG_2010[] = { 0x02, 0x85, 0x17, 0x08, 0x04, 0x04, 0x1D, 0x03, 0x1F, 0x12, 0x12, 0x12, 0x01, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

	};
    uint8_t ATS_CONTRACTS_2020[] = { 0x03, 0x85, 0x17, 0x09, 0x04, 0x02, 0x1D, 0x04, 0x1F, 0x12, 0x12, 0x00, 0x01, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x54

	};
    uint8_t ATS_CONTRACTS_2030[] = { 0x03, 0x85, 0x17, 0x06, 0x04, 0x02, 0x1D, 0x04, 0x1F, 0x12, 0x12, 0x00, 0x01, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x29, 0x68

	};
    uint8_t ATS_COUNTERS_202A[] = { 0x03, 0x85, 0x17, 0x0A, 0x04, 0x08, 0x1D, 0x01, 0x1F, 0x12, 0x12, 0x12, 0x01, 0x02, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x90, 0x00, 0x3D, 0x15

	};
    /*
    uint8_t ATS_COUNTERS_202B[] = {0x03, 0x85, 0x17, 0x0B, 0x04, 0x08, 0x1D, 0x01, 0x1F, 0x12, 0x12, 0x12, 0x01, 0x02, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x90, 0x00, 0x3D, 0x15

	};
    uint8_t ATS_COUNTERS_202D[] = { 0x02, 0x85, 0x17, 0x0D, 0x04, 0x08, 0x1D, 0x01, 0x1F, 0x12, 0x12, 0x12, 0x01, 0x02, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x90, 0x00, 0x42, 0xC6

	};
    */
    uint8_t ATS_COUNTERS_2069[] = { 0x02, 0x85, 0x17, 0x19, 0x04, 0x09, 0x1D, 0x01, 0x1F, 0x12, 0x12, 0x12, 0x01, 0x02, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x90, 0x00, 0xD7, 0x19

	};
    uint8_t ATS_SPECIAL_EVENTS_2040[] = { 0x02, 0x85, 0x17, 0x1D, 0x04, 0x02, 0x1D, 0x03, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x78, 0xF0

    };
    uint8_t ATS_CONTRACT_LIST_2050[] ={ 0x03, 0x85, 0x17, 0x1E, 0x04, 0x02, 0x1D, 0x01, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x33, 0x5C

	};


    bool active = false; // flag pour indiquer si la carte est en etat actif

    // ...PUPI/UID supplied from user. Adjust ATQB response accordingly
    if (memcmp("\x00\x00\x00\x00", pupi, 4) != 0) {
        memcpy(respATQB + 1, pupi, 4);
        AddCrc14B(respATQB, 12);
    }
    else
    {
		AddCrc14B(respATQB, 12);
	}


    uint16_t len, cmdsReceived = 0;
    int cardSTATE = SIMCAL_NOFIELD;
    int vHf = 0; // in mV

    tosend_t* ts = get_tosend();

    uint8_t* receivedCmd = BigBuf_calloc(MAX_FRAME_SIZE);

    // prepare "ATQB" tag answer (encoded):
    CodeIso14443bAsTag(respATQB, sizeof(respATQB));
    uint8_t* encodedATQB = BigBuf_malloc(ts->max);
    uint16_t encodedATQBLen = ts->max;
    memcpy(encodedATQB, ts->buf, ts->max);


    // prepare "OK" tag answer (encoded):
    CodeIso14443bAsTag(respOK, sizeof(respOK));
    uint8_t* encodedOK = BigBuf_malloc(ts->max);
    uint16_t encodedOKLen = ts->max;
    memcpy(encodedOK, ts->buf, ts->max);

    // prepare "REPA" tag answer (encoded):
    CodeIso14443bAsTag(respa, sizeof(respa));
    uint8_t* encodedRespa = BigBuf_malloc(ts->max);
    uint16_t encodedRespaLen = ts->max;
    memcpy(encodedRespa, ts->buf, ts->max);

    // prepare "REPB" tag answer (encoded):
    CodeIso14443bAsTag(respb, sizeof(respb));
    uint8_t* encodedRespb = BigBuf_malloc(ts->max);
    uint16_t encodedRespbLen = ts->max;
    memcpy(encodedRespb, ts->buf, ts->max);

    // prepare "REPC" tag answer (encoded):
    CodeIso14443bAsTag(respc, sizeof(respc));
    uint8_t* encodedRespc = BigBuf_malloc(ts->max);
    uint16_t encodedRespcLen = ts->max;
    memcpy(encodedRespc, ts->buf, ts->max);

    // prepare "REPD" tag answer (encoded):
    CodeIso14443bAsTag(respd, sizeof(respd));
    uint8_t* encodedRespd = BigBuf_malloc(ts->max);
    uint16_t encodedRespdLen = ts->max;
    memcpy(encodedRespd, ts->buf, ts->max);

    // prepare "REPE" tag answer (encoded):
    CodeIso14443bAsTag(respe, sizeof(respe));
    uint8_t* encodedRespe = BigBuf_malloc(ts->max);
    uint16_t encodedRespeLen = ts->max;
    memcpy(encodedRespe, ts->buf, ts->max);

    // prepare "REPF" tag answer (encoded):
    CodeIso14443bAsTag(respf, sizeof(respf));
    uint8_t* encodedRespf = BigBuf_malloc(ts->max);
    uint16_t encodedRespfLen = ts->max;
    memcpy(encodedRespf, ts->buf, ts->max);
    /*
    // prepare "VOID_REP_02" tag answer (encoded):
    CodeIso14443bAsTag(VOID_REP_02, sizeof(VOID_REP_02));
    uint8_t* encodedVOID_REP_02 = BigBuf_malloc(ts->max);
    uint16_t encodedVOID_REP_02Len = ts->max;
    memcpy(encodedVOID_REP_02, ts->buf, ts->max);
    
    // prepare "VOID_REP_03" tag answer (encoded):
    CodeIso14443bAsTag(VOID_REP_03, sizeof(VOID_REP_03));
    uint8_t* encodedVOID_REP_03 = BigBuf_malloc(ts->max);
    uint16_t encodedVOID_REP_03Len = ts->max;
    memcpy(encodedVOID_REP_03, ts->buf, ts->max);
    */
    // prepare "respAID_3F04" tag answer (encoded):
    CodeIso14443bAsTag(respAID_3F04, sizeof(respAID_3F04));
    uint8_t* encodedrespAID_3F04 = BigBuf_malloc(ts->max);
    uint16_t encodedrespAID_3F04Len = ts->max;
    memcpy(encodedrespAID_3F04, ts->buf, ts->max);

    // prepare "respICC_0002" tag answer (encoded):
    CodeIso14443bAsTag(respICC_0002, sizeof(respICC_0002));
    uint8_t* encodedrespICC_0002 = BigBuf_malloc(ts->max);
    uint16_t encodedrespICC_0002Len = ts->max;
    memcpy(encodedrespICC_0002, ts->buf, ts->max);

    // prepare "respID_0003" tag answer (encoded):
    CodeIso14443bAsTag(respID_0003, sizeof(respID_0003));
    uint8_t* encodedrespID_0003 = BigBuf_malloc(ts->max);
    uint16_t encodedrespID_0003Len = ts->max;
    memcpy(encodedrespID_0003, ts->buf, ts->max);

    // prepare "respDISPLAY_2F10_A" tag answer (encoded):
    CodeIso14443bAsTag(respDISPLAY_2F10_A, sizeof(respDISPLAY_2F10_A));
    uint8_t* encodedrespDISPLAY_2F10_A = BigBuf_malloc(ts->max);
    uint16_t encodedrespDISPLAY_2F10_ALen = ts->max;
    memcpy(encodedrespDISPLAY_2F10_A, ts->buf, ts->max);

    // prepare "respDISPLAY_2F10_B" tag answer (encoded):
    /*
    CodeIso14443bAsTag(respDISPLAY_2F10_B, sizeof(respDISPLAY_2F10_B));
    uint8_t* encodedrespDISPLAY_2F10_B = BigBuf_malloc(ts->max);
    uint16_t encodedrespDISPLAY_2F10_BLen = ts->max;
    memcpy(encodedrespDISPLAY_2F10_B, ts->buf, ts->max);
    */
    // prepare "respTICKETING_2000_ATS" tag answer (encoded):
    CodeIso14443bAsTag(respTICKETING_2000_ATS, sizeof(respTICKETING_2000_ATS));
    uint8_t* encodedrespTICKETING_2000_ATS = BigBuf_malloc(ts->max);
    uint16_t encodedrespTICKETING_2000_ATSLen = ts->max;
    memcpy(encodedrespTICKETING_2000_ATS, ts->buf, ts->max);

    // prepare "respAID_2004" tag answer (encoded):
    CodeIso14443bAsTag(respAID_2004, sizeof(respAID_2004));
    uint8_t* encodedrespAID_2004 = BigBuf_malloc(ts->max);
    uint16_t encodedrespAID_2004Len = ts->max;
    memcpy(encodedrespAID_2004, ts->buf, ts->max);

    // prepare "respENVIRONMENT_2001" tag answer (encoded):
    CodeIso14443bAsTag(respENVIRONMENT_2001, sizeof(respENVIRONMENT_2001));
    uint8_t* encodedrespENVIRONMENT_2001 = BigBuf_malloc(ts->max);
    uint16_t encodedrespENVIRONMENT_2001Len = ts->max;
    memcpy(encodedrespENVIRONMENT_2001, ts->buf, ts->max);

    // prepare "respEVENT_LOG_2010_1" tag answer (encoded):
    CodeIso14443bAsTag(respEVENT_LOG_2010_1, sizeof(respEVENT_LOG_2010_1));
    uint8_t* encodedrespEVENT_LOG_2010_1 = BigBuf_malloc(ts->max);
    uint16_t encodedrespEVENT_LOG_2010_1Len = ts->max;
    memcpy(encodedrespEVENT_LOG_2010_1, ts->buf, ts->max);
    /*
    // prepare "respEVENT_LOG_2010_3" tag answer (encoded):
    CodeIso14443bAsTag(respEVENT_LOG_2010_3, sizeof(respEVENT_LOG_2010_3));
    uint8_t* encodedrespEVENT_LOG_2010_3 = BigBuf_malloc(ts->max);
    uint16_t encodedrespEVENT_LOG_2010_3Len = ts->max;
    memcpy(encodedrespEVENT_LOG_2010_3, ts->buf, ts->max);
    */
    // prepare "respCONTRACTS_2020" tag answer (encoded):
    CodeIso14443bAsTag(respCONTRACTS_2020, sizeof(respCONTRACTS_2020));
    uint8_t* encodedrespCONTRACTS_2020 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCONTRACTS_2020Len = ts->max;
    memcpy(encodedrespCONTRACTS_2020, ts->buf, ts->max);

    /*
    // prepare "respCONTRACTS_2020_1" tag answer (encoded):
    CodeIso14443bAsTag(respCONTRACTS_2020_1, sizeof(respCONTRACTS_2020_1));
    uint8_t* encodedrespCONTRACTS_2020_1 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCONTRACTS_2020_1Len = ts->max;
    memcpy(encodedrespCONTRACTS_2020_1, ts->buf, ts->max);

    // prepare "respCONTRACTS_2020_2" tag answer (encoded):
    CodeIso14443bAsTag(respCONTRACTS_2020_2, sizeof(respCONTRACTS_2020_2));
    uint8_t* encodedrespCONTRACTS_2020_2 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCONTRACTS_2020_2Len = ts->max;
    memcpy(encodedrespCONTRACTS_2020_2, ts->buf, ts->max);
    */

    // prepare "respCONTRACTS_2030" tag answer (encoded):
    CodeIso14443bAsTag(respCONTRACTS_2030, sizeof(respCONTRACTS_2030));
    uint8_t* encodedrespCONTRACTS_2030 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCONTRACTS_2030Len = ts->max;
    memcpy(encodedrespCONTRACTS_2030, ts->buf, ts->max);

    // prepare "respCOUNTERS_202A" tag answer (encoded):
    CodeIso14443bAsTag(respCOUNTERS_202A, sizeof(respCOUNTERS_202A));
    uint8_t* encodedrespCOUNTERS_202A = BigBuf_malloc(ts->max);
    uint16_t encodedrespCOUNTERS_202ALen = ts->max;
    memcpy(encodedrespCOUNTERS_202A, ts->buf, ts->max);

    // prepare "respCOUNTERS_2069" tag answer (encoded):
    CodeIso14443bAsTag(respCOUNTERS_2069, sizeof(respCOUNTERS_2069));
    uint8_t* encodedrespCOUNTERS_2069 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCOUNTERS_2069Len = ts->max;
    memcpy(encodedrespCOUNTERS_2069, ts->buf, ts->max);

    // prepare "respSPECIAL_EVENTS_2040" tag answer (encoded):
    CodeIso14443bAsTag(respSPECIAL_EVENTS_2040, sizeof(respSPECIAL_EVENTS_2040));
    uint8_t* encodedrespSPECIAL_EVENTS_2040 = BigBuf_malloc(ts->max);
    uint16_t encodedrespSPECIAL_EVENTS_2040Len = ts->max;
    memcpy(encodedrespSPECIAL_EVENTS_2040, ts->buf, ts->max);

    // prepare "respCONTRACT_LIST_2050" tag answer (encoded):
    CodeIso14443bAsTag(respCONTRACT_LIST_2050, sizeof(respCONTRACT_LIST_2050));
    uint8_t* encodedrespCONTRACT_LIST_2050 = BigBuf_malloc(ts->max);
    uint16_t encodedrespCONTRACT_LIST_2050Len = ts->max;
    memcpy(encodedrespCONTRACT_LIST_2050, ts->buf, ts->max);

    //prepare ATS

    typedef struct {
        uint8_t* encodedData;
        uint16_t encodedDataLen;
        const uint8_t* data;
        size_t dataSize;
    } CalypsoATS;

    CalypsoATS  calypso_ATS[] = {
        { NULL, 0, ATS_AID_3F04, sizeof(ATS_AID_3F04) },
        { NULL, 0, ATS_ICC_0002, sizeof(ATS_ICC_0002) },
        { NULL, 0, ATS_ID_0003, sizeof(ATS_ID_0003) },
        { NULL, 0, ATS_DISPLAY_2F10, sizeof(ATS_DISPLAY_2F10) },
        { NULL, 0, ATS_TICKETING_2000_ATS, sizeof(ATS_TICKETING_2000_ATS) },
        { NULL, 0, ATS_AID_2004, sizeof(ATS_AID_2004) },
        { NULL, 0, ATS_ENVIRONMENT_2001, sizeof(ATS_ENVIRONMENT_2001) },
        { NULL, 0, ATS_EVENT_LOG_2010, sizeof(ATS_EVENT_LOG_2010) },
        { NULL, 0, ATS_CONTRACTS_2020, sizeof(ATS_CONTRACTS_2020) },
        { NULL, 0, ATS_CONTRACTS_2030, sizeof(ATS_CONTRACTS_2030) },
        { NULL, 0, ATS_COUNTERS_202A, sizeof(ATS_COUNTERS_202A) },
        { NULL, 0, ATS_COUNTERS_2069, sizeof(ATS_COUNTERS_2069) },
        { NULL, 0, ATS_SPECIAL_EVENTS_2040, sizeof(ATS_SPECIAL_EVENTS_2040) },
        { NULL, 0, ATS_CONTRACT_LIST_2050, sizeof(ATS_CONTRACT_LIST_2050) }
    };

    // Taille du tableau
    const size_t numCalypsoATS = sizeof(calypso_ATS) / sizeof(calypso_ATS[0]);

    // Boucle pour traiter chaque élément du tableau
    for (size_t i = 0; i < numCalypsoATS; ++i) {
        CodeIso14443bAsTag(calypso_ATS[i].data, calypso_ATS[i].dataSize);
        calypso_ATS[i].encodedData = BigBuf_malloc(ts->max);
        calypso_ATS[i].encodedDataLen = ts->max;
        memcpy(calypso_ATS[i].encodedData, ts->buf, ts->max);
    }

	// Free the buffer
	BigBuf_free();


    StartCountUS();
    uint32_t eof_time = 0;
    uint32_t sof_time = 0;

    // Simulation loop
    while (BUTTON_PRESS() == false) {
        //Dbprintf("Simulation loop");
        WDT_HIT();

        //iceman: limit with 2000 times..
        if (data_available()) {
            Dbprintf("Data available");
            break;
        }
        /*
        volatile int behind_by = ((uint16_t*)AT91C_BASE_PDC_SSC->PDC_RPR - upTo) & (DMA_BUFFER_SIZE - 1);
        if (behind_by < 1) {
            Dbprintf("behind_by < 1");
            continue;
        }
        */

        // find reader field
        if (cardSTATE == SIMCAL_NOFIELD) {
            vHf = (MAX_ADC_HF_VOLTAGE * SumAdc(ADC_CHAN_HF, 32)) >> 15;
            if (vHf > MF_MINFIELDV) {
                cardSTATE = SIMCAL_IDLE;
                Dbprintf("Champs RF detecte");
                LED_A_ON();
            }
        }

        if (cardSTATE == SIMCAL_NOFIELD) {
            continue;
        }
        sof_time = GetCountUS();
        // Get reader command
        if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
            Dbprintf("button pressed, received %d commands", cmdsReceived);
            break;
        }
        eof_time = GetCountUS();

        if (len == 5) {
            if (receivedCmd[4] == CALYPSO_WUPB_EOF && !active) {
                LogTrace(receivedCmd, len, (sof_time ), (eof_time ), NULL, true);
                cardSTATE = SIMCAL_SELECTING;
            }
        }
        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB && !active ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time ), NULL, true);
            cardSTATE = SIMCAL_HALTING;
        }
        else if (len == 19 && receivedCmd[0] == CALYPSO_A) {
            LogTrace(receivedCmd, len, (sof_time ), (eof_time ), NULL, true);
            cardSTATE = SIMCAL_EXA;
        }
        else if (len == 8 && receivedCmd[0] == CALYPSO_B) {
            LogTrace(receivedCmd, len, (sof_time ), (eof_time ), NULL, true);
            cardSTATE = SIMCAL_EXB;

        }
        else if (len == 17 && receivedCmd[0] == CALYPSO_C) {
            LogTrace(receivedCmd, len, (sof_time ), (eof_time ), NULL, true);
            cardSTATE = SIMCAL_EXC;

        }

        else if (len == 17 && receivedCmd[0] == CALYPSO_D) {
            LogTrace(receivedCmd, len, (sof_time ), (eof_time ), NULL, true);
            cardSTATE = SIMCAL_EXD;

        }
        else if (len == 3 && receivedCmd[0] == CALYPSO_F) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_EXF;
        }

        //SELECT (CardPeek)
        else if (len == 11){
            if (receivedCmd[6] == AID_3F04[0] && receivedCmd[7] == AID_3F04[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_AID_3F04;
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[0].encodedData, calypso_ATS[0].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_AID_3F04, sizeof(ATS_AID_3F04), (sof_time), (eof_time), NULL, false);
			}
            else if (receivedCmd[6] == ICC_0002[0] && receivedCmd[7] == ICC_0002[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_ICC_0002;
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[1].encodedData, calypso_ATS[1].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_ICC_0002, sizeof(ATS_ICC_0002), (sof_time), (eof_time), NULL, false);
            }
            else if (receivedCmd[6] == ID_0003[0] && receivedCmd[7] == ID_0003[1]) {

				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_ID_0003;
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[2].encodedData, calypso_ATS[2].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_ID_0003, sizeof(ATS_ID_0003), (sof_time), (eof_time), NULL, false);
                
			}
            else if (receivedCmd[6] == DISPLAY_2F10[0] && receivedCmd[7] == DISPLAY_2F10[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_DISPLAY_2F10;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[3].encodedData, calypso_ATS[3].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_DISPLAY_2F10, sizeof(ATS_DISPLAY_2F10), (sof_time), (eof_time), NULL, false);
			}
			else if (receivedCmd[6] == TICKETING_2000[0] && receivedCmd[7] == TICKETING_2000[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_TICKETING_2000;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[4].encodedData, calypso_ATS[4].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_TICKETING_2000_ATS, sizeof(ATS_TICKETING_2000_ATS), (sof_time), (eof_time), NULL, false);
			}
			else if (receivedCmd[6] == AID_2004[0] && receivedCmd[7] == AID_2004[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_AID_2004;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[5].encodedData, calypso_ATS[5].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_AID_2004, sizeof(ATS_AID_2004), (sof_time), (eof_time), NULL, false);
			}
            else if (receivedCmd[6] == ENVIRONMENT_2001[0] && receivedCmd[7] == ENVIRONMENT_2001[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_ENVIRONMENT_2001;
				sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[6].encodedData, calypso_ATS[6].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_ENVIRONMENT_2001, sizeof(ATS_ENVIRONMENT_2001), (sof_time), (eof_time), NULL, false);
            }
			else if (receivedCmd[6] == EVENT_LOG_2010[0] && receivedCmd[7] == EVENT_LOG_2010[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_EVENT_LOG_2010;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[7].encodedData, calypso_ATS[7].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_EVENT_LOG_2010, sizeof(ATS_EVENT_LOG_2010), (sof_time), (eof_time), NULL, false);
			}
			else if (receivedCmd[6] == CONTRACTS_2020[0] && receivedCmd[7] == CONTRACTS_2020[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_CONTRACTS_2020;
				sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[8].encodedData, calypso_ATS[8].encodedDataLen);
				eof_time = GetCountUS();
                LogTrace(ATS_CONTRACTS_2020, sizeof(ATS_CONTRACTS_2020), (sof_time), (eof_time), NULL, false);
            }
			else if (receivedCmd[6] == CONTRACTS_2030[0] && receivedCmd[7] == CONTRACTS_2030[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_CONTRACTS_2030;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[9].encodedData, calypso_ATS[9].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_CONTRACTS_2030, sizeof(ATS_CONTRACTS_2030), (sof_time), (eof_time), NULL, false);
			}
			else if (receivedCmd[6] == COUNTERS_202A[0] && receivedCmd[7] == COUNTERS_202A[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_COUNTERS_202A;
				sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[10].encodedData, calypso_ATS[10].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_COUNTERS_202A, sizeof(ATS_COUNTERS_202A), (sof_time), (eof_time), NULL, false);
            }
            else if (receivedCmd[6] == COUNTERS_2069[0] && receivedCmd[7] == COUNTERS_2069[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_COUNTERS_2069;
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[10].encodedData, calypso_ATS[10].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_COUNTERS_2069, sizeof(ATS_COUNTERS_2069), (sof_time), (eof_time), NULL, false);
            }
            else if (receivedCmd[6] == SPECIAL_EVENTS_2040[0] && receivedCmd[7] == SPECIAL_EVENTS_2040[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_SPECIAL_EVENTS_2040;
				sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_ATS[11].encodedData, calypso_ATS[11].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(ATS_SPECIAL_EVENTS_2040, sizeof(ATS_SPECIAL_EVENTS_2040), (sof_time), (eof_time), NULL, false);
            }
            else if (receivedCmd[6] == CONTRACT_LIST_2050[0] && receivedCmd[7] == CONTRACT_LIST_2050[1]) {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				cardSTATE = SIMCAL_CONTRACT_LIST_2050;
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(calypso_ATS[12].encodedData, calypso_ATS[12].encodedDataLen);
				eof_time = GetCountUS();
				LogTrace(ATS_CONTRACT_LIST_2050, sizeof(ATS_CONTRACT_LIST_2050), (sof_time), (eof_time), NULL, false);
			}
			else {
				LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
				continue;
			}
        }

        // Reset command
	    else if (len == 3 && receivedCmd[0] == CALYPSO_RESET) {
			LogTrace(receivedCmd, len, (sof_time), (eof_time ), NULL, true);
			active = false;
		}
        else
        {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            continue;
        }

        /*
        * How should this flow go?
        *  REQB or WUPB
        *   send response  ( waiting for Attrib)
        *  ATTRIB
        *   send response  ( waiting for commands 7816)
        *  HALT
            send halt response ( waiting for wupb )
        */

        switch (cardSTATE) {

            //case SIM_NOFIELD:
        case SIMCAL_IDLE: {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            //LogTrace(receivedCmd, len, 0, 0, NULL, true);
            break;
        }
        case SIMCAL_SELECTING: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
            eof_time = GetCountUS();
            LogTrace(respATQB, sizeof(respATQB), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_HALTING: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
            eof_time = GetCountUS();
            LogTrace(respOK, sizeof(respOK), (sof_time), (eof_time), NULL, false);
            break;
        }

        case SIMCAL_EXA: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespa, encodedRespaLen);
            eof_time = GetCountUS();
            LogTrace(respa, sizeof(respa), (sof_time), (eof_time), NULL, false);
            active = true; // la carte est activée
            break;

        }
        case SIMCAL_EXB: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespb, encodedRespbLen);
            eof_time = GetCountUS();
            LogTrace(respb, sizeof(respb), (sof_time), (eof_time), NULL, false);
            break;

        }
        case SIMCAL_EXC: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespc, encodedRespcLen);
            eof_time = GetCountUS();
            LogTrace(respc, sizeof(respc), (sof_time), (eof_time), NULL, false);
            break;

        }

        case SIMCAL_EXD: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespd, encodedRespdLen);
            eof_time = GetCountUS();
            LogTrace(respd, sizeof(respd), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_EXE: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespe, encodedRespeLen);
            eof_time = GetCountUS();
            LogTrace(respe, sizeof(respe), (sof_time), (eof_time), NULL, false);
            break;
            // Fin de la communication
        }
        case SIMCAL_EXF: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedRespf, encodedRespfLen);
            eof_time = GetCountUS();
            LogTrace(respf, sizeof(respf), (sof_time), (eof_time), NULL, false);
            break;
            // Fin de la communication
        }
// STATE (Cardpeek)
        case SIMCAL_AID_3F04: {
				sof_time = GetCountUS();
				TransmitFor14443b_AsTag(encodedrespAID_3F04, encodedrespAID_3F04Len);
				eof_time = GetCountUS();
                LogTrace(respAID_3F04, sizeof(respAID_3F04), (sof_time), (eof_time), NULL, false);
                break;
            
		}
        case SIMCAL_ICC_0002: {
			sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespICC_0002, encodedrespICC_0002Len);
			eof_time = GetCountUS();
			LogTrace(respICC_0002, sizeof(respICC_0002), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_ID_0003: {
            sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespID_0003, encodedrespID_0003Len);
			eof_time = GetCountUS();
			LogTrace(respID_0003, sizeof(respID_0003), (sof_time), (eof_time), NULL, false);
            break;
		}
        case SIMCAL_DISPLAY_2F10: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedrespDISPLAY_2F10_A, encodedrespDISPLAY_2F10_ALen);
            eof_time = GetCountUS();
            LogTrace(respDISPLAY_2F10_A, sizeof(respDISPLAY_2F10_A), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_TICKETING_2000: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedrespTICKETING_2000_ATS, encodedrespTICKETING_2000_ATSLen);
            eof_time = GetCountUS();
            LogTrace(respTICKETING_2000_ATS, sizeof(respTICKETING_2000_ATS), (sof_time), (eof_time), NULL, false);
            break;
        } 
        case SIMCAL_AID_2004: {
			sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespAID_2004, encodedrespAID_2004Len);
			eof_time = GetCountUS();
			LogTrace(respAID_2004, sizeof(respAID_2004), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_ENVIRONMENT_2001: {
            sof_time= GetCountUS();
			TransmitFor14443b_AsTag(encodedrespENVIRONMENT_2001, encodedrespENVIRONMENT_2001Len);
			eof_time = GetCountUS();
			LogTrace(respENVIRONMENT_2001, sizeof(respENVIRONMENT_2001), (sof_time), (eof_time), NULL, false);
			break;
		}

        case SIMCAL_EVENT_LOG_2010: {
			sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespEVENT_LOG_2010_1, encodedrespEVENT_LOG_2010_1Len);
			eof_time = GetCountUS();
			LogTrace(respEVENT_LOG_2010_1, sizeof(respEVENT_LOG_2010_1), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_CONTRACTS_2020: { 
            sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespCONTRACTS_2020, encodedrespCONTRACTS_2020Len);
			eof_time = GetCountUS();
			LogTrace(respCONTRACTS_2020, sizeof(respCONTRACTS_2020), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_CONTRACTS_2030: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedrespCONTRACTS_2030, encodedrespCONTRACTS_2030Len);
            eof_time = GetCountUS();
            LogTrace(respCONTRACTS_2030, sizeof(respCONTRACTS_2030), (sof_time), (eof_time), NULL, false);
            break;
        }

        case SIMCAL_COUNTERS_202A: {
			sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespCOUNTERS_202A, encodedrespCOUNTERS_202ALen);
			eof_time = GetCountUS();
			LogTrace(respCOUNTERS_202A, sizeof(respCOUNTERS_202A), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_COUNTERS_2069 : {
            sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespCOUNTERS_2069, encodedrespCOUNTERS_2069Len);
			eof_time = GetCountUS();
			LogTrace(respCOUNTERS_2069, sizeof(respCOUNTERS_2069), (sof_time), (eof_time), NULL, false);
			break;
		}
        case SIMCAL_SPECIAL_EVENTS_2040 : {
			sof_time = GetCountUS();
            TransmitFor14443b_AsTag(encodedrespSPECIAL_EVENTS_2040, encodedrespSPECIAL_EVENTS_2040Len);
			eof_time = GetCountUS();
            LogTrace(respSPECIAL_EVENTS_2040, sizeof(respSPECIAL_EVENTS_2040), (sof_time), (eof_time), NULL, false);
	        break;
        }

        case SIMCAL_CONTRACT_LIST_2050: {
			sof_time = GetCountUS();
			TransmitFor14443b_AsTag(encodedrespCONTRACT_LIST_2050, encodedrespCONTRACT_LIST_2050Len);
			eof_time = GetCountUS();
			LogTrace(respCONTRACT_LIST_2050, sizeof(respCONTRACT_LIST_2050), (sof_time), (eof_time), NULL, false);
			break;
		}

        default: {
            break;
        }
        }
        ++cmdsReceived;

        // Ici je fais une sorte de pause.. Necessaire pour que le lecteur puisse traiter la reponse sinon c'est trop rapide 
        // Attention a ne pas toucher a cette valeur. 700 microsecondes est le temps necessaire pour que le lecteur puisse traiter la reponse
        waitForFWT(800);
        

    }
    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
        BigBuf_free();
    }
}

