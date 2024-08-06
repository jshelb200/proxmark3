from re import T
import serial
import threading
import argparse
from pm3_cmd import CMD_PY_CLIENT
from utils import Debug, stylized_banner, pycli_print, bytes_to_hex_string 
from colorama import init, Fore, Style
import struct
import sys
import queue
import time

# Constants PM3 
TX_COMMANDNG_PREAMBLE_MAGIC = b"PM3a"  # 0x504d3361 PM3a 61334D50 a3MP 
TX_COMMANDNG_POSTAMBLE_MAGIC = b"a3"   # 0x6133
RX_COMMANDNG_PREAMBLE_MAGIC = b"PM3b"  # 0x504d3362
RX_COMMANDNG_POSTAMBLE_MAGIC = b"b3"   # 0x6233
PM3_CMD_DATA_SIZE = 512

# Constant PYPM3

TX_COMMANDNG_PREAMBLE_MAGIC_PY = b"Pym3"  
TX_COMMANDNG_POSTAMBLE_MAGIC_PY = b"a3"

# Pour la simulation
Reader = bytes_to_hex_string(b'\x03\x00\xCA\x7F\x68\x00\x0F\x7D')
pm = b'\x03\x6B\x00\x55\xAB'
received_queue = queue.Queue()
global receivedCmd

# Verrou global pour synchroniser l'affichage
display_lock = threading.Lock()


# Définir les commandes pm3
CMD_PY_SEND =                       0x0A00
CMD_PY_RECV =                       0x0A01
CMD_PY_INITCALYPSO =                0x0A02
CMD_HF_ISO14443BCAL_SIMULATE =      0x0A03
CMD_HF_CHECK =                      0x0A04
CMD_PY =                            0x0A05
CMD_PY_CLIENT_SIM =                 0x0A06
CMD_PY_CLIENT_DATA =                0x0A07
CMD_BREAK_LOOP =                    0x0118

debug = Debug()

class ProxmarkDevice:
    def __init__(self):
        self.serial_port = None
        self.run = False
        self.communication_thread = None

def uart_open(port, speed):
    try:
        return serial.Serial(port, baudrate=speed, timeout=1)
    except serial.SerialException as e:
        debug.error(f"SerialException: {e}")
        return None

# Serial Reader Task
# Fonction de lecture série
def serial_reader_task(dev, show_out):
    try:
        buffer = b''
        while dev.run:
            try:
                data = dev.serial_port.read(64)  # Lecture par blocs de 64 octets
                if data:
                    buffer += data

                    # Vérification de la présence du postambule complet
                    while buffer.find(RX_COMMANDNG_POSTAMBLE_MAGIC) != -1:
                        end_index = buffer.find(RX_COMMANDNG_POSTAMBLE_MAGIC) + len(RX_COMMANDNG_POSTAMBLE_MAGIC)
                        response = buffer[:end_index]
                        buffer = buffer[end_index:]

                        # Décomposition de la réponse en parties spécifiques
                        if len(response) >= 4:
                            received_data_len = response[4]
                            received_data = response[12:12 + received_data_len - 2]
                            debug.warning(f"Received response: {response.hex()} | {response.decode('ascii', errors='ignore')}")
                            debug.warning(f"Received Data len: {hex(received_data_len)} | {str(received_data_len)}")
                            debug.warning(f"Received Data: {received_data.hex()} | {received_data.decode('ascii', errors='ignore')}")
                            received_queue.put(received_data.decode('ascii', errors='ignore'))  # Ajouter la commande reçue à la queue
                            if show_out:
                                with display_lock:
                                    pycli_print(received_data.decode('ascii', errors='ignore'))
            except serial.SerialException as e:
                debug.error(f"Communication error: {e}")
                dev.run = False
            time.sleep(0.01)  # Attente courte pour éviter de bloquer le thread
    except Exception as e:
        debug.error(f"Unexpected error in communication thread: {e}")
    finally:
        if dev.serial_port:
            dev.serial_port.close()


