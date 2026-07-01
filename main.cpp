#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5
#define RST_PIN 22
MFRC522 rfid(SS_PIN, RST_PIN);

const int IR_PIN = 27;
const int HALL_PIN = 34;

const char* ssid = "CRITICAL_SHOW_NET";
const char* password = "password123";

IPAddress local_ip(192, 168, 50, 1);
IPAddress gateway(192, 168, 50, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

struct Student {
  byte uid[4];
  const char* name;
  int entryCount;
  int exitCount;
  bool isInside;
  unsigned long stateChangeTimestamp;
  unsigned long lastEntryTime;
  unsigned long lastExitTime;
  String colorAlert;
  int bunkCount;
  bool bunkLoggedForCurrentCycle;
};

Student students[] = {
  {{0x9E, 0x60, 0x02, 0x02}, "Yuvraj Singh", 0, 0, false, 0, 0, 0, "GREEN", 0, false},
  {{0x74, 0x8A, 0xBB, 0x02}, "Yash Pratap",  0, 0, false, 0, 0, 0, "GREEN", 0, false}
};

unsigned long windowStartTime = 0;
bool windowActive = false;
const unsigned long FUSION_WINDOW_MS = 1000;

int RFID_Scan_Count = 0;
int IR_Trip_Count = 0;
Student* activeStudent = nullptr;

bool lastIRState = false;
bool lastHallState = false;
unsigned long lastNetworkCheck = 0;

bool physicalBreachAlert = false;
bool tailgatingAlert = false;
unsigned long alertTriggerTime = 0; // Tracks exact timestamp of security breaches

unsigned long initialEpochSeconds = 0;
unsigned long epochSyncSystemTicks = 0;

String formatMilitaryTime(unsigned long epochTime) {
  if (epochTime == 0) return "--";
  unsigned long hours = (epochTime / 3600) % 24;
  unsigned long minutes = (epochTime / 60) % 60;
  char buffer[10];
  sprintf(buffer, "%02d%02dhrs", hours, minutes);
  return String(buffer);
}

unsigned long getLiveEpochTime() {
  if (initialEpochSeconds == 0) return 0;
  return initialEpochSeconds + ((millis() - epochSyncSystemTicks) / 1000);
}

String getDashboardHTML() {
  unsigned long currentMillis = millis();
  unsigned long liveNowEpoch = getLiveEpochTime();

  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>SHIELD SYSTEM v14.0</title>";
  html += "<meta http-equiv='refresh' content='1'>";
  html += "<style>";
  html += ":root {";
  html += "  --bg-color: #0f172a;";
  html += "  --card-bg: #1e293b;";
  html += "  --text-main: #f8fafc;";
  html += "  --text-muted: #94a3b8;";
  html += "  --accent: #38bdf8;";
  html += "  --success: #10b981;";
  html += "  --warning: #f59e0b;";
  html += "  --danger: #ef4444;";
  html += "  --border: #334155;";
  html += "}";

  if (physicalBreachAlert || tailgatingAlert) {
    html += "body { background-color: #450a0a; color: var(--text-main); font-family: system-ui, -apple-system, sans-serif; margin: 0; padding: 20px; border: 8px solid var(--danger); box-sizing: border-box; min-height: 100vh; }";
  } else {
    html += "body { background-color: var(--bg-color); color: var(--text-main); font-family: system-ui, -apple-system, sans-serif; margin: 0; padding: 20px; box-sizing: border-box; min-height: 100vh; }";
  }

  html += ".container { max-width: 1200px; margin: 0 auto; }";
  html += ".header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 30px; flex-wrap: wrap; gap: 20px; }";
  html += ".header-left { display: flex; flex-direction: column; }";
  html += "h1 { font-size: 36px; font-weight: 800; margin: 0; color: var(--accent); letter-spacing: 2px; text-shadow: 0 0 15px rgba(56,189,248,0.4); }";
  html += ".acronym { font-size: 14px; color: var(--text-muted); font-weight: 600; margin-top: 5px; text-transform: uppercase; letter-spacing: 1px; }";
  html += ".header-right { text-align: right; }";
  html += ".clock-display { font-family: monospace; font-size: 24px; color: var(--warning); font-weight: 700; margin-bottom: 10px; text-shadow: 0 0 10px rgba(245,158,11,0.3); background: rgba(0,0,0,0.3); padding: 10px 20px; border-radius: 8px; border: 1px solid var(--border); }";
  html += ".export-btn { display: inline-flex; align-items: center; justify-content: center; background: var(--accent); color: #0f172a; border: none; padding: 10px 24px; font-weight: 700; border-radius: 6px; cursor: pointer; font-size: 14px; text-decoration: none; transition: all 0.2s; text-transform: uppercase; letter-spacing: 1px; }";
  html += ".export-btn:hover { background: #7dd3fc; transform: translateY(-1px); box-shadow: 0 4px 12px rgba(56,189,248,0.3); }";
  html += ".status-banner { padding: 16px 24px; border-radius: 12px; font-weight: 700; font-size: 18px; margin-bottom: 30px; display: flex; align-items: center; justify-content: center; letter-spacing: 1px; text-align: center; }";
  html += ".status-secure { background: rgba(16, 185, 129, 0.1); border: 2px solid var(--success); color: var(--success); box-shadow: 0 0 20px rgba(16,185,129,0.1); }";
  html += ".status-alert { background: rgba(239, 68, 68, 0.1); border: 2px solid var(--danger); color: var(--danger); box-shadow: 0 0 20px rgba(239,68,68,0.2); animation: pulse 2s infinite; }";
  html += "@keyframes pulse { 0% { box-shadow: 0 0 0 0 rgba(239,68,68,0.4); } 70% { box-shadow: 0 0 0 15px rgba(239,68,68,0); } 100% { box-shadow: 0 0 0 0 rgba(239,68,68,0); } }";
  html += ".table-container { background: var(--card-bg); border-radius: 12px; border: 1px solid var(--border); overflow-x: auto; box-shadow: 0 10px 30px rgba(0,0,0,0.2); }";
  html += "table { width: 100%; border-collapse: collapse; text-align: left; }";
  html += "th, td { padding: 16px 20px; font-size: 14px; border-bottom: 1px solid var(--border); white-space: nowrap; }";
  html += "th { background-color: rgba(0,0,0,0.2); color: var(--text-muted); font-weight: 600; text-transform: uppercase; letter-spacing: 1px; font-size: 12px; }";
  html += "tbody tr:hover { background-color: rgba(255,255,255,0.02); }";
  html += "tbody tr:last-child td { border-bottom: none; }";
  html += ".student-name { font-weight: 600; font-size: 15px; color: var(--text-main); }";
  html += ".stat-pill { background: rgba(0,0,0,0.3); padding: 4px 10px; border-radius: 20px; font-family: monospace; font-size: 13px; color: var(--text-muted); }";
  html += ".loc-badge { display: inline-block; padding: 6px 12px; border-radius: 6px; font-weight: 700; font-size: 12px; letter-spacing: 0.5px; }";
  html += ".loc-inside { background: rgba(16,185,129,0.15); color: var(--success); }";
  html += ".loc-outside { background: rgba(245,158,11,0.15); color: var(--warning); }";
  html += ".timestamp { font-family: monospace; color: var(--text-muted); font-size: 13px; }";
  html += ".duration { font-family: monospace; color: var(--accent); font-size: 13px; }";
  html += ".bunk-count { font-weight: 800; font-size: 16px; color: var(--danger); text-align: center; font-family: monospace; }";
  html += ".alert-badge { display: inline-block; padding: 6px 12px; border-radius: 6px; font-weight: 700; font-size: 12px; letter-spacing: 0.5px; text-align: center; min-width: 100px; }";
  html += ".badge-G { background: rgba(16,185,129,0.15); color: var(--success); border: 1px solid rgba(16,185,129,0.3); }";
  html += ".badge-Y { background: rgba(245,158,11,0.15); color: var(--warning); border: 1px solid rgba(245,158,11,0.3); }";
  html += ".badge-R { background: rgba(239,68,68,0.15); color: var(--danger); border: 1px solid rgba(239,68,68,0.3); }";
  html += "</style></head><body>";

  html += "<div class='container'>";

  html += "<div class='header'>";
  html += "  <div class='header-left'>";
  html += "    <h1>SHIELD</h1>";
  html += "    <div class='acronym'>Secure Hardware Interface for Entry Logging & Discipline</div>";
  html += "  </div>";
  html += "  <div class='header-right'>";
  html += "    <div class='clock-display'>MASTER TIME: " + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "SYNCING...") + "</div>";
  html += "    <a href='/export' class='export-btn'>Export PDF Report</a>";
  html += "  </div>";
  html += "</div>";

  bool isDoorOpen = digitalRead(HALL_PIN);
  bool isAlert = isDoorOpen || physicalBreachAlert || tailgatingAlert;

  String bannerClass = isAlert ? "status-alert" : "status-secure";
  String bannerText = "PORTAL SECURE";

  if (tailgatingAlert) {
    bannerText = "CRITICAL ALERT: TAILGATING DETECTED - MULTIPLE PERSON ENTRIES";
  } else if (physicalBreachAlert) {
    bannerText = "CRITICAL ALERT: UNPAIRED SECURITY BREACH - NO CARD";
  } else if (isDoorOpen) {
    bannerText = "WARNING: PORTAL DOOR OPEN";
  }

  html += "<div class='status-banner " + bannerClass + "'>";
  html += bannerText;
  html += "</div>";

  html += "<div class='table-container'><table><thead><tr>";
  html += "<th>Student Account</th><th>Entries</th><th>Exits</th><th>Zone Loc</th><th>Timestamps (In/Out)</th><th>Running Duration</th><th>Total Bunks</th><th>Alert Status</th>";
  html += "</tr></thead><tbody>";

  for (auto &s : students) {
    html += "<tr>";
    html += "<td class='student-name'>" + String(s.name) + "</td>";
    html += "<td><span class='stat-pill'>" + String(s.entryCount) + "</span></td>";
    html += "<td><span class='stat-pill'>" + String(s.exitCount) + "</span></td>";
    html += "<td><span class='loc-badge " + String(s.isInside ? "loc-inside" : "loc-outside") + "'>" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "</span></td>";
    html += "<td class='timestamp'>In: " + formatMilitaryTime(s.lastEntryTime) + " <br> Out: " + formatMilitaryTime(s.lastExitTime) + "</td>";

    if (s.stateChangeTimestamp == 0) {
      html += "<td class='duration'>--</td>";
    } else {
      unsigned long totalDeltaSecs = (currentMillis - s.stateChangeTimestamp) / 1000;
      unsigned long runMinutes = totalDeltaSecs / 60;
      unsigned long runSeconds = totalDeltaSecs % 60;
      html += "<td class='duration'>" + String(runMinutes) + "m " + String(runSeconds) + "s Ago</td>";
    }

    html += "<td class='bunk-count'>" + String(s.bunkCount) + "</td>";

    String alertClass = "badge-G";
    String alertText = "SECURE";
    if (s.colorAlert == "YELLOW") { alertClass = "badge-Y"; alertText = "WARNING (10s+)"; }
    if (s.colorAlert == "RED") { alertClass = "badge-R"; alertText = "CRITICAL BUNK"; }

    html += "<td><span class='alert-badge " + alertClass + "'>" + alertText + "</span></td>";
    html += "</tr>";
  }

  html += "</tbody></table></div>";
  html += "</div>";

  html += "<script>";
  html += "window.onload = function() {";
  html += "  if (" + String(initialEpochSeconds) + " === 0) {";
  html += "    let d = new Date();";
  html += "    let localSecondsSinceMidnight = (d.getHours() * 3600) + (d.getMinutes() * 60) + d.getSeconds();";
  html += "    fetch('/sync?t=' + localSecondsSinceMidnight);";
  html += "  }";
  html += "};";
  html += "</script></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getDashboardHTML());
}

