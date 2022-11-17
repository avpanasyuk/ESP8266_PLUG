#include <stdint.h>
#include <Arduino.h>
#include "cse7766.h"

// CSE7766 data.
double power = 0;
double voltage = 0;
double current = 0;
double energy = 0;

struct ratio_t ratio = {1.08, 1.08, 1.16}; // ok, they were pretty similar on two first plugs, set them as a default

// Serial data input buffer.
unsigned char serialBuffer[24];
// Serial error flags.
int error;
// Energy reset counter.
int energyResetCounter;
#define MAX_ENREGY_RESET_COUNT 12

// CSE7766 error codes.
#define SENSOR_ERROR_OK 0            // No error.
#define SENSOR_ERROR_OUT_OF_RANGE 1  // Result out of sensor range.
#define SENSOR_ERROR_WARM_UP 2       // Sensor is warming-up.
#define SENSOR_ERROR_TIMEOUT 3       // Response from sensor timed out.
#define SENSOR_ERROR_UNKNOWN_ID 4    // Sensor did not report a known ID.
#define SENSOR_ERROR_CRC 5           // Sensor data corrupted.
#define SENSOR_ERROR_I2C 6           // Wrong or locked I2C address.
#define SENSOR_ERROR_GPIO_USED 7     // The GPIO is already in use.
#define SENSOR_ERROR_CALIBRATION 8   // Calibration error or not calibrated.
#define SENSOR_ERROR_OTHER 99        // Any other error.
#define CSE7766_V1R 1.0              // 1mR current resistor.
#define CSE7766_V2R 1.0              // 1M voltage resistor.

// CSE7766 checksum.
static bool CheckSum() {
  unsigned char checksum = 0;

  for (unsigned char i = 2; i < 23; i++)
    checksum += serialBuffer[i];

  return checksum == serialBuffer[23];
}

// Process a cse7766 data packet.
static void ProcessCse7766Packet() {
  // Confirm packet checksum.
  if (!CheckSum()) {
    error = SENSOR_ERROR_CRC;
    return;
  }

  // Check for calibration error.
  if (serialBuffer[0] == 0xAA) {
    error = SENSOR_ERROR_CALIBRATION;
    return;
  }
  if ((serialBuffer[0] & 0xFC) == 0xFC) {
    error = SENSOR_ERROR_OTHER;
    return;
  }

  // Retrieve calibration coefficients.
  unsigned long coefV = (serialBuffer[2] << 16 | serialBuffer[3] << 8 | serialBuffer[4]);
  unsigned long coefC = (serialBuffer[8] << 16 | serialBuffer[9] << 8 | serialBuffer[10]);
  unsigned long coefP = (serialBuffer[14] << 16 | serialBuffer[15] << 8 | serialBuffer[16]);
  uint8_t adj = serialBuffer[20];

  // Calculate voltage.
  voltage = 0;
  if ((adj & 0x40) == 0x40) {
    unsigned long voltageCycle = serialBuffer[5] << 16 | serialBuffer[6] << 8 | serialBuffer[7];
    voltage = ratio.V * coefV / voltageCycle / CSE7766_V2R;
  }

  // Calculate power.
  power = 0;
  if ((adj & 0x10) == 0x10) {
    if ((serialBuffer[0] & 0xF2) != 0xF2) {
      unsigned long powerCycle = serialBuffer[17] << 16 | serialBuffer[18] << 8 | serialBuffer[19];
      power = ratio.P * coefP / powerCycle / CSE7766_V1R / CSE7766_V2R;
    }
  }

  // Calculate current.
  current = 0;
  if ((adj & 0x20) == 0x20) {
    if (power > 0) {
      unsigned long currentCycle = serialBuffer[11] << 16 | serialBuffer[12] << 8 | serialBuffer[13];
      current = ratio.C * coefC / currentCycle / CSE7766_V1R;
    }
  }

  // Calculate energy.
  unsigned int difference;
  static unsigned int cfPulsesLast = 0;
  unsigned int cfPulses = serialBuffer[21] << 8 | serialBuffer[22];

  if (0 == cfPulsesLast)
    cfPulsesLast = cfPulses;

  if (cfPulses < cfPulsesLast)
    difference = cfPulses + (0xFFFF - cfPulsesLast) + 1;
  else
    difference = cfPulses - cfPulsesLast;

  energy += difference * (float)coefP / 1000000.0;
  cfPulsesLast = cfPulses;

  // Energy reset.
  if (power == 0)
    energyResetCounter++;
  else
    energyResetCounter = 0;
  if (energyResetCounter >= MAX_ENREGY_RESET_COUNT) {
    energy = 0.0;
    energyResetCounter = 0;
  }
} // ProcessCse7766Packet

// Read serial cse7766 power monitor data packet.
void ReadCse7766() {
  // Assume a non-specific error.
  error = SENSOR_ERROR_OTHER;
  static unsigned char index = 0;

  while (Serial.available() > 0) {
    uint8_t input = Serial.read();

    // first byte must be 0x55 or 0xF?.
    if (index == 0) {
      if ((input != 0x55) && (input < 0xF0))
        continue;
    }
    // second byte must be 0x5A.
    else if (index == 1) {
      if (input != 0x5A) {
        index = 0;
        continue;
      }
    }

    serialBuffer[index++] = input;

    if (index > 23) {
      Serial.flush();
      break;
    }
  }

  // Process packet.
  if (index == 24) {
    error = SENSOR_ERROR_OK;
    ProcessCse7766Packet();
    index = 0;
  }
} // ReadCse7766