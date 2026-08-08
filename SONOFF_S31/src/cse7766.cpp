#include <stdint.h>
#include <Arduino.h>
#include "cse7766.h"

// CSE7766 data.
double power = 0;   // W, from the CF pulse rate (see PowerWindow_ms below)
double voltage = 0; // V
double current = 0; // A
double energy = 0;  // Wh since boot or the last ResetEnergy()

struct ratio_t ratio = {1.04, 1.04, 1.08}; // ok, they were pretty similar on two first plugs, set them as a default

// Serial data input buffer.
unsigned char serialBuffer[24];
// Serial error flags.
int error;

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

// Header byte flags, meaningful when the byte reads 0xF?: the named measuring cycle
// overflowed the chip's 24-bit timer, i.e. that quantity is below what it can resolve.
#define CSE7766_COEF_CORRUPT 0x01
#define CSE7766_CURRENT_OVERFLOW 0x04
#define CSE7766_VOLTAGE_OVERFLOW 0x08
// Byte 20 flags: set on the frame that follows a refresh of the named register. Clear
// means "no new value in this frame", NOT "the quantity is zero".
#define CSE7766_CURRENT_UPDATED 0x20
#define CSE7766_VOLTAGE_UPDATED 0x40

// Instantaneous power is derived from the CF pulse train, not from the chip's power
// register: that register is refreshed only when a CF period completes -- once every
// ~3 s at 2 W -- and its update flag is set in a single frame out of the ~20 the chip
// emits per second, so at low load a reader almost never catches a fresh value.
// The pulse train needs no such luck and costs nothing in calibration: the chip defines
// P = coefP / power_cycle_us and CF emits one pulse per power_cycle, so one pulse is
// exactly coefP * 1e-6 joules at any load. Both edges of the averaging window are pulse
// arrivals, so the pulse count between them is exact and only timestamp jitter (a few ms
// against a >= 1 s window) enters the rate.
static constexpr uint32_t PowerWindow_ms = 1000;  // window floor; it stretches to the next pulse
static constexpr uint32_t IdleZero_ms = 30000;    // one missing pulse this long is < 0.2 W: call it off

