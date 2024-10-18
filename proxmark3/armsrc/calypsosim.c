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
#include <stdio.h>
#include "pyclient_handler.h"




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



// Pour la synchronisation
static int lastState = -1; // Pour suivre l'etat pr�c�dent de la carte


static WTXFrame create_WTX_command(uint8_t wtxm) {
    WTXFrame wtx_command;

    // V�rifier que la dur�e est comprise entre 1 et 59 ms
    if (wtxm < 1) {
        wtxm = 1;
    }
    else if (wtxm > 59) {
        wtxm = 59;
    }

    // Construire la commande WTX
    wtx_command.pcb = 0xF2; // PCB pour une commande S(WTX)
    wtx_command.wtxm = (0x00 << 6) | (wtxm & 0x3F); // WTXM avec les bits r�serv�s � 00 et les bits WTXM (b6-b1)

    return wtx_command;
}

// Fonction pour ajouter le CRC � une commande WTX et cr�er une trame compl�te
void create_WTX_command_with_crc(uint8_t duration_ms, uint8_t* frame, size_t* frame_size) {
    // Cr�er la commande WTX
    WTXFrame wtx_command = create_WTX_command(duration_ms);

    // Initialiser la trame avec PCB et WTXM
    frame[0] = wtx_command.pcb;
    frame[1] = wtx_command.wtxm;

    // Ajouter deux octets 0x00 pour le calcul du CRC
    frame[2] = 0x00;
    frame[3] = 0x00;
    *frame_size = 4;

    // Calculer le CRC sur tous les octets sauf les deux derniers
    AddCrc14B(frame, *frame_size - 2);

    // La taille de la trame finale est d�j� mise � jour avec l'ajout du CRC
}

void print_frame(uint8_t* frame, size_t frame_size) {
    Dbprintf("Frame : ");
    for (size_t i = 0; i < frame_size; ++i) {
        Dbprintf("0x%02X ", frame[i]);
    }
}

// Retourne 0 si l'I-Block a N(S) = 0, 1 si N(S) = 1, -1 si ce n'est pas un I-Block valide

IblockType analyzeIBlock(uint8_t* frame) {
    if (frame[0] == 0x02) {
        return NS_0;
    }
    else if (frame[0] == 0x03) {
        return NS_1;
    }
    else if (frame[0] == 0x05) {
        return WUPB;
    }
    else {
        return UNKNOWN;
    }
}



// Ajout du pcb � la trame � envoyer en fonction de l'I-Block re�u

void add_pcb(CalypsoFrame* entry, uint8_t* received_frame, size_t received_frame_size) {
    // V�rifiez si le PCB a d�j� �t� ajout�
    if (entry->pcb_added) {
        return; // Si le PCB a d�j� �t� ajout�, ne rien faire
    }

    IblockType iblock_type = analyzeIBlock(received_frame);

    if (iblock_type == NS_0) {
        // Trame re�ue commence par 0x02, ajouter 0x02 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->dataSize);
        entry->data[0] = 0x02;
        entry->dataSize++;
    }
    else if (iblock_type == NS_1) {
        // Trame re�ue commence par 0x03, ajouter 0x03 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->dataSize);
        entry->data[0] = 0x03;
        entry->dataSize++;

    }
    else if (iblock_type == WUPB) {
        // Trame re�ue commence par 0x05, ajouter 0x05 au d�but de la trame � envoyer
        memmove(entry->data + 1, entry->data, entry->dataSize);
        entry->data[0] = 0x50;
        entry->dataSize++;
    }
    else {
        // Trame re�ue ne commence ni par 0x02 ni par 0x03, gestion d'erreur
        //Dbprintf("Attention : ATS Iblock non identifie .\n");

    }

    // Marquer que le PCB a �t� ajout�
    entry->pcb_added = true;
    return;
}

