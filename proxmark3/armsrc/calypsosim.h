// States for CALYPSO SIM command

#define SIMCAL_NOFIELD      0  // �tat lorsque aucun champ RF n'est d�tect�
#define SIMCAL_IDLE         1  // �tat d'inactivit� o� le syst�me est pr�t � communiquer mais aucune activit� sp�cifique n'est en cours
#define SIMCAL_HALTED       2  // �tat o� la communication est suspendue ou arr�t�e
#define SIMCAL_SELECTING    3  // �tat o� le lecteur envoie une commande REQB � la carte pour initier la communication
#define SIMCAL_HALTING      4  // �tat o� la carte r�pond � la commande REQB avec un message ATQB
#define SIMCAL_ACKNOWLEDGE  5  // �tat o� la carte envoie une r�ponse � une commande ATTRIB du lecteur
#define SIMCAL_WORK		    6  // �tat o� la carte est activement engag�e dans la communication avec le lecteur
#define SIMCAL_WIN_APP	    8  // Etape 1 de l'�change de donn�es
#define SIMCAL_GET_DATA	    9  // Etape 2 de l'�change de donn�es
#define SIMCAL_EXC		    10 // Etape 3 de l'�change de donn�es
#define SIMCAL_EXD		    11 // Etape 4 de l'�change de donn�es
#define SIMCAL_EXE		    12 // Etape 5 de l'�change de donn�es
#define SIMCAL_READ_REC_MUL 13 // Etape 6 de l'�change de donn�es





// les etats de state ptr

#define DATA_INVALID 99
#define NO_DATA 0
#define FIRST_DATA 1
#define SECOND_DATA 2
#define THIRD_DATA 3




// Les commandes ISO14443B

#define CALYPSO_WUPB        0x05
#define CALYPSO_ATTRIB      0x1D
#define CALYPSO_RESET       0x0C
#define ISO14443B_HALT      0x50
#define GET_DATA            0xCA 
#define CALYPSO_C           0x02
#define CALYPSO_D           0x03
#define READ_RECORD         0xB2
#define READ_RECORD_MUL     0xB3
#define SELECT              0xA4
#define WTX_PCB             0xF2

// Les commandes Calypso (Cardpeek)

#define SELECT_AID 0xA4



// When the PM acts as tag and is receiving it takes
// 2 ticks delay in the RF part (for the first falling edge),
// 3 ticks for the A/D conversion,
// 8 ticks on average until the start of the SSC transfer,
// 8 ticks until the SSC samples the first data
// 7*16 ticks to complete the transfer from FPGA to ARM
// 8 ticks until the next ssp_clk rising edge
// 4*16 ticks until we measure the time
// - 8*16 ticks because we measure the time of the previous transfer
#define DELAY_AIR2ARM_AS_TAG (2 + 3 + 8 + 8 + 7*16 + 8 + 4*16 - 8*16)



// Les applications Calypso Navigo
#define WIN_APP              ( (unsigned char[]) {(0x0B), (0xA0)} )
#define NAV_INIT             ( (unsigned char[]) {(0x20), (0x00)} )
#define AID_3F04             ( (unsigned char[]) {(0x3F), (0x04)} )
#define ICC_0002             ( (unsigned char[]) {(0x00), (0x02)} )
#define ID_0003              ( (unsigned char[]) {(0x00), (0x03)} )
#define DISPLAY_2F10         ( (unsigned char[]) {(0x2F), (0x10)} )
#define TICKETING_2000       ( (unsigned char[]) {(0x20), (0x00)} )
#define AID_2004             ( (unsigned char[]) {(0x20), (0x04)} )
#define ENVIRONMENT_2001     ( (unsigned char[]) {(0x20), (0x01)} )
#define EVENT_LOG_2010       ( (unsigned char[]) {(0x20), (0x10)} )
#define CONTRACTS_2020       ( (unsigned char[]) {(0x20), (0x20)} )
#define CONTRACTS_2030       ( (unsigned char[]) {(0x20), (0x30)} )
#define COUNTERS_202A        ( (unsigned char[]) {(0x20), (0x2A)} )
#define COUNTERS_202B        ( (unsigned char[]) {(0x20), (0x2B)} )
#define COUNTERS_202C        ( (unsigned char[]) {(0x20), (0x2C)} )
#define COUNTERS_2069        ( (unsigned char[]) {(0x20), (0x69)} )
#define SPECIAL_EVENTS_2040  ( (unsigned char[]) {(0x20), (0x40)} )
#define CONTRACT_LIST_2050   ( (unsigned char[]) {(0x20), (0x50)} )
#define RECORD               ( (unsigned char[]) {(0x20), (0x00)} )