static uint32_t WindowStart_ms; // time of the pulse that opened the current window
static uint32_t LastPulse_ms;   // time of the most recent pulse, whatever window it fell in
static unsigned WindowPulses;   // pulses since WindowStart_ms
static unsigned cfPulsesLast;   // chip's free-running 16-bit pulse counter, previous frame
static bool CounterValid;       // cfPulsesLast holds a real reading
static bool WindowAnchored;     // WindowStart_ms is a pulse arrival, so a rate can be formed

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
  const uint8_t status = (serialBuffer[0] & 0xF0) == 0xF0 ? serialBuffer[0] : 0;
  if (status & CSE7766_COEF_CORRUPT) {
    error = SENSOR_ERROR_OTHER;
    return;
  }
  error = SENSOR_ERROR_OK;

  // Retrieve calibration coefficients.
  unsigned long coefV = (serialBuffer[2] << 16 | serialBuffer[3] << 8 | serialBuffer[4]);
  unsigned long coefC = (serialBuffer[8] << 16 | serialBuffer[9] << 8 | serialBuffer[10]);
  unsigned long coefP = (serialBuffer[14] << 16 | serialBuffer[15] << 8 | serialBuffer[16]);
  uint8_t adj = serialBuffer[20];

  // Voltage and current keep their last value through frames that carry no update:
  // an unset flag means the register has not been refreshed yet, so zeroing it here
  // would report 0 for every frame but the lucky one. An overflowed cycle, on the
  // other hand, is a real statement that the quantity is under the chip's floor.
  if (status & CSE7766_VOLTAGE_OVERFLOW) voltage = 0;
  else if (adj & CSE7766_VOLTAGE_UPDATED) {
    unsigned long voltageCycle = serialBuffer[5] << 16 | serialBuffer[6] << 8 | serialBuffer[7];
    if (voltageCycle) voltage = ratio.V * coefV / voltageCycle / CSE7766_V2R;
  }

  if (status & CSE7766_CURRENT_OVERFLOW) current = 0;
  else if (adj & CSE7766_CURRENT_UPDATED) {
    unsigned long currentCycle = serialBuffer[11] << 16 | serialBuffer[12] << 8 | serialBuffer[13];
    if (currentCycle) current = ratio.C * coefC / currentCycle / CSE7766_V1R;
  }

  // Energy and power from the pulse counter. A cycle overflow does not invalidate it:
  // the counter keeps ticking below the register's range, which is the whole point.
  const double Ws_per_pulse = ratio.P * coefP / 1000000.0;
  unsigned cfPulses = serialBuffer[21] << 8 | serialBuffer[22];
  const uint32_t now = millis();

  if (!CounterValid) {
    cfPulsesLast = cfPulses;
    CounterValid = true;
    WindowStart_ms = LastPulse_ms = now; // idle is timed from boot until a pulse arrives
  }
  unsigned difference = (cfPulses - cfPulsesLast) & 0xFFFF; // 16-bit counter, wraps
  cfPulsesLast = cfPulses;
  // Joules per pulse is what the chip gives; watt-hours is what a meter should report.
  energy += difference * Ws_per_pulse / 3600.0;

  if (difference) {
    LastPulse_ms = now;
    if (!WindowAnchored) { // first pulse: start timing from it, no rate yet
      WindowStart_ms = now;
      WindowPulses = 0;
      WindowAnchored = true;
    } else {
      WindowPulses += difference;
      const uint32_t elapsed_ms = now - WindowStart_ms;
      if (elapsed_ms >= PowerWindow_ms) {
        power = WindowPulses * Ws_per_pulse * 1000.0 / elapsed_ms;
        WindowStart_ms = now;
        WindowPulses = 0;
      }
    }
  } else {
    // No pulse yet, so the rate is at most one per the time since the last one -- that
    // caps the power and decays it, instead of the last rate sticking after the load
    // went away. Timed from the last PULSE, not from the window start: a window left
    // holding a pulse that never closed it would otherwise never satisfy the cutoff.
    const uint32_t idle_ms = now - LastPulse_ms;
    if (idle_ms) {
      double bound = Ws_per_pulse * 1000.0 / idle_ms;
      if (bound < power) power = bound;
    }
    // Below the meter's floor for this long: report it as off rather than as an ever
    // shrinking sliver. Clears the current reading too -- with an open relay the chip
    // still offers its ~0.06 A noise floor as a freshly updated value, and holding that
    // is a worse lie than dropping the reactive current of a load under 0.2 W. Drop the
    // window anchor with it, so the load's return is timed from its first new pulse
    // rather than against a window opened before the gap.
    if (idle_ms >= IdleZero_ms) {
      power = current = 0;
      WindowAnchored = false;
      WindowPulses = 0;
    }
  }

  // Energy used to self-clear after MAX_ENREGY_RESET_COUNT readings of zero power,
  // which fired every 12 s once the power register started reading 0 at low load. The
  // accumulator is explicit now; to re-enable an idle auto-reset, key it on
  // (elapsed_ms >= IdleZero_ms && !WindowPulses) above -- never on power == 0.
} // ProcessCse7766Packet

// Zero the accumulator and forget the pulse baseline, so the next frame re-adopts the
// chip's free-running counter instead of booking the pulses that happened meanwhile.
void ResetEnergy() {
  energy = 0;
  CounterValid = false;
  WindowAnchored = false;
} // ResetEnergy

// Read serial cse7766 power monitor data packets. Drains everything the chip has sent:
// it emits a frame every ~50 ms and the UART buffer holds only 256 bytes, so consuming
// one frame per call leaves the buffer permanently overflowing and the reader looking
// at whatever survived, half a second stale. Call it every loop pass.
void ReadCse7766() {
  static unsigned char index = 0;

  while (Serial.available() > 0) {
    uint8_t input = Serial.read();

    // second byte must be 0x5A; if it is not, this byte may itself start a frame.
    if (index == 1 && input != 0x5A) index = 0;
    // first byte must be 0x55 or 0xF? or 0xAA.
    if (index == 0 && input != 0x55 && input != 0xAA && input < 0xF0) continue;

    serialBuffer[index++] = input;

    if (index == sizeof serialBuffer) {
      ProcessCse7766Packet();
      index = 0;
    }
  }
} // ReadCse7766