void add_crc_and_update_trame(CalypsoFrame* entry) {
    if (entry->crc_added) {
        return;
    }

    // Ajouter deux octets 0x00 pour le calcul du CRC
    entry->data[entry->dataSize] = 0x00;
    entry->data[entry->dataSize + 1] = 0x00;
    entry->dataSize += 2;

    // Calculer le CRC sur tous les octets sauf les deux derniers
    AddCrc14B(entry->data, entry->dataSize - 2);

    // Marquer que le CRC a �t� ajout�
    entry->crc_added = true;
    return;
}




static void Send_WTX(uint8_t* encodedData, uint16_t encodedDataLen, uint8_t* logData, uint16_t logDataLen, int state) {
    uint32_t sof_time = GetCountUS();
    TransmitFor14443b_AsTag(encodedData, encodedDataLen);
    uint32_t eof_time = GetCountUS();
    LogTrace(logData, logDataLen, sof_time, eof_time, NULL, false);
    lastState = state;
}


static void process_and_send_frame(CalypsoFrame* frame, uint8_t* receivedCmd, size_t len) {
    uint64_t sof_time = GetCountUS();

    if (frame->pcb_added == false) {
        add_pcb(frame, receivedCmd, len);
    }

    if (frame->crc_added == false) {

        // Ajouter CRC et mettre � jour la trame
        add_crc_and_update_trame(frame);
    }

    // Encoder les donn�es selon ISO 14443b
    CodeIso14443bAsTag(frame->data, frame->dataSize);

    // Allouer de la m�moire pour les donn�es encod�es
    tosend_t* ts = get_tosend();
    frame->encodedData = BigBuf_malloc(ts->max);
    if (frame->encodedData == NULL) {
        Dbprintf("Erreur : Impossible d'allouer de la m�moire pour les donn�es encod�es.\n");
        return;
    }
    frame->encodedDataLen = ts->max;
    memcpy(frame->encodedData, ts->buf, ts->max);

    // Transmettre les donn�es encod�es
    TransmitFor14443b_AsTag(frame->encodedData, frame->encodedDataLen);

    // Lib�rer la m�moire allou�e
    BigBuf_free();

    uint64_t eof_time = GetCountUS();

    // Enregistrer la trace du log avec les timestamps
    LogTrace(frame->data, frame->dataSize, sof_time, eof_time, NULL, false);
}



