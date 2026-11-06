// ============================================================
// tiga_log.h — flash data log for TIGA v8
// ============================================================
// Appends one CSV row every LOG_INTERVAL_S seconds to /tiga_log.csv
// on the board's flash (LittleFS). The file survives reset, power
// off and battery swaps, so you can walk untethered and read the
// whole session back afterwards:
//
//   1. plug USB in, open Serial Monitor at 115200
//   2. type  d  and press Enter  -> the log prints between
//      "=== TIGA LOG BEGIN ===" and "=== TIGA LOG END ==="
//   3. copy everything, paste into the PWA "Session data" box
//
//   c = clear the log      (also cleared by the BTN2 session reset)
//
// Size cap: 400 KB (~16 hours at 10 s per row). When full, logging
// stops rather than overwriting, and the dump says so.
// Needs the default partition scheme (it includes a SPIFFS/LittleFS
// area). No extra library to install.
// ============================================================
#pragma once
#include <FFat.h>
#include <time.h>

#define LOG_FILE      "/tiga_log.csv"
#define LOG_MAX_BYTES 400000UL

bool logOK   = false;
bool logFull = false;

const char* LOG_HEADER =
  "up_s,epoch,hr,spo2,steps,alt_m,floors_up,floors_down,bat,worn,falls,"
  "p_hpa,taps,accel_g,balance,gps_fix,sats,dist_m";

size_t logSize() {
  if (!logOK) return 0;
  fs::File f = FFat.open(LOG_FILE, "r");
  if (!f) return 0;
  size_t n = f.size();
  f.close();
  return n;
}

void logWriteHeaderIfNew() {
  if (FFat.exists(LOG_FILE)) return;
  fs::File f = FFat.open(LOG_FILE, "w");
  if (!f) return;
  f.println(LOG_HEADER);
  f.close();
}

void logBegin() {
  logOK = FFat.begin(true);          // true = format on first use
  if (!logOK) {
    Serial.println("[LOG] FFat mount failed - flash log disabled");
    return;
  }
  logWriteHeaderIfNew();
  logFull = (logSize() >= LOG_MAX_BYTES);
  Serial.printf("[LOG] ready, %u bytes stored\n", (unsigned)logSize());
}

// A comment line (starts with #) so you can see reboots in the data
void logMarker(const char* text) {
  if (!logOK || logFull) return;
  fs::File f = FFat.open(LOG_FILE, "a");
  if (!f) return;
  f.printf("# %s at up_s=%lu\n", text, millis() / 1000UL);
  f.close();
}

void logAppendRow() {
  if (!logOK || logFull) return;
  if (logSize() >= LOG_MAX_BYTES) {
    logFull = true;
    Serial.println("[LOG] full - logging stopped (send d to dump, c to clear)");
    return;
  }
  fs::File f = FFat.open(LOG_FILE, "a");
  if (!f) return;
  time_t now = timeSet ? time(nullptr) : 0;
  f.printf("%lu,%lu,%.0f,%d,%d,%.1f,%d,%d,%.0f,%d,%d,%.1f,%d,%.2f,%d,%d,%d,%.0f\n",
           millis() / 1000UL, (unsigned long)now,
           data.heartRate, data.spO2Valid ? (int)data.spO2 : 0,
           data.steps, data.altitudeM, data.floorsUp, floorsDown,
           data.battery, data.wearing ? 1 : 0, daily.fallCount,
           data.pressureHPa, piezoTapCount, data.accelG, data.balanceScore,
           gpsData.hasFix ? 1 : 0, gpsData.satellites, gpsData.distanceM);
  f.close();
}

void logDumpSerial() {
  if (!logOK) { Serial.println("[LOG] not available"); return; }
  fs::File f = FFat.open(LOG_FILE, "r");
  if (!f) { Serial.println("[LOG] no log file"); return; }
  Serial.println();
  Serial.println("=== TIGA LOG BEGIN ===");
  Serial.printf("# firmware=v8 bytes=%u full=%d\n", (unsigned)f.size(), logFull ? 1 : 0);
  while (f.available()) {
    Serial.write(f.read());
    if ((f.position() & 0x3FF) == 0) delay(1);   // let the USB buffer drain
  }
  f.close();
  Serial.println("=== TIGA LOG END ===");
  Serial.println();
}

void logClear() {
  if (!logOK) return;
  FFat.remove(LOG_FILE);
  logFull = false;
  logWriteHeaderIfNew();
}
