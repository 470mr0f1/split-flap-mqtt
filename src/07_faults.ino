#include "declarations.h"

// Evaluation of the status byte reported by the units (see UNIT_STATUS_* in
// declarations.h). A unit reports UNIT_STATUS_FAULT (2) after its homing
// (hall sensor calibration) failed. It then stops moving, drops move requests
// and retries homing on its own after a pause. This file only detects,
// counts and reports that state; it uses the polling results of
// isDisplayMoving() (displayState[]), so it does not depend on how units are
// addressed on the bus.

static_assert(MAX_NUMBER_UNITS <= 16, "fault mask is 16 bit");

static uint16_t unitFaultMask = 0;   // bit i = unit at display position i reports status 2
static bool faultMaskChanged = false;
static uint32_t calibrationFaultEvents = 0;

const unsigned long UNIT_STATUS_POLL_MS = 10000;

// Evaluate displayState[]. Call only right after a COMPLETE poll, i.e. after
// isDisplayMoving() returned false (it stops at the first moving unit, so the
// remaining entries would be stale otherwise).
void evaluateUnitFaults() {
  uint16_t newMask = 0;
  for (int i = 0; i < number_units; i++) {
    if (displayState[i] == UNIT_STATUS_FAULT) {
      newMask |= (uint16_t)(1u << i);
    }
  }

  if (newMask == unitFaultMask) {
    return;
  }

  for (int i = 0; i < number_units; i++) {
    uint16_t bitMask = (uint16_t)(1u << i);
    bool isFaulty = (newMask & bitMask) != 0;
    bool wasFaulty = (unitFaultMask & bitMask) != 0;

    if (isFaulty && !wasFaulty) {
      calibrationFaultEvents++;
      Serial.print("Unit ");
      Serial.print(i);
      Serial.println(" reports failed calibration (status 2)");
    } else if (!isFaulty && wasFaulty) {
      Serial.print("Unit ");
      Serial.print(i);
      Serial.println(" recovered from calibration fault");
    }
  }

  unitFaultMask = newMask;
  faultMaskChanged = true;
}

// Periodic status check while the display is idle (call from loop context).
void pollUnitFaults() {
  static unsigned long lastPoll = 0;
  unsigned long now = millis();

  if (now - lastPoll < UNIT_STATUS_POLL_MS) {
    return;
  }
  lastPoll = now;

  // Some unit is moving: poll incomplete, try again at the next interval.
  if (isDisplayMoving()) {
    return;
  }
  evaluateUnitFaults();
}

uint16_t getUnitFaultMask() {
  return unitFaultMask;
}

// Returns true once after the set of faulty units changed.
bool consumeFaultChange() {
  bool changed = faultMaskChanged;
  faultMaskChanged = false;
  return changed;
}

uint32_t getCalibrationFaultEvents() {
  return calibrationFaultEvents;
}
