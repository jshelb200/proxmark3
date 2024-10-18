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
#include "pyclient_handler.h"
#include "pm3_cmd.h"
#include "usb_cdc.h"
#include "string.h"
#include "cmd.h"
#include "calypsosim.h"
#include "pyclient_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ticks.h"



#define DEBUG_MAX_MSG_SIZE  200
#define RESPONSE_SIZE 256

#ifndef ISO14B_TR0
# define ISO14B_TR0  HF14_ETU_TO_SSP(16)
#endif

#ifndef ISO14B_TR0_MAX
# define ISO14B_TR0_MAX HF14_ETU_TO_SSP(32)
// *   TR0 - 32 ETU's maximum for ATQB only
// *   TR0 - FWT for all other commands

// TR0 max is 159 �S or 32 samples from FPGA
// 16 ETU * 9.4395 �S == 151 �S
// 16 * 8 = 128 sub carrier cycles,
// 128 / 4 = 32 I/Q pairs.
// since 1 I/Q pair after 4 subcarrier cycles at 848kHz subcarrier
#endif

// 8 ETU = 75 �S == 256 SSP_CLK
#ifndef ISO14B_TR0_MIN
# define ISO14B_TR0_MIN HF14_ETU_TO_SSP(8)
#endif

// Synchronization time (per 14443-2) in ETU
// 16 ETU = 151 �S == 512 SSP_CLK
#ifndef ISO14B_TR1_MIN
# define ISO14B_TR1_MIN HF14_ETU_TO_SSP(16)
#endif
// Synchronization time (per 14443-2) in ETU
// 25 ETU == 236 �S == 800 SSP_CLK
#ifndef ISO14B_TR1_MAX
# define ISO14B_TR1 HF14_ETU_TO_SSP(25)
#endif

// Frame Delay Time PICC to PCD  (per 14443-3 Amendment 1) in ETU
// 14 ETU == 132 �S == 448 SSP_CLK
#ifndef ISO14B_TR2
# define ISO14B_TR2 HF14_ETU_TO_SSP(14)
#endif

#ifndef ISO14B_BLOCK_SIZE
# define ISO14B_BLOCK_SIZE  4
#endif

// 4sample
#define SEND4STUFFBIT(x) tosend_stuffbit(x);tosend_stuffbit(x);tosend_stuffbit(x);tosend_stuffbit(x);


static pycliresp_t pyresp;
uint8_t last_cmd[RESPONSE_SIZE]; // save last cmd before WTX since add_pcb need it

uint8_t _pyframe[] = { 0x00 };
CalypsoFrame tempframe = { NULL, 0, _pyframe, sizeof(_pyframe), true, true };



static void add_pcb_generic(pycliresp_t * entry, uint8_t* received_frame, size_t received_frame_size) {

    IblockType iblock_type = analyzeIBlock(received_frame);

    if (iblock_type == NS_0) {
        // Trame re�ue commence par 0x02, ajouter 0x02 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->len);
        entry->data[0] = 0x02;
        entry->len++;
    }
    else if (iblock_type == NS_1) {
        // Trame recue commence par 0x03, ajouter 0x03 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->len);
        entry->data[0] = 0x03;
        entry->len++;

    }
    else if (iblock_type == WUPB) {
        // Trame re�ue commence par 0x05, ajouter 0x05 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->len);
        entry->data[0] = 0x50;
        entry->len++;
    }
    else {
        // Trame re�ue ne commence ni par 0x02 ni par 0x03, gestion d'erreur
        Dbprintf("Attention : Iblock non identifie .\n");
    }
    return;
}


// Fonction pour ajouter le CRC
static void add_crc(pycliresp_t* entry) {
    if (entry->len < RESPONSE_SIZE - 2) { 
        // Ajouter deux octets 0x00 pour le calcul du CRC
        entry->data[entry->len] = 0x00;
        entry->data[entry->len + 1] = 0x00;

        entry->len += 2;

        // Calculer le CRC sur tous les octets sauf les deux derniers
        AddCrc14B(entry->data, entry->len - 2);
    }
    return;
}

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
// ptr tpour les reponses recues


// Fonction pour convertir un octet en sa repr�sentation hexad�cimale dans une cha�ne
static void byte_to_hex(uint8_t byte, char* hex) {
    static const char hex_digits[] = "0123456789ABCDEF";
    hex[0] = hex_digits[(byte >> 4) & 0xF];
    hex[1] = hex_digits[byte & 0xF];
    hex[2] = ' ';  // Ajouter un espace entre chaque octet hexad�cimal
}

