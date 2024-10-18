# -*- coding: utf-8 -*-
# =======================================================================================
# File: Cardlet_calyspo.py
# Project: Proxmark Advanced Python Client - Calypso Card Emulation
# File Description: This module provides a class to emulate a Calypso NFC card.
#                   The class allows for creating and managing applications (AID)
#                   and records within these applications, simulating a real NFC card
#                   functionality in a controlled environment.
# Author: Jerome.e & SpringCard Co
# =======================================================================================

# Import necessary modules
from apdu import CAPDU, RAPDU
from utils import Debug
from colorama import init, Fore, Style

class Application:
    def __init__(self, aid):
        self.aid = aid
        self.records = []

    def add_record(self, record):
        self.records.append(record)

    def get_record(self, index):
        if index < len(self.records):
            return self.records[index]
        else:
            return None

    def __repr__(self):
        return f"Application AID: {self.aid.hex()}, Records: {[record.hex() for record in self.records]}"

class CalypsoCard:
    def __init__(self, uid):
        self.uid = uid
        self.applications = {}  # Dictionnaire d'applications, avec l'AID comme cl?
        self.selected_application = None  # Objet de la classe Application, initialement None
        self.selected_record_index = 0
        self.status_word = RAPDU.SW.OK

    def __repr__(self):
        app_repr = {aid.hex(): repr(app) for aid, app in self.applications.items()}
        selected_app_repr = repr(self.selected_application) if self.selected_application else "None"
        return (f"CalypsoCard(uid={self.uid!r}, "
                f"Applications={app_repr}, "
                f"Selected Application={selected_app_repr}, "
                f"Selected Record Index={self.selected_record_index}, "
                f"Status Word={self.status_word.hex() if isinstance(self.status_word, bytes) else self.status_word})")

    def create_application(self, aid):
        if aid in self.applications:
            self.status_word = RAPDU.SW.FILE_EXISTS
            return self.status_word
        self.applications[aid] = Application(aid)  # Cr?er une nouvelle application
        self.status_word = RAPDU.SW.OK
        return self.status_word

    def select_application(self, aid):
        if aid in self.applications:
            self.selected_application = self.applications[aid]
            self.selected_record_index = 0  # R?initialiser l'index des records lors de la s?lection
            self.status_word = RAPDU.SW.OK
        else:
            self.selected_application = None
            self.status_word = RAPDU.SW.FILE_NOT_FOUND
        return self.status_word

    def add_record(self, aid, record):
        if aid not in self.applications:
            self.status_word = RAPDU.SW.FILE_NOT_FOUND
            return self.status_word
        self.applications[aid].add_record(record)  # Ajouter le record ? l'application
        self.status_word = RAPDU.SW.OK
        return self.status_word

    def get_record(self, aid, record_id):
        if aid not in self.applications:
            self.status_word = RAPDU.SW.FILE_NOT_FOUND
            return None
        record = self.applications[aid].get_record(record_id)  # Retourner le record demand?
        if record:
            self.status_word = RAPDU.SW.OK
        else:
            self.status_word = RAPDU.SW.RECORD_NOT_FOUND
        return record

    def get_selected_application(self):
        return self.selected_application


