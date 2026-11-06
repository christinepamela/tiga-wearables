// ============================================================
// tiga_persist.h — last-session snapshot in flash (Preferences)
// ============================================================
// Saves a snapshot every 60 s; on boot, prints the previous
// snapshot to Serial. For the full minute-by-minute record use the
// CSV log in tiga_log.h (send  d  in the Serial Monitor).
// ============================================================
#pragma once
#include <Preferences.h>

Preferences prefs;
#define PREFS_NS  "tiga"

void persistSave() {
  prefs.begin(PREFS_NS, false);
  unsigned long durSec = (millis() - sessionStart) / 1000;

  prefs.putUInt  ("dur",       durSec);
  prefs.putInt   ("steps",     data.steps);
  prefs.putInt   ("actMin",    daily.activityMins);
  prefs.putFloat ("hrAvg",     daily.avgHR);
  prefs.putFloat ("hrPeak",    sessionPeakHR);
  prefs.putFloat ("hrLow",     sessionLowHR > 900 ? 0 : sessionLowHR);
  prefs.putInt   ("hrN",       daily.hrSamples);
  prefs.putUChar ("spo2",      data.spO2);
  prefs.putBool  ("spo2ok",    data.spO2Valid);
  prefs.putFloat ("altM",      data.altitudeM);
  prefs.putInt   ("floors",    data.floorsUp);
  prefs.putInt   ("falls",     daily.fallCount);
  prefs.putFloat ("battery",   data.battery);
  prefs.putFloat ("pressure",  data.pressureHPa);
  prefs.putBool  ("gpsFix",    gpsData.hasFix);
  prefs.putFloat ("gpsDist",   gpsData.distanceM);
  prefs.putInt   ("day",       displayDay);
  prefs.putInt   ("month",     displayMonth);
  prefs.putInt   ("year",      displayYear);
  prefs.putInt   ("hour",      displayHour);
  prefs.putInt   ("minute",    displayMin);

  uint16_t snaps = prefs.getUShort("snaps", 0) + 1;
  prefs.putUShort("snaps", snaps);
  prefs.putBool("valid", true);
  prefs.end();

  Serial.printf("[PERSIST] snapshot #%d saved (%lus, steps=%d)\n",
                snaps, durSec, data.steps);
}

void persistClear() {
  prefs.begin(PREFS_NS, false);
  prefs.putBool("valid", false);
  prefs.putUShort("snaps", 0);
  prefs.end();
  Serial.println("[PERSIST] snapshot cleared");
}

void persistCheckAndDump() {
  prefs.begin(PREFS_NS, true);
  bool valid = prefs.getBool("valid", false);
  if (!valid) {
    prefs.end();
    Serial.println("[PERSIST] no saved session - clean start");
    return;
  }

  unsigned long durSec = prefs.getUInt  ("dur",      0);
  int   steps          = prefs.getInt   ("steps",    0);
  int   actMin         = prefs.getInt   ("actMin",   0);
  float hrAvg          = prefs.getFloat ("hrAvg",    0);
  float hrPeak         = prefs.getFloat ("hrPeak",   0);
  float hrLow          = prefs.getFloat ("hrLow",    0);
  int   hrN            = prefs.getInt   ("hrN",      0);
  uint8_t spo2         = prefs.getUChar ("spo2",     0);
  bool  spo2ok         = prefs.getBool  ("spo2ok",   false);
  float altM           = prefs.getFloat ("altM",     0);
  int   floors         = prefs.getInt   ("floors",   0);
  int   falls          = prefs.getInt   ("falls",    0);
  float battery        = prefs.getFloat ("battery",  0);
  float pressure       = prefs.getFloat ("pressure", 0);
  bool  gpsFix         = prefs.getBool  ("gpsFix",   false);
  float gpsDist        = prefs.getFloat ("gpsDist",  0);
  int   day            = prefs.getInt   ("day",      0);
  int   month          = prefs.getInt   ("month",    0);
  int   year           = prefs.getInt   ("year",     0);
  int   hour           = prefs.getInt   ("hour",     0);
  int   minute         = prefs.getInt   ("minute",   0);
  uint16_t snaps       = prefs.getUShort("snaps",    0);
  prefs.end();

  int durMin = durSec / 60;
  int durH   = durMin / 60;
  durMin     = durMin % 60;

  Serial.println();
  Serial.println("=================================================");
  Serial.println("  TIGA - SAVED SESSION RECOVERED FROM FLASH");
  Serial.println("=================================================");
  Serial.printf ("  Last saved:   %02d/%02d/%04d  %02d:%02d\n", day, month, year, hour, minute);
  Serial.printf ("  Duration:     %dh %dm  (%d snapshots)\n", durH, durMin, snaps);
  Serial.println("-------------------------------------------------");
  if (hrN > 0)
    Serial.printf("  HR avg %.0f  peak %.0f  low %.0f  (%d samples)\n", hrAvg, hrPeak, hrLow, hrN);
  else
    Serial.println("  HR: no readings");
  if (spo2ok && spo2 > 0) Serial.printf("  SpO2: %d%%\n", spo2);
  else                    Serial.println("  SpO2: no valid reading");
  Serial.printf ("  Steps %d   Active %d min\n", steps, actMin);
  if (gpsFix && gpsDist > 0) Serial.printf("  Distance %.0f m\n", gpsDist);
  Serial.printf ("  Altitude %.1f m   Floors up %d   Pressure %.1f hPa\n", altM, floors, pressure);
  Serial.printf ("  Falls %d   Battery %.0f%% at last save\n", falls, battery);
  Serial.println("  (full record: send  d  to dump the CSV log)");
  Serial.println("=================================================");
  Serial.println();
  delay(1000);
}
