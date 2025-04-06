#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <WebServer.h>

// --- Pin Configurations ---
#define DHTPIN 14
#define DHTTYPE DHT22
#define MQ135_PIN 34  // Analog output pin for MQ135 gas sensor
#define SDA_PIN 25     // I2C SDA
#define SCL_PIN 26     // I2C SCL

// --- LCD Setup ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD I2C address, cols, rows
DHT dht(DHTPIN, DHTTYPE);

// --- WiFi Credentials ---
const char* ssid = "realme7";
const char* password = "realme_7";

// --- WebServer Setup ---
WebServer server(80);

// --- Data History ---
#define HISTORY_SIZE 10
float tempHistory[HISTORY_SIZE] = {0};
float humHistory[HISTORY_SIZE] = {0};
int gasHistory[HISTORY_SIZE] = {0};
int historyIndex = 0;

// --- Thresholds ---
const float TEMP_LOW = 30.0;   // Normal cow body temp range
const float TEMP_HIGH = 39.5;
const float HUM_LOW = 40.0;    // Comfortable humidity range
const float HUM_HIGH = 70.0;
const int GAS_HIGH = 2000;     // Warning threshold for methane

// --- MQ135 Calibration ---
const float MQ135_RZERO = 76.63;  // Calibration value for MQ135 in clean air
const float PARA = 116.6020682;   // Curve parameter for CO2 calculation

// Forward declarations of functions
void handleRoot();
void handleData();
void handleHistory();
void handleCss();
void handleJs();
void handleNotFound();
String getTempStatus(float temp);
String getHumStatus(float hum);
String getGasStatus(int gas);
String getOverallStatus(float temp, float hum, int gas);
String getTimeString();
float getCorrectedPPM(int rawValue, float temperature, float humidity);

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.begin(16, 2);
  lcd.backlight();

  Serial.begin(115200);
  dht.begin();

  // Set ADC resolution to 12 bits (0-4095)
  analogReadResolution(12);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    static int dots = 0;
    lcd.print("        ");
    lcd.setCursor(0, 1);
    for(int i = 0; i < dots; i++) {
      lcd.print(".");
    }
    dots = (dots + 1) % 8;
  }
  
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connected!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);

  // --- Web Routes ---
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.on("/style.css", handleCss);
  server.on("/script.js", handleJs);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    lastUpdate = millis();
    
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    
    // Read MQ135 analog output
    int gasRaw = analogRead(MQ135_PIN);
    
    // Get corrected PPM value
    float gasPPM = getCorrectedPPM(gasRaw, temp, hum);
    
    // For history, store the raw value
    int gas = gasRaw;

    // Update data history
    tempHistory[historyIndex] = temp;
    humHistory[historyIndex] = hum;
    gasHistory[historyIndex] = gas;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;

    // Update LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    if (!isnan(temp) && !isnan(hum)) {
      lcd.print("T:");
      lcd.print(temp, 1);
      lcd.print("C H:");
      lcd.print(hum, 0);
      lcd.print("%");
    } else {
      lcd.print("DHT Err");
    }

    lcd.setCursor(0, 1);
    lcd.print("Gas:");
    lcd.print(gasPPM, 0);
    lcd.print(" ppm");
    
    // Debug output
    Serial.print("Temperature: ");
    Serial.print(temp);
    Serial.print("°C, Humidity: ");
    Serial.print(hum);
    Serial.print("%, Gas Raw: ");
    Serial.print(gasRaw);
    Serial.print(", Gas PPM: ");
    Serial.println(gasPPM);
  }
}

// --- Helper Functions ---
// Convert raw MQ135 reading to PPM with temperature and humidity correction
float getCorrectedPPM(int rawValue, float temperature, float humidity) {
  // Prevent division by zero
  if (rawValue == 0) return 0;
  
  // Calculate sensor resistance
  float vRL = (float)rawValue * (3.3 / 4095.0);
  float rS = ((3.3 * 10.0) / vRL) - 10.0; // Assuming 10K load resistor
  
  // Apply temperature and humidity correction
  float correctionFactor = 1.0;
  if (!isnan(temperature) && !isnan(humidity)) {
    correctionFactor = 1.0 + 0.02 * (temperature - 20.0) + 0.01 * (humidity - 65.0);
  }
  
  // Calculate corrected resistance
  float correctedRS = rS / correctionFactor;
  
  // Convert to ppm (simplified formula for CO2/air quality)
  float ratio = correctedRS / MQ135_RZERO;
  float ppm = PARA * pow(ratio, -1.53);
  
  return ppm;
}

