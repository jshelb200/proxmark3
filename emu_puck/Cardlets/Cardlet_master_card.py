"""
    file        Cardlet_master_card.py
    brief       SpringCore - Cardlet Master Card class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        20/08/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 20/08/2024: initial release
    """
import logging
from .Cardlet import Cardlet

# This is a fake example to check the cardlet auto-inclusion system
class Cardlet_master_card(Cardlet):

    _name= "Master Card"

    # this is also the select AID C-APDU
    _aid_v1= [0x43, 0x41, 0x50]
    _aid_v2= [0x32, 0x43, 0x4D]

    def __init__(self):
        self._name= "Master Card"

        # nothing specific here, just call the parent
        super().__init__()

    def process(self, apdu = None, apdu_raw = None):

        logging.debug(f"Processing {self._name}")

        # sanity check
        if apdu == None:
            return self.ReturnCode.SW_ClassNotSupported
        
        # process incoming message
        return self.ReturnCode.SW_ClassNotSupported

    # overrided version, we need to check if it's a v1 or v2 version
    def isMatching(self, first_command):

        if self._aid_v1 == first_command:
            self._name= "Master Card v1"
            return True

        if self._aid_v2 == first_command:
            self._name= "Master Card v2"
            return True
        
        return False

# EOF