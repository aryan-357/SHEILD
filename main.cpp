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
  html += "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&family=JetBrains+Mono:wght@400;700&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += ":root {";
  html += "  --bg-dark: #030712;";
  html += "  --glass-bg: rgba(17, 24, 39, 0.6);";
  html += "  --glass-border: rgba(255, 255, 255, 0.08);";
  html += "  --text-main: #f9fafb;";
  html += "  --text-muted: #9ca3af;";
  html += "  --accent-glow: #3b82f6;";
  html += "  --success-glow: #10b981;";
  html += "  --warning-glow: #f59e0b;";
  html += "  --danger-glow: #ef4444;";
  html += "}";

  html += "body { margin: 0; min-height: 100vh; background-color: var(--bg-dark); background-image: radial-gradient(circle at 15% 50%, rgba(59, 130, 246, 0.15), transparent 25%), radial-gradient(circle at 85% 30%, rgba(16, 185, 129, 0.1), transparent 25%); background-attachment: fixed; color: var(--text-main); font-family: 'Inter', sans-serif; padding: 40px 20px; box-sizing: border-box; overflow-x: hidden; }";

  if (physicalBreachAlert || tailgatingAlert) {
    html += "body { background-image: radial-gradient(circle at 50% 50%, rgba(239, 68, 68, 0.2), transparent 60%); box-shadow: inset 0 0 100px rgba(239, 68, 68, 0.2); animation: redAlert 2s infinite alternate; }";
    html += "@keyframes redAlert { 0% { box-shadow: inset 0 0 50px rgba(239, 68, 68, 0.1); } 100% { box-shadow: inset 0 0 150px rgba(239, 68, 68, 0.4); } }";
  }

  html += ".container { max-width: 1280px; margin: 0 auto; position: relative; z-index: 10; }";

  html += ".header { display: flex; justify-content: space-between; align-items: flex-end; margin-bottom: 40px; flex-wrap: wrap; gap: 20px; padding-bottom: 20px; border-bottom: 1px solid var(--glass-border); }";
  html += ".header-left h1 { font-size: 48px; font-weight: 800; margin: 0; background: linear-gradient(to right, #60a5fa, #c084fc); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -1px; text-shadow: 0 10px 30px rgba(96, 165, 250, 0.3); }";
  html += ".acronym { font-size: 12px; color: var(--text-muted); font-weight: 600; text-transform: uppercase; letter-spacing: 3px; margin-top: 8px; opacity: 0.8; }";

  html += ".header-right { display: flex; flex-direction: column; align-items: flex-end; gap: 15px; }";
  html += ".clock-card { background: var(--glass-bg); backdrop-filter: blur(12px); border: 1px solid var(--glass-border); border-radius: 12px; padding: 12px 24px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3); }";
  html += ".clock-label { font-size: 10px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 2px; margin-bottom: 4px; display: block; }";
  html += ".clock-time { font-family: 'JetBrains Mono', monospace; font-size: 28px; color: #facc15; font-weight: 700; text-shadow: 0 0 20px rgba(250, 204, 21, 0.4); }";

  html += ".export-btn { background: rgba(255, 255, 255, 0.05); color: var(--text-main); border: 1px solid rgba(255, 255, 255, 0.1); padding: 10px 20px; border-radius: 8px; font-weight: 600; font-size: 13px; text-decoration: none; text-transform: uppercase; letter-spacing: 1px; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); cursor: pointer; backdrop-filter: blur(5px); }";
  html += ".export-btn:hover { background: rgba(255, 255, 255, 0.1); border-color: rgba(255, 255, 255, 0.2); box-shadow: 0 0 20px rgba(255, 255, 255, 0.05); transform: translateY(-2px); }";

  html += ".status-banner { padding: 20px 30px; border-radius: 16px; font-weight: 600; font-size: 16px; margin-bottom: 40px; display: flex; align-items: center; justify-content: space-between; letter-spacing: 1px; backdrop-filter: blur(16px); }";
  html += ".status-indicator { display: flex; align-items: center; gap: 12px; }";
  html += ".pulse-dot { width: 12px; height: 12px; border-radius: 50%; }";

  html += ".status-secure { background: linear-gradient(90deg, rgba(16, 185, 129, 0.1), rgba(16, 185, 129, 0.05)); border: 1px solid rgba(16, 185, 129, 0.2); color: #34d399; box-shadow: 0 0 40px rgba(16, 185, 129, 0.1); }";
  html += ".status-secure .pulse-dot { background-color: #10b981; box-shadow: 0 0 15px #10b981; }";

  html += ".status-alert { background: linear-gradient(90deg, rgba(239, 68, 68, 0.15), rgba(239, 68, 68, 0.05)); border: 1px solid rgba(239, 68, 68, 0.3); color: #f87171; box-shadow: 0 0 50px rgba(239, 68, 68, 0.2); animation: borderPulse 1.5s infinite; }";
  html += ".status-alert .pulse-dot { background-color: #ef4444; box-shadow: 0 0 20px #ef4444; animation: dotPulse 1s infinite; }";
  html += "@keyframes borderPulse { 0% { border-color: rgba(239, 68, 68, 0.3); } 50% { border-color: rgba(239, 68, 68, 0.8); } 100% { border-color: rgba(239, 68, 68, 0.3); } }";
  html += "@keyframes dotPulse { 0% { opacity: 1; transform: scale(1); } 50% { opacity: 0.5; transform: scale(1.5); } 100% { opacity: 1; transform: scale(1); } }";

  html += ".table-glass { background: var(--glass-bg); backdrop-filter: blur(20px); border: 1px solid var(--glass-border); border-radius: 20px; overflow: hidden; box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5); }";
  html += "table { width: 100%; border-collapse: collapse; text-align: left; }";
  html += "th, td { padding: 20px 24px; font-size: 14px; border-bottom: 1px solid rgba(255, 255, 255, 0.03); white-space: nowrap; }";
  html += "th { color: var(--text-muted); font-weight: 600; text-transform: uppercase; letter-spacing: 1.5px; font-size: 11px; background: rgba(0, 0, 0, 0.2); }";
  html += "tbody tr { transition: background-color 0.2s; }";
  html += "tbody tr:hover { background-color: rgba(255, 255, 255, 0.02); }";
  html += "tbody tr:last-child td { border-bottom: none; }";

  html += ".student-name { font-weight: 600; font-size: 16px; color: var(--text-main); display: flex; align-items: center; gap: 12px; }";
  html += ".avatar-placeholder { width: 32px; height: 32px; border-radius: 50%; background: linear-gradient(135deg, #3b82f6, #8b5cf6); display: flex; align-items: center; justify-content: center; font-size: 12px; font-weight: bold; color: white; box-shadow: 0 0 10px rgba(139, 92, 246, 0.5); }";

  html += ".stat-pill { font-family: 'JetBrains Mono', monospace; font-size: 14px; color: #e2e8f0; }";

  html += ".loc-badge { display: inline-flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px; font-weight: 700; font-size: 11px; letter-spacing: 1px; text-transform: uppercase; }";
  html += ".loc-inside { background: rgba(16, 185, 129, 0.1); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.2); }";
  html += ".loc-inside::before { content: ''; width: 6px; height: 6px; border-radius: 50%; background: #34d399; box-shadow: 0 0 8px #34d399; }";
  html += ".loc-outside { background: rgba(245, 158, 11, 0.1); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.2); }";
  html += ".loc-outside::before { content: ''; width: 6px; height: 6px; border-radius: 50%; background: #fbbf24; box-shadow: 0 0 8px #fbbf24; }";

  html += ".timestamp-group { display: flex; flex-direction: column; gap: 4px; font-family: 'JetBrains Mono', monospace; font-size: 12px; color: var(--text-muted); }";
  html += ".timestamp-group span b { color: var(--text-main); font-weight: normal; }";

  html += ".duration { font-family: 'JetBrains Mono', monospace; color: #60a5fa; font-size: 13px; font-weight: 600; text-shadow: 0 0 10px rgba(96, 165, 250, 0.3); }";

  html += ".bunk-count { font-family: 'JetBrains Mono', monospace; font-weight: 800; font-size: 18px; color: #ef4444; text-shadow: 0 0 15px rgba(239, 68, 68, 0.4); text-align: center; }";
  html += ".bunk-count.zero { color: var(--text-muted); text-shadow: none; font-weight: 400; }";

  html += ".alert-badge { display: inline-flex; justify-content: center; width: 120px; padding: 8px 0; border-radius: 8px; font-weight: 700; font-size: 11px; letter-spacing: 1px; text-transform: uppercase; backdrop-filter: blur(4px); }";
  html += ".badge-G { background: rgba(16, 185, 129, 0.05); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.3); box-shadow: inset 0 0 10px rgba(16, 185, 129, 0.1); }";
  html += ".badge-Y { background: rgba(245, 158, 11, 0.05); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.3); box-shadow: inset 0 0 10px rgba(245, 158, 11, 0.1); }";
  html += ".badge-R { background: rgba(239, 68, 68, 0.05); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.5); box-shadow: inset 0 0 15px rgba(239, 68, 68, 0.2), 0 0 20px rgba(239, 68, 68, 0.2); }";
  html += "</style></head><body>";

  html += "<div class='container'>";

  html += "<div class='header'>";
  html += "  <div class='header-left'>";
  html += "    <h1>SHIELD</h1>";
  html += "    <div class='acronym'>Secure Hardware Interface for Entry Logging & Discipline</div>";
  html += "  </div>";
  html += "  <div class='header-right'>";
  html += "    <div class='clock-card'>";
  html += "      <span class='clock-label'>System Sync Time</span>";
  html += "      <span class='clock-time'>" + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "SYNCING...") + "</span>";
  html += "    </div>";
  html += "    <a href='/export' class='export-btn'>Export PDF Report</a>";
  html += "  </div>";
  html += "</div>";

  bool isDoorOpen = digitalRead(HALL_PIN);
  bool isAlert = isDoorOpen || physicalBreachAlert || tailgatingAlert;

  String bannerClass = isAlert ? "status-alert" : "status-secure";
  String bannerText = "PORTAL SECURE // ALL SYSTEMS NOMINAL";

  if (tailgatingAlert) {
    bannerText = "CRITICAL ALERT // TAILGATING DETECTED // MULTIPLE PERSON ENTRIES";
  } else if (physicalBreachAlert) {
    bannerText = "CRITICAL ALERT // UNPAIRED SECURITY BREACH // NO CARD DETECTED";
  } else if (isDoorOpen) {
    bannerText = "WARNING // PORTAL DOOR OPEN";
  }

  html += "<div class='status-banner " + bannerClass + "'>";
  html += "  <div class='status-indicator'><div class='pulse-dot'></div> <span>" + bannerText + "</span></div>";
  html += "  <div style='font-family: \"JetBrains Mono\", monospace; font-size: 12px; opacity: 0.6;'>STATUS:" + String(isAlert ? "0xDEAD" : "0x0000") + "</div>";
  html += "</div>";

  html += "<div class='table-glass'><table><thead><tr>";
  html += "<th>Student Account</th><th>Entries</th><th>Exits</th><th>Zone Loc</th><th>Timestamps</th><th>Running Duration</th><th style='text-align: center;'>Total Bunks</th><th>Alert Status</th>";
  html += "</tr></thead><tbody>";

  for (auto &s : students) {
    html += "<tr>";

    // Auto-generate initials for the sleek avatar placeholder
    String nameStr = String(s.name);
    char initial1 = nameStr.length() > 0 ? nameStr.charAt(0) : '?';
    char initial2 = ' ';
    int spaceIdx = nameStr.indexOf(' ');
    if (spaceIdx != -1 && spaceIdx + 1 < nameStr.length()) {
        initial2 = nameStr.charAt(spaceIdx + 1);
    }
    String initials = String(initial1) + String(initial2);
    initials.toUpperCase();

    html += "<td class='student-name'><div class='avatar-placeholder'>" + initials + "</div>" + nameStr + "</td>";
    html += "<td><span class='stat-pill'>" + String(s.entryCount) + "</span></td>";
    html += "<td><span class='stat-pill'>" + String(s.exitCount) + "</span></td>";
    html += "<td><span class='loc-badge " + String(s.isInside ? "loc-inside" : "loc-outside") + "'>" + String(s.isInside ? "INSIDE" : "OUTSIDE") + "</span></td>";
    html += "<td><div class='timestamp-group'><span>IN:  <b>" + formatMilitaryTime(s.lastEntryTime) + "</b></span><span>OUT: <b>" + formatMilitaryTime(s.lastExitTime) + "</b></span></div></td>";

    if (s.stateChangeTimestamp == 0) {
      html += "<td class='duration' style='color: var(--text-muted);'>--</td>";
    } else {
      unsigned long totalDeltaSecs = (currentMillis - s.stateChangeTimestamp) / 1000;
      unsigned long runMinutes = totalDeltaSecs / 60;
      unsigned long runSeconds = totalDeltaSecs % 60;
      char durBuf[16];
      sprintf(durBuf, "%02dm %02ds", runMinutes, runSeconds);
      html += "<td class='duration'>" + String(durBuf) + "</td>";
    }

    String bunkClass = s.bunkCount == 0 ? "zero" : "";
    html += "<td class='bunk-count " + bunkClass + "'>" + String(s.bunkCount) + "</td>";

    String alertClass = "badge-G";
    String alertText = "SECURE";
    if (s.colorAlert == "YELLOW") { alertClass = "badge-Y"; alertText = "WARNING"; }
    if (s.colorAlert == "RED") { alertClass = "badge-R"; alertText = "CRITICAL"; }

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
