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

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>SHIELD SYSTEM v14.0</title>";
  html += "<meta http-equiv='refresh' content='1'>";
  html += "<style>";

  if (physicalBreachAlert || tailgatingAlert) {
    html += "body { background-color: #120101; color: #FF3B30; font-family: Arial, sans-serif; text-align: center; margin: 30px; border: 6px dashed #FF3B30; }";
  } else {
    html += "body { background-color: #02060D; color: #00E5FF; font-family: Arial, sans-serif; text-align: center; margin: 30px; border: 6px solid transparent; }";
  }

  html += "h1 { font-size: 32px; text-shadow: 0 0 12px currentColor; letter-spacing: 3px; margin-bottom: 2px; }";
  html += ".acronym { font-size: 13px; color: #88A0B0; font-weight: bold; letter-spacing: 1px; margin-bottom: 15px; text-transform: uppercase; }";
  html += ".clock-display { font-size: 18px; color: #FFFF00; font-weight: bold; margin-bottom: 15px; text-shadow: 0 0 5px #FFFF00; }";
  html += ".box { display: inline-block; padding: 12px 25px; background: #0A111A; border: 2px solid #1A2636; font-weight: bold; margin-bottom: 20px; border-radius: 4px; }";
  html += ".export-btn { background: #FF3B30; color: #FFFFFF; border: none; padding: 10px 20px; font-weight: bold; border-radius: 4px; cursor: pointer; margin-bottom: 15px; font-size:14px; text-decoration: none; display: inline-block; }";
  html += "table { width: 95%; margin: 0 auto; border-collapse: collapse; background: #0A111A; text-align: left; }";
  html += "th, td { padding: 12px; border-bottom: 1px solid #1A2636; font-size: 14px; }";
  html += "th { background-color: #101B26; color: #00E5FF; }";
  html += ".INSIDE { color: #00FF66; font-weight: bold; }";
  html += ".OUTSIDE { color: #FF9F0A; font-weight: bold; }";
  html += ".BUNK-COUNT { color: #FF3B30; font-weight: bold; font-size: 16px; }";
  html += ".G { background: rgba(0, 255, 102, 0.2); color: #00FF66; padding: 4px 8px; border: 1px solid #00FF66; font-weight: bold; }";
  html += ".Y { background: rgba(255, 159, 10, 0.2); color: #FF9F0A; padding: 4px 8px; border: 1px solid #FF9F0A; font-weight: bold; }";
  html += ".R { background: rgba(255, 59, 48, 0.3); color: #FF3B30; padding: 4px 8px; border: 1px solid #FF3B30; font-weight: bold; }";
  html += "</style></head><body>";

  html += "<h1>SHIELD</h1>";
  html += "<div class='acronym'>Secure Hardware Interface for Entry Logging and Discipline</div>";

  html += "<div class='clock-display'>MASTER TIME: " + (liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "SYNCHRONIZING WITH COMPUTER...") + "</div>";
  html += "<a href='/export' class='export-btn'>PDF REPORT</a><br>";

  bool isDoorOpen = digitalRead(HALL_PIN);

  html += "<div class='box' style='color: " + String((isDoorOpen || physicalBreachAlert || tailgatingAlert) ? "#FF3B30" : "#00FF66") + ";'>";
  if (tailgatingAlert) {
    html += "PORTAL SECURITY LEVEL: TAILGATING DETECTED MULTIPLE PERSON ENTRIES";
  } else if (physicalBreachAlert) {
    html += "PORTAL SECURITY LEVEL: UNPAIRED SECURITY BREACH NO CARD";
  } else {
    html += "PORTAL SECURITY LEVEL: " + String(isDoorOpen ? "DOOR OPEN" : "SECURE");
  }
  html += "</div>";

  html += "<table><thead><tr>";
  html += "<th>STUDENT ACCOUNT</th><th>ENTRIES</th><th>EXITS</th><th>ZONE LOC</th><th>TIMESTAMPS</th><th>RUNNING DURATION</th><th>TOTAL BUNKS</th><th>ALERT STATUS</th>";
  html += "</tr></thead><tbody>";

  for (auto &s : students) {
    html += "<tr>";
    html += "<td>" + String(s.name) + "</td>";
    html += "<td>" + String(s.entryCount) + "</td>";
    html += "<td>" + String(s.exitCount) + "</td>";
    html += "<td class='" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "'>" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "</td>";
    html += "<td>In: " + formatMilitaryTime(s.lastEntryTime) + " | Out: " + formatMilitaryTime(s.lastExitTime) + "</td>";

    if (s.stateChangeTimestamp == 0) {
      html += "<td>NO DATA</td>";
    } else {
      unsigned long totalDeltaSecs = (currentMillis - s.stateChangeTimestamp) / 1000;
      unsigned long runMinutes = totalDeltaSecs / 60;
      unsigned long runSeconds = totalDeltaSecs % 60;
      html += "<td>" + String(runMinutes) + "m " + String(runSeconds) + "s Ago</td>";
    }

    html += "<td class='BUNK-COUNT'>" + String(s.bunkCount) + "</td>";

    String alertClass = "G";
    String alertText = "SECURE";
    if (s.colorAlert == "YELLOW") { alertClass = "Y"; alertText = "WARNING (10s+)"; }
    if (s.colorAlert == "RED") { alertClass = "R"; alertText = "CRITICAL BUNK"; }

    html += "<td><span class='" + alertClass + "'>" + alertText + "</span></td>";
    html += "</tr>";
  }

  html += "</tbody></table>";
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
  String pdfHtml = "<!DOCTYPE html><html><head>";
  pdfHtml += "<meta charset='UTF-8'>";
  pdfHtml += "<title>Security Log Report</title>";
  pdfHtml += "<style>";
  pdfHtml += "body { font-family: 'Helvetica', Arial, sans-serif; padding: 40px; color: #333; }";
  pdfHtml += ".header { border-bottom: 3px solid #333; padding-bottom: 10px; margin-bottom: 30px; }";
  pdfHtml += "h1 { margin: 0; color: #111; font-size: 24px; text-transform: uppercase; }";
  pdfHtml += ".meta { font-size: 12px; color: #666; margin-top: 5px; }";
  pdfHtml += "table { width: 100%; border-collapse: collapse; margin-top: 20px; }";
  pdfHtml += "th, td { border: 1px solid #999; padding: 10px; text-align: left; font-size: 13px; }";
  pdfHtml += "th { background-color: #f2f2f2; font-weight: bold; text-transform: uppercase; }";
  pdfHtml += ".bunk { color: #cc0000; font-weight: bold; }";
  pdfHtml += "</style></head><body>";

  pdfHtml += "<div class='header'>";
  pdfHtml += "<h1>SHIELD Log Report</h1>";
  pdfHtml += "<div class='meta'>Secure Hardware Interface for Entry Logging and Discipline</div>";
  pdfHtml += "</div>";

  pdfHtml += "<table><thead><tr>";
  pdfHtml += "<th>Student Name</th><th>Entries</th><th>Exits</th><th>Current Location</th><th>Last Entry</th><th>Last Exit</th><th>Total Bunks</th><th>Status</th>";
  pdfHtml += "</tr></thead><tbody>";

  for (auto &s : students) {
    pdfHtml += "<tr>";
    pdfHtml += "<td>" + String(s.name) + "</td>";
    pdfHtml += "<td>" + String(s.entryCount) + "</td>";
    pdfHtml += "<td>" + String(s.exitCount) + "</td>";
    pdfHtml += "<td>" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "</td>";
    pdfHtml += "<td>" + formatMilitaryTime(s.lastEntryTime) + "</td>";
    pdfHtml += "<td>" + formatMilitaryTime(s.lastExitTime) + "</td>";
    pdfHtml += "<td class='bunk'>" + String(s.bunkCount) + "</td>";
    pdfHtml += "<td>" + s.colorAlert + "</td>";
    pdfHtml += "</tr>";
  }

  pdfHtml += "</tbody></table>";
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
