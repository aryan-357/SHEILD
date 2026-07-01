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
  int rollNumber;
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
  unsigned long totalBunkDuration;
  unsigned long currentBunkStart;
  unsigned long lastBunkDuration;
};

Student students[] = {
  {{0x9E, 0x60, 0x02, 0x02}, 1, "Yuvraj Singh", 0, 0, false, 0, 0, 0, "GREEN", 0, false, 0, 0, 0},
  {{0x74, 0x8A, 0xBB, 0x02}, 2, "Yash Pratap",  0, 0, false, 0, 0, 0, "GREEN", 0, false, 0, 0, 0}
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
  unsigned long liveNowEpoch = getLiveEpochTime();
  unsigned long currentMillis = millis();

  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>SHIELD Admin</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;600&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += ":root {";
  html += "  --bg: #f8f9fa;";
  html += "  --surface: #ffffff;";
  html += "  --surface-alt: #f9fafb;";
  html += "  --text-main: #111827;";
  html += "  --text-muted: #6b7280;";
  html += "  --border: #e5e7eb;";
  html += "  --primary: #000000;";
  html += "  --radius: 8px;";
  html += "  --shadow: 0 1px 3px rgba(0,0,0,0.1);";
  html += "  --container-bg: rgba(255, 255, 255, 0.85);";
  html += "}";
  html += "[data-theme='dark'] {";
  html += "  --bg: #111827;";
  html += "  --surface: #1f2937;";
  html += "  --surface-alt: #374151;";
  html += "  --text-main: #f9fafb;";
  html += "  --text-muted: #9ca3af;";
  html += "  --border: #374151;";
  html += "  --primary: #ffffff;";
  html += "  --shadow: 0 1px 3px rgba(0,0,0,0.5);";
  html += "  --container-bg: rgba(31, 41, 55, 0.85);";
  html += "}";
  html += "body { margin: 0; color: var(--text-main); font-family: 'Inter', sans-serif; padding: 20px; transition: background-color 0.5s ease, color 0.5s ease; ";
  html += "  background-image: url('https://images.unsplash.com/photo-1557683316-973673baf926?q=80&w=2000&auto=format&fit=crop');";
  html += "  background-size: cover;";
  html += "  background-position: center;";
  html += "  background-attachment: fixed;";
  html += "  background-color: var(--bg);";
  html += "}";
  html += ".container { max-width: 1200px; margin: 0 auto; background: var(--container-bg); padding: 30px; border-radius: 12px; box-shadow: 0 4px 30px var(--shadow); backdrop-filter: blur(5px); transition: background-color 0.5s ease, box-shadow 0.5s ease; animation: slideUpFadeIn 0.8s cubic-bezier(0.16, 1, 0.3, 1) forwards; opacity: 0; transform: translateY(20px); }";
  html += "@keyframes slideUpFadeIn { to { opacity: 1; transform: translateY(0); } }";

  html += ".header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 30px; border-bottom: 2px solid var(--primary); padding-bottom: 20px; }";
  html += ".header h1 { margin: 0; font-size: 24px; font-weight: 700; text-transform: uppercase; letter-spacing: -0.5px; }";
  html += ".header .subtitle { color: var(--text-muted); font-size: 13px; font-weight: 500; }";
  html += ".controls { display: flex; gap: 15px; }";
  html += ".btn { background: var(--surface); color: var(--primary); border: 1px solid var(--border); padding: 8px 16px; border-radius: var(--radius); font-size: 13px; font-weight: 600; cursor: pointer; text-decoration: none; transition: 0.2s; box-shadow: var(--shadow); }";
  html += ".btn:hover { background: var(--bg); border-color: var(--text-muted); }";
  html += ".btn-primary { background: var(--primary); color: var(--bg); border-color: var(--primary); }";
  html += ".btn-primary:hover { opacity: 0.8; }";

  html += ".summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 30px; }";
  html += ".card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); padding: 20px; box-shadow: var(--shadow); transition: transform 0.3s ease, box-shadow 0.3s ease; }";
  html += ".card:hover { transform: translateY(-5px); box-shadow: 0 8px 15px rgba(0,0,0,0.1); }";
  html += ".card-title { font-size: 12px; color: var(--text-muted); text-transform: uppercase; font-weight: 600; margin-bottom: 10px; }";
  html += ".card-value { font-size: 24px; font-weight: 700; font-family: 'JetBrains Mono', monospace; }";

  html += ".toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; gap: 20px; }";
  html += ".search-box { flex: 1; max-width: 400px; }";
  html += ".search-box input { width: 100%; padding: 10px 15px; border: 1px solid var(--border); background: var(--surface); color: var(--text-main); border-radius: var(--radius); font-size: 14px; box-sizing: border-box; transition: box-shadow 0.3s ease, border-color 0.3s ease; }";
  html += ".search-box input:focus { outline: none; border-color: var(--text-muted); box-shadow: 0 0 0 3px rgba(107,114,128,0.2); }";
  html += ".filters { display: flex; gap: 10px; }";
  html += ".filter-btn { background: var(--surface); border: 1px solid var(--border); padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: 600; cursor: pointer; color: var(--text-muted); transition: all 0.3s ease; }";
  html += ".filter-btn:hover:not(.active) { background: var(--border); transform: scale(1.05); }";
  html += ".filter-btn.active { background: var(--primary); color: white; border-color: var(--primary); }";

  html += ".section-title { font-size: 16px; font-weight: 700; margin: 30px 0 15px 0; display: flex; align-items: center; justify-content: space-between; }";
  html += ".section-title span.badge { background: #fee2e2; color: #b91c1c; padding: 2px 8px; border-radius: 12px; font-size: 12px; }";

  html += "table { width: 100%; border-collapse: collapse; background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; box-shadow: var(--shadow); }";
  html += "th, td { padding: 12px 16px; text-align: left; border-bottom: 1px solid var(--border); font-size: 14px; transition: background-color 0.2s ease; }";
  html += "tbody tr:hover td { background-color: rgba(128,128,128,0.05); }";
  html += "th { background: var(--surface-alt); font-weight: 600; color: var(--text-muted); font-size: 12px; text-transform: uppercase; letter-spacing: 0.5px; }";
  html += "tr:last-child td { border-bottom: none; }";
  html += ".mono { font-family: 'JetBrains Mono', monospace; }";
  html += ".status-indicator { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 8px; }";
  html += ".status-in { background: #10b981; }";
  html += ".status-out { background: #f59e0b; }";
  html += ".alert-badge { padding: 4px 8px; border-radius: 4px; font-size: 11px; font-weight: 700; text-transform: uppercase; }";
  html += ".alert-red { background: #fee2e2; color: #b91c1c; border: 1px solid #fca5a5; }";
  html += ".alert-yellow { background: #fef3c7; color: #d97706; border: 1px solid #fcd34d; }";
  html += ".alert-green { background: #d1fae5; color: #047857; border: 1px solid #6ee7b7; }";

  html += ".toggle-btn { background: none; border: none; color: var(--text-muted); cursor: pointer; font-size: 14px; font-weight: 500; text-decoration: underline; }";
  html += ".hidden { display: none !important; }";
    html += ".status-secure { background: #d1fae5; color: #065f46; border: 1px solid #34d399; }";
  html += ".status-alert { background: #fee2e2; color: #991b1b; border: 1px solid #f87171; animation: pulse 2s infinite; }";
  html += "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.8; } 100% { opacity: 1; } }";
html += "</style></head><body>";

  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "  <div>";
  html += "    <h1>SHIELD Admin</h1>";
  html += "    <div class='subtitle'>Daily Attendance & Bunk Monitor</div>";
  html += "  </div>";
  html += "  <div class='controls'>";
  html += "    <button id='themeToggleBtn' class='btn'>Toggle Theme</button>";
  html += "    <a href='/export' class='btn btn-primary'>Export Daily Report</a>";
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

  html += "<div style='margin-bottom: 20px; padding: 15px; border-radius: 8px; font-weight: bold; text-align: center;' class='" + bannerClass + "'>";
  html += bannerText;
  html += "</div>";
  html += "<div class='summary-grid'>";
  html += "  <div class='card'><div class='card-title'>System Time</div><div class='card-value' id='sysTimeDisplay'>" + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "--:--") + "</div></div>";
  html += "  <div class='card'><div class='card-title'>Total Students</div><div class='card-value' id='statTotal'>0</div></div>";
  html += "  <div class='card'><div class='card-title'>Currently Inside</div><div class='card-value' id='statInside'>0</div></div>";
  html += "  <div class='card'><div class='card-title'>Active Bunks</div><div class='card-value' style='color:#b91c1c;' id='statBunks'>0</div></div>";
  html += "</div>";

  html += "<div class='toolbar'>";
  html += "  <div class='search-box'>";
  html += "    <input type='text' id='searchInput' placeholder='Search by Name or Roll #...'>";
  html += "  </div>";
  html += "  <div class='filters' id='filterGroup'>";
  html += "    <button class='filter-btn active' data-filter='all'>All</button>";
  html += "    <button class='filter-btn' data-filter='inside'>Inside</button>";
  html += "    <button class='filter-btn' data-filter='outside'>Outside</button>";
  html += "  </div>";
  html += "</div>";

  // Active Cases Section
  html += "<div class='section-title'>Active Cases / Leaderboard <span class='badge' id='activeBunksCount'>0</span></div>";
  html += "<table><thead><tr>";
  html += "<th>Roll #</th><th>Student Name</th><th>Location</th><th>Out Since</th><th>Current Dur.</th><th>Total Bunks</th><th>Alert Status</th>";
  html += "</tr></thead><tbody id='activeTableBody'></tbody></table>";

  // All Students Section
  html += "<div class='section-title'>All Students <button class='toggle-btn' id='toggleAllBtn'>Show</button></div>";
  html += "<table id='allTable' class='hidden'><thead><tr>";
  html += "<th>Roll #</th><th>Student Name</th><th>Location</th><th>IN</th><th>OUT</th><th>Bunks</th><th>Alert Status</th>";
  html += "</tr></thead><tbody id='allTableBody'></tbody></table>";

  html += "</div>";

  html += "<script>";
  html += "const themeToggleBtn = document.getElementById('themeToggleBtn');";
  html += "const currentTheme = localStorage.getItem('theme') || 'light';";
  html += "if (currentTheme === 'dark') document.documentElement.setAttribute('data-theme', 'dark');";
  html += "themeToggleBtn.addEventListener('click', () => {";
  html += "  let theme = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';";
  html += "  document.documentElement.setAttribute('data-theme', theme);";
  html += "  localStorage.setItem('theme', theme);";
  html += "});";  html += "const now = new Date();";
  html += "const localSecondsSinceMidnight = Math.floor((now.getHours() * 3600) + (now.getMinutes() * 60) + now.getSeconds());";
  html += "if (" + String(liveNowEpoch == 0 ? "true" : "false") + ") {";
  html += "  fetch('/sync?t=' + localSecondsSinceMidnight);";
  html += "}";

  html += "const currentMillis = " + String(currentMillis) + ";";
  html += "let students = [";
  for (int i = 0; i < sizeof(students)/sizeof(students[0]); i++) {
      auto &s = students[i];
      unsigned long runSecs = 0;
      if (s.stateChangeTimestamp > 0 && !s.isInside) {
          runSecs = (currentMillis - s.stateChangeTimestamp) / 1000;
      }

      html += "{";
      html += "roll: " + String(s.rollNumber) + ",";
      html += "name: '" + String(s.name) + "',";
      html += "inside: " + String(s.isInside ? "true" : "false") + ",";
      html += "inTime: '" + formatMilitaryTime(s.lastEntryTime) + "',";
      html += "outTime: '" + formatMilitaryTime(s.lastExitTime) + "',";
      html += "bunks: " + String(s.bunkCount) + ",";
      html += "alert: '" + s.colorAlert + "',";
      html += "runSecs: " + String(runSecs);
      html += "}";
      if (i < (sizeof(students)/sizeof(students[0])) - 1) html += ",";
  }
  html += "];";

  html += "function formatDur(secs) {";
  html += "  if(secs === 0) return '--';";
  html += "  let m = Math.floor(secs / 60);";
  html += "  let s = secs % 60;";
  html += "  return m + 'm ' + s + 's';";
  html += "}";

  html += "let currentFilter = 'all';";
  html += "let searchQuery = '';";

  html += "function render() {";
  html += "  let activeBunks = 0;";
  html += "  let insideCount = 0;";

  html += "  students.forEach(s => {";
  html += "    if(s.inside) insideCount++;";
  html += "    if(s.alert === 'RED' || s.alert === 'YELLOW') activeBunks++;";
  html += "  });";

  html += "  document.getElementById('statTotal').innerText = students.length;";
  html += "  document.getElementById('statInside').innerText = insideCount;";
  html += "  document.getElementById('statBunks').innerText = activeBunks;";
  html += "  document.getElementById('activeBunksCount').innerText = activeBunks + ' Active';";

  html += "  let filtered = students.filter(s => {";
  html += "    let matchSearch = s.name.toLowerCase().includes(searchQuery) || String(s.roll).includes(searchQuery);";
  html += "    let matchFilter = true;";
  html += "    if(currentFilter === 'inside') matchFilter = s.inside;";
  html += "    if(currentFilter === 'outside') matchFilter = !s.inside;";
  html += "    return matchSearch && matchFilter;";
  html += "  });";

  html += "  let activeHTML = '';";
  html += "  let allHTML = '';";

  html += "  let activeStudents = filtered.filter(s => s.alert === 'RED' || s.alert === 'YELLOW');";
  html += "  activeStudents.sort((a,b) => b.runSecs - a.runSecs);"; // Sort by current duration descending

  html += "  if(activeStudents.length === 0) {";
  html += "    activeHTML = `<tr><td colspan=\"7\" style='text-align:center; color:#6b7280; padding:20px;'>No active cases. All clear.</td></tr>`;";
  html += "  } else {";
  html += "    activeStudents.forEach(s => {";
  html += "      let alertClass = s.alert === 'RED' ? 'alert-red' : (s.alert === 'YELLOW' ? 'alert-yellow' : 'alert-green');";
  html += "      activeHTML += `<tr>";
  html += "        <td class='mono'>${String(s.roll).padStart(2, '0')}</td>";
  html += "        <td style='font-weight:600;'>${s.name}</td>";
  html += "        <td><span class='status-indicator status-out'></span>Outside</td>";
  html += "        <td class='mono'>${s.outTime}</td>";
  html += "        <td class='mono' style='color:#b91c1c; font-weight:600;'>${formatDur(s.runSecs)}</td>";
  html += "        <td class='mono'>${s.bunks}</td>";
  html += "        <td><span class='alert-badge ${alertClass}'>${s.alert}</span></td>";
  html += "      </tr>`;";
  html += "    });";
  html += "  }";

  html += "  filtered.sort((a,b) => a.name.localeCompare(b.name)).forEach(s => {";
  html += "    let locText = s.inside ? 'Inside' : 'Outside';";
  html += "    let locStatus = s.inside ? 'status-in' : 'status-out';";
  html += "    let alertClass = s.alert === 'RED' ? 'alert-red' : (s.alert === 'YELLOW' ? 'alert-yellow' : 'alert-green');";
  html += "    allHTML += `<tr>";
  html += "      <td class='mono'>${String(s.roll).padStart(2, '0')}</td>";
  html += "      <td style='font-weight:600;'>${s.name}</td>";
  html += "      <td><span class='status-indicator ${locStatus}'></span>${locText}</td>";
  html += "      <td class='mono'>${s.inTime}</td>";
  html += "      <td class='mono'>${s.outTime}</td>";
  html += "      <td class='mono'>${s.bunks}</td>";
  html += "      <td><span class='alert-badge ${alertClass}'>${s.alert}</span></td>";
  html += "    </tr>`;";
  html += "  });";

  html += "  document.getElementById('activeTableBody').innerHTML = activeHTML;";
  html += "  document.getElementById('allTableBody').innerHTML = allHTML || `<tr><td colspan=\"7\" style='text-align:center;'>No matching students found.</td></tr>`;";
  html += "}";

  html += "document.getElementById('searchInput').addEventListener('input', (e) => {";
  html += "  searchQuery = e.target.value.toLowerCase();";
  html += "  render();";
  html += "});";

  html += "document.querySelectorAll('.filter-btn').forEach(btn => {";
  html += "  btn.addEventListener('click', (e) => {";
  html += "    document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));";
  html += "    e.target.classList.add('active');";
  html += "    currentFilter = e.target.dataset.filter;";
  html += "    render();";
  html += "  });";
  html += "});";

  html += "document.getElementById('toggleAllBtn').addEventListener('click', (e) => {";
  html += "  const table = document.getElementById('allTable');";
  html += "  if(table.classList.contains('hidden')) {";
  html += "    table.classList.remove('hidden');";
  html += "    e.target.innerText = 'Hide';";
  html += "  } else {";
  html += "    table.classList.add('hidden');";
  html += "    e.target.innerText = 'Show';";
  html += "  }";
  html += "});";

  html += "render();";

  html += "async function fetchData() {";
  html += "  try {";
  html += "    const res = await fetch('/data');";
  html += "    if(!res.ok) return;";
  html += "    const data = await res.json();";
  html += "    students = data.students;";
  html += "    document.getElementById('sysTimeDisplay').innerText = data.sysTime;";
  html += "    render();";
  html += "  } catch(e) { console.error('Fetch error:', e); }";
  html += "}";
  html += "setInterval(fetchData, 3000);";

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

