#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// ---------------- Wi-Fi & Time Settings ----------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// India Standard Time (UTC+5:30)
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 5 * 3600 + 30 * 60;
const int   DAYLIGHT_OFFSET_SEC = 0;

// ---------------- Hardware Pins ----------------
const int MAT_PIN    = 27;  // Mat input with 10k ohm pull-down to GND
const int BUZZER_PIN = 26;  // Alert buzzer pin

// ---------------- Logging & Debounce ----------------
struct EventLog {
  int id;
  String timestamp;
  String dateStr;
  String timeStr;
  String mode;
  bool alarmTriggered;
};

const int MAX_LOGS = 100;
EventLog logs[MAX_LOGS];
int logCount = 0;
int totalEvents = 0;
int alarmCount = 0;

int lastMatState = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 250;

WebServer server(80);

// Returns true between 18:30:00 (6:30 PM) and 08:00:00 (next morning).
// Returns false between 08:00:00 and 18:29:59 (covering 8:00 AM to 6:29:55 PM).
bool isAlarmWindowActive(int hour, int minute, int second) {
  int secOfDay = hour * 3600 + minute * 60 + second;
  const int NIGHT_START = 18 * 3600 + 30 * 60;      // 18:30:00 (6:30 PM)
  const int MORNING_END = 8 * 3600;                 // 08:00:00 (8:00 AM)

  if (secOfDay >= NIGHT_START || secOfDay < MORNING_END) {
    return true;
  }
  return false;
}

String twoDigits(int n) {
  return (n < 10) ? ("0" + String(n)) : String(n);
}

void getFormattedTime(String& fullTs, String& datePart, String& timePart, int& hr, int& mn, int& sc) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 200)) {
    unsigned long secSinceBoot = millis() / 1000;
    hr = (secSinceBoot / 3600) % 24;
    mn = (secSinceBoot / 60) % 60;
    sc = secSinceBoot % 60;
    datePart = "Unsynced";
    timePart = twoDigits(hr) + ":" + twoDigits(mn) + ":" + twoDigits(sc);
    fullTs = datePart + " " + timePart;
    return;
  }

  hr = timeinfo.tm_hour;
  mn = timeinfo.tm_min;
  sc = timeinfo.tm_sec;

  char dBuf[16];
  char tBuf[16];
  strftime(dBuf, sizeof(dBuf), "%Y-%m-%d", &timeinfo);
  strftime(tBuf, sizeof(tBuf), "%H:%M:%S", &timeinfo);

  datePart = String(dBuf);
  timePart = String(tBuf);
  fullTs = datePart + " " + timePart;
}

void soundAlarmPulse() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(140);
    digitalWrite(BUZZER_PIN, LOW);
    delay(90);
  }
}

