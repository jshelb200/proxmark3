"""
    file        Cardlet.py
    brief       SpringCore - Cardlet class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        17/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 16/07/2024: initial release, plain connexion
    """
import logging

class Cardlet:
    _aid = []
    _name= "undefined"
    _step = 0
    _session_counter = 0
    _stop_required = False

    APDU_SELECT_APPLICATION =  [0x00, 0xA4, 0x04, 0x00]

    class ReturnCode:
        SW_Success = [0x90, 0x00]
        SW_ConditionsOfUseNotSatisfied = [0x69, 0x85]
        SW_ClassNotSupported = [0x6E, 0x00]
    
    def __init__(self):
        pass

    def isMatching(self, aid):
        return (aid == self._aid)
    
    def getAid(self):
        return self._aid

    def shallReset(self):
        rc = self._stop_required
        self._stop_required = False
        return rc

    # our vPICC requests to end all exchanges
    def requestReset(self):
        self._step = 0
        self._stop_required = True

    def getName(self):
        return self._name    

    # this method is called when an application is already selected
    def select(self, aid = None):
        logging.debug(f"{self._name}: Select AGAIN")

        # sanity check
        if aid == None:
            return False

        # reset to initial step
        self._step = 0

        # check AID again
        return self.isMatching(aid)

    def process(self, apdu = None, apdu_raw = None):
        return None
    
    def deselect(self, buffer = None):
        return None

    def prepare(self):
        self._step = 0
        self._session_counter = 0

    def pad(self,buffer):
        if buffer == None:
            return None

        padded = False
        while len(buffer) % 16 != 0:
            if not padded:
                padded = True
                buffer.append(0x80)
            else:
                buffer.append(0x00)

        logging.debug("padded: "+''.join(format(x, '02X') for x in buffer))
        
        return buffer

    
# EOF