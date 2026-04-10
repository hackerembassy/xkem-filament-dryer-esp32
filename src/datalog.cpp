#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "datalog.h"

#define LOG_PATH        "/log.csv"
#define BUFFER_SIZE     16
#define FLUSH_INTERVAL  30000   // 30 seconds
#define DISK_RESERVE    4096    // stop logging when free space < 4KB
#define LOG_MAX_SIZE    524288  // 512KB — ~8h at 2s interval, prevents unbounded growth
#define DOWNLOAD_CHUNK  4096    // chunk size for streaming file downloads

static const char CSV_HEADER[] = "timestamp_ms,millis_boot,chamber_temp,humidity,"
    "heatsink_temp,chamber_valid,heatsink_valid,relay_on,lid_open,"
    "setpoint,overtemp,thermal_fault,mode\n";

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
    bool overtemp;
    bool thermalFault;
    char mode[10];
};

static LogRecord buffer[BUFFER_SIZE];
static int bufferCount = 0;
static unsigned long lastFlush = 0;
static bool loggingActive = true;
static bool fsReady = false;
static int totalRecordCount = 0;  // persistent in-memory record count

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

        snprintf(line, sizeof(line), "%llu,%lu,%s,%s,%s,%d,%d,%d,%d,%s,%d,%d,%s\n",
                 r.timestampMs, r.millisBoot,
                 ct, hu, ht,
                 r.chamberValid ? 1 : 0,
                 r.heatsinkValid ? 1 : 0,
                 r.relayOn ? 1 : 0,
                 r.lidOpen ? 1 : 0,
                 sp,
                 r.overtemp ? 1 : 0,
                 r.thermalFault ? 1 : 0,
                 r.mode);
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

    // Initialize record count from existing log file (one-time at boot)
    File f = LittleFS.open(LOG_PATH, "r");
    if (f) {
        int lines = 0;
        int bytesRead = 0;
        while (f.available()) {
            if (f.read() == '\n') lines++;
            // Reset WDT periodically during large file scan
            if (++bytesRead % 1024 == 0) esp_task_wdt_reset();
        }
        // Subtract 1 for the CSV header line
        totalRecordCount = (lines > 0) ? lines - 1 : 0;
        size_t existingSize = f.size();
        f.close();
        Serial.printf("Datalog: %d existing records (%u bytes)\n",
                       totalRecordCount, (unsigned)existingSize);
    }

    // NTP setup (non-blocking, syncs in background after WiFi connects)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void datalog_record(float chamberTemp, float humidity, float heatsinkTemp,
                    bool chamberValid, bool heatsinkValid,
                    bool relayOn, bool lidOpen,
                    float setpoint, bool overtemp,
                    bool thermalFault, const char* mode) {
    if (!loggingActive || !fsReady) return;

    if (!diskHasSpace()) {
        loggingActive = false;
        Serial.println("Datalog: disk full, logging stopped");
        return;
    }

    // Check log file size cap to prevent unbounded growth
    File lf = LittleFS.open(LOG_PATH, "r");
    if (lf) {
        size_t sz = lf.size();
        lf.close();
        if (sz >= LOG_MAX_SIZE) {
            loggingActive = false;
            Serial.println("Datalog: max file size reached, logging stopped");
            return;
        }
    }

    // Flush if buffer is full
    if (bufferCount >= BUFFER_SIZE) {
        writeBufferToFile();
    }

    LogRecord &r = buffer[bufferCount++];
    totalRecordCount++;
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
    r.overtemp      = overtemp;
    r.thermalFault  = thermalFault;
    strncpy(r.mode, mode, sizeof(r.mode) - 1);
    r.mode[sizeof(r.mode) - 1] = '\0';
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

    // Chunked download with WDT resets to prevent watchdog timeout on large files
    size_t fileSize = f.size();
    httpServer->setContentLength(fileSize);
    httpServer->send(200, "text/csv", "");

    uint8_t buf[DOWNLOAD_CHUNK];
    while (f.available()) {
        size_t bytesRead = f.read(buf, sizeof(buf));
        if (bytesRead > 0) {
            httpServer->client().write(buf, bytesRead);
        }
        esp_task_wdt_reset();
        yield();
    }
    f.close();
}

static void handleLogClear(void) {
    if (!fsReady) {
        httpServer->send(503, "application/json", "{\"error\":\"filesystem not ready\"}");
        return;
    }

    bufferCount = 0;
    totalRecordCount = 0;
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
    File f = LittleFS.open(LOG_PATH, "r");
    if (f) {
        fileSize = f.size();
        f.close();
    }

    size_t diskFree = LittleFS.totalBytes() - LittleFS.usedBytes();

    char json[192];
    snprintf(json, sizeof(json),
        "{\"file_size\":%u,\"record_count\":%d,\"disk_free\":%u,\"logging_active\":%s}",
        (unsigned)fileSize, totalRecordCount, (unsigned)diskFree,
        loggingActive ? "true" : "false");
    httpServer->send(200, "application/json", json);
}

void datalog_registerEndpoints(WebServer &server) {
    httpServer = &server;
    server.on("/api/log", HTTP_GET, handleLogDownload);
    server.on("/api/log", HTTP_DELETE, handleLogClear);
    server.on("/api/log/stats", HTTP_GET, handleLogStats);
}
