"""
    file        Cardlet.py
    brief       SpringCore - Cardlet class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        17/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 16/07/2024: initial release, plain connexion
    """

import os
from Crypto.Hash import CMAC
from Crypto.Cipher import AES
from mbedtls import hmac, hashlib, cipher
import logging
from .Cardlet import Cardlet

from mbedtls import cipher

class Cardlet_smid(Cardlet):

    _name= "SMID NFC"

    class SMIDReturnCode:
        SW_Operation_OK             = 0x00
        SW_NoCardWithThisSiteCode   = 0x0C
        SW_IntegrityError           = 0x1E
        SW_PermissionDenied         = 0x6C
        SW_LengthError              = 0x7E
        SW_AuthenticateError        = 0xAE
        
    class StateMachine:
        AuthenticateNfcHce_Step1 = 1
        AuthenticateNfcHce_Step2 = 2
        AuthenticateNfcHce_Done = 3

    # this is the test USI
    _USI = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06]

    # this is also the select AID C-APDU
    _aid = [0xF0, 0x51, 0xBC, 0x53, 0x54, 0x69, 0x64, 0x4D, 0x6F, 0x62, 0x69, 0x6C, 0x65, 0x2D, 0x49, 0x44]

    # SpringCard specific Salt
    _Salt = [0x1E, 0x49, 0x7E, 0x61, 0x3A, 0x99, 0xBE, 0x4f, 0x91, 0x87, 0x9f, 0xE3, 0x85, 0x65, 0x81, 0x97]

    # well, our secret key
    _secret_key = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]
    
    # STID constants
    _SMID_RFU               = 0x00
    _SMID_AUTHENTICATE      = 0xA1
    _SMID_GET_DATA          = 0x6D
    _SMID_ADDITIONAL_FRAME  = 0xFF

    # NFCiD represents the identifiant used to authenticate with the virtual cards.    
    _SMID_NFC_ID            = 0x5C

    # our actual code site
    _code_site = 0x12AB

    # session variables
    _div_key = None
    _aes_enc_ctx = None
    _aes_dec_ctx = None
    _rndA = None
    _rndB = None
    _rndC = None
    _KsesAuthEnc = None
    _KsesAuthMac = None

    def __init__(self):

        # nothing specific here, just call the parent
        super().__init__()

    def deselect(self, buffer = None):
        
        self._div_key = None
        self._aes_enc_ctx = None
        self._aes_dec_ctx = None
        self._rndA = None
        self._rndB = None
        self._rndC = None
        self._KsesAuthEnc = None
        self._KsesAuthMac = None

    # Compute Kx=CMAC.Ku(0x01|USI)
    def crypto_compute_hashkey(self):

        buffer = [0x01]
        buffer.extend(self._USI)

        # we do use PyCryptodome for the CMAC calculation
        Kx = CMAC.new(key = bytearray(self._secret_key), ciphermod = AES)
        Kx.update(bytearray(buffer))
        self._div_key = Kx.digest()
        
        # prepare crypto context using MBEDTLS
        self._aes_enc_ctx = cipher.AES.new(key = self._div_key, mode= cipher.MODE_CBC, iv = bytearray(16))
        self._aes_dec_ctx = cipher.AES.new(key = self._div_key, mode= cipher.MODE_CBC, iv = bytearray(16))

    
    # Calculate KsesAuthEnc and KsesAuthMac
    def crypto_prepare_context(self):

        # calculate KsesAuthEnc
        buffer = bytearray()
        for cpt in range(0,16):
            buffer.append(self._rndA[cpt])
            buffer.append(self._rndB[cpt])
        for cpt in range(0,16):
            buffer.append(self._Salt[cpt])

        KsesAuthEnc = CMAC.new(key = bytearray(self._div_key), ciphermod = AES)
        KsesAuthEnc.update(buffer)
        self._KsesAuthEnc = KsesAuthEnc.digest()

        # calculate KsesAuthMac
        buffer = bytearray()
        for cpt in range(0,16):
            buffer.append(self._rndB[15 - cpt])
            buffer.append(self._rndA[15 - cpt])
        for cpt in range(0,16):
            buffer.append(self._Salt[cpt])

        KsesAuthMac = CMAC.new(key = bytearray(self._div_key), ciphermod = AES)
        KsesAuthMac.update(buffer)
        self._KsesAuthMac = KsesAuthMac.digest()

    def crypto_decipher_data(self, output_data, block_length):

        # switch to KsesAuthEnc
        self._aes_enc_ctx = cipher.AES.new(key = self.KsesAuthEnc, mode= cipher.MODE_CBC, iv = bytearray(16))
        self._aes_dec_ctx = cipher.AES.new(key = self.KsesAuthEnc, mode= cipher.MODE_CBC, iv = bytearray(16))

        # calculate IV for CBC mode
        #... TODO

    def process(self, apdu = None, apdu_raw = None):

        # sanity check
        if apdu == None and apdu_raw == None:
            return self.ReturnCode.SW_ClassNotSupported
        
        # process incoming message
        if self._step == 0:

            # This is, for sure, a SELECT APPLICATION, remove the command header to keep the AID
            if self._aid == apdu.DATA:
                logging.info(f"{self._name}: Select AID")
                aid = self._USI.copy()
                aid.extend(self.ReturnCode.SW_Success)
                self._step = self.StateMachine.AuthenticateNfcHce_Step1
                return aid
            
        elif self._step == self.StateMachine.AuthenticateNfcHce_Step1 and apdu_raw != None:

            #is this an optional  GetNbCard request?
            if apdu_raw[0] == 0x6C and len(apdu_raw) == 4:

                # check code site
                site_code = apdu_raw[1] << 8 | apdu_raw[2]
                if site_code != self._code_site:
                    return [self.SMIDReturnCode.SW_NoCardWithThisSiteCode]
                
                # we do have one card only
                return [self.SMIDReturnCode.SW_Operation_OK, 0x01]
            
            if apdu_raw[0] == self._SMID_AUTHENTICATE and len(apdu_raw) == 6:

                logging.info(f"{self._name}: Authenticate Step 1")

                # check code site
                site_code = apdu_raw[1] << 8 | apdu_raw[2]
                if site_code != self._code_site:
                    return [self.SMIDReturnCode.SW_NoCardWithThisSiteCode]
                
                # valid NFC ID?
                if apdu_raw[3] != 1 or apdu_raw[4] != self._SMID_NFC_ID:
                    return [self.SMIDReturnCode.SW_NoCardWithThisSiteCode]

                # let's create our diversified key
                self.crypto_compute_hashkey()

                # generate rndB
                self._rndB = os.urandom(16)
                c_rndB = self._aes_enc_ctx.encrypt(self._rndB)
                response = [self._SMID_ADDITIONAL_FRAME]
                response.extend(c_rndB)

                self._step = self.StateMachine.AuthenticateNfcHce_Step2
                return response                
        elif self._step == self.StateMachine.AuthenticateNfcHce_Step2 and apdu_raw != None:            

            if apdu_raw[0] == self._SMID_ADDITIONAL_FRAME and len(apdu_raw) == 33:
                
                logging.info(f"{self._name}: Authenticate Step 2")
                CKx_RndA_RndB = bytearray(apdu_raw[1:])
                
                # decipher both RndA and RndB
                RndA_RndB = self._aes_dec_ctx.decrypt(CKx_RndA_RndB)

                # save rndA
                self._rndA = RndA_RndB[0:16]

                # check if both rndB are identicla
                rndB = RndA_RndB[16:]
                if rndB != self._rndB:
                    logging.error("RndB ERROR!")
                    return [self.SMIDReturnCode.SW_AuthenticateError]

                # create RndC and CKx(RndC | RndA)
                self._rndC = os.urandom(16)
                buffer = self._rndC + self._rndA
                buffer = self._aes_enc_ctx.encrypt(buffer)
                response = [self.SMIDReturnCode.SW_Operation_OK]
                response.extend(buffer)

                # create sessions keys
                self.crypto_prepare_context()
                self._step = self.StateMachine.AuthenticateNfcHce_Done
                logging.info(f"{self._name}: Authenticate Done")
                return response
                
        elif self._step == self.StateMachine.AuthenticateNfcHce_Done and apdu_raw != None:     
            logging.info(f"{self._name}: Authenticate Step NEXT")
            if apdu_raw[0] == self._SMID_GET_DATA and len(apdu_raw) == 5:
                offset = apdu_raw[1] << 8 | apdu_raw[2]
                length = apdu_raw[3] << 8 | apdu_raw[4]
            
            # TODO complete this
            self.requestReset()
            return self.ReturnCode.SW_ConditionsOfUseNotSatisfied
                

            
        # we do not know yet what to do!
        return self.ReturnCode.SW_ClassNotSupported


# logging.error("RndB ERROR!"+''.join(format(x, '02X') for x in RndA_RndB))
# > 6C12AB00
# < 0002

# EOF