String getTempStatus(float temp) {
  if (isnan(temp)) return "Sensor error";
  if (temp < TEMP_LOW) return "Below normal range";
  if (temp > TEMP_HIGH) return "Above normal range";
  return "Normal range";
}

String getHumStatus(float hum) {
  if (isnan(hum)) return "Sensor error";
  if (hum < HUM_LOW) return "Too dry";
  if (hum > HUM_HIGH) return "Too humid";
  return "Comfortable";
}

String getGasStatus(int gas) {
  float ppm = getCorrectedPPM(gas, dht.readTemperature(), dht.readHumidity());
  
  if (ppm < 700) return "Good air quality";
  if (ppm < 1000) return "Moderate";
  if (ppm < 2000) return "Poor air quality";
  return "Dangerous levels";
}

String getOverallStatus(float temp, float hum, int gas) {
  int issues = 0;
  
  if (isnan(temp) || isnan(hum)) return "Sensor Error";
  
  if (temp < TEMP_LOW || temp > TEMP_HIGH) issues++;
  if (hum < HUM_LOW || hum > HUM_HIGH) issues++;
  
  float ppm = getCorrectedPPM(gas, temp, hum);
  if (ppm >= 2000) issues++;
  
  if (issues == 0) return "Excellent";
  if (issues == 1) return "Good";
  if (issues == 2) return "Warning";
  return "Danger";
}

String getTimeString() {
  unsigned long currentMillis = millis();
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  // Format: HH:MM:SS
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", hours % 24, minutes % 60, seconds % 60);
  return String(timeStr);
}

// --- Web Handlers ---
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Cow Health Monitor</title>
    <link rel='stylesheet' href='/style.css'>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
  </head>
  <body>
    <div class="container">
      <header>
        <h1>🐄 Cow Health Monitor</h1>
        <p class="subtitle">Real-time environmental monitoring system</p>
      </header>
      
      <div class="dashboard">
        <div id="tempCard" class="card">
          <div class="card-icon">🌡️</div>
          <div class="card-title">Temperature</div>
          <div class="card-value"><span id="temp">--</span> °C</div>
          <div class="card-status" id="tempStatus">Loading...</div>
          <div class="progress-container">
            <div class="progress-bar" id="tempBar"></div>
          </div>
        </div>
        
        <div id="humCard" class="card">
          <div class="card-icon">💧</div>
          <div class="card-title">Humidity</div>
          <div class="card-value"><span id="hum">--</span> %</div>
          <div class="card-status" id="humStatus">Loading...</div>
          <div class="progress-container">
            <div class="progress-bar" id="humBar"></div>
          </div>
        </div>
        
        <div id="gasCard" class="card">
          <div class="card-icon">🧪</div>
          <div class="card-title">Air Quality</div>
          <div class="card-value"><span id="gas">--</span> ppm</div>
          <div class="card-status" id="gasStatus">Loading...</div>
          <div class="progress-container">
            <div class="progress-bar" id="gasBar"></div>
          </div>
        </div>
      </div>
      
      <div class="chart-container">
        <h2>Monitoring History</h2>
        <div class="chart-tabs">
          <button class="chart-tab active" data-type="temperature">Temperature</button>
          <button class="chart-tab" data-type="humidity">Humidity</button>
          <button class="chart-tab" data-type="gas">Air Quality</button>
        </div>
        <div class="chart" id="chart">
          <div class="chart-y-axis" id="yAxis"></div>
          <div class="chart-grid" id="chartGrid"></div>
        </div>
        <div class="chart-x-axis">
          <span>10</span>
          <span>9</span>
          <span>8</span>
          <span>7</span>
          <span>6</span>
          <span>5</span>
          <span>4</span>
          <span>3</span>
          <span>2</span>
          <span>1</span>
          <span>Now</span>
        </div>
      </div>
      
      <div class="health-status">
        <h2>Cow Health Status</h2>
        <div class="status-indicator" id="overallStatus">
          <div class="status-icon" id="statusIcon">⏳</div>
          <div class="status-text" id="statusText">Analyzing data...</div>
        </div>
      </div>
      
      <footer>
        <p>Last updated: <span id="lastUpdate">--</span></p>
        <p>System IP: <span>)rawliteral" + WiFi.localIP().toString() + R"rawliteral(</span></p>
      </footer>
    </div>
    
    <script src='/script.js'></script>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleData() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int gasRaw = analogRead(MQ135_PIN);
  float gasPPM = getCorrectedPPM(gasRaw, temp, hum);

  String json = "{";
  json += "\"temperature\":" + String(temp, 1) + ",";
  json += "\"humidity\":" + String(hum, 1) + ",";
  json += "\"gas\":" + String(gasPPM, 0) + ",";
  
  // Add status information
  json += "\"tempStatus\":\"" + getTempStatus(temp) + "\",";
  json += "\"humStatus\":\"" + getHumStatus(hum) + "\",";
  json += "\"gasStatus\":\"" + getGasStatus(gasRaw) + "\",";
  
  // Calculate overall status
  String overallStatus = getOverallStatus(temp, hum, gasRaw);
  json += "\"overallStatus\":\"" + overallStatus + "\",";
  
  // Add timestamp
  json += "\"timestamp\":\"" + getTimeString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleHistory() {
  String json = "{\"temperature\":[";
  
  // Get temperature history
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    json += String(tempHistory[idx], 1);
    if (i < HISTORY_SIZE - 1) json += ",";
  }
  
  json += "],\"humidity\":[";
  
  // Get humidity history
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    json += String(humHistory[idx], 1);
    if (i < HISTORY_SIZE - 1) json += ",";
  }
  
  json += "],\"gas\":[";
  
  // Get gas history - convert raw values to PPM
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    float temp = tempHistory[idx];
    float hum = humHistory[idx];
    int gasRaw = gasHistory[idx];
    float gasPPM = getCorrectedPPM(gasRaw, temp, hum);
    json += String(gasPPM, 0);
    if (i < HISTORY_SIZE - 1) json += ",";
  }
  
  json += "]}";
  server.send(200, "application/json", json);
}

