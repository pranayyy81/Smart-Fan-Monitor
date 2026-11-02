#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "smart fan monitor"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

char ssid[] = "YOUR_HOTSPOT_NAME";
char pass[] = "YOUR_HOTSPOT_PASSWORD";

// Hardware pins
#define DHTPIN       4
#define DHTTYPE      DHT11
#define BUZZER_PIN   25
#define RED_LED      21
#define GREEN_LED    18
#define BLUE_LED     26     // fan indicator LED

const float TEMP_THRESHOLD = 30.0;   // > 30 = hot
const int   MAX_DHT_FAILS  = 5;

DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

bool fanSwitch    = false;   // V3
bool buzzerSwitch = false;   // V4 (test)

bool  sensorValid = false;
int   dhtFails    = 0;
float temperature = 0;
float humidity    = 0;

bool fanOn       = false;
int  fanSpeed    = 0;
bool buzzerOn    = false;
bool buzzerState = false;
bool alertActive = false;

unsigned long lastBeepMillis = 0;
const unsigned long BEEP_INTERVAL = 250;

void readSensor();
void applyOutputs();
void updateBuzzer();
void sendToBlynk();
void connectBlynk();
String buildStatus();

BLYNK_CONNECTED() {
  Serial.println("Blynk connected!");
  Blynk.syncVirtual(V3, V4);
  sendToBlynk();
}

BLYNK_WRITE(V3) {
  fanSwitch = (param.asInt() == 1);
  if (fanSwitch && buzzerSwitch) {
    buzzerSwitch = false;
    Blynk.virtualWrite(V4, 0);
  }
  applyOutputs();
  sendToBlynk();
}

BLYNK_WRITE(V4) {
  buzzerSwitch = (param.asInt() == 1);
  applyOutputs();
  sendToBlynk();
}

void applyOutputs() {
  bool hot = sensorValid && (temperature > TEMP_THRESHOLD);

  digitalWrite(RED_LED,   (sensorValid && hot)  ? HIGH : LOW);
  digitalWrite(GREEN_LED, (sensorValid && !hot) ? HIGH : LOW);

  fanOn    = fanSwitch;
  fanSpeed = fanOn ? 100 : 0;
  ledcWrite(BLUE_LED, fanOn ? 255 : 0);

  bool wasBuzzerOn = buzzerOn;
  buzzerOn = !fanOn && (hot || buzzerSwitch);

  if (buzzerOn && !wasBuzzerOn) {
    buzzerState = true;
    digitalWrite(BUZZER_PIN, HIGH);
    lastBeepMillis = millis();
  }
  if (!buzzerOn) {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (sensorValid) {
    if (hot && !alertActive) {
      alertActive = true;
      if (Blynk.connected()) {
        Blynk.logEvent("heat_alert", "Room temperature is above 30 degrees C! Turn on the fan.");
      }
    } else if (!hot) {
      alertActive = false;
    }
  }
}

void readSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    dhtFails++;
    Serial.print("ERROR: DHT11 reading failed (");
    Serial.print(dhtFails);
    Serial.println(" in a row)");
    if (dhtFails >= MAX_DHT_FAILS) {
      sensorValid = false;
      applyOutputs();
      sendToBlynk();
    }
    return;
  }

  dhtFails    = 0;
  sensorValid = true;
  temperature = t;
  humidity    = h;

  applyOutputs();
  sendToBlynk();

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.print(" C | Humidity: ");
  Serial.print(humidity);
  Serial.print(" % | Fan: ");
  Serial.print(fanOn ? "ON" : "OFF");
  Serial.print(" | Buzzer: ");
  Serial.println(buzzerOn ? "ON" : "OFF");
}

void updateBuzzer() {
  if (buzzerOn) {
    if (millis() - lastBeepMillis >= BEEP_INTERVAL) {
      lastBeepMillis = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  } else {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
    lastBeepMillis = millis();
  }
}

String buildStatus() {
  if (!sensorValid) {
    return String("DHT11 error | fan ") + (fanOn ? "ON" : "OFF");
  }
  String s = String(temperature, 1) + "C ";
  if (temperature > TEMP_THRESHOLD) {
    s += fanOn ? "> 30C - fan ON, buzzer OFF" : "> 30C - ALERT! turn on fan";
  } else {
    s += "<= 30C - normal, fan ";
    s += fanOn ? "ON" : "OFF";
  }
  return s;
}

void sendToBlynk() {
  if (!Blynk.connected()) return;
  if (sensorValid) {
    Blynk.virtualWrite(V0, temperature);
    Blynk.virtualWrite(V1, humidity);
  }
  Blynk.virtualWrite(V5, fanSpeed);
  Blynk.virtualWrite(V7, buildStatus());
}

void connectBlynk() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    WiFi.reconnect();
    return;
  }
  if (!Blynk.connected()) {
    Serial.println("Attempting Blynk connection...");
    if (Blynk.connect(5000)) {
      Serial.println("Blynk connection successful!");
    } else {
      Serial.println("Blynk connection failed; will retry.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  if (!ledcAttach(BLUE_LED, 5000, 8)) {
    Serial.println("ERROR: Blue LED PWM setup failed");
  }
  ledcWrite(BLUE_LED, 0);

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to Wi-Fi");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
  } else {
    Serial.println("\nWi-Fi connection timed out.");
  }

  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect(10000);
  }

  applyOutputs();

  timer.setInterval(2000L, readSensor);
  timer.setInterval(50L, updateBuzzer);
  timer.setInterval(15000L, connectBlynk);
}

void loop() {
  if (Blynk.connected()) {
    Blynk.run();
  }
  timer.run();
}
