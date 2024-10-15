# -*- coding: utf-8 -*-
# ==============================================================================
# File: apdu.py
# Project: Proxmark Advanced Python Client
# File Description: This module defines APDU response codes (RAPDU) as byte sequences 
#              compliant with the ISO 7816-4 standard. These codes are used to 
#              interpret the response status from a smart card or NFC device.
# Author: Jerome.e & SpringCard Co
# ==============================================================================

class RAPDU:
    class SW:
        OK = b'\x90\x00'

        # 6x xx = APDU ERROR CODES

        # 61 xx
        BYTES_REMAINING_00 = b'\x61\x00'  # Response bytes remaining

        # 62 xx
        WARNING_STATE_UNCHANGED = b'\x62\x00'  # Warning, card state unchanged
        DATA_CORRUPT = b'\x62\x81'  # Returned data may be corrupted
        FILE_EOF = b'\x62\x82'  # The end of the file has been reached before the end of reading
        INVALID_DF = b'\x62\x83'  # Invalid DF
        INVALID_FILE = b'\x62\x84'  # Selected file is not valid
        FILE_TERMINATED = b'\x62\x85'  # File is terminated

        # 63 xx
        AUTH_FAILED = b'\x63\x00'  # Authentication failed
        FILE_FILLED = b'\x63\x81'  # File filled up by the last write

        # 65 xx
        MEMORY_FULL = b'\x65\x01'  # Memory failure
        WRITE_MEMORY_ERR = b'\x65\x81'  # Write problem / Memory failure / Unknown mode

        # 67 xx
        WRONG_LENGTH = b'\x67\x00'  # Wrong length

        # 68 xx
        LOGICAL_CHANNEL_NOT_SUPPORTED = b'\x68\x81'  # Card does not support the operation on the specified logical channel
        SECURE_MESSAGING_NOT_SUPPORTED = b'\x68\x82'  # Card does not support secure messaging
        LAST_COMMAND_EXPECTED = b'\x68\x83'  # Last command in chain expected
        COMMAND_CHAINING_NOT_SUPPORTED = b'\x68\x84'  # Command chaining not supported

        # 69 xx
        TRANSACTION_FAIL = b'\x69\x00'  # No successful transaction executed during session
        SELECT_FILE_ERR = b'\x69\x81'  # Cannot select indicated file, command not compatible with file organization
        SECURITY_STATUS_NOT_SATISFIED = b'\x69\x82'  # Security condition not satisfied
        FILE_INVALID = b'\x69\x83'  # File invalid
        DATA_INVALID = b'\x69\x84'  # Data invalid
        CONDITIONS_NOT_SATISFIED = b'\x69\x85'  # Conditions of use not satisfied
        COMMAND_NOT_ALLOWED = b'\x69\x86'  # Command not allowed (no current EF)
        SM_DATA_MISSING = b'\x69\x87'  # Expected SM data objects missing
        SM_DATA_INCORRECT = b'\x69\x88'  # SM data objects incorrect
        APPLET_SELECT_FAILED = b'\x69\x99'  # Applet selection failed

        # 6A xx
        INVALID_P1P2 = b'\x6A\x00'  # Bytes P1 and/or P2 are invalid
        WRONG_DATA = b'\x6A\x80'  # Wrong data
        FUNC_NOT_SUPPORTED = b'\x6A\x81'  # Function not supported
        FILE_NOT_FOUND = b'\x6A\x82'  # File not found
        RECORD_NOT_FOUND = b'\x6A\x83'  # Record not found
        FILE_FULL = b'\x6A\x84'  # Not enough memory space in the file
        LC_TLV_CONFLICT = b'\x6A\x85'  # LC / TLV conflict
        INCORRECT_P1P2 = b'\x6A\x86'  # Incorrect parameters (P1,P2)
        FILE_EXISTS = b'\x6A\x89'  # File exists
        NOT_IMPLEMENTED = b'\x6A\xFF'  #

        # 6x 00
        WRONG_P1P2 = b'\x6B\x00'  # Incorrect parameters (P1,P2)
        CORRECT_LENGTH_00 = b'\x6C\x00'  # Correct Expected Length (Le)
        INS_NOT_SUPPORTED = b'\x6D\x00'  # INS value not supported
        CLA_NOT_SUPPORTED = b'\x6E\x00'  # CLA value not supported
        UNKNOWN = b'\x6F\x00'  # No precise diagnosis
        
class CAPDU:
    class CLA: 
        BASE = b'\x00' # Commandes ISO de base 
        ADMIN_FILE = b'\x94' # Read record from file
        SPRINGCARD = b'\xFF' # Commandes spécifiques SpringCard
    class INS:
        SELECT = b'\xA4'
        READ_RECORD = b'\xB2'
        UPDATE_BINARY = b'\xD6'
        GET_RESPONSE = b'\xC0'
        


#print(RAPDU.SW.OK)  # Affiche : b'\x90\x00'