// Pour envoyer des donn�es au thread du client python..
static void SendToPyCli(const uint8_t* data, size_t length) {
    if (length > PYCLIENT_MAX_MSG_SIZE) {
        length = PYCLIENT_MAX_MSG_SIZE;  // S'assurer que la longueur ne d�passe pas la taille maximale
    }

    char output[PYCLIENT_MAX_MSG_SIZE * 2 + 1] = { 0 };
    size_t index = 0;
    for (size_t i = 0; i < length; ++i) {
        byte_to_hex(data[i], &output[index]);
        index += 2;  
    }

    Dbprintf("%s", output);
}

// First function called when we received a data over the UART from pyclient
void notify_middleware(PacketCommandNG* packet, uint8_t * receivedcmd, size_t cmdlen) {
    if (packet->length == 0) {
        return;
    }
    
    pyresp.data = packet->data.asBytes;
    pyresp.len = packet->length;
    pyresp.ok = true;
    add_pcb_generic(&pyresp, receivedcmd, cmdlen);
    add_crc(&pyresp);
    memcpy(tempframe.data, pyresp.data, pyresp.len);
    tempframe.dataSize = pyresp.len;
    Dbprintf("tempframe.dataSize: %d", tempframe.dataSize);
    Dbprintf("tempframe.data: %02X %02X %02X %02X", tempframe.data[0], tempframe.data[1], tempframe.data[2], tempframe.data[3]); // les 4 premiers octets
    // Dbprintf tempframe
    //Dbprintf("tempframe: %02X %02X %02X %02X", tempframe[0], tempframe[1], tempframe[2], tempframe[3]); // les 4 premiers octets
}

// Mini packetReceived juste pour gerer les donnees recu du pyclient lors de la simulation.
static void PyCliPacketReceived(PacketCommandNG* packet, uint8_t* receivedcmd, size_t cmdlen) {

    switch (packet->cmd) {
    case CMD_BREAK_LOOP:{
        Dbprintf("CMD_BREAK_LOOP\n");
        WaitUS(10);
        break;
    }

    case CMD_PY_CLIENT_DATA: {
        // Afficher les donnees re�ues pour d�bogage
        Dbprintf("Py-pm3 data received");

        //useful for debugging
        /*
        for (int i = 0; i < packet->length; i++) {
            Dbprintf("%02X ", packet->data.asBytes[i]);
        }
        */
        notify_middleware(packet, receivedcmd, cmdlen);
        break;
    }

    default:
        break;
    
    }
}
// Fonction pour encoder les paquets avant transmission RF

static void encodeFrames(CalypsoFrame* frames, size_t frameCount) {
    tosend_t* ts = get_tosend();  // Récupère le tampon d'envoi global

    for (size_t i = 0; i < frameCount; ++i) {
        CodeIso14443bAsTag(frames[i].data, frames[i].dataSize);
        frames[i].encodedData = BigBuf_malloc(ts->max);
        frames[i].encodedDataLen = ts->max;
        memcpy(frames[i].encodedData, ts->buf, ts->max);
    }
    BigBuf_free();
}


//Fonnction pour encoder une frame avant transmission RF [one shot]
static void encodeFrame(CalypsoFrame* frame) {
    tosend_t* ts = get_tosend(); 
    CodeIso14443bAsTag(frame->data, frame->dataSize);
    frame->encodedData = BigBuf_malloc(ts->max);
    frame->encodedDataLen = ts->max;
    memcpy(frame->encodedData, ts->buf, ts->max);
    BigBuf_free();
}