// les etats associ�s a chaque commandes
#define SIMCAL_INIT                     14
#define SIMCAL_AID_3F04                 15
#define SIMCAL_ICC_0002                 16
#define SIMCAL_ID_0003                  17
#define SIMCAL_DISPLAY_2F10             18
#define SIMCAL_TICKETING_2000           19
#define SIMCAL_AID_2004                 20
#define SIMCAL_ENVIRONMENT_2001         21
#define SIMCAL_EVENT_LOG_2010           22
#define SIMCAL_CONTRACTS_2020           23
#define SIMCAL_CONTRACTS_2030		    24
#define SIMCAL_COUNTERS_202A            25
#define SIMCAL_COUNTERS_202B            26
#define SIMCAL_COUNTERS_202C            27 
#define SIMCAL_COUNTERS_2069            28
#define SIMCAL_SPECIAL_EVENTS_2040      29
#define SIMCAL_CONTRACT_LIST_2050       30
#define SIMCAL_SELECT_NULL              31
#define SIMCAL_READ_RECORD              32
#define SIMCAL_REQUESTING               33
#define SIMCAL_UNKNOW                   34
#define SIMCAL_WAITING                  35
#define PYTHON_HANDLER				    36
#define HANDLE_WTX                      37
#define SIMCAL_B3					    38


//structs pour les ATS et RESP
typedef struct {
    uint8_t* encodedData;
    uint16_t encodedDataLen;
    uint8_t* data;
    size_t dataSize;
    bool pcb_added;
    bool crc_added;
} CalypsoFrame;

//la structure pour contenir la trame S(WTX)
typedef struct {
    uint8_t pcb;
    uint8_t wtxm;
} WTXFrame;

// Les type de Iblock support�s
typedef enum {
    NS_0,
    NS_1,
    WUPB,
    ATTRIB,
    REQB,
    UNKNOWN
} IblockType;


void SimulateCalypsoTag(const uint8_t* pupi);
void InitCalypso(const uint8_t* pupi);
void add_crc_and_update_trame(CalypsoFrame* entry);
IblockType analyzeIBlock(uint8_t* frame);
void add_pcb(CalypsoFrame* entry, uint8_t* received_frame, size_t received_frame_size);
void create_WTX_command_with_crc(uint8_t duration_ms, uint8_t* frame, size_t* frame_size);
void print_frame(uint8_t* frame, size_t frame_size);
void CheckRF(const uint8_t* period_ms);
void init14b(const uint8_t* pupi);




// Shema cardpeek : Les fichiers et dossiers de la carte Calypso
/*LFI_LIST = {

  { "AID",              "/3F04",      "file" },
  { "ICC",              "/0002",      "file" },
  { "ID",               "/0003",      "file" },
  { "Holder Extended",  "/3F1C",      "file" },
  { "Display / Free",   "/2F10",      "file" },

  { "Ticketing",        "/2000",    "folder" },
  { "AID",              "/2000/2004", "file" },
  { "Environment",      "/2000/2001", "file" },
  { "Holder",           "/2000/2002", "file" },
  { "Event logs",       "/2000/2010", "file" },
  { "Contracts",        "/2000/2020", "file" },
  { "Contracts",        "/2000/2030", "file" },
  { "Counters",         "/2000/202A", "file" },
  { "Counters",         "/2000/202B", "file" },
  { "Counters",         "/2000/202C", "file" },
  { "Counters",         "/2000/202D", "file" },
  { "Counters",         "/2000/202E", "file" },
  { "Counters",         "/2000/202F", "file" },
  { "Counters",         "/2000/2060", "file" },
  { "Counters",         "/2000/2061", "file" },
  { "Counters",         "/2000/2062", "file" },
  { "Counters",         "/2000/2069", "file" },
  { "Counters",         "/2000/206A", "file" },
  { "Special events",   "/2000/2040", "file" },
  { "Contract list",    "/2000/2050", "file" },
  { "Free",             "/2000/20F0", "file" },

  { "MPP",              "/3100",    "folder" },
    { "AID",            "/3100/3104", "file" },
    { "Public Param.",  "/3100/3102", "file" },
    { "Log",            "/3100/3115", "file" },
    { "Contracts",      "/3100/3120", "file" },
    { "Counters",       "/3100/3113", "file" },
    { "Counters",       "/3100/3123", "file" },
    { "Counters",       "/3100/3133", "file" },
    { "Counters",       "/3100/3169", "file" },
    { "Miscellaneous",  "/3100/3150", "file" },
    { "Free",           "/3100/31F0", "file" },

  { "RT2",              "/2100",    "folder" },
    { "AID",            "/2100/2104", "file" },
    { "Environment",    "/2100/2101", "file" },
    { "Event logs",     "/2100/2110", "file" },
    { "Contract list",  "/2100/2150", "file" },
    { "Contracts",      "/2100/2120", "file" },
    { "Counters",       "/2100/2169", "file" },
    { "Special events", "/2100/2140", "file" },
    { "Free",           "/2100/21F0", "file" },

  { "EP",               "/1000",    "folder" },
   { "AID",             "/1000/1004", "file" },
   { "Load Log",        "/1000/1014", "file" },
   { "Purchase Log",    "/1000/1015", "file" },

  { "eTicket",          "/8000",    "folder" },
   { "AID",             "/8000/8004", "file" },
   { "Preselection",    "/8000/8030", "file" },
   { "Event logs",      "/8000/8010", "file" }
}*/