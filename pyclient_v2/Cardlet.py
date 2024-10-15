from apdu import CAPDU, RAPDU
from utils import Debug

class Cardlet:
    _name = "undefined"
    _aid = None
    
    def __init__(self, serial_number):
        pass
        

class Application:
    def __init__(self, aid):
        self.aid = aid
        self.records = []

    def write_record(self, record):
        self.records.append(record)

    def read_record(self, index):
        if index < len(self.records):
            return self.records[index]
        else:
            return None

        def __repr__(self):
            return f"Application AID: {self.aid.hex()}, Records: {[record.hex() for record in self.records]}"

# EOF
