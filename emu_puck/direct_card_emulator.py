"""
    file        smid_usb_client.py
    brief       SpringCore - SMID USB test tool
    author      Matthieu Barreteau <matthieu.b@springcard.com>
    date        16/07/2024
 
    copyright   Copyright (c) 2021 SPRINGCARD SAS, FRANCE - www.springcard.com
    version     MBA 16/07/2024: initial release, plain connexion

    Requirements:
    - Python >= 3.7.3
    - pyusb => 1.2.1 (python -m pip install pyusb==1.2.1)
    - libusb
    - PyCryptodome >= 3.20.0

    And add libusb package's folder to tour path, example:
    C:/Users/matthieu.b/AppData/Local/Programs/Python/Python312/Lib/site-packages/libusb/_platform/_windows/x64

    Purpose:
    SMID protocol integration validation tool for IWM2
    The goal of this script is to simulate a phone, in card emulation mode.
    """

# libraries import
import pathlib
import queue
import random
import usb.core
import usb.util
import logging
import getopt
import time
import sys
import os

# helpers import
from threading import Thread, Event
from SpringCore.APDU import APDU
from SpringCore.DirectDevice import DirectDevice
from SpringCore.UsbDirectDevice import UsbDirectDevice

# import all cardlets
from SpringCore.Cardlets.Cardlet_smid import *
from SpringCore.Cardlets.Cardlet_colorado import *
from SpringCore.Cardlets.Cardlet_master_card import *
from SpringCore.Cardlets.Cardlet_desfire import Cardlet_desfire

CTRL_NFC_HCE_STOP   = 0x64
CTRL_NFC_HCE_START  = 0x65

USB_READ_TIMEOUT_MS = 1000

ALERT_EVENT = [0xFF, 0xFF, 0xFF, 0xFF, 0xFF]
SELECT_APPLICATION_CHECK =  [0x00, 0xA4, 0x04, 0x00]
SELECT_APPLICATION_ISO_CHECK =  [0x5A]

C_APDU_QUEUE_SIZE = 10
AES128_BLOCK_LENGTH = 16


# display_help function
def display_help( script_name ):

    print(script_name)
    print("-i --index=<INDEX>      Select device by its index (default is 0)")
    print("-l --list               List all compliant device(s), then exit")
    print("-L --list-details       List all compliant device(s) with details and full path, then exit")
    print( "\n" )
    sys.exit()

###########################################
def find_endpoint(intf, direction, type):
    ep = usb.util.find_descriptor(intf, custom_match = \
        lambda e:
            (usb.util.endpoint_direction(e.bEndpointAddress) == \
            direction) and (usb.util.endpoint_type(e.bmAttributes)  == \
                            type))
    return ep

###########################################
class EventListener(Thread):    

    def __init__(self, device, interrupt_endpoint, apdu_queue):
        Thread.__init__(self)        
        self._event = Event()
        self._event.clear()
        self._device = device
        self._interrupt_endpoint = interrupt_endpoint
        self._c_apdu_queue = apdu_queue

    def run(self):
        # do nothing but wait for an interrupt request!
        while not self._event.is_set():
            try:
                # wait for some event on the interrupt endpoint
                buffer = self._device.read(self._interrupt_endpoint, 64 , USB_READ_TIMEOUT_MS)
                event = DirectDevice.IncomingEvent(buffer)
                if event.isValid():                    
                    self._c_apdu_queue.put(event)
                else:
                    logging.error("Invalid event!"+''.join(format(x, '02X') for x in buffer))

            except usb.core.USBTimeoutError:
                # this is normal!
                pass
            except Exception as e:
                # this is fatal, inform the main thread!
                self._c_apdu_queue.put(ALERT_EVENT)

                # end this thread
                self.terminate()

    def get_event(self):
        return self._event

    def terminate(self):
        self._event.set()

###########################################
def Cardlet_getList():
    # this should return Cardlet
    local_class_name = "Cardlet_"

    # find comliant files in current folder
    compliant_list = []
    check_list = []
    script_path = str(pathlib.Path(__file__).parent.resolve())
    script_path = script_path + "\\SpringCore\\Cardlets"

    # list all matching files
    with os.scandir(script_path) as it:
        for entry in it:
            if entry.name.endswith(".py") and entry.is_file() and entry.name.startswith(local_class_name):
                check_list.append(entry.name)

    #retrieve class list
    if len(check_list) != 0:
        for entry in check_list:
            entry = entry.replace(".py","")
            
            try:
                cls = eval(entry)
                compliant_list.append(cls)
            except Exception as e:
                print(e)
            
    return compliant_list

