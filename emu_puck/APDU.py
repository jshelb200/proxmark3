"""
    file        APDU.py
    brief       SpringCore - APDU class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        19/07/2024
 
    copyright   Copyright (c) 2024 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 19/07/2024: initial release, plain connexion
    """
import logging

class APDU:
        
    CLA = 0x00
    INS = 0x00
    P1 = 0x00
    P2 = 0x00
    header = []
    
    Lc = 0
    _hasLc = False

    Le = 0
    _hasLe = False

    DATA = []
    SW1 = 0x00
    SW2 = 0x00

    # I only deal with short format APDUs (Lc and Le on 1 byte each)
    def __init__(self, buffer):

        # this shall not happen
        if buffer == None or len(buffer) < 4:
            logging.error(f"{self.__class__} too short!")
            return None
        
        # mandatory APDU entries
        self.CLA = buffer[0]
        self.INS = buffer[1]
        self.P1 =  buffer[2]
        self.P2 =  buffer[3]

        # make a copy of the header, for fast comparison
        self.header = buffer[:4]

        # remove header length
        buffer = buffer[4:]
        buffer_length = len(buffer)
        if buffer_length == 0:
            # there is nothing more
            pass
        elif buffer_length == 1:
            # Le only
            self.Le = buffer[0]
        else:
            self._hasLc = True
            self.Lc = buffer[0]
            buffer = buffer[1:]
            buffer_length = len(buffer)

            if buffer_length < self.Lc:
                logging.error(f"{self.__class__} data too short!")
                return None
            
            # copy data
            self.DATA = buffer[:self.Lc]
            buffer = buffer[self.Lc:]
            buffer_length = len(buffer)
            if buffer_length != 0:
                self._hasLe = True
                self.Le = buffer[0]

    # Human readable version
    def __repr__(self):
        text = ""
        text = text + "CLA=" + format(self.CLA, "02X")
        text = text + ", INS=" + format(self.INS, "02X")
        text = text + ", P1=" + format(self.P1, "02X")
        text = text + ", P2=" + format(self.P2, "02X")

        if self._hasLc != 0:
            text = text + f" Lc={self.Lc} DATA=" + ''.join(format(x, '02X') for x in self.DATA) + " "

        if self._hasLe != 0:
            text = text + f"Le={self.Le}"

        return text

# EOF
        