void handleSync() {
  if (server.hasArg("t")) {
    initialEpochSeconds = server.arg("t").toInt();
    epochSyncSystemTicks = millis();
  }
  server.send(200, "text/plain", "OK");
}

void handleExport() {
  unsigned long liveNowEpoch = getLiveEpochTime();
  String pdfHtml = "<!DOCTYPE html><html lang='en'><head>";
  pdfHtml += "<meta charset='UTF-8'>";
  pdfHtml += "<title>SHIELD Security Log Report</title>";
  pdfHtml += "<style>";
  pdfHtml += ":root {";
  pdfHtml += "  --text-main: #1f2937;";
  pdfHtml += "  --text-muted: #6b7280;";
  pdfHtml += "  --border: #e5e7eb;";
  pdfHtml += "  --primary: #2563eb;";
  pdfHtml += "  --bg-alt: #f9fafb;";
  pdfHtml += "}";
  pdfHtml += "@page { margin: 20mm; size: A4 portrait; }";
  pdfHtml += "body { font-family: system-ui, -apple-system, sans-serif; color: var(--text-main); margin: 0; padding: 0; background: #fff; line-height: 1.5; }";
  pdfHtml += ".report-container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  pdfHtml += ".header { border-bottom: 2px solid var(--primary); padding-bottom: 20px; margin-bottom: 30px; display: flex; justify-content: space-between; align-items: flex-end; }";
  pdfHtml += ".header-left h1 { margin: 0 0 5px 0; color: var(--text-main); font-size: 28px; font-weight: 800; letter-spacing: -0.5px; }";
  pdfHtml += ".header-left .meta { font-size: 13px; color: var(--text-muted); font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }";
  pdfHtml += ".header-right { text-align: right; }";
  pdfHtml += ".report-time { font-family: monospace; font-size: 14px; font-weight: 600; color: var(--primary); background: #eff6ff; padding: 6px 12px; border-radius: 4px; border: 1px solid #bfdbfe; }";
  pdfHtml += "table { width: 100%; border-collapse: separate; border-spacing: 0; margin-top: 10px; border: 1px solid var(--border); border-radius: 8px; overflow: hidden; }";
  pdfHtml += "th, td { padding: 12px 16px; text-align: left; font-size: 13px; border-bottom: 1px solid var(--border); }";
  pdfHtml += "th { background-color: var(--bg-alt); font-weight: 600; color: var(--text-muted); text-transform: uppercase; font-size: 11px; letter-spacing: 0.5px; }";
  pdfHtml += "tbody tr:last-child td { border-bottom: none; }";
  pdfHtml += "tbody tr:nth-child(even) { background-color: var(--bg-alt); }";
  pdfHtml += ".student-name { font-weight: 600; font-size: 14px; }";
  pdfHtml += ".bunk { color: #dc2626; font-weight: 700; font-family: monospace; font-size: 15px; }";
  pdfHtml += ".loc { font-weight: 700; font-size: 12px; }";
  pdfHtml += ".timestamp { font-family: monospace; color: var(--text-muted); }";
  pdfHtml += ".status-G { color: #059669; font-weight: 700; }";
  pdfHtml += ".status-Y { color: #d97706; font-weight: 700; }";
  pdfHtml += ".status-R { color: #dc2626; font-weight: 700; }";
  pdfHtml += ".footer { margin-top: 40px; text-align: center; font-size: 11px; color: var(--text-muted); border-top: 1px solid var(--border); padding-top: 20px; }";
  pdfHtml += "</style></head><body>";

  pdfHtml += "<div class='report-container'>";
  pdfHtml += "<div class='header'>";
  pdfHtml += "  <div class='header-left'>";
  pdfHtml += "    <h1>SHIELD LOG REPORT</h1>";
  pdfHtml += "    <div class='meta'>Secure Hardware Interface for Entry Logging & Discipline</div>";
  pdfHtml += "  </div>";
  pdfHtml += "  <div class='header-right'>";
  pdfHtml += "    <div class='report-time'>GENERATED: " + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "--") + "</div>";
  pdfHtml += "  </div>";
  pdfHtml += "</div>";

  pdfHtml += "<table><thead><tr>";
  pdfHtml += "<th>Student Name</th><th>Entries</th><th>Exits</th><th>Current Loc</th><th>Last Entry</th><th>Last Exit</th><th>Total Bunks</th><th>Alert Status</th>";
  pdfHtml += "</tr></thead><tbody>";

  for (auto &s : students) {
    pdfHtml += "<tr>";
    pdfHtml += "<td class='student-name'>" + String(s.name) + "</td>";
    pdfHtml += "<td>" + String(s.entryCount) + "</td>";
    pdfHtml += "<td>" + String(s.exitCount) + "</td>";
    pdfHtml += "<td class='loc' style='color: " + String(s.isInside ? "#059669" : "#d97706") + ";'>" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "</td>";
    pdfHtml += "<td class='timestamp'>" + formatMilitaryTime(s.lastEntryTime) + "</td>";
    pdfHtml += "<td class='timestamp'>" + formatMilitaryTime(s.lastExitTime) + "</td>";
    pdfHtml += "<td class='bunk'>" + String(s.bunkCount) + "</td>";

    String sClass = "status-G";
    if (s.colorAlert == "YELLOW") sClass = "status-Y";
    if (s.colorAlert == "RED") sClass = "status-R";

    pdfHtml += "<td class='" + sClass + "'>" + s.colorAlert + "</td>";
    pdfHtml += "</tr>";
  }

  pdfHtml += "</tbody></table>";
  pdfHtml += "<div class='footer'>Report auto-generated by SHIELD System v14.0 &bull; Official Security Record</div>";
  pdfHtml += "</div>";

  pdfHtml += "<script>window.onload = function() { window.print(); }</script>";
  pdfHtml += "</body></html>";

  server.send(200, "text/html", pdfHtml);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(IR_PIN, INPUT);
  pinMode(HALL_PIN, INPUT);
  SPI.begin();
  rfid.PCD_Init();

  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/sync", handleSync);
  server.on("/export", handleExport);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long currentTime = millis();
  bool currentIR = digitalRead(IR_PIN);
  bool currentHall = digitalRead(HALL_PIN);

  if (currentTime - lastNetworkCheck >= 300) {
    lastNetworkCheck = currentTime;
    for (auto &s : students) {
      if (s.stateChangeTimestamp > 0 && !s.isInside) {
        unsigned long runningOutsideTime = (currentTime - s.stateChangeTimestamp) / 1000;
        if (runningOutsideTime >= 20) {
          s.colorAlert = "RED";
          if (!s.bunkLoggedForCurrentCycle) {
            s.bunkCount++;
            s.bunkLoggedForCurrentCycle = true;
          }
        } else if (runningOutsideTime >= 10) {
          s.colorAlert = "YELLOW";
        }
      }
    }
  }

  // Instant Trigger Mechanism
  if ((currentHall && !lastHallState) || (currentIR && !lastIRState)) {
    if (!windowActive) {
      physicalBreachAlert = true;
      alertTriggerTime = currentTime; // Lock down the start of the breach
      windowActive = true;
      windowStartTime = currentTime;
      RFID_Scan_Count = 0;
      IR_Trip_Count = 0;
      activeStudent = nullptr;
    }
  }

  if (windowActive && (currentIR && !lastIRState)) {
    IR_Trip_Count++;
    if (RFID_Scan_Count > 0 && IR_Trip_Count >= 2) {
      tailgatingAlert = true;
      physicalBreachAlert = false;
      alertTriggerTime = currentTime;
    }
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Student* s = nullptr;
    for (auto &st : students) {
      bool match = true;
      for (int i = 0; i < 4; i++) {
        if (rfid.uid.uidByte[i] != st.uid[i]) match = false;
      }
      if (match) s = &st;
    }

    if (s) {
      if (physicalBreachAlert && RFID_Scan_Count == 0) {
        physicalBreachAlert = false;
      }
      if (!windowActive) {
        windowActive = true;
        windowStartTime = currentTime;
        IR_Trip_Count = 0;
      }
      RFID_Scan_Count++;
      activeStudent = s;
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  if (windowActive && (currentTime - windowStartTime >= FUSION_WINDOW_MS)) {
    if (RFID_Scan_Count == 0) {
      physicalBreachAlert = true;
      tailgatingAlert = false;
      alertTriggerTime = currentTime;
    }
    else if (activeStudent != nullptr) {
      if (IR_Trip_Count >= 2) {
        tailgatingAlert = true;
        physicalBreachAlert = false;
        alertTriggerTime = currentTime;
      } else {
        tailgatingAlert = false;
        physicalBreachAlert = false;
      }

      activeStudent->stateChangeTimestamp = currentTime;
      activeStudent->isInside = !activeStudent->isInside;
      activeStudent->colorAlert = "GREEN";

      if (activeStudent->isInside) {
        activeStudent->entryCount++;
        activeStudent->lastEntryTime = getLiveEpochTime();
        activeStudent->bunkLoggedForCurrentCycle = false;
      } else {
        activeStudent->exitCount++;
        activeStudent->lastExitTime = getLiveEpochTime();
      }
    }
    windowActive = false;
    activeStudent = nullptr;
  }

  // Pure Timestamp-driven Auto-Reset Engine (Bypasses state checks to guarantee clear-out)
  if ((physicalBreachAlert || tailgatingAlert) && (currentTime - alertTriggerTime >= 4000)) {
     physicalBreachAlert = false;
     tailgatingAlert = false;
  }

  lastIRState = currentIR;
  lastHallState = currentHall;
}