void SimulateCalypsoTag(const uint8_t* pupi) {

    // uint32_t fwt = 0; // Pour un test sur le temps de r�ponse en vue de trouver la bonne valeur de FWT


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

    uint8_t* receivedCmd = BigBuf_calloc(MAX_FRAME_SIZE);

    // response to ATQB sans CRC (On le calcule nous meme)

    uint8_t respATQB[] = {
        0x50,                       // Code de r�ponse ATQB
        0x00, 0x00, 0x00, 0x00,     // PUPI/UID / Place holder for UID
        0x00, 0x00, 0x00, 0x00,	   // Application data
        0x00,                      // Bit rate capacity
        0x71,                       // Max frame size
        0x71, 					   // FWI/Coding Options
        0x1E,0x37                  // CRC de l'ATQB
    };
    // response to HLTB and ATTRIB
    uint8_t respOK[] = { 0x00, 0x78,  0xF0 };
    uint8_t resp_get_data[] = { 0x6B, 0x00 };
    uint8_t respFILE_NOT_FOUND[] = { 0x6A, 0x82 };
    // uint8_t respd[] = { 0x6A, 0x82 };
    uint8_t respe[] = { 0xA3,  0xE9,  0x67 };

    uint8_t resp_rec_mul[] = { 0xA2 };

    //uint8_t resp_select_null[] = { 0x6A,  0x82 };

    uint8_t respNO_MORE_RECORD[] = { 0x6A, 0x83 };

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
    uint8_t respAID_3F04[] = { 0x33, 0x4D, 0x54, 0x52, 0x2E, 0x49, 0x43, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00 };
    uint8_t respICC_0002[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x71, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xF7, 0x0B, 0xF7, 0x02, 0x50, 0x14, 0x00, 0x01, 0x2C, 0xA7, 0x3A, 0x00, 0x90, 0x00 };
    uint8_t respID_0003[] =  { 0x02,  0x69, 0x82 };


    // ATS response sans CRC, On le calcule nous meme

    uint8_t ATS_INIT[] =     { 0x85, 0x17, 0x00, 0x02, 0x00, 0x00, 0x00, 0x12, 0x12, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00, 0x15, 0x15, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00 };
    uint8_t ATS_AID_3F04[] = { 0x85, 0x17, 0x04, 0x04, 0x02, 0x10, 0x01, 0x1F, 0x12, 0x00, 0x00, 0x01, 0x01, 0x01,0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00, 0x90, 0x00  };
    uint8_t ATS_ICC_0002[] = { 0x85, 0x17, 0x02, 0x04, 0x02, 0x1D, 0x01, 0x1F, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x90, 0x00 };
    uint8_t ATS_ID_0003[] =  { 0x85, 0x17, 0x03, 0x04, 0x02, 0x1D, 0x01, 0x01, 0x12, 0x00, 0x00, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00 };

    uint8_t REQ_WTX[] = { 0xF2, 0x3B, 0xAF, 0xCF }; // S(WTX) avec un WTXM = 59 [1-59]

    bool active = false; // flag pour indiquer si la carte est en etat actif

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

    CalypsoFrame  calypso_ATS[] = {
        { NULL, 0, ATS_INIT, sizeof(ATS_INIT), false, false},
        { NULL, 0, ATS_AID_3F04, sizeof(ATS_AID_3F04), false, false },
        { NULL, 0, ATS_ICC_0002, sizeof(ATS_ICC_0002), false, false },
        { NULL, 0, ATS_ID_0003, sizeof(ATS_ID_0003), false, false }

    };

    CalypsoFrame  calypso_RESP[] = {
    { NULL, 0, respATQB, sizeof(respATQB), true, true },
    { NULL, 0, respOK, sizeof(respOK), true, true },
    { NULL, 0, respe, sizeof(respe), false, false }, 
    { NULL, 0, resp_get_data, sizeof(resp_get_data), false, false },
    { NULL, 0, respFILE_NOT_FOUND, sizeof(respFILE_NOT_FOUND), false, false },
    { NULL, 0, resp_rec_mul, sizeof(resp_rec_mul), false, false },
    { NULL, 0, respAID_3F04, sizeof(respAID_3F04), false, false },
    { NULL, 0, respICC_0002, sizeof(respICC_0002), false, false },
    { NULL, 0, respID_0003, sizeof(respID_0003), false, false },
    { NULL, 0, respNO_MORE_RECORD, sizeof(respNO_MORE_RECORD), false, false },
    };

    CalypsoFrame  calypso_REQ[] = {
    { NULL, 0, REQ_WTX, sizeof(REQ_WTX), true, true}

    };

    uint16_t len, cmdsReceived = 0;
    int cardSTATE = SIMCAL_NOFIELD;
    int vHf = 0; // in mV

    tosend_t* ts = get_tosend();


    // Prepare WTX
    CodeIso14443bAsTag(calypso_REQ[0].data, calypso_REQ[0].dataSize);
    calypso_REQ[0].encodedData = BigBuf_malloc(ts->max);
    calypso_REQ[0].encodedDataLen = ts->max;
    memcpy(calypso_REQ[0].encodedData, ts->buf, ts->max);

    // Prepare first frame for initialisation (ATQB et OK)

    for (size_t i = 0; i < 3; ++i) {
        CodeIso14443bAsTag(calypso_RESP[i].data, calypso_RESP[i].dataSize);
        calypso_RESP[i].encodedData = BigBuf_malloc(ts->max);
        calypso_RESP[i].encodedDataLen = ts->max;
        memcpy(calypso_RESP[i].encodedData, ts->buf, ts->max);
    }

    BigBuf_free();
    StartCountUS();

    // Donn�es de synchronisation
    uint32_t eof_time = 0;
    uint32_t sof_time = 0;
    uint8_t state = 0;
    uint8_t* data_ptr = &state;
 

    // Simulation loop
    while (BUTTON_PRESS() == false) {

        WDT_HIT();
        //   Dbprintf("state : %d", *data_ptr);

        uint8_t* prev_cmd = 0; // Pour suivre la derni�re commande re�ue cmd -1

        if (data_available()) {
            Dbprintf("Data available");
            break;
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
        sof_time = GetCountUS();

        // Je vais utiliser un systeme de jeton pour gerer les WTX
        if (cardSTATE == HANDLE_WTX ){
            bool jetons = false;
            while (!jetons) {
				if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
					Dbprintf("button pressed, received %d commands", cmdsReceived);
					break;
				}
				if (len == 4 && receivedCmd[0] == WTX_PCB) {
					LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                    jetons = true; // On a re�u un jeton (ack de wtx par PCD)
                    cardSTATE = SIMCAL_IDLE; // On revient � l'�tat idle
					continue;
				}

			}
		}
        else 
        {
            if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
                Dbprintf("button pressed, received %d commands", cmdsReceived);
                break;
            }

        }
        // Get reader command


        eof_time = GetCountUS();

        // Control automatic the FWT
        /*
        if (receivedCmd[0] == CALYPSO_ATTRIB) {
            Dbprintf("FWT ideal trouv� : %d", fwt);
        }
        else
        {
            fwt = fwt + 10;
            Dbprintf("fwt : %d", fwt);
        }
        */
        BigBuf_free();



        if (receivedCmd[0] != WTX_PCB) {
            //save the last command received
            prev_cmd = receivedCmd;
        }

        if (receivedCmd[0] == CALYPSO_WUPB) {
            if (len == 5 && receivedCmd[2] == 0x08 && !active) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_SELECTING;
            }
            else if (len == 5 && receivedCmd[2] == 0x00 && !active) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_REQUESTING;
            }
            else
            {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                continue;
            }
        }

        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB && !active ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_HALTING;
        }

        else if (len == 19 && receivedCmd[5] == WIN_APP[0] && receivedCmd[6] == WIN_APP[1] ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_WIN_APP;
        }
        else if (len == 8 && receivedCmd[2] == GET_DATA) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_GET_DATA;

        }
        else if (len == 17 && receivedCmd[0] == CALYPSO_C ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_EXC;

        }

        else if (len == 3 && receivedCmd[0] == READ_RECORD ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_READ_RECORD;
            *data_ptr = DATA_INVALID; // Pour l'application WIN_APP
        }

        else if (len == 3 && receivedCmd[0] == READ_RECORD_MUL  ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_READ_REC_MUL;
        }

        //SELECT (CardPeek)
        else if (len == 11 && receivedCmd[6] == NAV_INIT[0] && receivedCmd[7] == NAV_INIT[1]) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_INIT;
        }

        else if (len == 11 && receivedCmd[2] == SELECT ) {

            if (receivedCmd[6] == AID_3F04[0] && receivedCmd[7] == AID_3F04[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_AID_3F04;
            }
            else if (receivedCmd[6] == ICC_0002[0] && receivedCmd[7] == ICC_0002[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_ICC_0002;
            }
            else if (receivedCmd[6] == ID_0003[0] && receivedCmd[7] == ID_0003[1]) {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_ID_0003;
            }
            else
            {
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                cardSTATE = SIMCAL_SELECT_NULL;
                continue;
            }
        }

        else if (len == 8 && receivedCmd[2] == READ_RECORD  ) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_READ_RECORD;
            
        }

        // Reset command
        else if (len == 3 && receivedCmd[0] == CALYPSO_RESET) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            active = false;
        }

        else if (len == 4 && receivedCmd[0] == WTX_PCB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_WAITING;
        }

        else {
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
                           //Windows (Generic)

        case SIMCAL_WIN_APP: {
            // process_and_send_frame(&calypso_RESP[2], receivedCmd, len);
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_WIN_APP);
            cardSTATE = HANDLE_WTX;
            // process_and_send_frame(&calypso_RESP[3], receivedCmd, len);
            active = true; // la carte est activ�e
            break;
        }
        case SIMCAL_GET_DATA: {

            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_GET_DATA);
            //process_and_send_frame(&calypso_RESP[2], receivedCmd, len);
            break;
        }
        case SIMCAL_EXC: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_EXC);
            //process_and_send_frame(&calypso_RESP[3], receivedCmd, len);
            break;

        }

        case SIMCAL_EXE: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_EXE);
            //process_and_send_frame(&calypso_RESP[4], receivedCmd, len);
            break;
        }

        case SIMCAL_READ_REC_MUL: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_READ_REC_MUL);
            //process_and_send_frame(&calypso_RESP[5], receivedCmd, len);
            break;
        }
                                // STATE (Cardpeek)

        case SIMCAL_INIT: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_INIT);
            process_and_send_frame(&calypso_ATS[0], receivedCmd, len);
            break;
        }
        case SIMCAL_AID_3F04: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_INIT);
            //process_and_send_frame(&calypso_ATS[1], receivedCmd, len);
            *data_ptr = FIRST_DATA; //POur le prochain etat (Select_file)
            break;

        }
        case SIMCAL_ICC_0002: {
            // Dbprintf("ICC_0002");
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_INIT);
            //process_and_send_frame(&calypso_ATS[2], receivedCmd, len);
            *data_ptr = SECOND_DATA;
            break;
        }
        case SIMCAL_ID_0003: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_INIT);
            //process_and_send_frame(&calypso_ATS[3], receivedCmd, len);
            *data_ptr = THIRD_DATA;
            break;
        }
        case SIMCAL_SELECT_NULL: {
            Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_INIT);
            //process_and_send_frame(&calypso_RESP[4], receivedCmd, len);
            break;
        }

        case SIMCAL_READ_RECORD: {
            // Vu que la comd pour lire un fichier apres l'ATS est la meme que pour les autres, je vais mettre un suivi state pour les differencier
            if (*data_ptr == FIRST_DATA) { // Premier donn�e de l'application AID_3F04
                Send_WTX(calypso_REQ[0].encodedData, calypso_REQ[0].encodedDataLen, REQ_WTX, sizeof(REQ_WTX), SIMCAL_READ_RECORD);
                //process_and_send_frame(&calypso_RESP[6], receivedCmd, len);
                *data_ptr = NO_DATA;
                break;
            }
            else if (*data_ptr == SECOND_DATA) {
                process_and_send_frame(&calypso_RESP[7], receivedCmd, len);
                eof_time = GetCountUS();
                *data_ptr = NO_DATA;
                break;
            }
            else if (*data_ptr == THIRD_DATA) {
                process_and_send_frame(&calypso_RESP[8], receivedCmd, len);
                *data_ptr = NO_DATA;
                break;
            }

            // Si on a pas de donn�e a lire cela signifie qu'il faudra remttre state a 0 a chaque fin de lecture des donn�es
            else if (*data_ptr == NO_DATA) {
                process_and_send_frame(&calypso_RESP[9], receivedCmd, len);
                break;
            }
            else if (*data_ptr == DATA_INVALID) {
                sof_time = GetCountUS();
                TransmitFor14443b_AsTag(calypso_RESP[2].encodedData, calypso_RESP[2].encodedDataLen);
                eof_time = GetCountUS();
                LogTrace(respe, sizeof(respe), (sof_time), (eof_time), NULL, false);
				break;
			}
            else
            {
                //process_and_send_frame(&calypso_REQ[0], receivedCmd, len);
                break;
            }
            break;
        }

        case SIMCAL_WAITING: {

            switch (lastState)
            {
            case SIMCAL_WIN_APP: {
                process_and_send_frame(&calypso_RESP[4], prev_cmd, sizeof(prev_cmd));
                break;
            case SIMCAL_GET_DATA: {
                process_and_send_frame(&calypso_RESP[4], prev_cmd, sizeof(prev_cmd));
                break;
            }

            }
            default:
                break;
            }
            break;

        }

        default: {

            Dbprintf("Etat non reconnu");
            break;
        }

        }

        ++cmdsReceived;
        // Ici je fais une sorte de pause.. Necessaire pour que le lecteur puisse traiter la reponse sinon c'est trop rapide 
        // Attention a ne pas toucher a cette valeur. 700 microsecondes est le temps necessaire pour que le lecteur puisse traiter la reponse
        //waitForFWT(600);


	}

       
    
    switch_off();

    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
        BigBuf_free();
    }
}


