"""
    file        DirectDevice.py
    brief       SpringCore - DirectDevice class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        16/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 16/07/2024: initial release, plain connexion
    """
import logging
from .Cardlets import *

class DirectDevice:

###########################################
    # CLAsses uses for direct communication with a SpringCore device (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/List_of_CLAsses)
    class CLA:    
        Protocol = 0x00     # PROTOCOL class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/PROTOCOL_class/)
        HCE = 0x57          # HCE class
        Control = 0x58      # CONTROL class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/CONTROL_class/)
        AtCrypto = 0x59     # CRYPTO_COMPANION class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/CRYPTO_COMPANION_class/)
        NxpSam = 0x5A       # SECURE_ELEMENT class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/SECURE_ELEMENT_class/)
        RDR = 0x5B          # READER class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/READER_class)
        I2C = 0x5C          # I2C class
        DFU = 0x5D          # DFU class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/DFU_class)
        Echo = 0x5E         # ECHO class (https://docs.springcard.com/books/SpringCore/Host_interfaces/Logical/Direct_Protocol/ECHO_class)

###########################################
    # Implementation of the HCE class
    class HCEI:

        # INStructions exposed by this class
        class INS:
            Stop = 0x00
            Start = 0x01
            GetStatus = 0x02
            PullCommandApdu = 0x03
            PushResponseApdu = 0x04

###########################################
    class IncomingEvent:

        # validity indicator
        _valid = False

        def __init__(self, buffer):
            
            # this shall not happen
            if buffer == None or len(buffer) < 5:
                return None
            
            self.Class = buffer[1]

            length = (buffer[2] << 8|buffer[3]) + 1
            self.Buffer = buffer[4:]

            if length != len(self.Buffer):
                logging.error(f"{self.__class__} Wrong data length!")
                return None            
            
            # all good!
            self._valid = True

        def isValid(self):
            return self._valid

###########################################
    class HceEvent:

        class HceEventBits:
            ExternalField = 0x01
            Selected = 0x02
            ApduReady = 0x04
            Deselected = 0x08

        _directEvent = None            

        def __init__(self, directEvent ):
            self._directEvent = directEvent

            if self.isExternalField():
                logging.debug("Hce:External field")

            if self.isSelected():
                logging.debug("Hce:Selected")

            if self.isApduReady():
                logging.debug("Hce:Apdu ready")

            if self.isDeselected():
                logging.debug("Hce:Deselected")

        def isExternalField(self):
            return self._directEvent.Buffer[0] & self.HceEventBits.ExternalField != 0
        
        def isSelected(self):
            return self._directEvent.Buffer[0] & self.HceEventBits.Selected != 0
        
        def isApduReady(self):
            return self._directEvent.Buffer[0] & self.HceEventBits.ApduReady != 0
        
        def isDeselected(self):
            return self._directEvent.Buffer[0] & self.HceEventBits.Deselected != 0
    
    ###########################################
    def __init__(self):
        pass

# EOF        