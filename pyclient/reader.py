# -*- coding: utf-8 -*-
from smartcard.System import readers
from smartcard.util import toHexString

# 1. Se connecter au lecteur de cartes
available_readers = readers()
if not available_readers:
    raise Exception("Aucun lecteur de carte detecte.")
else:
    reader = available_readers[0]
    print("Lecteur selectionne :", reader)

    # 2. Connexion à la carte
    connection = reader.createConnection()
    connection.connect()

    # 3. Sélectionner l'application 3F04
    # Commande APDU: CLA INS P1 P2 Lc Data
    SELECT_APPLICATION = [0x00, 0xA4, 0x00, 0x00, 0x02, 0x3F, 0x04]
    response, sw1, sw2 = connection.transmit(SELECT_APPLICATION)
    if sw1 != 0x90:
        raise Exception(f"Erreur lors de la selection de l'application: {sw1:02X} {sw2:02X}")
    print(f"Application 3F04 selectionnee. Reponse: {toHexString(response)}")

    # 4. Lire un record (exemple: record 1)
    # Commande APDU pour lire un record: CLA INS P1 P2 Le
    # CLA = 0x00 (Class byte)
    # INS = 0xB2 (Instruction byte for READ RECORD)
    # P1 = Record number (1-based)
    # P2 = 0x04 (indicates reading by record number)
    # Le = Maximum number of bytes expected in the response
    RECORD_NUMBER = 1
    READ_RECORD = [0x00, 0xB2, RECORD_NUMBER, 0x04, 0x00]
    response, sw1, sw2 = connection.transmit(READ_RECORD)
    
    if sw1 == 0x90:
        print(f"Donnees du record {RECORD_NUMBER} : {toHexString(response)}")
    else:
        raise Exception(f"Erreur lors de la lecture du record: {sw1:02X} {sw2:02X}")
