import serial
import struct
import crcmod.predefined
import threading
import time


# Définir les constantes
CMD_PING = 0x0109
COMMANDNG_PREAMBLE_MAGIC = b'PM3b'    #0x504d3362
COMMANDNG_POSTAMBLE_MAGIC = b'b3'     #0x6233
PM3_CMD_DATA_SIZE = 512  # Taille maximale de la commande

# Calculer le CRC16 
def calculate_crc(data):
    crc16 = crcmod.predefined.Crc('crc-ccitt-false')
    crc16.update(data)
    return crc16.crcValue

# Construire la commande NG pour ping
def build_ping_command():
    preamble = struct.pack('<IHBH', COMMANDNG_PREAMBLE_MAGIC, 1, 1, CMD_PING)
    data = b''  # Pas de données pour la commande de ping
    crc_value = calculate_crc(preamble + data)
    crc = struct.pack('<H', crc_value)
    postamble = struct.pack('<H', COMMANDNG_POSTAMBLE_MAGIC)
    
    full_command = preamble + data + crc + postamble
    
    return full_command


# Fonction pour la lecture continue du port série
# La structure des données est parsée par rapport aux commandes Proxmark3
def serial_reader(port):
    try:
        ser = serial.Serial(port, baudrate=115200, timeout=0.5)
        print("Serial reader daemon Start.")
        
        buffer = b''  # Buffer pour accumuler les données reçues

        while True:
            data = ser.read(1)  # Lire un octet à la fois
            if data:
                buffer += data

                # Vérifier si nous avons un postambule complet  
                if len(buffer) >= 4 and buffer[-2:] == COMMANDNG_POSTAMBLE_MAGIC :
                    response = buffer
                    # Décomposer la réponse en parties spécifiques
                    #received_preamble = response[:4]
                    received_data_len = response[4]
                    received_data = response[12:12 + received_data_len - 2]
                    #received_postamble = response[12 + received_data_len - 2:12 + received_data_len]

                    #print("Received response:", response.hex(), " | ", response.decode('ascii', errors='ignore'))
                    #print("Received Preamble:", received_preamble.hex(), " | ", received_preamble.decode('ascii', errors='ignore'))
                    print("Received Data len:", hex(received_data_len), " | ", str(received_data_len))
                    print("Received Data:", received_data.hex(), " | ", received_data.decode('ascii', errors='ignore'))
                    #print("Received Postamble:", received_postamble.hex(), " | ", received_postamble.decode('ascii', errors='ignore'))
                    print ("___________________________________________________________________________________________________________")
                    buffer = b''
                            
            else:
                print(".")
            
            time.sleep(0.1)  # Attente courte pour éviter une utilisation excessive du CPU
        
        ser.close()
    except Exception as e:
        print("Error in serial_reader:", e)

# Fonction principale pour envoyer la commande de ping
def send_ping_command(port):
    try:
        ser = serial.Serial(port, baudrate=115200, timeout=1)
        ping_command = build_ping_command()
        ser.write(ping_command)
        
        # Attendre un court moment pour laisser le Proxmark3 répondre
        time.sleep(0.2)
        
        #Print send details
        print("Sent command:", ping_command.hex(), " | ", ping_command.decode('ascii', errors='ignore'))
        print("Preamble sent:", ping_command[:8].hex(), " | ", ping_command[:8].decode('ascii', errors='ignore'))
        print("CRC sent:", ping_command[8:10].hex(), " | ", ping_command[8:10].decode('ascii', errors='ignore'))
        print("Postamble sent:", ping_command[10:12].hex(), " | ", ping_command[10:12].decode('ascii', errors='ignore'))
        print("Data sent:", ping_command[12:].hex(), " | ", ping_command[12:].decode('ascii', errors='ignore'))
        
        ser.close()  # Toujours fermer le port série après utilisation
    
    except Exception as e:
        print("Error in send_ping_command:", e)

# Test
try:
    port = 'COM10'  # Remplacez par le port série correct pour votre Proxmark3
    
    # Créer et démarrer le thread pour la lecture série en arrière-plan
    thread = threading.Thread(target=serial_reader, args=(port,), daemon=True)
    thread.start()
    while (1) :
        # juste attendre la reponse du proxmark3
        time.sleep(1)
    
except Exception as e:
    print("An error occurred:", e)
