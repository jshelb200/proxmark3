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


    // We won't start recording the frames that we acquire until we trigger;
    // a good trigger condition to get started is probably when we see a
    // response from the tag.
    int dma_start_time = 0;


    // Count of samples received so far, so that we can include timing
    int samples = 0;

    uint16_t* upTo = dma->buf;



    uint8_t respATQB[] = {
    0x50,                       // Code de réponse ATQB
    0xC3, 0xF7, 0x0B, 0xF7,     // PUPI/UID
    0x00, 0x00, 0x00, 0x00, 0x00,// Octets de remplissage
    0x71,                       // Protocole ISO/IEC 14443-3 supporté
    0x71, 0x1E,                 // CRC de l'ATQB (partie haute et basse)
    0x37                        // CRC inversé de l'ATQB
    };

    uint8_t respa[] = {
		0x20, 0x6A, 0x82, 0x4B, 0x4C
	};

    // ...PUPI/UID supplied from user. Adjust ATQB response accordingly
    if (memcmp("\x00\x00\x00\x00", pupi, 4) != 0) {
        memcpy(respATQB + 1, pupi, 4);
        AddCrc14B(respATQB, 12);
    }

    // response to HLTB and ATTRIB
    static const uint8_t respOK[] = { 0x00, 0x78, 0xF0 };

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
            

        samples++;
        if (samples == 1) {
            // DMA has transferred the very first data
            dma_start_time = GetCountSspClk() & 0xfffffff0;
            Dbprintf("dma start time : %d ", dma_start_time);

        }

        upTo++;

        // we have read all of the DMA buffer content.
        if (upTo >= dma->buf + DMA_BUFFER_SIZE) {

            // start reading the circular buffer from the beginning again
            upTo = dma->buf;

            // DMA Counter Register had reached 0, already rotated.
            if (AT91C_BASE_SSC->SSC_SR & (AT91C_SSC_ENDRX)) {

                // primary buffer was stopped
                if (AT91C_BASE_PDC_SSC->PDC_RCR == false) {
                    AT91C_BASE_PDC_SSC->PDC_RPR = (uint32_t)dma->buf;
                    AT91C_BASE_PDC_SSC->PDC_RCR = DMA_BUFFER_SIZE;
                }
                // secondary buffer sets as primary, secondary buffer was stopped
                if (AT91C_BASE_PDC_SSC->PDC_RNCR == false) {
                    AT91C_BASE_PDC_SSC->PDC_RNPR = (uint32_t)dma->buf;
                    AT91C_BASE_PDC_SSC->PDC_RNCR = DMA_BUFFER_SIZE;
                }

                WDT_HIT();
                if (BUTTON_PRESS()) {
                    DbpString("Simulaion stopped");
                    break;
                }
            }
        }


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

        // Get reader command
        if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
            Dbprintf("button pressed, received %d commands", cmdsReceived);
            break;
        }


        //Dbprintf("new cmd from reader: len=%d, SOF de la commande=%X", len, receivedCmd[sizeof(len)]);
        // Modify by me
        if (len == 5) {
            if (receivedCmd[4] == CALYPSO_WUPB_EOF) {
                uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
                uint32_t sof_time = eof_time
                    - 32 * 16          // time for SOF transfer
                    - 16 * 16;         // time for EOF transfer
                LogTrace(receivedCmd,len, (sof_time * 4), (eof_time * 4), NULL, true);
 
                //LogTrace(receivedCmd, len, 0, 0, NULL, true);
                cardSTATE = SIMCAL_SELECTING;
                // And ready to receive another command.
                Uart14bReset();
                //expect_tag_answer = true; // A remplacer par un autre flag par la suite
                //Dbprintf("WUPB detecte");
            }
        }

        // ISO14443-B protocol states:
        // REQ or WUP request in ANY state
        // WUP in HALTED state
        /*
        if (len == 5) {
            if (((receivedCmd[0] == ISO14443B_REQB) && ((receivedCmd[2] & 0x08) == 0x08) && (cardSTATE == SIMCAL_HALTED)) ||
                (receivedCmd[0] == ISO14443B_REQB)) {

                LogTrace(receivedCmd, len, 0, 0, NULL, true);
                cardSTATE = SIMCAL_SELECTING;
            }
        }
        */

        /*
        // Check for the specific command "10 F9 E0"
        if (len == 3 && receivedCmd[0] == 0x10 && receivedCmd[1] == 0xF9 && receivedCmd[2] == 0xE0) {
            // If the command matches, respond with "DD"
            TransmitFor14443b_AsTag((uint8_t*)"DD", 2);
            cardSTATE = SIM_WORK; // Update the card state
            continue; // Skip the rest of the loop iteration
        }
        */

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
        case SIMCAL_HALTED:
        case SIMCAL_IDLE: {
            Dbprintf("SIMCAL_IDLE");
            uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
            uint32_t sof_time = eof_time
                - 32 * 16          // time for SOF transfer
                - 16 * 16;         // time for EOF transfer
            LogTrace(receivedCmd, len, (sof_time * 4), (eof_time * 4), NULL, true);
            
            //LogTrace(receivedCmd, len, 0, 0, NULL, true);
            break;
        }
        case SIMCAL_SELECTING: {
            Dbprintf("SIMCAL_SELECTING");
            TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
            
            Dbprintf("Tag --> Reader : %x ", encodedATQB);
            uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
            uint32_t sof_time = eof_time
                - 32 * 16          // time for SOF transfer
                - 16 * 16;         // time for EOF transfer
            LogTrace(respATQB, sizeof(respATQB), (sof_time * 4), (eof_time * 4), NULL, false);

            //LogTrace(respATQB, sizeof(respATQB), 0, 0, NULL, false);
            cardSTATE = SIMCAL_WORK;
            break;
        }
        case SIMCAL_HALTING: {
            Dbprintf("SIMCAL_HALTING");
            TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
            
            uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
            //Dbprintf("Uart.byteCnt = %d", Uart.byteCnt);
            uint32_t sof_time = eof_time
                - 32 * 16          // time for SOF transfer
                - 16 * 16;         // time for EOF transfer
            LogTrace(respOK, sizeof(respOK), (sof_time * 4), (eof_time * 4), NULL, false);
            
            //LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
            cardSTATE = SIMCAL_HALTED;
            break;
        }
        case SIMCAL_ACKNOWLEDGE: {

            Dbprintf("SIMCAL_ACKNOWLEDGE");
            TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
            
            uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
            //Dbprintf("Uart.byteCnt = %d", Uart.byteCnt);
            uint32_t sof_time = eof_time
                - 32 * 16          // time for SOF transfer
                - 16 * 16;         // time for EOF transfer
            LogTrace(respOK, sizeof(respOK), (sof_time * 4), (eof_time * 4), NULL, false);
            
            //LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
            cardSTATE = SIMCAL_EX;
            break;
        }
        case SIMCAL_WORK: {
            Dbprintf("SIMCAL_WORK");

			if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
                uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
                //Dbprintf("Uart.byteCnt = %d", Uart.byteCnt);
                uint32_t sof_time = eof_time
                    - 32 * 16          // time for SOF transfer
                    - 16 * 16;         // time for EOF transfer
                LogTrace(receivedCmd, len, (sof_time * 4), (eof_time * 4), NULL, true);
                
				cardSTATE = SIMCAL_ACKNOWLEDGE;
			}
            else {
                break;
            }

            /*
            
            if (len == 7 && receivedCmd[0] == ISO14443B_HALT) {
                
                cardSTATE = SIMCAL_HALTED;
            }
            else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
                cardSTATE = SIMCAL_ACKNOWLEDGE;
            }
            else {
                // Todo:
                // - SLOT MARKER
                // - ISO7816
                // - emulate with a memory dump
                if (g_dbglevel >= DBG_DEBUG) {
                    Dbprintf("new cmd from reader: len=%d, cmdsRecvd=%d", len, cmdsReceived);
                }

                cardSTATE = SIM_IDLE;
            }
            break;
            */
        }

        case SIMCAL_EX: {
			Dbprintf("SIMCAL_EX");
			if (receivedCmd[0] == CALYPSO_DEMANDE) {
                Dbprintf("OK");
				uint32_t eof_time = dma_start_time + (samples * 16) + 8; // - DELAY_READER_TO_ARM_SNIFF; // end of EOF
				//Dbprintf("Uart.byteCnt = %d", Uart.byteCnt);
				uint32_t sof_time = eof_time
					- 32 * 16          // time for SOF transfer
					- 16 * 16;         // time for EOF transfer
				LogTrace(receivedCmd, len, (sof_time * 4), (eof_time * 4), NULL, true);
                TransmitFor14443b_AsTag(encodedRespa, encodedRespaLen);
				cardSTATE = SIMCAL_ACKNOWLEDGE;
			}
			else {
				break;
			}
		}
        
        default: {
            break;
        }
        }
        ++cmdsReceived;
        
    }
    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
    }
}