def open_proxmark(dev, port, wait_for_port, timeout, flash_mode, speed, print_out):
    if wait_for_port:
        debug.print(f"Waiting for Proxmark3 to appear on {port}")
        open_count = 0
        while open_count < timeout:
            dev.serial_port = uart_open(port, speed)
            if dev.serial_port:
                break
            open_count += 1
            time.sleep(0.5)
            debug.print(f"Waiting {timeout - open_count}s")
    else:
        debug.print(f"Using UART port {port}")
        dev.serial_port = uart_open(port, speed)
    
    if not dev.serial_port:
        debug.error(f"ERROR: invalid serial port {port}")
        return False
    
    # Démarrer le thread de communication
    dev.run = True
    dev.communication_thread = threading.Thread(target=serial_reader_task, args=(dev, print_out))
    dev.communication_thread.start()
    return True

def send_command(serial_port, cmd, data):
    if cmd == CMD_PY_CLIENT_DATA:
        print(f"Envoi d'une donnée : {data.hex()}")  # Afficher en hexadécimal
    else:
        print(f"Envoi d'une commande : {cmd}")

    # Préparation des données
    cmd_bytes = struct.pack('<H', cmd)
    preamble = struct.pack('<4sB', TX_COMMANDNG_PREAMBLE_MAGIC, len(data))
    ng_flag = struct.pack('<B', 0x80)
    command = struct.pack('<H', cmd_bytes[1] << 8 | cmd_bytes[0])
    data_packet = data
    command_packet = preamble + ng_flag + command + data_packet + TX_COMMANDNG_POSTAMBLE_MAGIC

    # Envoyer la commande
    serial_port.write(command_packet)
    debug.warning(f"Commande envoyée: {command_packet.hex()}")
    
    
# Pour envoyer des données au proxmark. Le FW le reçoit via usb et le transmet au lecteur via RF.. 
# Idéé: Pond PC/SC coté Carte ?
def send_data(serial_port, data):
    send_command(serial_port, CMD_PY_CLIENT_DATA, data)
    


def main():
    parser = argparse.ArgumentParser(description="Proxmark3 Serial Communication")
    parser.add_argument('-v', dest='verbosity', action='store_const', const=1, default=1, help='Verbose mode (print)')
    parser.add_argument('-vv', dest='verbosity', action='store_const', const=2, help='Verbose mode (print + warning)')
    parser.add_argument('-vvv', dest='verbosity', action='store_const', const=3, help='Verbose mode (print + warning + error)')
    args = parser.parse_args()
    port = 'COM10'
    speed = 115200
    timeout = 10
    banner = stylized_banner({port})
    print(banner)

    global debug
    debug = Debug(verbosity_level=args.verbosity)

    try:
        proxmark_device = ProxmarkDevice()
        if open_proxmark(proxmark_device, port, 
                         wait_for_port=True, 
                         timeout=timeout, 
                         flash_mode=False, 
                         speed=speed, 
                         print_out = False # Print the output : mettre en false en mode interactif
                         ):
            print("Proxmark3 ouvert et communication démarrée.")
            print("Envoi de la commande CMD_PY_CLIENT_SIM...")
            command_data = b'\x00\x00\x00\x00'
            send_command(proxmark_device.serial_port, CMD_PY_CLIENT_SIM, command_data)
            # print(Reader)

            # Emulation loop
            while proxmark_device.run:
                try:
                    receivedCmd = received_queue.get_nowait() 
                    if receivedCmd == Reader:
                        print (f"\n{Fore.GREEN}## Init OK ##{Style.RESET_ALL}\n")
                        print(f"C-APDU: {receivedCmd}")
                        #pm =input("R-APDU:")
                        #print(f"R-APDU: {pm}")
                        time.sleep(0.5)
                        send_data(proxmark_device.serial_port, pm)
                except queue.Empty:
                    pass  # Do nothing if the queue is empty
                time.sleep(0.1)
        else:
            debug.error("Échec de l'ouverture de Proxmark3.")
    except KeyboardInterrupt:
        print("CTRL+C. Fermeture...")
        proxmark_device.run = False
        if proxmark_device.communication_thread:
            proxmark_device.communication_thread.join()
        if proxmark_device.serial_port and proxmark_device.serial_port.is_open:
            proxmark_device.serial_port.close()
        sys.exit(0)  # Exit cleanly after closing the serial port
    except Exception as e:
        debug.error(f"An error occurred: {e}")

if __name__ == "__main__":
    main()