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
  unsigned long firstInTime;
  unsigned long totalInsideTimeSecs;
  unsigned long lastInsideTickTimestamp;
};

Student students[] = {
  {{0x9E, 0x60, 0x02, 0x04}, "Yuvraj Singh", 0, 0, false, 0, 0, 0, "GREEN", 0, false, 0, 0, 0},
  {{0x74, 0x8A, 0xBB, 0x02}, "Yash Pratap",  0, 0, false, 0, 0, 0, "GREEN", 0, false, 0, 0, 0}
};

struct BunkRecord {
  String name;
  unsigned long outTime;
  unsigned long inTime;
  String durationStr;
  int absoluteBunkTally;
};

BunkRecord bunkLogs[100]; 
int totalLoggedBunks = 0;

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

String calculateIntervalStr(unsigned long start, unsigned long end) {
  if (start == 0 || end == 0 || end < start) return "Calculating...";
  unsigned long diff = end - start;
  return String(diff / 60) + "m " + String(diff % 60) + "s";
}

String getDashboardHTML() {
  int totalCount = sizeof(students) / sizeof(students[0]);
  
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>SHIELD Control Center</title>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Inter:wght=400;500;600;700;900&family=JetBrains+Mono:wght=400;600&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += ":root { --bg: #f8f9fa; --surface: #ffffff; --surface-alt: #f9fafb; --text-main: #111827; --text-muted: #6b7280; --border: #e5e7eb; --primary: #000000; --radius: 8px; --shadow: 0 1px 3px rgba(0,0,0,0.1); --container-bg: rgba(255, 255, 255, 0.90); }";
  html += "[data-theme='dark'] { --bg: #111827; --surface: #1f2937; --surface-alt: #374151; --text-main: #f9fafb; --text-muted: #9ca3af; --border: #374151; --primary: #ffffff; --shadow: 0 1px 3px rgba(0,0,0,0.5); --container-bg: rgba(31, 41, 55, 0.90); }";
  html += "body { margin: 0; color: var(--text-main); font-family: 'Inter', sans-serif; padding: 20px; transition: background-color 0.3s; background-image: url('https://images.unsplash.com/photo-1486406146926-c627a92ad1ab?q=80&w=2000&auto=format&fit=crop'); background-size: cover; background-position: center; background-attachment: fixed; background-color: var(--bg); }";
  html += ".container { max-width: 1200px; margin: 0 auto; background: var(--container-bg); padding: 30px; border-radius: 12px; box-shadow: 0 4px 30px var(--shadow); backdrop-filter: blur(10px); }";
  html += ".header { text-align: center; margin-bottom: 30px; border-bottom: 2px solid var(--primary); padding-bottom: 25px; position: relative; }";
  html += ".header h1 { margin: 0; font-size: 42px; font-weight: 900; text-transform: uppercase; letter-spacing: 4px; line-height: 1.1; }";
  html += ".header .full-form { font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 1.5px; color: var(--text-muted); margin-top: 6px; }";
  html += ".header .subtitle { color: var(--text-muted); font-size: 13px; font-weight: 500; margin-top: 4px; }";
  html += ".header .controls { display: flex; gap: 10px; position: absolute; right: 0; top: 10px; }";
  html += ".btn { background: var(--surface); color: var(--primary); border: 1px solid var(--border); padding: 8px 16px; border-radius: var(--radius); font-size: 13px; font-weight: 600; cursor: pointer; transition: 0.2s; box-shadow: var(--shadow); }";
  html += ".btn-primary { background: var(--primary); color: var(--bg); border-color: var(--primary); }";
  html += ".layout-grid { display: grid; grid-template-columns: 2fr 1fr; gap: 20px; margin-top: 20px; }";
  html += ".summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 20px; margin-bottom: 30px; }";
  html += ".card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); padding: 20px; box-shadow: var(--shadow); }";
  html += ".card-title { font-size: 12px; color: var(--text-muted); text-transform: uppercase; font-weight: 600; margin-bottom: 10px; }";
  html += ".card-value { font-size: 24px; font-weight: 700; font-family: 'JetBrains Mono', monospace; }";
  html += ".toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; gap: 20px; }";
  html += ".search-box { flex: 1; max-width: 400px; }";
  html += ".search-box input { width: 100%; padding: 10px 15px; border: 1px solid var(--border); background: var(--surface); color: var(--text-main); border-radius: var(--radius); font-size: 14px; box-sizing: border-box; }";
  html += ".filters { display: flex; gap: 10px; }";
  html += ".filter-btn { background: var(--surface); border: 1px solid var(--border); padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: 600; cursor: pointer; color: var(--text-muted); }";
  html += ".filter-btn.active { background: var(--primary); color: var(--bg); border-color: var(--primary); }";
  html += ".section-title { font-size: 16px; font-weight: 700; margin: 25px 0 15px 0; display: flex; align-items: center; justify-content: space-between; }";
  html += ".section-title span.badge { background: #fee2e2; color: #b91c1c; padding: 2px 8px; border-radius: 12px; font-size: 12px; }";
  html += "table { width: 100%; border-collapse: collapse; background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; box-shadow: var(--shadow); }";
  html += "th, td { padding: 12px 16px; text-align: left; border-bottom: 1px solid var(--border); font-size: 14px; }";
  html += "th { background: var(--surface-alt); font-weight: 600; color: var(--text-muted); font-size: 12px; text-transform: uppercase; }";
  html += ".mono { font-family: 'JetBrains Mono', monospace; }";
  html += ".status-indicator { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 8px; }";
  html += ".status-in { background: #10b981; }";
  html += ".status-out { background: #f59e0b; }";
  html += ".alert-badge { padding: 4px 8px; border-radius: 4px; font-size: 11px; font-weight: 700; text-transform: uppercase; }";
  html += ".alert-red { background: #fee2e2; color: #b91c1c; border: 1px solid #fca5a5; }";
  html += ".alert-yellow { background: #fef3c7; color: #d97706; border: 1px solid #fcd34d; }";
  html += ".alert-green { background: #d1fae5; color: #047857; border: 1px solid #6ee7b7; }";
  html += ".toggle-btn { background: none; border: none; color: var(--text-muted); cursor: pointer; font-size: 14px; text-decoration: underline; }";
  html += ".hidden { display: none !important; }";
  html += ".banner-box { margin-bottom: 20px; padding: 15px; border-radius: 8px; font-weight: bold; text-align: center; }";
  html += ".status-secure { background: #d1fae5; color: #065f46; border: 1px solid #34d399; }";
  html += ".status-alert { background: #fee2e2; color: #991b1b; border: 1px solid #f87171; animation: pulse 2s infinite; }";
  html += ".cctv-card { background: #0f172a; border: 1px solid #334155; border-radius: var(--radius); padding: 15px; color: #38bdf8; font-family: 'JetBrains Mono', monospace; box-shadow: var(--shadow); }";
  html += ".cctv-title { color: #94a3b8; font-size: 11px; text-transform: uppercase; font-weight: bold; border-bottom: 1px solid #334155; padding-bottom: 6px; margin-bottom: 10px; }";
  html += ".cctv-stream { background: #020617; padding: 12px; border-radius: 4px; font-size: 12px; line-height: 1.5; height: 320px; overflow-y: auto; border-left: 3px solid #38bdf8; }";
  html += ".cctv-stream.breach { border-left-color: #ef4444; }";
  html += ".log-line { border-bottom: 1px solid #1e293b; padding: 4px 0; font-size: 11px; }";
  html += ".bunk-sub-row { background: var(--surface-alt); }";
  html += ".bunk-sub-row td { padding: 10px 16px; border-left: 3px solid var(--primary); font-size: 13px; }";
  html += ".leaderboard-row { cursor: pointer; transition: background 0.2s; }";
  html += ".leaderboard-row:hover { background: var(--bg); }";
  
  html += "#bunkPrintSection, #attendancePrintSection { display: none; }";
  html += "@media print {";
  html += "  body { background: none !important; padding: 0; color: #000000; }";
  html += "  .container { display: none !important; }"; 
  html += "  .print-hdr { text-align: center; border-bottom: 3px solid #000; padding-bottom: 15px; margin-bottom: 25px; }";
  html += "  .print-hdr h1 { margin: 0; font-size: 36px; font-weight: 900; text-transform: uppercase; letter-spacing: 4px; }";
  html += "  .print-hdr .print-full-form { font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; color: #555; margin-top: 5px; }";
  html += "  .print-hdr .print-title { font-size: 16px; font-weight: 700; text-transform: uppercase; margin-top: 15px; letter-spacing: 0.5px; }";
  html += "  .print-table { width: 100%; border-collapse: collapse; margin-top: 15px; }";
  html += "  .print-table th { background: #f2f2f2 !important; color: #000 !important; font-weight: bold; border: 1px solid #000; padding: 8px; font-size: 11px; text-transform: uppercase; }";
  html += "  .print-table td { border: 1px solid #000; padding: 8px; font-size: 11px; text-align: left; }";
  html += "  .print-mono { font-family: 'JetBrains Mono', monospace; font-weight: 600; }";
  html += "}";
  html += "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.8; } 100% { opacity: 1; } }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "  <div class='header'>";
  html += "    <h1>SHIELD</h1>";
  html += "    <div class='full-form'>Strategic Human Incident Evaluation & Logistics Database</div>";
  html += "    <div class='subtitle'>Daily Attendance & Bunk Monitor</div>";
  html += "    <div class='controls'><button id='themeToggleBtn' class='btn'>Toggle Theme</button><button onclick='printAttendanceReport()' class='btn btn-primary' style='background:#000; color:#fff;'>Print Attendance PDF</button><button onclick='printBunkerReport()' class='btn btn-primary'>Print Bunkers PDF</button></div>";
  html += "  </div>";
  html += "  <div id='statusBanner' class='banner-box status-secure'>SYSTEM INITIALIZING...</div>";
  html += "  <div class='summary-grid'>";
  html += "    <div class='card'><div class='card-title'>System Time</div><div class='card-value' id='sysTimeDisplay'>--:--</div></div>";
  html += "    <div class='card'><div class='card-title'>Total Students</div><div class='card-value' id='statTotal'>0</div></div>";
  html += "    <div class='card'><div class='card-title'>Currently Inside</div><div class='card-value' id='statInside'>0</div></div>";
  html += "    <div class='card'><div class='card-title'>Active Bunks</div><div class='card-value' style='color:#b91c1c;' id='statBunks'>0</div></div>";
  html += "  </div>";
  html += "  <div class='toolbar'>";
  html += "    <div class='search-box'><input type='text' id='searchInput' placeholder='Search by Name...'></div>";
  html += "    <div class='filters'><button class='filter-btn active' id='fAll'>All</button><button class='filter-btn' id='fIn'>Inside</button><button class='filter-btn' id='fOut'>Outside</button></div>";
  html += "  </div>";
  html += "  <div class='layout-grid'>";
  html += "    <div>"; 
  html += "      <div class='section-title'>Active Cases / Leaderboard <span class='badge' id='badgeActiveCount'>0 Active</span></div>";
  html += "      <table><thead><tr><th>Roll #</th><th>Student Name</th><th>Location</th><th>Out Since</th><th>Current Dur.</th><th>Total Bunks</th><th>Alert Status</th></tr></thead><tbody id='activeTableBody'></tbody></table>";
  html += "      <div class='section-title'>Daily Bunk Leaderboard</div>";
  html += "      <table><thead><tr><th>Rank</th><th>Student Name</th><th>Total Bunks</th><th>Total Duration</th></tr></thead><tbody id='bunkLeaderboardBody'></tbody></table>";
  html += "      <div class='section-title'>All Students <button class='toggle-btn' id='toggleAllBtn'>Show</button></div>";
  html += "      <table id='allTable' class='hidden'><thead><tr><th>Roll #</th><th>Student Name</th><th>Location</th><th>IN</th><th>OUT</th><th>Bunks</th><th>Alert Status</th></tr></thead><tbody id='allTableBody'>";
  
  for (int i = 0; i < totalCount; i++) {
    html += "<tr id='all-row-" + String(i) + "' data-name='" + String(students[i].name) + "'>";
    html += "  <td>" + String(i + 1) + "</td>";
    html += "  <td style='font-weight:600;'>" + String(students[i].name) + "</td>";
    html += "  <td class='loc-cell'>--</td>";
    html += "  <td class='mono in-cell'>--</td>";
    html += "  <td class='mono out-cell'>--</td>";
    html += "  <td class='mono bunk-cell'>0</td>";
    html += "  <td><span class='alert-badge alert-green alert-cell'>GREEN</span></td>";
    html += "</tr>";
  }
  html += "      </tbody></table>";
  html += "    </div>";
  html += "    <div>"; 
  html += "      <div class='section-title'>CCTV LOG DATA</div>";
  html += "      <div class='cctv-card'><div class='cctv-title'>[CAM_01_CORE_FEED]</div><div id='cctvTerminal' class='cctv-stream'></div></div>";
  html += "    </div>";
  html += "  </div>";
  html += "</div>";

  html += "<div id='bunkPrintSection'>";
  html += "  <div class='print-hdr'><h1>SHIELD</h1><div class='print-full-form'>Strategic Human Incident Evaluation & Logistics Database</div><div class='print-title'>Granular Time-Series Bunk Incident Ledger</div><div style='font-size:11px; margin-top:5px;'>Compilation System Time: <span id='printTimeField'>--:--</span></div></div>";
  html += "  <table class='print-table'><thead><tr><th>S.No.</th><th>Name of Bunker</th><th>Total Bunks</th><th>Bunk Details</th></tr></thead><tbody id='printTableBody'></tbody></table>";
  html += "</div>";

  html += "<div id='attendancePrintSection'>";
  html += "  <div class='print-hdr'><h1>SHIELD</h1><div class='print-full-form'>Strategic Human Incident Evaluation & Logistics Database</div><div class='print-title'>Master Roll-Call & Operational Attendance Sheet</div><div style='font-size:11px; margin-top:5px;'>Compilation System Time: <span id='printAttTimeField'>--:--</span></div></div>";
  html += "  <table class='print-table'><thead><tr><th>S.No.</th><th>Student Name</th><th>Roll No.</th><th>Status</th><th>First In Time</th><th>Total Active Hours</th></tr></thead><tbody id='printAttTableBody'></tbody></table>";
  html += "</div>";

  html += "<script>";
  html += "window.activeFilter = 'all'; let lastStateStr = ''; let lastFetchedData = null;"; 
  html += "const themeToggleBtn = document.getElementById('themeToggleBtn');";
  html += "if (localStorage.getItem('theme') == 'dark') document.documentElement.setAttribute('data-theme', 'dark');";
  html += "themeToggleBtn.addEventListener('click', () => { let theme = document.documentElement.getAttribute('data-theme') == 'dark' ? 'light' : 'dark'; document.documentElement.setAttribute('data-theme', theme); localStorage.setItem('theme', theme); });";
  
  html += "document.getElementById('toggleAllBtn').addEventListener('click', (e) => { const table = document.getElementById('allTable'); if(table.classList.contains('hidden')) { table.classList.remove('hidden'); e.target.innerText = 'Hide'; } else { table.classList.add('hidden'); e.target.innerText = 'Show'; } });";

  html += "function pushLog(time, msg, isErr) { const term = document.getElementById('cctvTerminal'); term.innerHTML += `<div class='log-line ${isErr?\"log-err\":\"\"}'>[${time}] ${msg}</div>`; term.scrollTop = term.scrollHeight; }";

  html += "async function printBunkerReport() {";
  html += "  try {";
  html += "    document.getElementById('attendancePrintSection').style.display = 'none';";
  html += "    document.getElementById('bunkPrintSection').style.display = 'block';";
  html += "    let res = await fetch('/bunklogs'); let logs = await res.json();";
  html += "    document.getElementById('printTimeField').innerText = document.getElementById('sysTimeDisplay').innerText;";
  html += "    let grouped = {}; logs.forEach(log => { if(!grouped[log.name]) grouped[log.name] = []; grouped[log.name].push(log); });";
  html += "    let pBody = ''; let idx = 1; Object.keys(grouped).forEach(name => {";
  html += "      let gBunks = grouped[name];";
  html += "      let detailsHtml = gBunks.map((b, i) => `<div><b>Bunk no. ${i+1}:</b> Out: ${b.out} | In: ${b.in} | Dur: ${b.duration}</div>`).join('');";
  html += "      pBody += `<tr><td>${idx++}</td><td style='font-weight:bold;'>${name}</td><td class='print-mono'>${gBunks.length}</td><td class='print-mono'>${detailsHtml}</td></tr>`;";
  html += "    });";
  html += "    if(logs.length == 0) pBody = '<tr><td colspan=\"4\" style=\"text-align:center; padding:15px;\">No historic bunk incidents recorded.</td></tr>';";
  html += "    document.getElementById('printTableBody').innerHTML = pBody; window.print();";
  html += "  } catch(e) { console.error(e); }";
  html += "}";

  html += "function printAttendanceReport() {";
  html += "  document.getElementById('bunkPrintSection').style.display = 'none';";
  html += "  document.getElementById('attendancePrintSection').style.display = 'block';";
  html += "  document.getElementById('printAttTimeField').innerText = document.getElementById('sysTimeDisplay').innerText;";
  html += "  if(!lastFetchedData) return;";
  html += "  let pBody = '';";
  html += "  lastFetchedData.students.forEach((s, idx) => {";
  html += "    pBody += `<tr><td>${idx + 1}</td><td style='font-weight:bold;'>${s.name}</td><td class='print-mono'>${s.roll.toString().padStart(2,'0')}</td><td style='font-weight:600; color:${s.isInside?\"#10b981\":\"#f59e0b\"};'>${s.isInside?\"INSIDE\":\"OUTSIDE\"}</td><td class='print-mono'>${s.firstIn}</td><td class='print-mono'>${s.totalHours}</td></tr>`;";
  html += "  });";
  html += "  document.getElementById('printAttTableBody').innerHTML = pBody; window.print();";
  html += "}";

  html += "async function updateDashboard() {";
  html += "  try {";
  html += "    let [resData, resLogs] = await Promise.all([fetch('/data'), fetch('/bunklogs')]);";
  html += "    let data = await resData.json(); let logs = await resLogs.json(); lastFetchedData = data;";
  html += "    window.latestBunkLogs = logs;";
  html += "    let banner = document.getElementById('statusBanner'); let cctv = document.getElementById('cctvTerminal');";
  html += "    let currentStateStr = data.tailgating + '-' + data.breach + '-' + data.doorOpen;";
  html += "    if(currentStateStr !== lastStateStr) {";
  html += "      if(data.tailgating) pushLog(data.sysTime, 'CRITICAL: TAILGATING DETECTED', true);";
  html += "      else if(data.breach) pushLog(data.sysTime, 'CRITICAL: SECURITY BREACH DETECTED', true);";
  html += "      else pushLog(data.sysTime, 'Portal status secure.', false); lastStateStr = currentStateStr;";
  html += "    }";
  html += "    if (data.tailgating) { banner.className = 'banner-box status-alert'; banner.innerText = '🚨 PORTAL CRITICAL: TAILGATING DETECTED'; }";
  html += "    else if (data.breach) { banner.className = 'banner-box status-alert'; banner.innerText = '🚨 PORTAL CRITICAL: SECURITY BREACH'; }";
  html += "    else { banner.className = 'banner-box status-secure'; banner.innerText = 'SYSTEM SECURE // PORTAL MONITOR: LOCKED'; }";
  
  html += "    document.getElementById('sysTimeDisplay').innerText = data.sysTime;";
  html += "    document.getElementById('statTotal').innerText = data.total; document.getElementById('statInside').innerText = data.inside; document.getElementById('statBunks').innerText = data.bunksCount; document.getElementById('badgeActiveCount').innerText = data.bunksCount + ' Active';";
  
  html += "    let query = document.getElementById('searchInput').value.toLowerCase(); let activeHTML = ''; let activeCount = 0;";
  html += "    data.students.forEach((s, idx) => {";
  html += "      let allRow = document.getElementById('all-row-' + idx);";
  html += "      if(allRow) {";
  html += "        allRow.querySelector('.loc-cell').innerHTML = `<span class='status-indicator ${s.isInside ? 'status-in':'status-out'}'></span>${s.isInside ? 'Inside':'Outside'}`;";
  html += "        allRow.querySelector('.in-cell').innerText = s.lastEntry; allRow.querySelector('.out-cell').innerText = s.lastExit; allRow.querySelector('.bunk-cell').innerText = s.bunkCount;";
  html += "        let ab = allRow.querySelector('.alert-cell'); ab.innerText = s.alert; ab.className = `alert-badge ${s.alert=='RED'?'alert-red':(s.alert=='YELLOW'?'alert-yellow':'alert-green')} alert-cell`;";
  html += "        if(s.name.toLowerCase().includes(query) && (window.activeFilter=='all' || (window.activeFilter=='inside'&&s.isInside) || (window.activeFilter=='outside'&&!s.isInside))) allRow.style.display = ''; else allRow.style.display = 'none';";
  html += "      }";
  html += "      if (!s.isInside && (s.alert=='RED' || s.alert=='YELLOW')) {";
  html += "        activeCount++; let alertBadge = s.alert=='RED'?'alert-red':'alert-yellow';";
  html += "        activeHTML += `<tr><td>${s.roll}</td><td style='font-weight:600;'>${s.name}</td><td><span class='status-indicator status-out'></span>Outside</td><td class='mono'>${s.lastExit}</td><td class='mono' style='color:#b91c1c; font-weight:600;'>${s.duration}</td><td class='mono'>${s.bunkCount}</td><td><span class='alert-badge ${alertBadge}'>${s.alert}</span></td></tr>`;";
  html += "      }";
  html += "    });";
  html += "    if(activeCount == 0) activeHTML = '<tr><td colspan=\"7\" style=\"text-align:center; color:#6b7280; padding:20px;\">No active cases. All clear.</td></tr>';";
  html += "    document.getElementById('activeTableBody').innerHTML = activeHTML;";
  html += "    renderLeaderboard(logs);";
  html += "  } catch(e) { console.error(e); }";
  html += "}";
  html += "window.expandedRows = window.expandedRows || new Set();";
  html += "window.toggleBunkDetails = function(name) { if(window.expandedRows.has(name)) window.expandedRows.delete(name); else window.expandedRows.add(name); renderLeaderboard(window.latestBunkLogs); };";
  html += "function formatDur(secs) { if(secs==0) return '--'; return Math.floor(secs/60) + 'm ' + (secs%60) + 's'; }";

  html += "function renderLeaderboard(logs) {";
  html += "  if(!logs || logs.length==0) { document.getElementById('bunkLeaderboardBody').innerHTML = '<tr><td colspan=\"4\" style=\"text-align:center; color:#6b7280; padding:20px;\">No bunks recorded today.</td></tr>'; return; }";
  html += "  let allBunks = [...logs].sort((a,b) => b.durationSecs - a.durationSecs);";
  html += "  allBunks.forEach((b, i) => b.classRank = i + 1);";
  html += "  let grouped = {};";
  html += "  logs.forEach(log => {";
  html += "    if(!grouped[log.name]) grouped[log.name] = { name: log.name, bunks: [], totalDur: 0 };";
  html += "    grouped[log.name].bunks.push(log);";
  html += "    grouped[log.name].totalDur += log.durationSecs;";
  html += "  });";
  html += "  let studentsArr = Object.values(grouped).sort((a,b) => b.totalDur - a.totalDur);";
  html += "  let lHtml = '';";
  html += "  studentsArr.forEach((s, i) => {";
  html += "    let isExpanded = window.expandedRows.has(s.name);";
  html += "    lHtml += `<tr class='leaderboard-row' onclick='window.toggleBunkDetails(\"${s.name}\")'><td>#${i+1}</td><td style='font-weight:600;'>${s.name} ${isExpanded?'▼':'▶'}</td><td class='mono'>${s.bunks.length}</td><td class='mono' style='font-weight:bold; color:#b91c1c;'>${formatDur(s.totalDur)}</td></tr>`;";
  html += "    if(isExpanded) {";
  html += "      lHtml += `<tr class='bunk-sub-row'><td colspan='4'><div style='margin-bottom:8px; font-weight:bold; font-size:12px; color:var(--text-muted);'>DETAILS:</div><table style='box-shadow:none; border:none; margin:0;'><thead><tr><th>Bunk No.</th><th>Out Time</th><th>In Time</th><th>Duration</th><th>Class Rank</th></tr></thead><tbody>`;";
  html += "      s.bunks.forEach((b, j) => {";
  html += "        lHtml += `<tr><td>#${j+1}</td><td class='mono'>${b.out}</td><td class='mono'>${b.in}</td><td class='mono'>${formatDur(b.durationSecs)}</td><td><b>#${b.classRank}</b></td></tr>`;";
  html += "      });";
  html += "      lHtml += `</tbody></table></td></tr>`;";
  html += "    }";
  html += "  });";
  html += "  document.getElementById('bunkLeaderboardBody').innerHTML = lHtml;";
  html += "}";

  html += "const fAll = document.getElementById('fAll'), fIn = document.getElementById('fIn'), fOut = document.getElementById('fOut');";
  html += "const setFilter = (btn, val) => { [fAll, fIn, fOut].forEach(b=>b.classList.remove('active')); btn.classList.add('active'); window.activeFilter = val; updateDashboard(); };";
  html += "fAll.addEventListener('click', () => setFilter(fAll, 'all')); fIn.addEventListener('click', () => setFilter(fIn, 'inside')); fOut.addEventListener('click', () => setFilter(fOut, 'outside'));";
  html += "document.getElementById('searchInput').addEventListener('input', updateDashboard); setInterval(updateDashboard, 1000);";
  html += "window.onload = function() { if (" + String(initialEpochSeconds) + " === 0) { let d = new Date(); fetch('/sync?t=' + ((d.getHours() * 3600) + (d.getMinutes() * 60) + d.getSeconds())); } updateDashboard(); };";
  html += "</script></body></html>";
  return html;
}