void handlePyClientSim(uint8_t* pupi) {
    /*
    l'objectif de cette fonction est de simuler une carte calypso avec un script python.
     On va donc envoyer les commandes du lecteur au client python et attendre la reponse
     Avant il faut initialiser la communication avec la carte en repondant aux
     commandes WUPB, REQB, ATTRIB, HALT.. le WTX ne fonctionnant qu'avec les cmd de BLOC I
    */

    // INITIALISATION DE LA SIMULATION
    LED_A_ON();
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

    uint8_t* receivedCmd = BigBuf_calloc(MAX_FRAME_SIZE);
   

    // response to ATQB sans CRC (On le calcule nous meme)

    uint8_t respATQB[] = {
        0x50,                       // Code de r�ponse ATQB
        0x00, 0x00, 0x00, 0x00,     // PUPI/UID / Place holder for UID
        0x00, 0x00, 0x00, 0x00,	   // Application data
        0x00,                      // Bit rate capacity 
        0x71,                       // Max frame size
        0x71, 					   // FWI/Coding Options
        0x00,0x00                  // CRC placeholder
    };

    uint8_t respOK[] = { 0x00, 0x78,  0xF0 };
    uint8_t respFILE_NOT_FOUND[] = { 0x02, 0x6A, 0x82, 0x4B, 0x4C };
    uint8_t REQ_WTX[] = { 0xF2, 0x3B, 0xAF, 0xCF }; // S(WTX) avec un WTXM(0x3B) = 59 [1-59] = 2283ms [1-59] * 38.7ms
    //uint8_t REQ_WTX[] = { 0xF2, 0x32, 0x6E, 0x52 }; // S(WTX) avec un WTXM(0x32) = 50 [1-59] = 1935ms [1-59] * 38.7ms
    //uint8_t REQ_WTX[] = { 0xF2, 0x01, 0x76, 0x51 }; // S(WTX) avec un WTXM(0x00) = 0 [1-59] = 38.7ms [1-59] * 38.7ms
    uint8_t respEnd[] = { 0xA2,  0x60,  0x76 };
    
    // bool active = false; // flag pour indiquer si la carte est en etat actif

     // ...PUPI/UID supplied from user. Adjust ATQB response accordingly
    if (memcmp("\x00\x00\x00\x00", pupi, 4) != 0) {
        respATQB[1] = pupi[0];
        respATQB[2] = pupi[1];
        respATQB[3] = pupi[2];
        respATQB[4] = pupi[3];
    }
    else
    {
        respATQB[1] = 0xC3;
        respATQB[2] = 0xF7;
        respATQB[3] = 0x0B;
        respATQB[4] = 0xF7;
    }
    AddCrc14B(respATQB, 12);

    CalypsoFrame  calypso_RESP[] = {
    { NULL, 0, respATQB, sizeof(respATQB), true, true },
    { NULL, 0, respOK, sizeof(respOK), true, true },
    { NULL, 0, REQ_WTX, sizeof(REQ_WTX), true, true },
    { NULL, 0, respFILE_NOT_FOUND, sizeof(respFILE_NOT_FOUND), true, true },
    { NULL, 0, respEnd, sizeof(respEnd), true, true }
    };

    

    uint16_t len, cmdsReceived = 0;
    int cardSTATE = SIMCAL_NOFIELD;
    int vHf = 0; // in mV

    encodeFrames(calypso_RESP, 4);
    // 6 premiers octets de tempframe

    StartCountUS();
    uint32_t eof_time = 0; // pour les logs
    uint32_t sof_time = 0;
    uint32_t sow_time = 0; // pour suivre le WTX
    uint32_t eow_time = 0;


    // Simulation loop
    while (BUTTON_PRESS() == false) {

        WDT_HIT();

        cardSTATE = SIMCAL_IDLE; // On revient a l'etat idle

        // find reader field
        if (cardSTATE == SIMCAL_NOFIELD) {
            vHf = (MAX_ADC_HF_VOLTAGE * SumAdc(ADC_CHAN_HF, 32)) >> 15;
            if (vHf > MF_MINFIELDV) {
                cardSTATE = SIMCAL_IDLE;
                //Dbprintf("Champs RF detecte");
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
        SendToPyCli(receivedCmd, len);
        eof_time = GetCountUS();
        BigBuf_free();


        if (receivedCmd[0] == CALYPSO_WUPB) {
            if (len == 5 && receivedCmd[2] == 0x08) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_SELECTING;
            }
            else if (len == 5 && receivedCmd[2] == 0x00) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_REQUESTING;
            }
            else
            {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                continue;
            }
        }
        else if (len == 3 && receivedCmd[0] == 0xB3) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_B3;
        }
        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_HALTING;
        }
        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_HALTING;
        }
        else if (len == 4 && receivedCmd[0] == WTX_PCB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = HANDLE_WTX;
        }
        else if (len == 19 && receivedCmd[6] == WIN_APP[0] && receivedCmd[7] == WIN_APP[1]) {
			LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
			cardSTATE = SIMCAL_WIN_APP;
		}
        else if (receivedCmd[0] == 0x02 || receivedCmd[0] == 0x03) { // Iblock avec NS = 1 ou NS = 0
            memcpy(last_cmd, receivedCmd, len); // Copy cmd since add_pcb need it
            cardSTATE = PYTHON_HANDLER;
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
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

            //Initialisation de la communication

        case SIMCAL_IDLE: {

            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            break;

        }
        case SIMCAL_SELECTING: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[0].encodedData, calypso_RESP[0].encodedDataLen);
            eof_time = GetCountUS();
            LogTrace(respATQB, sizeof(respATQB), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_REQUESTING: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[0].encodedData, calypso_RESP[0].encodedDataLen);
            eof_time = GetCountUS();
            LogTrace(respATQB, sizeof(respATQB), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_HALTING: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[1].encodedData, calypso_RESP[1].encodedDataLen);
            eof_time = GetCountUS();
            LogTrace(respOK, sizeof(respOK), (sof_time), (eof_time), NULL, false);
            break;
        }

        case SIMCAL_WIN_APP: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[3].encodedData, calypso_RESP[3].encodedDataLen);
            eof_time = GetCountUS();
            LogTrace(respFILE_NOT_FOUND, sizeof(respFILE_NOT_FOUND), (sof_time), (eof_time), NULL, false);
            break;
        }
        case SIMCAL_B3: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[4].encodedData, calypso_RESP[4].encodedDataLen);
            eof_time = GetCountUS(); 
            LogTrace(respEnd, sizeof(respEnd), (sof_time), (eof_time), NULL, false);
            break;
        }
        case HANDLE_WTX: {
            //WTX = [2283 ms ]
            sow_time = GetCountUS();
            eow_time = GetCountUS();
            bool flag = true;
            while ((eow_time - sow_time) < ( 1 * 100000 ) || flag == true ) {   // Sortir une fois donné recu ou apres 2s pour faire une autre requete
                // Check if there is a packet available
                PacketCommandNG rx;
                memset(&rx.data, 0, sizeof(rx.data));
                int ret = receive_ng(&rx);
                if (ret == PM3_SUCCESS && flag == true) {
                    PyCliPacketReceived(&rx, last_cmd, sizeof(last_cmd));
                    // encode tempframe
                    encodeFrame(&tempframe);
                    pyresp.ok = true;
                    flag = false;
                    Dbprintf("data encoded");
                }
                else if (ret != PM3_ENODATA) {
                    Dbprintf("Error in data reception from pyclient : %d %s", ret, (ret == PM3_EIO) ? "PM3_EIO" : "");
                    // TODO if error, shall we resync ?
                }
                WaitUS(100);
                eow_time = GetCountUS(); 
            }

            Dbprintf("py resp :  %d", pyresp.ok);  
           if (pyresp.ok) {
                Dbprintf("py resp ok");
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(tempframe.encodedData, tempframe.encodedDataLen);
                LogTrace(tempframe.data, tempframe.dataSize , sof_time, eof_time, NULL, false);
                Dbprintf("frame sent to reader"); 
                pyresp.ok = false; 
            }
            else {
                Dbprintf("py resp not ok");
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_RESP[2].encodedData, calypso_RESP[2].encodedDataLen); // On renvoie le WTX
                eof_time = GetCountUS();
                LogTrace(REQ_WTX, sizeof(REQ_WTX), (sof_time), (eof_time), NULL, false);

            }
            break;
        }
        case PYTHON_HANDLER: {
            sof_time = GetCountUS();
            TransmitFor14443b_AsTag(calypso_RESP[2].encodedData, calypso_RESP[2].encodedDataLen);
            eof_time = GetCountUS();
            LogTrace(REQ_WTX, sizeof(REQ_WTX), (sof_time), (eof_time), NULL, false);
            Dbprintf("REQ_WTX sent");
            break;
        }

        default: {
            sof_time = GetCountUS();
            eof_time = GetCountUS();
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            break;
        }
        }
        ++cmdsReceived;
        // Ici je fais une sorte de pause.. Necessaire pour que le lecteur puisse traiter la reponse sinon c'est trop rapide 
        WaitUS(500);
    }
    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
        BigBuf_free();
    }
}

/*
void SendToPyCli(const char* fmt, ...) {
    char output_string[DEBUG_MAX_MSG_SIZE] = { 0x00 };
    char formatted_string[DEBUG_MAX_MSG_SIZE + 4] = { 0x00 }; // Extra space for preamble
    va_list ap;
    va_start(ap, fmt);
    kvsprintf(fmt, output_string, 10, ap);
    va_end(ap);

    // Add the new preamble
    snprintf(formatted_string, sizeof(formatted_string), "%s%s", PYCLI_PREAMBLE, output_string);
    DbpString(formatted_string); 
}
*/