void handleCss() {
  String css = R"rawliteral(
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }
    
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background-color: #f7f9fc;
      color: #333;
      line-height: 1.6;
    }
    
    .container {
      max-width: 1200px;
      margin: 0 auto;
      padding: 20px;
    }
    
    header {
      text-align: center;
      margin-bottom: 30px;
    }
    
    h1 {
      color: #1e3a8a;
      font-size: 2.5rem;
      margin-bottom: 5px;
    }
    
    .subtitle {
      color: #6b7280;
      font-size: 1.1rem;
    }
    
    .dashboard {
      display: flex;
      flex-wrap: wrap;
      justify-content: space-between;
      gap: 20px;
      margin-bottom: 40px;
    }
    
    .card {
      background: white;
      border-radius: 16px;
      padding: 25px;
      box-shadow: 0 4px 15px rgba(0, 0, 0, 0.05);
      flex: 1;
      min-width: 250px;
      transition: transform 0.3s ease, box-shadow 0.3s ease;
      position: relative;
      overflow: hidden;
    }
    
    .card:hover {
      transform: translateY(-5px);
      box-shadow: 0 6px 20px rgba(0, 0, 0, 0.1);
    }
    
    .card::before {
      content: '';
      position: absolute;
      top: 0;
      left: 0;
      width: 5px;
      height: 100%;
      transition: background-color 0.3s ease;
    }
    
    .card.normal::before { background-color: #10b981; }
    .card.warning::before { background-color: #f59e0b; }
    .card.danger::before { background-color: #ef4444; }
    
    .card-icon {
      font-size: 2rem;
      margin-bottom: 15px;
    }
    
    .card-title {
      font-size: 1.1rem;
      font-weight: 600;
      color: #4b5563;
      margin-bottom: 10px;
    }
    
    .card-value {
      font-size: 2.2rem;
      font-weight: 700;
      margin-bottom: 15px;
    }
    
    .card-status {
      font-size: 0.95rem;
      color: #6b7280;
      margin-bottom: 15px;
    }
    
    .progress-container {
      height: 6px;
      background-color: #e5e7eb;
      border-radius: 3px;
      overflow: hidden;
    }
    
    .progress-bar {
      height: 100%;
      width: 0%;
      transition: width 0.5s ease, background-color 0.3s ease;
    }
    
    .chart-container {
      background: white;
      border-radius: 16px;
      padding: 25px;
      box-shadow: 0 4px 15px rgba(0, 0, 0, 0.05);
      margin-bottom: 40px;
    }
    
    .chart-container h2 {
      color: #1e3a8a;
      font-size: 1.5rem;
      margin-bottom: 20px;
    }
    
    .chart-tabs {
      display: flex;
      margin-bottom: 20px;
      border-bottom: 1px solid #e5e7eb;
    }
    
    .chart-tab {
      padding: 10px 20px;
      background: none;
      border: none;
      border-bottom: 3px solid transparent;
      cursor: pointer;
      font-weight: 600;
      color: #6b7280;
      transition: color 0.3s ease, border-color 0.3s ease;
    }
    
    .chart-tab.active {
      color: #1e3a8a;
      border-bottom-color: #1e3a8a;
    }
    
    .chart {
      height: 250px;
      margin-bottom: 10px;
      position: relative;
      display: flex;
    }
    
    .chart-y-axis {
      width: 50px;
      height: 100%;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      align-items: flex-end;
      padding-right: 10px;
      color: #6b7280;
      font-size: 0.8rem;
    }
    
    .chart-grid {
      flex: 1;
      height: 100%;
      position: relative;
      background: linear-gradient(to bottom, #e5e7eb 1px, transparent 1px);
      background-size: 100% 25%;
    }
    
    .chart-line {
      position: absolute;
      bottom: 0;
      left: 0;
      width: calc(100% - 4px);
      height: 100%;
      fill: none;
      stroke-width: 3;
      stroke-linecap: round;
      stroke-linejoin: round;
      transition: all 0.5s ease;
    }
    
    .chart-line.temperature { stroke: #3b82f6; }
    .chart-line.humidity { stroke: #10b981; }
    .chart-line.gas { stroke: #f59e0b; }
    
    .chart-dots {
      position: absolute;
      bottom: 0;
      left: 0;
      width: 100%;
      height: 100%;
    }
    
    .chart-dot {
      position: absolute;
      width: 8px;
      height: 8px;
      border-radius: 50%;
      transform: translate(-50%, 50%);
      transition: all 0.5s ease;
    }
    
    .chart-dot.temperature { background-color: #3b82f6; }
    .chart-dot.humidity { background-color: #10b981; }
    .chart-dot.gas { background-color: #f59e0b; }
    
    .chart-x-axis {
      display: flex;
      justify-content: space-between;
      padding-left: 50px;
      color: #6b7280;
      font-size: 0.8rem;
    }
    
    .health-status {
      background: white;
      border-radius: 16px;
      padding: 25px;
      box-shadow: 0 4px 15px rgba(0, 0, 0, 0.05);
      margin-bottom: 40px;
      text-align: center;
    }
    
    .health-status h2 {
      color: #1e3a8a;
      font-size: 1.5rem;
      margin-bottom: 20px;
    }
    
    .status-indicator {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    
    .status-icon {
      font-size: 4rem;
      margin-bottom: 15px;
      transition: transform 0.5s ease;
    }
    
    .status-icon:hover {
      transform: scale(1.1);
    }
    
    .status-text {
      font-size: 1.5rem;
      font-weight: 600;
    }
    
    footer {
      text-align: center;
      color: #6b7280;
      font-size: 0.9rem;
      margin-top: 20px;
    }
    
    @media (max-width: 768px) {
      .dashboard {
        flex-direction: column;
      }
      
      .card {
        width: 100%;
      }
      
      h1 {
        font-size: 2rem;
      }
    }
  )rawliteral";
  server.send(200, "text/css", css);
}

void handleJs() {
  String js = R"rawliteral(
    // Elements
    const tempEl = document.getElementById('temp');
    const humEl = document.getElementById('hum');
    const gasEl = document.getElementById('gas');
    const tempStatusEl = document.getElementById('tempStatus');
    const humStatusEl = document.getElementById('humStatus');
    const gasStatusEl = document.getElementById('gasStatus');
    const tempBarEl = document.getElementById('tempBar');
    const humBarEl = document.getElementById('humBar');
    const gasBarEl = document.getElementById('gasBar');
    const tempCardEl = document.getElementById('tempCard');
    const humCardEl = document.getElementById('humCard');
    const gasCardEl = document.getElementById('gasCard');
    const lastUpdateEl = document.getElementById('lastUpdate');
    const overallStatusEl = document.getElementById('overallStatus');
    const statusIconEl = document.getElementById('statusIcon');
    const statusTextEl = document.getElementById('statusText');
    const chartGrid = document.getElementById('chartGrid');
    const yAxis = document.getElementById('yAxis');
    const chartTabs = document.querySelectorAll('.chart-tab');
    
    // Current chart type
    let currentChartType = 'temperature';
    
    // Animation frame request
    let animationFrame;
    
    // Update current data
    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          // Update values
          tempEl.textContent = data.temperature;
          humEl.textContent = data.humidity;
          gasEl.textContent = data.gas;
          
          // Update status texts
          tempStatusEl.textContent = data.tempStatus;
          humStatusEl.textContent = data.humStatus;
          gasStatusEl.textContent = data.gasStatus;
          
          // Update progress bars
          updateProgressBar(tempBarEl, data.temperature, 20, 45, tempCardEl);
          updateProgressBar(humBarEl, data.humidity, 0, 100, humCardEl);
          updateProgressBar(gasBarEl, data.gas, 400, 3000, gasCardEl); // PPM range for air quality
          
          // Update overall status
          updateOverallStatus(data.overallStatus);
          
          // Update timestamp
          lastUpdateEl.textContent = data.timestamp;
          
          // Get history data and update chart
          updateChart();
        })
        .catch(error => {
          console.error('Error fetching data:', error);
        });
    }
    
    // Update progress bar with color coding
    function updateProgressBar(barEl, value, min, max, cardEl) {
      const percentage = Math.min(100, Math.max(0, ((value - min) / (max - min)) * 100));
      barEl.style.width = percentage + '%';
      
      // Calculate color and status
      let color, status;
      if (percentage < 25) {
        color = '#10b981'; // Green
        status = 'normal';
      } else if (percentage < 75) {
        color = '#f59e0b'; // Yellow
        status = 'warning';
      } else {
        color = '#ef4444'; // Red
        status = 'danger';
      }
      
      // Apply color and status
      barEl.style.backgroundColor = color;
      cardEl.className = 'card ' + status;
    }
    
    // Update overall status indicator
    function updateOverallStatus(status) {
      let icon, text, color;
      
      switch(status) {
        case 'Excellent':
          icon = '😀';
          text = 'All parameters normal';
          color = '#10b981';
          break;
        case 'Good':
          icon = '🙂';
          text = 'Conditions are good';
          color = '#60a5fa';
          break;
        case 'Warning':
          icon = '😐';
          text = 'Monitor conditions';
          color = '#f59e0b';
          break;
        case 'Danger':
          icon = '😨';
          text = 'Immediate attention needed';
          color = '#ef4444';
          break;
        default:
          icon = '⏳';
          text = 'Analyzing data...';
          color = '#6b7280';
      }
      
      statusIconEl.textContent = icon;
      statusTextEl.textContent = text;
      statusTextEl.style.color = color;
    }
    
    // Update chart
    function updateChart() {
      fetch('/history')
        .then(response => response.json())
        .then(data => {
          // Clear previous chart
          while (chartGrid.firstChild) {
            chartGrid.removeChild(chartGrid.firstChild);
          }
          
          let values = [];
          let yMin = 0;
          let yMax = 100;
          
          // Set data and y-axis range based on current chart type
          switch(currentChartType) {
            case 'temperature':
              values = data.temperature;
              yMin = 20;
              yMax = 45;
              break;
            case 'humidity':
              values = data.humidity;
              yMin = 0;
              yMax = 100;
              break;
            case 'gas':
              values = data.gas;
              yMin = 400;
              yMax = 3000;
              break;
          }
          
          // Filter out invalid values (0 or NaN)
          values = values.filter(v => v > 0 && !isNaN(v));
          
          if (values.length === 0) return;
          
          // Create Y-axis labels
          yAxis.innerHTML = '';
          const steps = 5;
          for (let i = 0; i <= steps; i++) {
            const label = document.createElement('div');
            label.textContent = Math.round(yMax - (i * (yMax - yMin) / steps));
            yAxis.appendChild(label);
          }
          
          // Create SVG for line chart
          const svgNS = "http://www.w3.org/2000/svg";
          const svg = document.createElementNS(svgNS, "svg");
          svg.setAttribute("width", "100%");
          svg.setAttribute("height", "100%");
          
          // Create path for line chart
          const path = document.createElementNS(svgNS, "path");
          path.classList.add("chart-line", currentChartType);
          
          // Calculate path data
          const width = 100; // Width percentage
          const segmentWidth = width / (values.length - 1);
          let pathData = '';
          
          // Create dots container
          const dotsContainer = document.createElement('div');
          dotsContainer.classList.add('chart-dots');
          
          values.forEach((value, index) => {
            // Calculate point position
            const x = index * segmentWidth;
            const y = 100 - ((value - yMin) / (yMax - yMin) * 100);
            
            // Add point to path
            if (index === 0) {
              pathData += `M${x},${y}`;
            } else {
              pathData += ` L${x},${y}`;
            }
            
            // Create a dot
            const dot = document.createElement('div');
            dot.classList.add('chart-dot', currentChartType);
            dot.style.left = x + '%';
            dot.style.bottom = y + '%';
            
            // Add tooltip
            dot.title = value;
            
            dotsContainer.appendChild(dot);
          });
          
          path.setAttribute("d", pathData);
          svg.appendChild(path);
          chartGrid.appendChild(svg);
          chartGrid.appendChild(dotsContainer);
          
          // Add animation if supported
          if (window.IntersectionObserver) {
            const observer = new IntersectionObserver((entries) => {
              entries.forEach(entry => {
                if (entry.isIntersecting) {
                  // When chart is visible, animate it
                  animatePath(path);
                  animateDots(dotsContainer.querySelectorAll('.chart-dot'));
                }
              });
            });
            
            observer.observe(chartGrid);
          }
        })
        .catch(error => {
          console.error('Error fetching history:', error);
        });
    }
    
    // Animate path drawing
    function animatePath(path) {
      const length = path.getTotalLength();
      
      // Clear any previous animation
      path.style.transition = 'none';
      path.style.strokeDasharray = length + ' ' + length;
      path.style.strokeDashoffset = length;
      
      // Trigger reflow
      path.getBoundingClientRect();
      
      // Start animation
      path.style.transition = 'stroke-dashoffset 1.5s ease-in-out';
      path.style.strokeDashoffset = '0';
    }
    
    // Animate dots appearance
    function animateDots(dots) {
      dots.forEach((dot, index) => {
        dot.style.opacity = '0';
        dot.style.transform = 'translate(-50%, 50%) scale(0)';
        
        setTimeout(() => {
          dot.style.transition = 'opacity 0.3s ease, transform 0.3s ease';
          dot.style.opacity = '1';
          dot.style.transform = 'translate(-50%, 50%) scale(1)';
        }, 1500 + index * 100);
      });
    }
    
    // Add event listeners to chart tabs
    chartTabs.forEach(tab => {
      tab.addEventListener('click', () => {
        // Remove active class from all tabs
        chartTabs.forEach(t => t.classList.remove('active'));
        
        // Add active class to clicked tab
        tab.classList.add('active');
        
        // Update chart type and redraw
        currentChartType = tab.dataset.type;
        updateChart();
      });
    });
    
    // Initial update
    updateData();
    
    // Set interval for updates
    setInterval(updateData, 3000);
    
    // Add some animations on load
    document.addEventListener('DOMContentLoaded', () => {
      const cards = document.querySelectorAll('.card');
      cards.forEach((card, index) => {
        card.style.opacity = '0';
        card.style.transform = 'translateY(20px)';
        
        setTimeout(() => {
          card.style.transition = 'opacity 0.5s ease, transform 0.5s ease';
          card.style.opacity = '1';
          card.style.transform = 'translateY(0)';
        }, 100 + index * 100);
      });
    });
  )rawliteral";
  server.send(200, "application/javascript", js);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