###########################################
# main function
def main( script_name, argv ):

    cardlet_list = Cardlet_getList()

    # initial values    
    device_index = -1
    show_list = 0

    # compliant devices
    compliant_product_id_list = [0x6210, 0x6120]

    # options parsing
    try:
        opts, _ = getopt.getopt(argv,"hi:lL",[ "help", "index=", "list", "list-details"])
    except getopt.GetoptError as e:
        print(e)
        display_help( script_name )

    for opt, arg in opts:
        if opt in ( "-h", "--help"):
            display_help( script_name )
        elif opt in ( "-i", "--index"):
            try:
                device_index = int(arg)
            except:
                print( "Invalid Index!" )
                sys.exit(1)
        elif opt in ( "-l", "--list"):
            show_list = 1

    # set debug options
    logging.basicConfig(format='%(asctime)s - %(message)s', \
                            datefmt='%d-%b-%y %H:%M:%S', \
                            level=logging.DEBUG )  

    # try to find a compliant product (aka Direct profile)    
    devices = usb.core.find(find_all=True, idVendor=0x1c34)
    if devices is None:
        logging.error("No device found!")
        sys.exit(1)

    # create a compliant device list
    compliant_device_list = []
    for a_device in devices:
        if a_device.idProduct in compliant_product_id_list:
            compliant_device_list.append(a_device)

    # show comliant device list if needed
    if (len(compliant_device_list) > 1 and device_index == -1) or (show_list != 0):
        index = 0
        print(f"Found {len(compliant_device_list)} possibly SpringCore compliant device(s):")
        for a_device in compliant_device_list:
            try:
                vendor_string = usb.util.get_string(a_device, a_device.iManufacturer, None)
                product_string = usb.util.get_string(a_device, a_device.iProduct, None)
            except Exception as error:
                logging.error("Something goes wrong...please stop Companion if running and retry!")
                sys.exit(1)
                
            print(f"\t{index}: {vendor_string} {product_string}")
            index = index + 1
        sys.exit(0)

    # select one (or the only one) device
    if device_index == -1:
        device_index = 0
    if len(compliant_device_list) == 0:
        logging.error("No device!")
        sys.exit(1)
    
    # grab an instance of that device
    usb_dev = compliant_device_list[device_index]
    product_name = usb.util.get_string(usb_dev, usb_dev.iProduct, None)
    serial_number = usb.util.get_string(usb_dev, usb_dev.iSerialNumber, None)
    logging.info(f"Working on {device_index}: {product_name} {serial_number}")
        
    # Composite devices are not anaged/allowed
    interface_count = 0
    for cfg in usb_dev: 
        for i in cfg:
            interface_count = interface_count +1
    if interface_count > 1:
        logging.error("Composite devices are not allowed!")
        sys.exit(1)
    
    # set the default configuration
    usb_dev.set_configuration()

    # look for endpoints
    cfg = usb_dev.get_active_configuration()
    intf = cfg[(0,0)]

    ep_out = find_endpoint(intf, usb.util.ENDPOINT_OUT, usb.util.ENDPOINT_TYPE_BULK)    
    if ep_out == None:
        logging.error("Invalid Endpoint!")
        sys.exit(1)

    ep_in = find_endpoint(intf, usb.util.ENDPOINT_IN, usb.util.ENDPOINT_TYPE_BULK)
    if ep_in == None:
        logging.error("Invalid Endpoint!")
        sys.exit(1)

    ep_interrupt = find_endpoint(intf, usb.util.ENDPOINT_IN, usb.util.ENDPOINT_TYPE_INTR)
    if ep_interrupt == None:
        logging.error("Invalid Endpoint!")
        sys.exit(1)

    # create our new USB Direct device
    device = UsbDirectDevice(usb_dev, product_name, serial_number, ep_in, ep_out, ep_interrupt)

    # start HCE operations
    rsp = device.Exchange(DirectDevice.CLA.Control, CTRL_NFC_HCE_START)
    if rsp != None:
        logging.info( "Card Emulation started!" )
    else:
        logging.info( "Card Emulation start error!" )
        sys.exit(1)

    c_apdu_queue = queue.Queue(C_APDU_QUEUE_SIZE)
    listener = EventListener(usb_dev, ep_interrupt, c_apdu_queue)
    listener.start()

    exit_cond = False
    cardlet = None
    try:
        while not exit_cond:

            event = None
            try:
                event = c_apdu_queue.get(timeout = 1)
            except queue.Empty as error:
                pass
            except Exception as error:
                logging.error(f"main loop ERROR {error}")
                exit_cond = True
                continue

            if event != None:
                # interrupt listener error?
                if event == ALERT_EVENT:
                    logging.error( "Listener ERROR!..." )
                    exit_cond = True
                    continue
                
                # event is present and valid
                hce_event = DirectDevice.HceEvent(event)                
                if hce_event.isApduReady():
                    
                    # retrieve this c-apdu
                    buffer = device.Exchange(DirectDevice.CLA.HCE,DirectDevice.HCEI.INS.PullCommandApdu)
                    
                    if buffer != None:                        

                        # logging.debug("buffer: "+''.join(format(x, '02X') for x in buffer))
                        new_event = DirectDevice.IncomingEvent(buffer)                        
                        if new_event.isValid(): 
                        
                            # check status byte
                            sta = new_event.Buffer[0]
                            if sta == 0x00:

                                # we do not know yet if this is an apdu or a manufacturer specific entry
                                apdu = None

                                # remove STA byte
                                c_apdu_raw = new_event.Buffer[1:].tolist()
                                logging.debug("c_apdu: "+''.join(format(x, '02X') for x in c_apdu_raw))

                                # ISO or Desfire? Let's try to find an AID!
                                aid = None

                                # Select Application?
                                if c_apdu_raw[0:1] == SELECT_APPLICATION_ISO_CHECK  and len(c_apdu_raw) == 4:                                        
                                    aid = c_apdu_raw[1:]                    

                                # Select Application ISO?
                                elif len(c_apdu_raw) >= 4 and c_apdu_raw[:4] == SELECT_APPLICATION_CHECK:
                                    apdu = APDU(c_apdu_raw)
                                    if apdu.Lc != 0:
                                        aid = apdu.DATA

                                if aid != None:

                                    # 2 cases: cardlet already selected or not
                                    if cardlet == None:            
                                        # look for a compliant cardlet
                                        for a_cardlet in cardlet_list:
                                            test_cardlet = a_cardlet()
                                            if test_cardlet.isMatching(aid):
                                                logging.info(f"Card detected: {test_cardlet.getName()}")

                                                # use this cardlet from now and reset its state machine
                                                cardlet = test_cardlet
                                                cardlet.prepare()
                                                break
                                    else:
                                        # Let's select the card again
                                        cardlet.select(aid)

                                # no Cardlet detected? Use our fallback cardlet instead
                                if cardlet == None:
                                    cardlet = Cardlet_desfire()
                                    cardlet.prepare()

                                # process request: we do work using the raw frame as it may not be a real apdu
                                response = cardlet.process(apdu, c_apdu_raw)

                                if response != None and len(response) != 0:
                                    
                                    logging.debug("r_apdu: "+''.join(format(x, '02X') for x in response))
                                    buffer = device.Exchange(DirectDevice.CLA.HCE,DirectDevice.HCEI.INS.PushResponseApdu,response)
                                    if buffer != None:
                                        logging.debug("Exchange OK")

                                    # restart HCE operations on vPICC's request
                                    if cardlet.shallReset():
                                        rsp = device.Exchange(DirectDevice.CLA.Control, CTRL_NFC_HCE_STOP)
                                        if rsp == None:
                                            exit_cond = True
                                            continue

                                        rsp = device.Exchange(DirectDevice.CLA.Control, CTRL_NFC_HCE_START)
                                        if rsp == None:
                                            exit_cond = True
                                            continue

                                        logging.info( "Card Emulation restarted!" )                                        

                                    if response == cardlet.ReturnCode.SW_ClassNotSupported:
                                        logging.info("vPICC reset")
                                        cardlet.deselect()
                                        cardlet = None
                elif cardlet != None:
                    logging.info("End for exchanges")                     
                    cardlet.deselect()
                    cardlet = None

                # eventully release event from the queue
                c_apdu_queue.task_done()             

    # assuming CTRL+C is correctly handled by Python/System
    except KeyboardInterrupt:
        pass

    # stop all process and exit
    logging.info( "Cleaning..." )
    listener.terminate()
    listener.join()

    # stop HCE operations
    rsp = device.Exchange(DirectDevice.CLA.Control, CTRL_NFC_HCE_STOP)
    if rsp != None:
        logging.info( "Card Emulation stopped!" )
    else:
        logging.info( "Card Emulation stop error!" )
        sys.exit(1)
   
    logging.info( "Done." )

# entry point
if __name__ == "__main__":
    # add "this script" folder to the library search path
    # could be useful on Windows platform
    sys.path.append( os.path.dirname( os.path.realpath( __file__ ) ) )
  
    # start our application
    main( os.path.basename( __file__ ), sys.argv[1:] )
    
# EOF