void handleData() {
  unsigned long currentMillis = millis();
  int totalCount = sizeof(students) / sizeof(students[0]);
  int insideCount = 0; int activeBunksCount = 0;
  for (auto &s : students) { if (s.isInside) insideCount++; if (s.colorAlert == "RED" || s.colorAlert == "YELLOW") activeBunksCount++; }

  String json = "{";
  json += "\"sysTime\":\"" + formatMilitaryTime(getLiveEpochTime()) + "\",";
  json += "\"total\":" + String(totalCount) + ",";
  json += "\"inside\":" + String(insideCount) + ",";
  json += "\"bunksCount\":" + String(activeBunksCount) + ",";
  json += "\"tailgating\":" + String(tailgatingAlert ? "true" : "false") + ",";
  json += "\"breach\":" + String(physicalBreachAlert ? "true" : "false") + ",";
  json += "\"doorOpen\":" + String(digitalRead(HALL_PIN) ? "true" : "false") + ",";
  json += "\"students\":[";
  
  for (int i = 0; i < totalCount; i++) {
    unsigned long durationSecs = (students[i].stateChangeTimestamp > 0) ? (currentMillis - students[i].stateChangeTimestamp) / 1000 : 0;
    String durStr = String(durationSecs / 60) + "m " + String(durationSecs % 60) + "s";
    
    unsigned long runningInsideSecs = students[i].totalInsideTimeSecs;
    if (students[i].isInside && students[i].lastInsideTickTimestamp > 0) {
      runningInsideSecs += (currentMillis - students[i].lastInsideTickTimestamp) / 1000;
    }
    String totalInsideStr = String(runningInsideSecs / 3600) + "h " + String((runningInsideSecs % 3600) / 60) + "m";

    json += "{";
    json += "\"roll\":" + String(i + 1) + ",";
    json += "\"name\":\"" + String(students[i].name) + "\",";
    json += "\"isInside\":" + String(students[i].isInside ? "true" : "false") + ",";
    json += "\"lastEntry\":\"" + formatMilitaryTime(students[i].lastEntryTime) + "\",";
    json += "\"lastExit\":\"" + formatMilitaryTime(students[i].lastExitTime) + "\",";
    json += "\"firstIn\":\"" + formatMilitaryTime(students[i].firstInTime) + "\",";
    json += "\"totalHours\":\"" + totalInsideStr + "\",";
    json += "\"duration\":\"" + durStr + "\",";
    json += "\"bunkCount\":" + String(students[i].bunkCount) + ",";
    json += "\"alert\":\"" + students[i].colorAlert + "\"";
    json += "}";
    if (i < totalCount - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleBunkLogs() {
  String json = "[";
  for(int i = 0; i < totalLoggedBunks; i++) {
    json += "{";
    json += "\"name\":\"" + bunkLogs[i].name + "\",";
    json += "\"out\":\"" + formatMilitaryTime(bunkLogs[i].outTime) + "\",";
    json += "\"in\":\"" + formatMilitaryTime(bunkLogs[i].inTime) + "\",";
    unsigned long endT = (bunkLogs[i].inTime == 0) ? getLiveEpochTime() : bunkLogs[i].inTime;
    unsigned long dSecs = (endT > bunkLogs[i].outTime) ? (endT - bunkLogs[i].outTime) : 0;
    json += "\"tally\":" + String(bunkLogs[i].absoluteBunkTally) + ",";
    json += "\"duration\":\"" + bunkLogs[i].durationStr + "\",";
    json += "\"durationSecs\":" + String(dSecs);
    json += "}";
    if(i < totalLoggedBunks - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleRoot() { server.send(200, "text/html", getDashboardHTML()); }
void handleSync() { if (server.hasArg("t")) { initialEpochSeconds = server.arg("t").toInt(); epochSyncSystemTicks = millis(); } server.send(200, "text/plain", "OK"); }

void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT); pinMode(HALL_PIN, INPUT);
  SPI.begin(); rfid.PCD_Init();
  WiFi.softAPConfig(local_ip, gateway, subnet); WiFi.softAP(ssid, password);
  server.on("/", handleRoot); server.on("/data", handleData); server.on("/bunklogs", handleBunkLogs); server.on("/sync", handleSync);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long currentTime = millis();
  bool currentIR = digitalRead(IR_PIN); bool currentHall = digitalRead(HALL_PIN); 

  if (currentTime - lastNetworkCheck >= 500) {
    lastNetworkCheck = currentTime;
    for (auto &s : students) {
      if (s.isInside) {
        if (s.lastInsideTickTimestamp > 0) {
          s.totalInsideTimeSecs += (currentTime - s.lastInsideTickTimestamp) / 1000;
        }
        s.lastInsideTickTimestamp = currentTime;
      }
      if (s.stateChangeTimestamp > 0 && !s.isInside) { 
        unsigned long runningOutsideTime = (currentTime - s.stateChangeTimestamp) / 1000;
        if (runningOutsideTime >= 20) {
          s.colorAlert = "RED";
          if (!s.bunkLoggedForCurrentCycle) {
            s.bunkCount++; s.bunkLoggedForCurrentCycle = true; 
            if (totalLoggedBunks < 100) {
              bunkLogs[totalLoggedBunks] = {String(s.name), s.lastExitTime, 0, "Active...", s.bunkCount};
              totalLoggedBunks++;
            }
          }
        } else if (runningOutsideTime >= 10) { s.colorAlert = "YELLOW"; }
      }
    }
  }

  if (((currentHall && !lastHallState) || (currentIR && !lastIRState)) && !windowActive) {
    windowActive = true; windowStartTime = currentTime; RFID_Scan_Count = 0; IR_Trip_Count = 0; activeStudent = nullptr;
  }
  if (windowActive && (currentIR && !lastIRState)) IR_Trip_Count++;

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Student* s = nullptr;
    for (auto &st : students) {
      bool match = true; for (int i = 0; i < 4; i++) { if (rfid.uid.uidByte[i] != st.uid[i]) match = false; }
      if (match) s = &st;
    }
    if (s) {
      if (!windowActive) { windowActive = true; windowStartTime = currentTime; RFID_Scan_Count = 0; IR_Trip_Count = 0; }
      RFID_Scan_Count++; activeStudent = s; physicalBreachAlert = false;
    }
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
  }

  if (windowActive && (currentTime - windowStartTime >= FUSION_WINDOW_MS)) {
    if (RFID_Scan_Count == 0) { physicalBreachAlert = true; tailgatingAlert = false; } 
    else if (activeStudent != nullptr) {
      tailgatingAlert = (IR_Trip_Count >= 2); physicalBreachAlert = false;
      activeStudent->stateChangeTimestamp = currentTime; 
      activeStudent->isInside = !activeStudent->isInside;
      activeStudent->colorAlert = "GREEN"; 
      
      if (activeStudent->isInside) {
        if(activeStudent->firstInTime == 0) activeStudent->firstInTime = getLiveEpochTime();
        activeStudent->entryCount++; activeStudent->lastEntryTime = getLiveEpochTime();
        activeStudent->lastInsideTickTimestamp = currentTime;
        activeStudent->bunkLoggedForCurrentCycle = false; 
        for(int k = totalLoggedBunks - 1; k >= 0; k--) {
          if(bunkLogs[k].name == String(activeStudent->name) && bunkLogs[k].inTime == 0) {
            bunkLogs[k].inTime = activeStudent->lastEntryTime;
            bunkLogs[k].durationStr = calculateIntervalStr(bunkLogs[k].outTime, bunkLogs[k].inTime); break;
          }
        }
      } else {
        activeStudent->exitCount++; activeStudent->lastExitTime = getLiveEpochTime();
        if (activeStudent->lastInsideTickTimestamp > 0) {
          activeStudent->totalInsideTimeSecs += (currentTime - activeStudent->lastInsideTickTimestamp) / 1000;
        }
        activeStudent->lastInsideTickTimestamp = 0;
      }
    }
    windowActive = false; activeStudent = nullptr;
  }
  if (!currentHall && !currentIR && (physicalBreachAlert || tailgatingAlert) && (currentTime - windowStartTime > 4000)) {
     physicalBreachAlert = false; tailgatingAlert = false;
  }
  lastIRState = currentIR; lastHallState = currentHall;
}