void recordMovementEvent() {
  String fullTs, datePart, timePart;
  int hr, mn, sc;
  getFormattedTime(fullTs, datePart, timePart, hr, mn, sc);

  bool alarmActive = isAlarmWindowActive(hr, mn, sc);
  totalEvents++;
  if (alarmActive) {
    alarmCount++;
  }

  EventLog entry;
  entry.id = totalEvents;
  entry.timestamp = fullTs;
  entry.dateStr = datePart;
  entry.timeStr = timePart;
  entry.mode = alarmActive ? "ALERT WINDOW (6:30 PM - 8:00 AM)" : "SILENT LOG (8:00 AM - 6:29:55 PM)";
  entry.alarmTriggered = alarmActive;

  // Shift newest event to index 0
  int limit = (logCount < MAX_LOGS) ? logCount : (MAX_LOGS - 1);
  for (int i = limit; i > 0; i--) {
    logs[i] = logs[i - 1];
  }
  logs[0] = entry;
  if (logCount < MAX_LOGS) {
    logCount++;
  }

  Serial.printf("[#%d] %s | %s | Alarm: %s\n",
                entry.id,
                entry.timestamp.c_str(),
                entry.mode.c_str(),
                entry.alarmTriggered ? "ON" : "OFF");

  if (alarmActive) {
    soundAlarmPulse();
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

String jsonEscape(const String& s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out += c;
  }
  return out;
}

const char PRESSENSE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>PRESSENSE — Anti-Theft Mat Monitor</title>
<style>
  :root {
    --bg: #FFFFFF;
    --surface: #F7F7F7;
    --ink: #111111;
    --muted: #555555;
    --border: #E5E5E5;
    --radius: 4px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Inter, sans-serif;
    background: var(--bg);
    color: var(--ink);
    line-height: 1.45;
  }
  .app {
    display: grid;
    grid-template-columns: 240px 1fr;
    min-height: 100vh;
  }
  aside {
    background: var(--surface);
    border-right: 1px solid var(--border);
    padding: 20px 16px;
    display: flex;
    flex-direction: column;
    gap: 24px;
  }
  .brand {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.14em;
    padding: 4px 8px;
    border-left: 3px solid var(--ink);
  }
  .nav-group { display: flex; flex-direction: column; gap: 4px; }
  .nav-item {
    padding: 9px 12px;
    border-radius: var(--radius);
    font-size: 13px;
    color: var(--muted);
    text-decoration: none;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .nav-item.active {
    background: var(--ink);
    color: var(--bg);
    font-weight: 600;
  }
  .side-meta {
    margin-top: auto;
    border-top: 1px solid var(--border);
    padding-top: 14px;
    font-size: 12px;
    color: var(--muted);
  }
  .main-wrap {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  header {
    height: 60px;
    border-bottom: 1px solid var(--border);
    padding: 0 24px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    background: var(--bg);
  }
  .header-title {
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0.04em;
  }
  .toolbar {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  input[type="search"] {
    height: 34px;
    padding: 0 12px;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg);
    color: var(--ink);
    font-size: 13px;
    width: 210px;
    outline: none;
  }
  input[type="search"]:focus { border-color: var(--ink); }
  button {
    height: 34px;
    padding: 0 14px;
    border-radius: var(--radius);
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    border: 1px solid var(--ink);
    background: var(--ink);
    color: var(--bg);
  }
  button.secondary {
    background: var(--bg);
    color: var(--ink);
    border-color: var(--border);
  }
  button.secondary:hover { border-color: var(--ink); }
  main {
    padding: 24px;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .kpi-grid {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 14px;
  }
  .card {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
  }
  .kpi-label {
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--muted);
    margin-bottom: 8px;
  }
  .kpi-value {
    font-size: 24px;
    font-weight: 700;
    font-variant-numeric: tabular-nums;
  }
  .kpi-sub {
    margin-top: 6px;
    font-size: 12px;
    color: var(--muted);
  }
  .section-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
  }
  .section-title {
    font-size: 13px;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
  }
  .table-wrap {
    width: 100%;
    overflow-x: auto;
    border: 1px solid var(--border);
    border-radius: var(--radius);
  }
  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
    text-align: left;
  }
  th, td {
    padding: 11px 14px;
    border-bottom: 1px solid var(--border);
    font-variant-numeric: tabular-nums;
  }
  th {
    background: var(--surface);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--muted);
    font-weight: 600;
  }
  tr:last-child td { border-bottom: none; }
  .pill {
    display: inline-block;
    padding: 2px 8px;
    border-radius: var(--radius);
    font-size: 11px;
    font-weight: 600;
    border: 1px solid var(--ink);
  }
  .pill.alert-on {
    background: var(--ink);
    color: var(--bg);
  }
  .pill.alert-off {
    background: var(--surface);
    color: var(--muted);
    border-color: var(--border);
  }
  @media (max-width: 900px) {
    .app { grid-template-columns: 1fr; }
    aside { display: none; }
    .kpi-grid { grid-template-columns: repeat(2, 1fr); }
  }
</style>
</head>
<body>
<div class="app">
  <aside>
    <div class="brand">PRESSENSE</div>
    <div class="nav-group">
      <a class="nav-item active" href="#logs">Movement Logs</a>
      <a class="nav-item" href="#schedule">Schedule Rule</a>
    </div>
    <div class="side-meta" id="schedule">
      <div><strong>Alert Window:</strong> 18:30 – 08:00</div>
      <div><strong>Silent Log:</strong> 08:00 – 18:29:55</div>
      <div style="margin-top:6px;">GPIO 27 (10kΩ) · GPIO 26</div>
    </div>
  </aside>

  <div class="main-wrap">
    <header>
      <div class="header-title">PRESSENSE — MAT MOVEMENT &amp; ALERT LOG</div>
      <div class="toolbar">
        <input id="searchInput" type="search" placeholder="Filter time or status..." oninput="renderTable()" />
        <button class="secondary" onclick="fetchStatus()">Refresh</button>
        <button onclick="clearLogs()">Clear Logs</button>
      </div>
    </header>

    <main>
      <section class="kpi-grid">
        <div class="card">
          <div class="kpi-label">Device Clock</div>
          <div class="kpi-value" id="clockVal">--:--:--</div>
          <div class="kpi-sub" id="dateVal">Syncing...</div>
        </div>
        <div class="card">
          <div class="kpi-label">Alarm Window Status</div>
          <div class="kpi-value" id="windowVal">--</div>
          <div class="kpi-sub">Active 6:30 PM to 8:00 AM</div>
        </div>
        <div class="card">
          <div class="kpi-label">Total Movements</div>
          <div class="kpi-value" id="totalVal">0</div>
          <div class="kpi-sub">Every touch recorded</div>
        </div>
        <div class="card">
          <div class="kpi-label">Alarm Triggers</div>
          <div class="kpi-value" id="alarmVal">0</div>
          <div class="kpi-sub">Nighttime alerts sounded</div>
        </div>
      </section>

      <section class="card" id="logs">
        <div class="section-head">
          <div class="section-title">Recorded Mat Movements</div>
          <div class="kpi-sub" id="shownCount">0 entries</div>
        </div>
        <div class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>#</th>
                <th>Date</th>
                <th>Time</th>
                <th>Window Rule</th>
                <th>Buzzer</th>
              </tr>
            </thead>
            <tbody id="logBody">
              <tr><td colspan="5">No movements recorded yet.</td></tr>
            </tbody>
          </table>
        </div>
      </section>
    </main>
  </div>