def process_tpdu(card, tpdu):
    Iblock = {b'\x02', b'\x03'}
    response = b''
    sw = b''
    pcb, cla, ins, p1_p2, lc, data, le, crc = parse_tpdu(tpdu)
    if (pcb in Iblock) : # Si la commande reçue est un bloc I (02 ou 03) 
        print (f"\n{Fore.GREEN}## Init OK ##{Style.RESET_ALL}\n")
        print(f"TPDU[Iblock Cmd] - PCB: {pcb}, CLA: {cla}, INS: {ins}, P1_P2: {p1_p2}, LC: {lc}, DATA: {data}, LE: {le}, CRC: {crc}")
        if cla == CAPDU.CLA.ADMIN_FILE:
            if ins == CAPDU.INS.SELECT:
                aid = data  # Utiliser directement les bytes pour l'AID
                print(f"AID (hex): {aid.hex()}")
                status = card.select_application(aid)
                print(f"Selection status: {status}")
                print(f"Selected application: {card.get_selected_application()}")

                if card.selected_application:
                    response = bytes.fromhex("85170204021D011F0000000101010100000000000000000000")  # Exemple de FCI
                    sw = RAPDU.SW.OK
                else:
                    response = b''
                    sw = RAPDU.SW.FILE_NOT_FOUND
            elif ins == CAPDU.INS.READ_RECORD:
                if card.selected_application is None:
                    response = b''
                    sw = RAPDU.SW.FILE_NOT_FOUND
                else:
                    record = card.get_record(card.selected_application.aid, card.selected_record_index)
                    if record:
                        card.selected_record_index += 1
                        response = record
                        sw = RAPDU.SW.OK
                    else:
                        response = b''
                        sw = RAPDU.SW.RECORD_NOT_FOUND
            else:
                response = b''
                sw = RAPDU.SW.INS_NOT_SUPPORTED
        else:
            response = b''
            sw = RAPDU.SW.CLA_NOT_SUPPORTED

    return response, sw


def parse_tpdu(tpdu):
    # Initialisation des champs
    pcb = cla = ins = p1_p2 = lc = data = le = crc = None
    data_len = 0
    size = len(tpdu)-2  

    # PCB (1 octet)
    if size >= 1:
        pcb = tpdu[:1]

    # CLA (1 octet)
    if size >= 2:
        cla = tpdu[1:2]

    # INS (1 octet)
    if size >= 3:
        ins = tpdu[2:3]

    # P1_P2 (2 octets)
    if size >= 5:
        p1_p2 = tpdu[3:5]

    # Lc (1 octet, longueur des données)
    if size >= 6:
        lc = tpdu[5:6]
        data_len = int.from_bytes(lc, 'big') if lc else 0

    # DATA (N c octets) // AID size = 2 bytes all time ? 
    if size >= 6 + 2:
        data = tpdu[6:6 + 2]  # Extraire LC octets de données (par exemple, l'AID)

    # Le (1 octet, longueur maximale des données attendues)
    if size >= 6 + data_len + 1:
        le = tpdu[6 + data_len:6 + data_len + 1]

    # CRC (2 octets, contrôle de redondance cyclique)
    if size >= 6 + data_len + 2:
        crc = tpdu[-2:]

    # Traitement spécifique pour les commandes READ RECORD (INS = 0xB2)
    if ins == b'\xb2':
        data = None
        le = None

    return pcb, cla, ins, p1_p2, lc, data, le, crc



def test_process_tpdu():
    card = CalypsoCard(uid="00000000")
    
    # Cr?ation des applications et ajout des enregistrements
    aid_1 = b'\x3F\x00'
    card.create_application(aid_1)  
    record_1 = bytes.fromhex("DDDDDDDDDDDD")
    card.add_record(aid_1, record_1)
    
    # Affichage de la carte
    print(f"Card: {card}")
    
    # Test de la commande SELECT
    tpdu_select = b'\x02\x94\xa4\x08\x00\x04\x3f\x00\x00\x02\xb9\x7a'
    response, sw = process_tpdu(card, tpdu_select)
    print(f"SELECT response: {response.hex()}, Status: {sw.hex()}")

    # Test de la commande READ RECORD
    tpdu_read_record = bytes.fromhex("0394B201041DC546")
    response, sw = process_tpdu(card, tpdu_read_record)
    print(f"READ RECORD response: {response.hex()}, Status: {sw.hex()}")

    # Test de la commande READ RECORD avec aucune application selectionnee
    card.selected_application = None  # D?s?lectionner l'application
    response, sw = process_tpdu(card, tpdu_read_record) 
    print(f"READ RECORD without SELECT response: {response.hex()}, Status: {sw.hex()}")
test_process_tpdu()