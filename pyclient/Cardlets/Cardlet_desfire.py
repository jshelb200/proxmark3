"""
    file        Cardlet_desfire.py
    brief       SpringCore - Cardlet class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        19/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 19/07/2024: initial release, plain connexion
    """
import logging
from .Cardlet import Cardlet

class Cardlet_desfire(Cardlet):

    _name= "FALLBACK"
  
    # there is no seeker
    _aid = []

    def __init__(self):

        # nothing specific here, just call the parent
        super().__init__()

    def process(self, apdu = None, apdu_raw = None):

        # sanity check
        if apdu == None:
            return self.ReturnCode.SW_ClassNotSupported
        
        # process incoming message
        return self.ReturnCode.SW_ClassNotSupported


# EOF