</div>

<script>
let latestLogs = [];

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    document.getElementById('clockVal').textContent = data.time;
    document.getElementById('dateVal').textContent = data.date;
    document.getElementById('windowVal').textContent = data.alarmWindow ? 'ARMED (ON)' : 'SILENT (OFF)';
    document.getElementById('totalVal').textContent = data.totalEvents;
    document.getElementById('alarmVal').textContent = data.alarmCount;
    latestLogs = data.logs || [];
    renderTable();
  } catch (e) {
    document.getElementById('dateVal').textContent = 'Connection lost';
  }
}

function renderTable() {
  const q = document.getElementById('searchInput').value.trim().toLowerCase();
  const tbody = document.getElementById('logBody');
  const filtered = latestLogs.filter(item =>
    !q ||
    item.date.toLowerCase().includes(q) ||
    item.time.toLowerCase().includes(q) ||
    item.mode.toLowerCase().includes(q) ||
    (item.alarm ? 'on' : 'off').includes(q)
  );

  document.getElementById('shownCount').textContent = filtered.length + ' entries';

  if (!filtered.length) {
    tbody.innerHTML = '<tr><td colspan="5">No matching movements found.</td></tr>';
    return;
  }

  tbody.innerHTML = filtered.map(item => `
    <tr>
      <td>${item.id}</td>
      <td>${item.date}</td>
      <td><strong>${item.time}</strong></td>
      <td>${item.mode}</td>
      <td><span class="pill ${item.alarm ? 'alert-on' : 'alert-off'}">${item.alarm ? 'ALERT ON' : 'OFF'}</span></td>
    </tr>
  `).join('');
}

async function clearLogs() {
  await fetch('/api/clear', { method: 'POST' });
  fetchStatus();
}

fetchStatus();
setInterval(fetchStatus, 1500);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", PRESSENSE_HTML);
}

void handleStatus() {
  String fullTs, datePart, timePart;
  int hr, mn, sc;
  getFormattedTime(fullTs, datePart, timePart, hr, mn, sc);
  bool windowActive = isAlarmWindowActive(hr, mn, sc);

  String json = "{";
  json += "\"date\":\"" + jsonEscape(datePart) + "\",";
  json += "\"time\":\"" + jsonEscape(timePart) + "\",";
  json += "\"alarmWindow\":" + String(windowActive ? "true" : "false") + ",";
  json += "\"totalEvents\":" + String(totalEvents) + ",";
  json += "\"alarmCount\":" + String(alarmCount) + ",";
  json += "\"logs\":[";

  for (int i = 0; i < logCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"id\":" + String(logs[i].id) + ",";
    json += "\"date\":\"" + jsonEscape(logs[i].dateStr) + "\",";
    json += "\"time\":\"" + jsonEscape(logs[i].timeStr) + "\",";
    json += "\"mode\":\"" + jsonEscape(logs[i].mode) + "\",";
    json += "\"alarm\":" + String(logs[i].alarmTriggered ? "true" : "false");
    json += "}";
  }

  json += "]}";
  server.send(200, "application/json", json);
}

void handleClear() {
  logCount = 0;
  totalEvents = 0;
  alarmCount = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  pinMode(MAT_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. PRESSENSE IP: http://");
    Serial.println(WiFi.localIP());
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  } else {
    Serial.println("Wi-Fi not connected yet; running with uptime clock.");
  }

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.begin();
}

void loop() {
  server.handleClient();

  int reading = digitalRead(MAT_PIN);
  if (reading != lastMatState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    static int stableState = LOW;
    if (reading != stableState) {
      stableState = reading;
      // Detect rising edge when mat is pressed
      if (stableState == HIGH) {
        recordMovementEvent();
      }
    }
  }

  lastMatState = reading;

  // Keep buzzer off outside the active night alarm window
  String fullTs, datePart, timePart;
  int hr, mn, sc;
  getFormattedTime(fullTs, datePart, timePart, hr, mn, sc);
  if (!isAlarmWindowActive(hr, mn, sc)) {
    digitalWrite(BUZZER_PIN, LOW);
  }
}