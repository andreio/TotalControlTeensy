#include <Adafruit_FRAM_SPI.h>
#include <Arduino.h>
#include <Bounce2.h>
#include <MIDI.h>

// Types

enum REQUEST_TYPES : byte {
  REQUEST_CONTROLLER_PRESET_STATE,
  SEND_CONTROLLER_PRESET_STATE,
  REQUEST_RACK_PRESET_STATE,
  SEND_RACK_PRESET_STATE,
  REQUEST_RACK_LOOP_NAMES,
  SEND_RACK_LOOP_NAMES,
  REQUEST_CONTROLLER_PRESET_IDS,
  REQUEST_RACK_PRESET_IDS,
  PING,
  RESET
};

enum RESPONSE_TYPES : byte {
  RECEIVE_CONTROLLER_PRESET_STATE,
  RECEIVE_RACK_PRESET_STATE,
  RECEIVE_CONTROLLER_PRESET_IDS,
  RECEIVE_RACK_PRESET_IDS,
  RECEIVE_RACK_LOOP_NAMES,
  PONG,
};

enum MSG_TYPE : byte {
  NONE_TYPE,
  PC,
  CC,
  TCCC,
  TCPC,
};

enum MSG_ACTION : byte {
  NONE_ACTION,
  PRESS,
  RELEASE,
  LONG_PRESS,
  LONG_PRESS_RELEASE,
  TAP
};
enum LOOP_TOGGLE : byte {
  UNCHANGED,
  SET,
  UNSET,
  TOGGLE,
};

enum NEXTION_RETURN { TOUCH_EVENT = 0x65 };
enum NEXTION_BUTTONS {
  EDIT = 3,
  COPY = 4,
  PASTE = 5,
  BANK_UP = 6,
  BANK_DOWN = 7,
  PAGE_LEFT = 8,
  PAGE_RIGHT = 9,
  SETTINGS = 10,
  P0 = 21,
  P1 = 22,
  P2 = 23,
  P3 = 24,
  P4 = 25,
  P5 = 26,
  P6 = 27,
  P7 = 28,
  L0 = 11,
  L1 = 12,
  L2 = 13,
  L3 = 14,
  L4 = 15,
  L5 = 17,
  L6 = 18,
  L7 = 19,
  L8 = 20,
  TAP_BTN = 36
};

enum MIDI_IN_CC : byte {
  BANK_MOVE_CC,
  PAGE_MOVE_CC,
  BANK_CC,
  PAGE_CC,
  TAP_MOVE_CC,
  TAP_CC,
};

enum NEXTION_PAGE {
  MAIN_PAGE = 0,
  PERF1_PAGE = 1,
  PERF2_PAGE = 2,
  TAP_PAGE = 3
};

typedef LOOP_TOGGLE Loops[9];

typedef struct {
  MSG_TYPE type = NONE_TYPE;
  MSG_ACTION action = NONE_ACTION;
  uint8_t ccNumber = 0;
  uint8_t pcNumber = 0;
  uint8_t ccValue = 0;
  uint8_t midiChannel = 0;
  uint8_t omni = 0;
  uint8_t rackPreset = 0;
  Loops loops = {UNCHANGED, UNCHANGED, UNCHANGED, UNCHANGED, UNCHANGED,
                 UNCHANGED, UNCHANGED, UNCHANGED, UNCHANGED};
} Message;

typedef char PresetName[9];
typedef Message Messages[8];

typedef struct {
  uint8_t index = 0;
  PresetName presetName = {};
  PresetName bankName = {};
} PresetID;

typedef struct {
  PresetName presetName = {};
  PresetName toggleName = {};
  Messages messages;
} ControllerPreset;

typedef struct {
  PresetName presetName = {};
  Loops loops = {UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET};
} RackPreset;

typedef struct {
  uint8_t index = 0;
  PresetName bankName = {};
  ControllerPreset preset;
} ControllerState;

typedef struct {
  uint8_t index = 0;
  PresetName bankName = {};
  RackPreset preset;
} RackState;

typedef ControllerPreset BankPresets[8];

typedef struct {
  BankPresets presets;
  PresetName name;
  uint8_t index;
} PseudoBank;

PseudoBank currentBank = {{}, "", 0};

// VARS/CONSTS

const uint8_t FRAM_CS = 10;

uint SWITCH_COUNT = 8;
const uint DEBOUNCE_DURATION = 50;

uint switches[] = {14, 15, 16, 17, 18, 19, 20, 21};
Bounce2::Button bounces[8];