void InitCalypso(const uint8_t* pupi) {
    init14b(pupi);
}

/*
void InitCalypso(const uint8_t* pupi) {

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
        0x1E,0x37                  // CRC de l'ATQB
    };

    uint8_t respOK[] = { 0x00, 0x78,  0xF0 };
    uint8_t respFILE_NOT_FOUND[] = { 0x02, 0x6A, 0x82, 0x4B, 0x4C };
    uint8_t REQ_WTX[] = { 0xF2, 0x14, 0x5A, 0x16 }; // S(WTX) avec un WTXM = 20 [1-59]



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




    CalypsoFrame  calypso_RESP[] = {
    { NULL, 0, respATQB, sizeof(respATQB), true, true },
    { NULL, 0, respOK, sizeof(respOK), true, true },
    { NULL, 0, REQ_WTX, sizeof(REQ_WTX), true, true },
	{ NULL, 0, respFILE_NOT_FOUND, sizeof(respFILE_NOT_FOUND), true, true }
    };



    uint16_t len, cmdsReceived = 0;
    int cardSTATE = SIMCAL_NOFIELD;
    int vHf = 0; // in mV

    tosend_t* ts = get_tosend();

    // Prepare first frame for initialisation (ATQB et OK)

    for (size_t i = 0; i < 4; ++i) {
        CodeIso14443bAsTag(calypso_RESP[i].data, calypso_RESP[i].dataSize);
        calypso_RESP[i].encodedData = BigBuf_malloc(ts->max);
        calypso_RESP[i].encodedDataLen = ts->max;
        memcpy(calypso_RESP[i].encodedData, ts->buf, ts->max);
    }

    BigBuf_free();


    StartCountUS();
    uint32_t eof_time = 0; // pours les logs
    uint32_t sof_time = 0;

    bool flag = true; // 


    // Simulation loop
    while (BUTTON_PRESS() == false) {

        WDT_HIT();

        cardSTATE = SIMCAL_IDLE; // On revient a l'etat idle
        //   Dbprintf("state : %d", *data_ptr);

        if (data_available()) {
            Dbprintf("Data available");
            break;
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
        sof_time = GetCountUS();
        // Get reader command
        if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
            Dbprintf("button pressed, received %d commands", cmdsReceived);
            break;
        }
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
        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_HALTING;
        }
        else if (len == 19 && receivedCmd[5] == WIN_APP[0] && receivedCmd[6] == WIN_APP[1]) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = SIMCAL_WIN_APP;
        }
        else if (len == 4 && receivedCmd[0] == WTX_PCB) {
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
            cardSTATE = HANDLE_WTX;
        }

        else if (receivedCmd[0] == 0x02 || receivedCmd[0] == 0x03) { // Iblock avec NS = 1 ou NS = 0
            cardSTATE = PYTHON_HANDLER;
            LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
        }


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


        case PYTHON_HANDLER: {
            while (BUTTON_PRESS() == false && flag ) {
                WDT_HIT();
                sof_time = GetCountUS();
				if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
                    ++cmdsReceived;
				}
                eof_time = GetCountUS();
                LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true); // Ne devrais pas etre la mais je fais pas confiance au lecteur
				
                if (receivedCmd[0] == WTX_PCB) {
					LogTrace(receivedCmd, len, (sof_time), (eof_time), NULL, true);
                    Dbprintf("Ack OK");
                    flag = false;
				}
                
            }
            
            break;
        }


        default: {
            // Dbprintf("Etat non reconnu");
            break;

        }

        }

        ++cmdsReceived;

        // Ici je fais une sorte de pause.. Necessaire pour que le lecteur puisse traiter la reponse sinon c'est trop rapide 
        // Attention a ne pas toucher a cette valeur. 700 microsecondes est le temps necessaire pour que le lecteur puisse traiter la reponse
        WaitUS(700);

    }
    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
        BigBuf_free();
    }
}
*/

