"""
    file        UsbDirectDevice.py
    brief       SpringCore - UsbDirectDevice class implementation
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        17/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 17/07/2024: initial release, plain connexion
    """
import logging
from .DirectDevice import DirectDevice

class UsbDirectDevice(DirectDevice):

    _USB_READ_TIMEOUT_MS = 1000
    
    def __init__(self, usb_handler, product_name, serial_number, ep_in, ep_out, ep_interrupt):
        self._pcb = 0

        self._usb_handler = usb_handler
        self._product_name = product_name
        self._serial_number = serial_number
        self._ep_in = ep_in
        self._ep_out = ep_out
        self._ep_interrupt = ep_interrupt

        # always ask your parent!
        super().__init__()
    
    def Exchange(self, cla, cmd, data = None):
        buffer = []
        
        # add pcb and cla
        buffer.append(self._pcb)
        buffer.append(cla)

        # add length
        data_length = 0
        if data != None:
            data_length = len(data)
        buffer.append((data_length >> 8) & 0xFF)
        buffer.append((data_length >> 0) & 0xFF)

        # add cmd
        buffer.append(cmd)

        # and eventually data if any
        if data != None:
            buffer.extend(data)

        # perform the actual exchange
        try:
            self._usb_handler.write(self._ep_out, buffer)
            rsp = self._usb_handler.read(self._ep_in, 64 , self._USB_READ_TIMEOUT_MS)
            if rsp != None and len(rsp) != 0:
                return rsp
        except Exception as error:
            logging.error(error)
        
        return None
# EOF