const uint SYSEX_START = 0xf0;
const uint SYSEX_STOP = 0xf7;
const uint SYSEX_DEV = 23;

const uint8_t NT = 255;

uint8_t nextionBuffer[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t nextionBufferLength = 0;
uint8_t NTCount = 0;
const uint8_t MAX_NT = 3;
uint8_t nextionPage = 0;
uint8_t nextionBlinks[3] = {0, 0, 0};
const int NEXTION_LOW_BAUD = 9600;
const int NEXTION_HIGH_BAUD = 921600;

double lastTapMs = 0;
int tapValue = 1000;
bool tapSent = false;
int tapTimeoutMs = 2000;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
Adafruit_FRAM_SPI fram = Adafruit_FRAM_SPI(FRAM_CS);

// put function declarations here:
void setupSwitches();
void setupMIDI();
void onSysex(const uint8_t *data, uint16_t length, bool complete);
void stripSysexConstants(const uint8_t *buffer, uint8_t *newBuffer,
                         size_t bufferLength);
void insertSysexConstants(uint8_t *buffer, RESPONSE_TYPES response);
REQUEST_TYPES getRequestType(const uint8_t *buffer);
void sendControllerState(uint8_t index);
void sendResponse(uint8_t *data, RESPONSE_TYPES response, size_t dataSize);
void sendControllerPresetIds();
void sendRackPresetIds();
void sendRackState(uint8_t index);
void receiveControllerState(ControllerState *buffer);
void receiveRackState(RackState *buffer);
void reset();
template <typename T, typename P>
void receiveState(T *buffer, uint32_t presetsAddress, uint32_t banksAddress);
template <typename T>
void sendPresetIds(uint32_t presetAddress, uint32_t bankAddress,
                   RESPONSE_TYPES responseType);
template <typename T, typename P>
void sendState(uint8_t index, uint32_t presetsAddress, uint32_t banksAddress,
               RESPONSE_TYPES responseType);
void receiveRackLoopNames(PresetName loopNames[9]);
void sendRackLoopNames();
void selectBank(uint8_t index);
void updateUI();
void processNextionBuffer();
void updateNextionBuffer();
void processNextionTouch();
void setupNextion();
void setNextionPageRolled(uint8_t page);
void setNextionPageAbsolute(uint8_t page);
void updateNextionBlink(uint8_t index);
void updateNextionBlinks();
void onControlChange(uint8_t channel, uint8_t control, uint8_t value);
void processTap();
void updateTap();
void sendTap();

// MEMORY LOCATIONS
const uint32_t CONTROLLER_PRESETS_ADDRESS = 1000;
const uint32_t RACK_PRESETS_ADDRESS = 50000;
const uint32_t CONTROLLER_BANK_NAMES_ADDRESS = 60000;
const uint32_t RACK_BANK_NAMES_ADDRESS = 62000;
const uint32_t RACK_LOOP_NAMES_ADDRESS = 64000;

void setup() {
  MIDI.begin();
  Serial.begin(9600);
  setupSwitches();
  MIDI.turnThruOff();
  MIDI.setHandleControlChange(onControlChange);
  usbMIDI.begin();
  usbMIDI.setHandleSysEx(onSysex);
  fram.begin();
  setupNextion();
}

void loop() {
  MIDI.read();
  usbMIDI.read();
  for (uint i = 0; i < SWITCH_COUNT; i++) {
    Bounce2::Button *sw = &bounces[i];
    sw->update();
    if (sw->changed()) {
      if (sw->read()) {
        updateNextionBlink(i);
      }
    }
  }
  updateNextionBuffer();
  processTap();
}

void setupSwitches() {
  for (uint i = 0; i < SWITCH_COUNT; i++) {
    bounces[i].attach(switches[i], INPUT_PULLUP);
    bounces[i].interval(DEBOUNCE_DURATION);
    bounces[i].setPressedState(LOW);
  }
}

void setupMidi() {
  MIDI.begin();
  MIDI.turnThruOff();
}

void onSysex(const uint8_t *data, uint16_t length, bool complete) {
  if (data[1] != 23) {
    return;
  }
  REQUEST_TYPES request = getRequestType(data);
  uint8_t buffer[length - 4] = {};
  stripSysexConstants(data, buffer, length);
  switch (request) {
  case SEND_CONTROLLER_PRESET_STATE: {
    receiveControllerState((ControllerState *)buffer);
    uint8_t index = ((ControllerState *)buffer)->index;
    selectBank(index / 8);
    break;
  }
  case REQUEST_CONTROLLER_PRESET_STATE: {
    uint8_t index = buffer[0];
    sendControllerState(index);
    selectBank(index / 8);
    break;
  }
  case REQUEST_CONTROLLER_PRESET_IDS: {
    sendControllerPresetIds();
    break;
  }
  case SEND_RACK_PRESET_STATE: {
    receiveRackState((RackState *)buffer);
    break;
  }
  case REQUEST_RACK_PRESET_STATE: {
    uint8_t index = buffer[0];
    sendRackState(index);
    break;
  }
  case REQUEST_RACK_PRESET_IDS: {
    sendRackPresetIds();
    break;
  }
  case SEND_RACK_LOOP_NAMES: {
    receiveRackLoopNames((PresetName *)buffer);
    updateUI();
    break;
  }
  case REQUEST_RACK_LOOP_NAMES: {
    sendRackLoopNames();
    break;
  }
  case PING: {
    sendResponse((uint8_t *){}, PONG, 0);
    break;
  }
  case RESET: {
    reset();
    break;
  }
  }
  usbMIDI.send_now();
}

void receiveControllerState(ControllerState *buffer) {
  receiveState<ControllerState, ControllerPreset>(
      buffer, CONTROLLER_PRESETS_ADDRESS, CONTROLLER_BANK_NAMES_ADDRESS);
  sendControllerPresetIds();
}

void receiveRackState(RackState *buffer) {
  receiveState<RackState, RackPreset>(buffer, RACK_PRESETS_ADDRESS,
                                      RACK_BANK_NAMES_ADDRESS);
  sendRackPresetIds();
}

void receiveRackLoopNames(PresetName loopNames[9]) {
  fram.writeEnable(true);
  fram.write(RACK_LOOP_NAMES_ADDRESS, (uint8_t *)loopNames,
             sizeof(PresetName[9]));
  fram.writeEnable(false);
  sendRackLoopNames();
}

void sendControllerPresetIds() {
  sendPresetIds<ControllerPreset>(CONTROLLER_PRESETS_ADDRESS,
                                  CONTROLLER_BANK_NAMES_ADDRESS,
                                  RECEIVE_CONTROLLER_PRESET_IDS);
}

void sendRackPresetIds() {
  sendPresetIds<RackPreset>(RACK_PRESETS_ADDRESS, RACK_BANK_NAMES_ADDRESS,
                            RECEIVE_RACK_PRESET_IDS);
}

void sendControllerState(uint8_t index) {
  sendState<ControllerState, ControllerPreset>(
      index, CONTROLLER_PRESETS_ADDRESS, CONTROLLER_BANK_NAMES_ADDRESS,
      RECEIVE_CONTROLLER_PRESET_STATE);
}

void sendRackState(uint8_t index) {
  sendState<RackState, RackPreset>(index, RACK_PRESETS_ADDRESS,
                                   RACK_BANK_NAMES_ADDRESS,
                                   RECEIVE_RACK_PRESET_STATE);
}

void sendRackLoopNames() {
  PresetName loopNames[9] = {};
  fram.read(RACK_LOOP_NAMES_ADDRESS, (uint8_t *)loopNames,
            sizeof(PresetName[9]));
  sendResponse((uint8_t *)loopNames, RECEIVE_RACK_LOOP_NAMES,
               sizeof(PresetName[9]));
}

void reset() {
  ControllerPreset controllerPreset = {};
  RackPreset rackPreset = {};
  PresetName controllerBank = {};
  PresetName rackBank = {};
  for (int i = 0; i < 128; i++) {
    const uint8_t program = i % 8;
    snprintf(controllerPreset.presetName, sizeof(PresetName), "P%d", program);
    snprintf(controllerPreset.toggleName, sizeof(PresetName), "T%d", program);
    fram.writeEnable(true);
    fram.write(CONTROLLER_PRESETS_ADDRESS + sizeof(ControllerPreset) * i,
               (uint8_t *)(&controllerPreset), sizeof(ControllerPreset));
    fram.writeEnable(false);

    snprintf(rackPreset.presetName, 9, "RP%d", program);
    fram.writeEnable(true);
    fram.write(RACK_PRESETS_ADDRESS + sizeof(RackPreset) * i,
               (uint8_t *)(&rackPreset), sizeof(rackPreset));
    fram.writeEnable(false);
    if (!program) {
      const uint8_t bank = i / 8;

      snprintf(controllerBank, sizeof(PresetName), "B%d", bank);
      snprintf(rackBank, sizeof(PresetName), "RB%d", bank);
      fram.writeEnable(true);
      fram.write(CONTROLLER_BANK_NAMES_ADDRESS + sizeof(PresetName) * bank,
                 (uint8_t *)(&controllerBank), sizeof(PresetName));
      fram.writeEnable(false);
      fram.writeEnable(true);
      fram.write(RACK_BANK_NAMES_ADDRESS + sizeof(PresetName) * bank,
                 (uint8_t *)(&rackBank), sizeof(PresetName));
      fram.writeEnable(false);
    }
  }
}

void sendResponse(uint8_t *data, RESPONSE_TYPES responseType, size_t dataSize) {
  uint8_t response[dataSize + 2] = {};
  insertSysexConstants(response, responseType);
  memcpy(response + 2, data, dataSize);
  usbMIDI.sendSysEx(dataSize + 2, response);
}

void insertSysexConstants(uint8_t *buffer, RESPONSE_TYPES responseType) {
  buffer[0] = SYSEX_DEV;
  buffer[1] = responseType;
}

void stripSysexConstants(const uint8_t *buffer, uint8_t *newBuffer,
                         size_t bufferLength) {
  memcpy(newBuffer, buffer + 3, bufferLength - 4);
}

REQUEST_TYPES getRequestType(const uint8_t *buffer) {
  return (REQUEST_TYPES)buffer[2];
}

template <typename T, typename P>
void receiveState(T *buffer, uint32_t presetsAddress, uint32_t banksAddress) {
  fram.writeEnable(true);
  fram.write(banksAddress + sizeof(PresetName) * (buffer->index / 8),
             (uint8_t *)buffer->bankName, sizeof(PresetName));
  fram.writeEnable(false);
  fram.writeEnable(true);
  fram.write(presetsAddress + sizeof(P) * (buffer->index),
             (uint8_t *)&(buffer->preset), sizeof(P));
  fram.writeEnable(false);
}

template <typename T>
void sendPresetIds(uint32_t presetAddress, uint32_t bankAddress,
                   RESPONSE_TYPES responseType) {
  PresetID presetIds[128] = {};
  int presetNameOffset = offsetof(T, presetName);
  for (uint8_t i = 0; i < 128; i++) {
    presetIds[i].index = i;
    fram.read(presetAddress + i * sizeof(T) + presetNameOffset,
              (uint8_t *)(presetIds[i].presetName), sizeof(PresetName));
    fram.read(bankAddress + (i / 8) * sizeof(PresetName),
              (uint8_t *)(presetIds[i].bankName), sizeof(PresetName));
  }
  sendResponse((uint8_t *)presetIds, responseType, sizeof(presetIds));
}

template <typename S, typename P>
void sendState(uint8_t index, uint32_t presetsAddress, uint32_t banksAddress,
               RESPONSE_TYPES responseType) {
  S controllerState = {};
  fram.read(presetsAddress + sizeof(P) * index,
            (uint8_t *)&(controllerState.preset), sizeof(P));
  fram.read(banksAddress + sizeof(PresetName) * (index / 8),
            (uint8_t *)controllerState.bankName, sizeof(PresetName));
  controllerState.index = index;
  sendResponse((uint8_t *)(&controllerState), responseType,
               sizeof(controllerState));
}

// UI

void selectBank(uint8_t index) {
  index = (16 + index) % 16;
  fram.read(CONTROLLER_PRESETS_ADDRESS + sizeof(ControllerPreset) * index * 8,
            (uint8_t *)&(currentBank.presets), sizeof(ControllerPreset) * 8);
  fram.read(CONTROLLER_BANK_NAMES_ADDRESS + sizeof(PresetName) * index,
            (uint8_t *)&(currentBank.name), sizeof(PresetName));
  currentBank.index = index;
  updateUI();
}

void updateUI() {
  switch (nextionPage) {
  case MAIN_PAGE:
    for (int i = 0; i < 8; i++) {
      Serial2.printf("loop%d.txt=\"%s\"", i, currentBank.presets[i].presetName);
      Serial2.write(NT);
      Serial2.write(NT);
      Serial2.write(NT);
    }
    Serial2.printf("pInf0.txt=\"Bank %d\"", currentBank.index);
    Serial2.write(NT);
    Serial2.write(NT);
    Serial2.write(NT);
    Serial2.printf("pInf1.txt=\"%s\"", currentBank.name);
    Serial2.write(NT);
    Serial2.write(NT);
    Serial2.write(NT);
    break;

  case PERF1_PAGE:
    PresetName loopNames[8] = {};
    fram.read(RACK_LOOP_NAMES_ADDRESS, (uint8_t *)loopNames,
              sizeof(PresetName) * 8);
    for (int i = 0; i < 8; i++) {
      Serial2.printf("loop%d.txt=\"%s\"", i, loopNames[i]);
      Serial2.write(NT);
      Serial2.write(NT);
      Serial2.write(NT);
    }
    break;
  }
}

void processNextionBuffer() {
  NEXTION_RETURN nextionReturn = (NEXTION_RETURN)nextionBuffer[0];
  switch (nextionReturn) {
  case TOUCH_EVENT:
    processNextionTouch();
    break;
  }
}

void processNextionTouch() {
  uint8_t pageNumber = nextionBuffer[1];
  NEXTION_BUTTONS button = (NEXTION_BUTTONS)nextionBuffer[2];
  bool toggle = nextionBuffer[3];
  switch (button) {
  case BANK_UP:
    if (!toggle && pageNumber == MAIN_PAGE) {
      selectBank(currentBank.index + 1);
    }

    break;
  case BANK_DOWN:
    if (!toggle && pageNumber == MAIN_PAGE) {
      selectBank(currentBank.index - 1);
    }
    break;
  case PAGE_LEFT:
    if (!toggle) {
    }
    setNextionPageRolled(nextionPage - 1);
    break;
  case PAGE_RIGHT:
    if (!toggle) {
    }
    setNextionPageRolled(nextionPage + 1);
    break;
  case TAP_BTN:
    updateTap();
    break;
  }
  Serial.printf("Page %d; button: %d: state: %d", pageNumber, button, toggle);
  Serial.println();
}

void updateNextionBuffer() {
  while (Serial2.available()) {
    uint8_t nextByte = Serial2.read();
    nextionBuffer[nextionBufferLength] = nextByte;
    nextionBufferLength++;
    if (nextByte == NT) {
      NTCount++;
    }
    if (NTCount == MAX_NT) {
      nextionBufferLength -= MAX_NT;
      processNextionBuffer();
      nextionBufferLength = 0;
      NTCount = 0;
      for (uint8_t i = 0; i < 10; i++) {
        nextionBuffer[i] = 0;
      }
      return;
    }
  }
}

void setupNextion() {
  Serial2.begin(NEXTION_LOW_BAUD);
  Serial2.printf("baud=%d", NEXTION_HIGH_BAUD);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.end();
  Serial2.begin(NEXTION_HIGH_BAUD);
  selectBank(0);
}

void setNextionPageAbsolute(uint8_t page) {
  Serial2.printf("page %d", page);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.write(NT);
  nextionPage = page;
  updateUI();
  updateNextionBlinks();
  sendTap();
}

void setNextionPageRolled(uint8_t page) {
  setNextionPageAbsolute((3 + page) % 3);
}

void updateNextionBlink(uint8_t index) {
  nextionBlinks[nextionPage] = nextionBlinks[nextionPage] ^ (1 << index);
  updateNextionBlinks();
}

void updateNextionBlinks() {
  Serial2.printf("blink.val=%d", nextionBlinks[nextionPage]);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.write(NT);
}

// MIDI IN
void onControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  Serial.printf("Channel%dControl%dvalue%d", channel, control, value);
  switch ((MIDI_IN_CC)control) {
  case BANK_MOVE_CC:
    if (value) {
      selectBank(currentBank.index + 1);
    } else {
      selectBank(currentBank.index - 1);
    }
    break;
  case BANK_CC: {
    selectBank(value);
    break;
  }
  case PAGE_MOVE_CC:
    if (control) {
      setNextionPageRolled(nextionPage + 1);
    } else {
      setNextionPageRolled(nextionPage - 1);
    }
    break;
  case PAGE_CC:
    setNextionPageRolled(value);
    break;
  case TAP_MOVE_CC:
    setNextionPageAbsolute(value);
    break;
  case TAP_CC:
    updateTap();
    break;
  }
}

void updateTap() {
  tapSent = false;
  double ms = millis();
  if (lastTapMs) {
    tapValue = (tapValue + (ms - lastTapMs)) / 2;
  }
  lastTapMs = ms;
  Serial.printf("Tapping...%d", tapValue);
}

void processTap() {
  double ms = millis();
  double diff = ms - lastTapMs;
  if (diff > tapTimeoutMs) {
    lastTapMs=0;
    return;
  }
  if (!tapSent) {
    sendTap();
    tapSent = true;
  };
}

void sendTap() {
  Serial2.printf("pInf2.txt=\"%d\"", tapValue);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.printf("tap_timer.tim=%d", tapValue);
  Serial2.write(NT);
  Serial2.write(NT);
  Serial2.write(NT);
}