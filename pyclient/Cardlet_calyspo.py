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
    def __init__(self, serial_number):
        self.serial_number = serial_number
        self.applications = {}  # Dictionnaire d'applications, avec l'AID comme cl?
        self.selected_application = None  # Objet de la classe Application, initialement None
        self.selected_record_index = 0
        self.status_word = RAPDU.SW.OK

    def __repr__(self):
        app_repr = {aid.hex(): repr(app) for aid, app in self.applications.items()}
        selected_app_repr = repr(self.selected_application) if self.selected_application else "None"
        return (f"CalypsoCard(serial_number={self.serial_number!r}, "
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
    response = b''
    sw = b''
    print("tpdu=", tpdu)
    pcb, cla, ins, p1_p2, lc, data, le, crc = parse_tpdu(tpdu)
    print(f"TPDU - PCB: {pcb}, CLA: {cla}, INS: {ins}, P1_P2: {p1_p2}, LC: {lc}, DATA: {data}, LE: {le}, CRC: {crc}")

    if cla == CAPDU.CLA.ADMIN_FILE:
        if ins == CAPDU.INS.SELECT:
            aid = data  # Utiliser directement les bytes pour l'AID
            print(f"AID (hex): {aid.hex()}")
            status = card.select_application(aid)
            print(f"Selection status: {status}")
            print(f"Selected application: {card.get_selected_application()}")

            if card.selected_application:
                response = bytes.fromhex("03851704040210011F1200000101010100000000000000000010004810")  # Exemple de FCI
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
    pcb = cla = ins = p1_p2 = lc = data = le = crc = None
    data_len = 0

    if len(tpdu) >= 1:
        pcb = tpdu[:1]

    if len(tpdu) >= 2:
        cla = tpdu[1:2]

    if len(tpdu) >= 3:
        ins = tpdu[2:3]

    if len(tpdu) >= 5:
        p1_p2 = tpdu[3:5]

    if len(tpdu) >= 6:
        lc = tpdu[5:6]
        data_len = int.from_bytes(lc, 'big') if lc else 0

    if len(tpdu) >= 6 + data_len:
        data = tpdu[6:6 + data_len]

    if len(tpdu) >= 6 + data_len + 1:
        le = tpdu[6 + data_len:6 + data_len + 1]

    if len(tpdu) >= 6 + data_len + 2:
        crc = tpdu[-2:]

    # Traitement sp?cifique pour les commandes READ RECORD
    if ins == b'\xb2':
        data = None
        le = None

    return pcb, cla, ins, p1_p2, lc, data, le, crc


def test_process_tpdu():
    card = CalypsoCard(serial_number="00000000")
    
    # Cr?ation des applications et ajout des enregistrements
    aid_1 = b'\x3F\x04'
    aid_2 = b'\x3F\x05'
    card.create_application(aid_1)
    card.create_application(aid_2)
    
    record_1 = bytes.fromhex("DDDDDDDDDDDD")
    record_2 = bytes.fromhex("CCCCCCCCCCCC")
    card.add_record(aid_1, record_1)
    card.add_record(aid_2, record_2)
    
    # Affichage de la carte
    print(f"Card: {card}")
    
    # Test de la commande SELECT
    tpdu_select = b'\x02\x94\xa4\x00\x00\x02\x3f\x04\x00\xce\xb4'
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