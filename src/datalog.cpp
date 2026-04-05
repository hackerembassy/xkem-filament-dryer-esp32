#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <time.h>
#include "datalog.h"

#define LOG_PATH        "/log.csv"
#define BUFFER_SIZE     16
#define FLUSH_INTERVAL  30000   // 30 seconds
#define DISK_RESERVE    4096    // stop logging when free space < 4KB

static const char CSV_HEADER[] = "timestamp_ms,millis_boot,chamber_temp,humidity,"
    "heatsink_temp,chamber_valid,heatsink_valid,relay_on,lid_open,"
    "setpoint,enabled,overtemp\n";

struct LogRecord {
    unsigned long long timestampMs;
    unsigned long millisBoot;
    float chamberTemp;
    float humidity;
    float heatsinkTemp;
    bool chamberValid;
    bool heatsinkValid;
    bool relayOn;
    bool lidOpen;
    float setpoint;
    bool enabled;
    bool overtemp;
};

static LogRecord buffer[BUFFER_SIZE];
static int bufferCount = 0;
static unsigned long lastFlush = 0;
static bool loggingActive = true;
static bool fsReady = false;

static unsigned long long getEpochMs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // Before NTP sync, tv_sec is near 0 (epoch 1970)
    if (tv.tv_sec < 1000000000) return 0;
    return (unsigned long long)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

static bool diskHasSpace(void) {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    return (total - used) > DISK_RESERVE;
}

static void writeBufferToFile(void) {
    if (bufferCount == 0) return;
    if (!fsReady) return;

    File f = LittleFS.open(LOG_PATH, "a");
    if (!f) return;

    // Write header if file is empty (new or cleared)
    if (f.size() == 0) {
        f.print(CSV_HEADER);
    }

    char line[160];
    for (int i = 0; i < bufferCount; i++) {
        LogRecord &r = buffer[i];
        char ct[8], hu[8], ht[8], sp[8];
        snprintf(ct, sizeof(ct), "%.1f", (double)r.chamberTemp);
        snprintf(hu, sizeof(hu), "%.1f", (double)r.humidity);
        snprintf(ht, sizeof(ht), "%.1f", (double)r.heatsinkTemp);
        snprintf(sp, sizeof(sp), "%.1f", (double)r.setpoint);

        snprintf(line, sizeof(line), "%llu,%lu,%s,%s,%s,%d,%d,%d,%d,%s,%d,%d\n",
                 r.timestampMs, r.millisBoot,
                 ct, hu, ht,
                 r.chamberValid ? 1 : 0,
                 r.heatsinkValid ? 1 : 0,
                 r.relayOn ? 1 : 0,
                 r.lidOpen ? 1 : 0,
                 sp,
                 r.enabled ? 1 : 0,
                 r.overtemp ? 1 : 0);
        f.print(line);
    }
    f.close();
    bufferCount = 0;
}

void datalog_init(void) {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS: mount failed");
        return;
    }
    fsReady = true;
    Serial.println("LittleFS: mounted");

    // NTP setup (non-blocking, syncs in background after WiFi connects)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void datalog_record(float chamberTemp, float humidity, float heatsinkTemp,
                    bool chamberValid, bool heatsinkValid,
                    bool relayOn, bool lidOpen,
                    float setpoint, bool enabled, bool overtemp) {
    if (!loggingActive || !fsReady) return;

    if (!diskHasSpace()) {
        loggingActive = false;
        Serial.println("Datalog: disk full, logging stopped");
        return;
    }

    // Flush if buffer is full
    if (bufferCount >= BUFFER_SIZE) {
        writeBufferToFile();
    }

    LogRecord &r = buffer[bufferCount++];
    r.timestampMs   = getEpochMs();
    r.millisBoot    = millis();
    r.chamberTemp   = chamberTemp;
    r.humidity       = humidity;
    r.heatsinkTemp  = heatsinkTemp;
    r.chamberValid  = chamberValid;
    r.heatsinkValid = heatsinkValid;
    r.relayOn       = relayOn;
    r.lidOpen       = lidOpen;
    r.setpoint      = setpoint;
    r.enabled       = enabled;
    r.overtemp      = overtemp;
}

void datalog_flush(void) {
    unsigned long now = millis();
    if (now - lastFlush >= FLUSH_INTERVAL) {
        lastFlush = now;
        writeBufferToFile();
    }
}

// --- HTTP handlers ---

static WebServer *httpServer = nullptr;

static void handleLogDownload(void) {
    if (!fsReady) {
        httpServer->send(503, "text/plain", "filesystem not ready");
        return;
    }

    // Flush pending records before download
    writeBufferToFile();

    File f = LittleFS.open(LOG_PATH, "r");
    if (!f) {
        httpServer->send(200, "text/csv", CSV_HEADER);
        return;
    }

    httpServer->streamFile(f, "text/csv");
    f.close();
}

static void handleLogClear(void) {
    if (!fsReady) {
        httpServer->send(503, "application/json", "{\"error\":\"filesystem not ready\"}");
        return;
    }

    bufferCount = 0;
    LittleFS.remove(LOG_PATH);
    loggingActive = true;
    httpServer->send(200, "application/json", "{\"cleared\":true}");
    Serial.println("Datalog: log cleared");
}

static void handleLogStats(void) {
    if (!fsReady) {
        httpServer->send(503, "application/json", "{\"error\":\"filesystem not ready\"}");
        return;
    }

    // Flush so stats reflect latest data
    writeBufferToFile();

    size_t fileSize = 0;
    int recordCount = 0;

    File f = LittleFS.open(LOG_PATH, "r");
    if (f) {
        fileSize = f.size();
        // Count lines (subtract 1 for header)
        while (f.available()) {
            if (f.read() == '\n') recordCount++;
        }
        if (recordCount > 0) recordCount--; // exclude header line
        f.close();
    }

    size_t diskFree = LittleFS.totalBytes() - LittleFS.usedBytes();

    char json[192];
    snprintf(json, sizeof(json),
        "{\"file_size\":%u,\"record_count\":%d,\"disk_free\":%u,\"logging_active\":%s}",
        (unsigned)fileSize, recordCount, (unsigned)diskFree,
        loggingActive ? "true" : "false");
    httpServer->send(200, "application/json", json);
}

void datalog_registerEndpoints(WebServer &server) {
    httpServer = &server;
    server.on("/api/log", HTTP_GET, handleLogDownload);
    server.on("/api/log", HTTP_DELETE, handleLogClear);
    server.on("/api/log/stats", HTTP_GET, handleLogStats);
}