void handleData() {
  unsigned long currentMillis = millis();
  unsigned long liveNowEpoch = getLiveEpochTime();

  String json = "{";
  json += "\"sysTime\": \"" + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "--:--") + "\",";
  json += "\"students\": [";

  for (int i = 0; i < sizeof(students)/sizeof(students[0]); i++) {
      auto &s = students[i];
      unsigned long runSecs = 0;
      if (s.stateChangeTimestamp > 0 && !s.isInside) {
          runSecs = (currentMillis - s.stateChangeTimestamp) / 1000;
      }

      json += "{";
      json += "\"roll\": " + String(s.rollNumber) + ",";
      json += "\"name\": \"" + String(s.name) + "\",";
      json += "\"inside\": " + String(s.isInside ? "true" : "false") + ",";
      json += "\"inTime\": \"" + formatMilitaryTime(s.lastEntryTime) + "\",";
      json += "\"outTime\": \"" + formatMilitaryTime(s.lastExitTime) + "\",";
      json += "\"bunks\": " + String(s.bunkCount) + ",";
      json += "\"alert\": \"" + s.colorAlert + "\",";
      json += "\"runSecs\": " + String(runSecs);
      json += "}";
      if (i < (sizeof(students)/sizeof(students[0])) - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleExport() {
  unsigned long liveNowEpoch = getLiveEpochTime();
  String pdfHtml = "<!DOCTYPE html><html lang='en'><head>";
  pdfHtml += "<meta charset='UTF-8'>";
  pdfHtml += "<title>SHIELD Security Log Report</title>";
  pdfHtml += "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&family=JetBrains+Mono:wght@400;700&display=swap' rel='stylesheet'>";
  pdfHtml += "<style>";
  pdfHtml += ":root {";
  pdfHtml += "  --text-main: #000000;";
  pdfHtml += "  --text-muted: #555555;";
  pdfHtml += "  --border: #cccccc;";
  pdfHtml += "  --bg-alt: #f4f4f4;";
  pdfHtml += "}";
  pdfHtml += "@page { margin: 20mm; size: A4 portrait; }";
  pdfHtml += "body { font-family: 'Inter', sans-serif; color: var(--text-main); margin: 0; padding: 0; background: #fff; line-height: 1.5; }";
  pdfHtml += ".report-container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  pdfHtml += ".header { border-bottom: 2px solid var(--text-main); padding-bottom: 20px; margin-bottom: 30px; display: flex; justify-content: space-between; align-items: flex-end; }";
  pdfHtml += ".header-left h1 { margin: 0 0 5px 0; font-size: 24px; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; }";
  pdfHtml += ".header-left .meta { font-size: 11px; color: var(--text-muted); font-weight: 600; text-transform: uppercase; letter-spacing: 1px; }";
  pdfHtml += ".header-right { text-align: right; }";
  pdfHtml += ".report-time { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 600; border: 1px solid var(--border); padding: 5px 10px; }";
  pdfHtml += "table { width: 100%; border-collapse: collapse; margin-top: 10px; }";
  pdfHtml += "th, td { padding: 10px 12px; text-align: left; font-size: 12px; border-bottom: 1px solid var(--border); }";
  pdfHtml += "th { background-color: var(--bg-alt); font-weight: 700; text-transform: uppercase; font-size: 10px; letter-spacing: 1px; }";
  pdfHtml += ".student-name { font-weight: 600; font-size: 13px; }";
  pdfHtml += ".rank-roll { font-family: 'JetBrains Mono'; font-size: 11px; color: var(--text-muted); }";
  pdfHtml += ".data-mono { font-family: 'JetBrains Mono'; }";
  pdfHtml += ".bunk { font-weight: 700; font-size: 13px; }";
  pdfHtml += ".footer { margin-top: 40px; text-align: center; font-size: 10px; color: var(--text-muted); border-top: 1px solid var(--border); padding-top: 15px; }";
  pdfHtml += "</style></head><body>";

  pdfHtml += "<div class='report-container'>";
  pdfHtml += "<div class='header'>";
  pdfHtml += "  <div class='header-left'>";
  pdfHtml += "    <h1>SHIELD LOG REPORT</h1>";
  pdfHtml += "    <div class='meta'>Secure Hardware Interface // Daily Bunk Summary</div>";
  pdfHtml += "  </div>";
  pdfHtml += "  <div class='header-right'>";
  pdfHtml += "    <div class='report-time'>GENERATED: " + String(liveNowEpoch > 0 ? formatMilitaryTime(liveNowEpoch) : "--") + "</div>";
  pdfHtml += "  </div>";
  pdfHtml += "</div>";

  pdfHtml += "<table id='reportTable'><thead><tr>";
  pdfHtml += "<th>Rank</th><th>Roll #</th><th>Student Name</th><th>Entries</th><th>Exits</th><th>Total Bunks</th><th>Total Bunk Dur.</th><th>Last Bunk Details</th>";
  pdfHtml += "</tr></thead><tbody>";

  pdfHtml += "</tbody></table>";
  pdfHtml += "<div class='footer'>Report auto-generated by SHIELD System v14.0 &bull; Official Security Record</div>";
  pdfHtml += "</div>";

  // Inject students for sorting
  pdfHtml += "<script>";
  pdfHtml += "const students = [";
  for (int i = 0; i < sizeof(students)/sizeof(students[0]); i++) {
      auto &s = students[i];
      pdfHtml += "{";
      pdfHtml += "roll: " + String(s.rollNumber) + ",";
      pdfHtml += "name: '" + String(s.name) + "',";
      pdfHtml += "entries: " + String(s.entryCount) + ",";
      pdfHtml += "exits: " + String(s.exitCount) + ",";
      pdfHtml += "bunks: " + String(s.bunkCount) + ",";
      pdfHtml += "totalDur: " + String(s.totalBunkDuration) + ",";
      pdfHtml += "lastDur: " + String(s.lastBunkDuration) + ",";
      pdfHtml += "lastExit: '" + formatMilitaryTime(s.lastExitTime) + "',";
      pdfHtml += "lastEntry: '" + formatMilitaryTime(s.lastEntryTime) + "',";
      pdfHtml += "isInside: " + String(s.isInside ? "true" : "false");
      pdfHtml += "}";
      if (i < (sizeof(students)/sizeof(students[0])) - 1) pdfHtml += ",";
  }
  pdfHtml += "];";

  pdfHtml += "function formatDur(secs) {\n";
  pdfHtml += "    if(secs === 0) return '--';\n";
  pdfHtml += "    let m = Math.floor(secs / 60);\n";
  pdfHtml += "    let s = secs % 60;\n";
  pdfHtml += "    return m + 'm ' + s + 's';\n";
  pdfHtml += "}\n";

  pdfHtml += "window.onload = function() {\n";
  pdfHtml += "    students.sort((a,b) => {\n";
  pdfHtml += "       if(b.totalDur !== a.totalDur) return b.totalDur - a.totalDur;\n";
  pdfHtml += "       return b.bunks - a.bunks;\n";
  pdfHtml += "    });\n";

  pdfHtml += "    let tbody = document.querySelector('#reportTable tbody');\n";
  pdfHtml += "    let html = '';\n";
  pdfHtml += "    let rank = 1;\n";
  pdfHtml += "    for(let s of students) {\n";
  pdfHtml += "       let rStr = s.bunks > 0 ? '#' + rank : '--';\n";
  pdfHtml += "       html += `<tr>";
  pdfHtml += "          <td class='data-mono'><b>${rStr}</b></td>";
  pdfHtml += "          <td class='rank-roll'>${String(s.roll).padStart(2, '0')}</td>";
  pdfHtml += "          <td class='student-name'>${s.name}</td>";
  pdfHtml += "          <td class='data-mono'>${s.entries}</td>";
  pdfHtml += "          <td class='data-mono'>${s.exits}</td>";
  pdfHtml += "          <td class='data-mono bunk'>${s.bunks}</td>";
  pdfHtml += "          <td class='data-mono'>${formatDur(s.totalDur)}</td>";
  pdfHtml += "          <td class='data-mono'>${formatDur(s.lastDur)}</td>";
  pdfHtml += "       </tr>`;\n";
  pdfHtml += "       if(s.bunks > 0) rank++;\n";
  pdfHtml += "    }\n";
  pdfHtml += "    tbody.innerHTML = html;\n";
  pdfHtml += "    window.print();\n";
  pdfHtml += "}\n";

  pdfHtml += "</script>";
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
  server.on("/data", handleData);
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
            s.currentBunkStart = currentTime;
          }
          if (s.currentBunkStart > 0 && (currentTime - s.currentBunkStart >= 1000)) {
            unsigned long dur = (currentTime - s.currentBunkStart) / 1000;
            s.totalBunkDuration += dur;
            s.lastBunkDuration += dur;
            s.currentBunkStart += dur * 1000; // Advance by the exact seconds added
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
        activeStudent->currentBunkStart = 0;
      } else {
        activeStudent->exitCount++;
        activeStudent->lastExitTime = getLiveEpochTime();
        activeStudent->lastBunkDuration = 0; // Reset when they go outside, ready for a new bunk

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