void CheckRF(const uint8_t* period_ms) {

    Dbprintf("RF Detector");
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
    

    int vHf = 0; // in mV
    int cardSTATE = SIMCAL_NOFIELD;

    // Setup and start DMA.
    if (!FpgaSetupSscDma((uint8_t*)dma->buf, DMA_BUFFER_SIZE)) {
        if (g_dbglevel > DBG_ERROR) DbpString("FpgaSetupSscDma failed. Exiting");
        switch_off();
        return;
    }

    BigBuf_free();
    StartCountUS();
   // uint8_t datatest = { 0x01, 0x02 };
    while (BUTTON_PRESS() == false) {

        WDT_HIT();
        //   Dbprintf("state : %d", *data_ptr);


        if (data_available()) {
            Dbprintf("Data available");
            break;
        }

        // find reader field
        vHf = (MAX_ADC_HF_VOLTAGE * SumAdc(ADC_CHAN_HF, 32)) >> 15;

        if (vHf > MF_MINFIELDV) {
            cardSTATE = SIMCAL_IDLE;
        }
        else {
            cardSTATE = SIMCAL_NOFIELD;

        }
        switch (cardSTATE) {

            case SIMCAL_IDLE: {
                //SendPyClient("Champs Radio detecte");
                Dbprintf("Champs Radio OK");
                LED_A_ON();
                break;

            }
            case SIMCAL_NOFIELD: {
                LED_A_OFF();
                break;
            }
            default: {
                break;
            }
        }
        WaitMS((uint32_t)*period_ms);
    }
    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
        BigBuf_free();
    }
}


