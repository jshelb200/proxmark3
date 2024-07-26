#include <ctype.h>          // tolower
#include "cmdparser.h"      // command_t
#include "cliparser.h"      // parse
#include "comms.h"          // clearCommandBuffer
#include "lfdemod.h"        // computeSignalProperties
#include "cmdhf14a.h"       // ISO14443-A
#include "cmdhf14b.h"       // ISO14443-B
#include "cmdhf15.h"        // ISO15693
#include "cmdhfcipurse.h"   // CIPURSE transport cards
#include "cmdhfcryptorf.h"  // CryptoRF
#include "cmdhfepa.h"       // German Identification Card
#include "cmdhfemrtd.h"     // eMRTD
#include "cmdhffelica.h"    // ISO18092 / FeliCa
#include "cmdhffido.h"      // FIDO authenticators
#include "cmdhffudan.h"     // Fudan cards
#include "cmdhfgallagher.h" // Gallagher DESFire cards
#include "cmdhficlass.h"    // ICLASS
#include "cmdhfict.h"       // ICT MFC / DESfire cards
#include "cmdhfjooki.h"     // MFU based Jooki
#include "cmdhfksx6924.h"   // KS X 6924
#include "cmdhflegic.h"     // LEGIC
#include "cmdhflto.h"       // LTO-CM
#include "cmdhfmf.h"        // CLASSIC
#include "cmdhfmfu.h"       // ULTRALIGHT/NTAG etc
#include "cmdhfmfp.h"       // Mifare Plus
#include "cmdhfmfdes.h"     // DESFIRE
#include "cmdhfntag424.h"   // NTAG 424 DNA
#include "cmdhfseos.h"      // SEOS
#include "cmdhfst25ta.h"    // ST25TA
#include "cmdhftesla.h"     // Tesla
#include "cmdhftexkom.h"    // Texkom
#include "cmdhfthinfilm.h"  // Thinfilm
#include "cmdhftopaz.h"     // TOPAZ
#include "cmdhfvas.h"       // Value added services
#include "cmdhfwaveshare.h" // Waveshare
#include "cmdhfxerox.h"     // Xerox
#include "cmdtrace.h"       // trace list
#include "ui.h"
#include "proxgui.h"
#include "cmddata.h"
#include "graph.h"
#include "fpga.h"
#include "cmdscard.h"

//static int CmdHelp(const char* Cmd);

// Pour Python

static int CmdpyClientSim(const char* Cmd) {

    CLIParserContext* ctx;
    CLIParserInit(&ctx, "scard pyclient",
        "Mode d'emulation Calypso [python]",
        "scard pyclient -u 00000000"
    );

    void* argtable[] = {
        arg_param_begin,
        arg_str1("u", "uid", "hex", "4 byte "),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    uint8_t data[256];
    int n = 0;
    int res = CLIParamHexToBuf(arg_get_str(ctx, 1), data, sizeof(data), &n);
    CLIParserFree(ctx);

    if (res) {
        PrintAndLogEx(FAILED, "failed to read data");
        return PM3_EINVARG;
    }

    //PrintAndLogEx(INFO, "Send data : " _GREEN_("%s"), sprint_hex_inrow(data, sizeof(data)));
    clearCommandBuffer();
    SendCommandNG(CMD_PY_CLIENT_SIM, data, sizeof(data));
    return PM3_SUCCESS;
}

static int CmdpyInitCal(const char* Cmd) {

    CLIParserContext* ctx;
    CLIParserInit(&ctx, "scard pyinitcal",
        "Utilise le PUPI pour s'authentifier en tant qu'une carte calypso",
        "scard pyinitcal -u 00000000"
    );

    void* argtable[] = {
        arg_param_begin,
        arg_str1("u", "uid", "hex", "4 byte"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    uint8_t pupi[4];
    int n = 0;
    int res = CLIParamHexToBuf(arg_get_str(ctx, 1), pupi, sizeof(pupi), &n);
    CLIParserFree(ctx);

    if (res) {
        PrintAndLogEx(FAILED, "failed to read data");
        return PM3_EINVARG;
    }

    //PrintAndLogEx(INFO, "Send data : " _GREEN_("%s"), sprint_hex_inrow(data, sizeof(data)));
    clearCommandBuffer();
    SendCommandNG(CMD_PY_INITCALYPSO, pupi, sizeof(pupi));
    //return PM3_SUCCESS;
    return PM3_NONE;

}


//Pour usage interne

static int CmdHF14BSimCal(const char* Cmd) {

    CLIParserContext* ctx;
    CLIParserInit(&ctx, "scard simcalypso",
        "Simulate a Calypso Card [Add by jev]",
        "scard simcalypso -u 00000000"
    );

    void* argtable[] = {
        arg_param_begin,
        arg_str1("u", "uid", "hex", "4byte UID/PUPI"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    uint8_t pupi[4];
    int n = 0;
    int res = CLIParamHexToBuf(arg_get_str(ctx, 1), pupi, sizeof(pupi), &n);
    CLIParserFree(ctx);

    if (res) {
        PrintAndLogEx(FAILED, "failed to read pupi");
        return PM3_EINVARG;
    }

    PrintAndLogEx(INFO, "Simulate with PUPI : " _GREEN_("%s"), sprint_hex_inrow(pupi, sizeof(pupi)));
    PrintAndLogEx(INFO, "Press " _GREEN_("pm3 button") " to abort simulation");
    clearCommandBuffer();
    SendCommandNG(CMD_HF_ISO14443BCAL_SIMULATE, pupi, sizeof(pupi));
    return PM3_SUCCESS;
}

static int CmdHFCheck(const char* Cmd) {

    CLIParserContext* ctx;
    CLIParserInit(&ctx, "scard ckeck",
        "Check Reader RF [Add by jev]",
        "scard check -p 1000"
    );

    void* argtable[] = {
        arg_param_begin,
        arg_str1("p", "period", "dec", "Millisecond"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);
    uint8_t period[2];
    int n = 0;
    int res = CLIParamHexToBuf(arg_get_str(ctx, 1), period, sizeof(period), &n);
    CLIParserFree(ctx);

    if (res) {
        PrintAndLogEx(FAILED, "failed to read period");
        return PM3_EINVARG;
    }

    CLIParserFree(ctx);

    PrintAndLogEx(INFO, "Press " _GREEN_("pm3 button") " to abort cheching");

    //PacketResponseNG resp;
    clearCommandBuffer();
    SendCommandNG(CMD_HF_CHECK, period, sizeof(period));
    //WaitForResponse(CMD_HF_ISO14443B_SNIFF, &resp);
    return PM3_SUCCESS;
}

// Les commandes supportées : Springcard

static command_t CommandTable[] = {

        {"simcalypso",  CmdHF14BSimCal,   AlwaysAvailable, "{ Emulation de carte calypso }"},
        {"pyinitcal",   CmdpyInitCal,     AlwaysAvailable, "{ Commande (A utiliser dans python) pour initialiser une comm calypso (ATQB, WUPB..)}"},
        {"pyclient",    CmdpyClientSim,   AlwaysAvailable, "{ Commande (A utiliser dans python) recuperer les requetes d'un PCD  }"},
        {"check",       CmdHFCheck,       AlwaysAvailable, "{ Verifie periodiquement le champs d'un lecteur  }"},
        {NULL, NULL, NULL, NULL}
};

int CmdScard(const char* Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}