"""
    file        Cardlet_colorado.py
    brief       SpringCore - Cardlet Colorado class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        17/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 16/07/2024: initial release, plain connexion
    """
import logging
import os
from mbedtls import cipher
from .Cardlet import Cardlet

# This is a fake example to check the cardlet auto-inclusion system
class Cardlet_colorado(Cardlet):

    _name= "Colorado"
    _uid = None

    class StateMachine:
        Authenticate = 1
        Authenticate_Done = 2

    # Colorado encryption key
    _master_key = [0x43, 0x6f, 0x6c, 0x6f, 0x72, 0x61, 0x64, 0x6f, 0x20, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x31]

    # this is also the select AID C-APDU
    _aid = [0xA0, 0x00, 0x00, 0x06, 0x14, 0x4E, 0x46, 0x43, 0x2D, 0x43, 0x4F, 0x4E, 0x46]

    def __init__(self):

        self._step = 0

        # generate a randome UID
        self._uid = os.urandom(16)
        logging.debug(f"{self._name}: UID=" + ''.join(format(x, '02X') for x in self._uid))

        # nothing specific here, just call the parent
        super().__init__()

    def process(self, apdu = None, apdu_raw = None):

        # sanity check
        if apdu == None and apdu_raw == None:
            return self.ReturnCode.SW_ClassNotSupported
     
        # process incoming message
        if self._step == 0:
            logging.info(f"{self._name}: Select AID")

            response = []
            response.extend(self._uid)
            response.extend(self.ReturnCode.SW_Success)
            self._step = self.StateMachine.Authenticate
            return response

        if self._step == self.StateMachine.Authenticate:
            
            # look for a 16 bytes challenge
            if len(apdu_raw) == 22 and apdu_raw[0:5] == [0x00, 0x88, 0x00, 0x00, 0x10] and apdu_raw[21] == 0:
                challenge = apdu_raw[5:21]

                # create a diversified key based on our uid
                aes_ctx = cipher.AES.new(key = bytearray(self._master_key), mode= cipher.MODE_CBC, iv = bytearray(16))
                div_key = aes_ctx.encrypt(self._uid)

                # now cipher the challenge with this key
                aes_ctx = cipher.AES.new(key = div_key, mode= cipher.MODE_CBC, iv = bytearray(16))
                encrypted_challenge = aes_ctx.encrypt(bytearray(challenge))

                logging.debug(f"{self._name}: challenge=" + ''.join(format(x, '02X') for x in challenge))
                logging.debug(f"{self._name}: encrypted_challenge=" + ''.join(format(x, '02X') for x in encrypted_challenge))

                response = []
                response.extend(encrypted_challenge)
                response.extend(self.ReturnCode.SW_Success)
                self._step = self.StateMachine.Authenticate_Done
                return response

        if self._step == self.StateMachine.Authenticate_Done:
            if len(apdu_raw) >= 5 and apdu_raw[0:3] == [0x00, 0xDA, 0x01]:
                item_id = apdu_raw[3]
                item_len = apdu_raw[4]

                # Don't stay active forever
                if item_id == 0xFF:                    
                    self.requestReset()
                elif item_len == (len(apdu_raw) - 6):
                    item = apdu_raw[5:]
                    item = item[:-1]
                    logging.info(f"{self._name}: item {item_id}=" + ''.join(format(x, 'c') for x in item))                    
                
                return self.ReturnCode.SW_Success   
        
        return self.ReturnCode.SW_ClassNotSupported


# EOF