//-----------------------------------------------------------------------------
// Main loop of simulated tag: receive commands from reader, decide what
// response to send, and send it.
//-----------------------------------------------------------------------------
void  init14b(const uint8_t* pupi) {

    /*
        // the only commands we understand is WUPB, AFI=0, Select All, N=1:
        static const uint8_t cmdWUPB[] = { ISO14443B_REQB, 0x00, 0x08, 0x39, 0x73 };
        // ... and REQB, AFI=0, Normal Request, N=1:
        static const uint8_t cmdREQB[] = { ISO14443B_REQB, 0x00, 0x00, 0x71, 0xFF };
        // ... and HLTB
        static const uint8_t cmdHLTB[] = { 0x50, 0xff, 0xff, 0xff, 0xff };
        // ... and ATTRIB
        static const uint8_t cmdATTRIB[] = { ISO14443B_ATTRIB, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    */
    LED_A_ON();

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

    // ... if not PUPI/UID is supplied we always respond with ATQB, PUPI = 820de174, Application Data = 0x20381922,
    // supports only 106kBit/s in both directions, max frame size = 32Bytes,
    // supports ISO14443-4, FWI=8 (77ms), NAD supported, CID not supported:
    uint8_t respATQB[] = {
        0x50,
        0x82, 0x0d, 0xe1, 0x74,
        0x20, 0x38, 0x19,
        0x22, 0x00, 0x21, 0x85,
        0x5e, 0xd7
    };

    // ...PUPI/UID supplied from user. Adjust ATQB response accordingly
    if (memcmp("\x00\x00\x00\x00", pupi, 4) != 0) {
        memcpy(respATQB + 1, pupi, 4);
        AddCrc14B(respATQB, 12);
    }

    // response to HLTB and ATTRIB
    static const uint8_t respOK[] = { 0x00, 0x78, 0xF0 };

    uint16_t len, cmdsReceived = 0;
    int cardSTATE = SIM_POWER_OFF;
    int vHf = 0; // in mV

    const tosend_t* ts = get_tosend();

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

    // Simulation loop
    while (BUTTON_PRESS() == false) {
        WDT_HIT();

        //iceman: limit with 2000 times..
        if (data_available()) {
            break;
        }

        // find reader field
        vHf = (MAX_ADC_HF_VOLTAGE * SumAdc(ADC_CHAN_HF, 32)) >> 15;
        if (vHf > MF_MINFIELDV) {
            if (cardSTATE == SIM_POWER_OFF) {
                cardSTATE = SIM_IDLE;
                LED_A_ON();
            }
        }
        else {
            cardSTATE = SIM_POWER_OFF;
            LED_A_OFF();
        }

        if (cardSTATE == SIM_POWER_OFF) {
            continue;
        }

        // Get reader command
        if (GetIso14443bCommandFromReader(receivedCmd, &len) == false) {
            Dbprintf("button pressed, received %d commands", cmdsReceived);
            break;
        }

        LogTrace(receivedCmd, len, 0, 0, NULL, true);

        if ((len == 5) && (receivedCmd[0] == ISO14443B_REQB) && (receivedCmd[2] & 0x08)) {
            // WUPB
            switch (cardSTATE) {
            case SIM_IDLE:
            case SIM_READY:
            case SIM_HALT: {
                TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
                LogTrace(respATQB, sizeof(respATQB), 0, 0, NULL, false);
                cardSTATE = SIM_READY;
                break;
            }
            case SIM_ACTIVE:
            default: {
                TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
                LogTrace(respATQB, sizeof(respATQB), 0, 0, NULL, false);
                break;
            }
            }
        }
        else if ((len == 5) && (receivedCmd[0] == ISO14443B_REQB) && !(receivedCmd[2] & 0x08)) {
            // REQB
            switch (cardSTATE) {
            case SIM_IDLE:
            case SIM_READY: {
                TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
                LogTrace(respATQB, sizeof(respATQB), 0, 0, NULL, false);
                cardSTATE = SIM_READY;
                break;
            }
            case SIM_ACTIVE: {
                TransmitFor14443b_AsTag(encodedATQB, encodedATQBLen);
                LogTrace(respATQB, sizeof(respATQB), 0, 0, NULL, false);
                break;
            }
            case SIM_HALT:
            default: {
                break;
            }
            }
        }
        else if ((len == 7) && (receivedCmd[0] == ISO14443B_HALT)) {
            // HLTB
            switch (cardSTATE) {
            case SIM_READY: {
                TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
                LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
                cardSTATE = SIM_HALT;
                break;
            }
            case SIM_IDLE:
            case SIM_ACTIVE: {
                TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
                LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
                break;
            }
            case SIM_HALT:
            default: {
                break;
            }
            }
        }
        else if (len == 11 && receivedCmd[0] == ISO14443B_ATTRIB) {
            // ATTRIB
            switch (cardSTATE) {
            case SIM_READY: {
                TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
                LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
                cardSTATE = SIM_ACTIVE;
                break;
            }
            case SIM_IDLE:
            case SIM_ACTIVE: {
                TransmitFor14443b_AsTag(encodedOK, encodedOKLen);
                LogTrace(respOK, sizeof(respOK), 0, 0, NULL, false);
                break;
            }
            case SIM_HALT:
            default: {
                break;
            }
            }
        }
		else {
			// Unknown command
			LogTrace(receivedCmd, len, 0, 0, NULL, true);
			continue;
		}

        ++cmdsReceived;
    }

    switch_off();
    if (g_dbglevel >= DBG_DEBUG) {
        Dbprintf("Emulator stopped. Trace length: %d ", BigBuf_get_traceLen());
    }
}




