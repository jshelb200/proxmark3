import logging

class Cardlet:
    _aid = []
    _name = "undefined"
    _step = 0
    _session_counter = 0

    class ReturnCode:
        SW_Success = [0x90, 0x00]
        SW_ConditionsOfUseNotSatisfied = [0x69, 0x85]
        SW_ClassNotSupported = [0x6E, 0x00]
        SW_FileNotFound = [0x6A, 0x82]
        SW_RecordNotFound = [0x6A, 0x83]

    def __init__(self, serial_number):
        self.serial_number = serial_number
        self.applications = {}
        self.selected_application = None
        self.selected_record_index = 0
        self.status_word = self.ReturnCode.SW_Success

    def is_matching(self, first_command):
        return first_command == self._aid

    def get_aid(self):
        return self._aid

    def get_name(self):
        return self._name

    def select(self, aid):
        if aid in self.applications:
            self.selected_application = self.applications[aid]
            self.selected_record_index = 0
            self.status_word = self.ReturnCode.SW_Success
        else:
            self.selected_application = None
            self.status_word = self.ReturnCode.SW_FileNotFound
        return self.status_word

    def deselect(self):
        self.selected_application = None
        self.selected_record_index = 0

    def add_record(self, aid, record):
        if aid not in self.applications:
            self.status_word = self.ReturnCode.SW_FileNotFound
            return self.status_word
        self.applications[aid].add_record(record)
        self.status_word = self.ReturnCode.SW_Success
        return self.status_word

    def get_record(self, aid, record_id):
        if aid not in self.applications:
            self.status_word = self.ReturnCode.SW_FileNotFound
            return None
        record = self.applications[aid].get_record(record_id)
        if record:
            self.status_word = self.ReturnCode.SW_Success
        else:
            self.status_word = self.ReturnCode.SW_RecordNotFound
        return record

    def prepare(self):
        self._step = 0
        self._session_counter = 0

    def pad(self, buffer):
        if buffer is None:
            return None

        padded = False
        while len(buffer) % 16 != 0:
            if not padded:
                padded = True
                buffer.append(0x80)
            else:
                buffer.append(0x00)

        logging.debug("padded: " + ''.join(format(x, '02X') for x in buffer))
        return buffer

# EOF
