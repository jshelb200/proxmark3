# -*- coding: utf-8 -*-
# ==============================================================================
# File: utils.py
# Project: Proxmark Advanced Python Client
# Author: Jerome.e & SpringCard Co
# ==============================================================================

from msilib import PID_LASTPRINTED
import os
from colorama import init, Fore, Style
from datetime import datetime 
import time

# Initialise Colorama pour que les couleurs fonctionnent sous Windows
init()

class Debug:
    def __init__(self, verbosity_level=1):
        if not os.path.exists('logs'):
            os.makedirs('logs')
        self.log_file = 'logs/log.txt'
        self.verbosity_level = verbosity_level
        
        if not os.path.exists(self.log_file):
            with open(self.log_file, 'w'):
                pass 

    def _log_message(self, level, message):
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        formatted_message = f"{timestamp} {message}\n"
        print(message)
        with open(self.log_file, 'a') as f:
            f.write(formatted_message)

    def print(self, message):
        if self.verbosity_level >= 1:
            self._log_message("PRINT", f"{Fore.WHITE}{Style.BRIGHT} {message}{Style.RESET_ALL}")

    def warning(self, message):
        if self.verbosity_level >= 2:
            self._log_message("WARNING", f"{Fore.YELLOW} {message}{Style.RESET_ALL}")

    def error(self, message):
        if self.verbosity_level >= 3:
            self._log_message("ERROR", f"{Fore.RED} {message}{Style.RESET_ALL}")
            

            
# Pour differencier les messages du FW et ceux du client
def pmfw_print(data):
    # Add space in data xx xx xx xx if num in data
     #   data = ' '.join(data[i:i+2] for i in range(0, len(data), 2))
    print(f"{Fore.CYAN}[+] {Style.RESET_ALL}{data}")             
        
def compute_crc(data):
    # Necessaire ?
    crc = 0
    for b in data:
        crc ^= b
    return crc

def bytes_to_hex_string(data):
    if isinstance(data, (bytes, bytearray)):
        return ' '.join(f'{byte:02X}' for byte in data)
    raise ValueError("Data must be bytes or bytearray")

def only_apdu(tpdu):
    if len(tpdu) <= 2:
        # Si la longueur de la trame est de 2 octets ou moins, il n'y aura rien à retourner
        return b''
    return tpdu[2:-5]
    

def stylized_banner(port):
    logs_dir = 'logs'
    if not os.path.exists(logs_dir):
        os.makedirs(logs_dir)
    banner = (
        f"{Fore.CYAN}[=] Session log log file path{Style.RESET_ALL}\n"
        f"{Fore.GREEN}[+] Using UART port {port}{Style.RESET_ALL}\n"
        f"{Fore.GREEN}[+] Communicating with PM3 over USB-CDC{Style.RESET_ALL}\n"
        f"{Fore.CYAN}   ____        ____       ___\n"
        f"  (  _ ( \\/ )_(  _ ( \\/ | __ \\\n"
        f"   ) __/)  (___) __/ \\/ \\(__ (\n"
        f"  (__) (__/   (__) \\_)(_(____/\n"
        f"{' '*50}     \n"
        f"{Fore.RED}{' '*35}[SpringCard]{Style.RESET_ALL}\n\n"
        f"{Fore.YELLOW}{' '*2}[ Proxmark3 RFID instrument : Python Client]{Style.RESET_ALL}\n\n"
        f"{' '*4} MCU....... AT91SAM7S512 Rev A\n"
        f"{' '*4} Client.... Springcard repo [soon] (git)\n"
        f"{' '*4} Bootrom... Iceman/master/v4.18589\n"
        f"{' '*4} OS........ Fork of iceman/master/v4.18589\n"
        f"{' '*4} Target.... RDV4\n"
    )
    return banner

