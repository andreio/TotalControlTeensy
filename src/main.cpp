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
enum LoopToggle : byte {
  UNCHANGED,
  SET,
  UNSET,
  TOGGLE,
};

typedef LoopToggle Loops[9];

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

// VARS/CONSTS

const uint8_t FRAM_CS = 10;

uint SWITCH_COUNT = 8;
const uint DEBOUNCE_DURATION = 50;

uint switches[] = {14, 15, 16, 17, 18, 19, 20, 21};
Bounce2::Button bounces[8];

const uint SYSEX_START = 0xf0;
const uint SYSEX_STOP = 0xf7;
const uint SYSEX_DEV = 23;

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
#ifdef usbMIDI
  usbMIDI.begin();
  usbMIDI.setHandleSysEx(onSysex);
#endif
  fram.begin();
}

void loop() {
  for (uint i = 0; i < SWITCH_COUNT; i++) {
    Bounce2::Button *sw = &bounces[i];
    sw->update();
    if (sw->changed()) {
      Serial.println("Clicked");
    }
  }
#ifdef usbMIDI
  usbMIDI.read();
#endif
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
    break;
  }
  case REQUEST_CONTROLLER_PRESET_STATE: {
    uint8_t index = buffer[0];
    sendControllerState(index);
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
  PresetName loopNames[9];
  fram.read(RACK_LOOP_NAMES_ADDRESS, (uint8_t *)loopNames,
            sizeof(PresetName[9]));
  sendResponse((uint8_t *)loopNames, RECEIVE_RACK_LOOP_NAMES,
               sizeof(PresetName[9]));
}

void reset() {
  ControllerPreset controllerPreset;
  RackPreset rackPreset;
  PresetName controllerBank;
  PresetName rackBank;
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
                 (uint8_t *)controllerBank, sizeof(PresetName));
      fram.writeEnable(false);
      fram.writeEnable(true);
      fram.write(RACK_BANK_NAMES_ADDRESS + sizeof(PresetName) * bank,
                 (uint8_t *)rackBank, sizeof(PresetName));
      fram.writeEnable(false);
    }
  }
}

void sendResponse(uint8_t *data, RESPONSE_TYPES responseType, size_t dataSize) {
  uint8_t response[dataSize + 2];
  insertSysexConstants(response, responseType);
  memcpy(response + 2, data, dataSize);
#ifdef usbMIDI
  usbMIDI.sendSysEx(dataSize + 2, response);
#endif
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
  fram.write(banksAddress + sizeof(PresetName) * (buffer->index),
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
  PresetID presetIds[128];
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
  sendResponse((uint8_t *)&controllerState, responseType,
               sizeof(controllerState));
}