#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
const char* ssid = "vivo V60";
const char* password = "12345678";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
WiFiClient espClient;
PubSubClient client(espClient);
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define TRIG1 5
#define ECHO1 18
#define TRIG2 19
#define ECHO2 21
#define PIR_PIN 22
#define IR_PIN 23
#define SOUND_PIN 34
#define TOUCH_PIN 32
#define LCD_SDA 33
#define LCD_SCL 15
#define GREEN_LED 26
#define BLUE_LED 13
#define RED_LED 25
#define YELLOW_LED 14
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
int totalEntered = 0;
int totalExited = 0;
int peopleInside = 0;
int sequenceState = 0;
unsigned long sequenceStartTime = 0;
const unsigned long SEQUENCE_TIMEOUT = 3000;
const float PERSON_DISTANCE = 50.0;
float entranceDistance = 0;
float insideDistance = 0;
float temperature = 0;
float humidity = 0;
int pirRaw = 0;
int irRaw = 0;
int soundValue = 0;
int touchRaw = 0;
bool sensor1Detected = false;
bool sensor2Detected = false;
bool liftMoving = false;
bool liftStuck = false;
bool doorOpen = false;
bool loudSound = false;
bool emergencyTouch = false;
bool highTemperature = false;
bool overcrowded = false;
bool liftCommandON = true;
String elevatorStatus = "STARTING";
int SOUND_THRESHOLD = 700;
const int MAX_PEOPLE = 5;
const unsigned long STUCK_TIME = 10000;
unsigned long noMovementStart = 0;
unsigned long lastLCDChange = 0;
unsigned long lastSerialTime = 0;
unsigned long lastMQTTTime = 0;
const unsigned long LCD_INTERVAL = 2000;
int lcdScreen = 0;
float getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return 999.0;
  }
  return duration * 0.0343 / 2.0;
}
int readSoundSensor() {
  int maxSoundValue = 0;
  for (int i = 0; i < 100; i++) {
    int currentValue = analogRead(SOUND_PIN);
    if (currentValue > maxSoundValue) {
      maxSoundValue = currentValue;
    }
    delayMicroseconds(200);
  }
  return maxSoundValue;
}
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 10000
  ) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println("WiFi connection failed");
  }
}
void reconnectMQTT() {
  if (client.connected()) {
    return;
  }
  String clientId = "ESP32_Elevator_";
  clientId += String(random(0xffff), HEX);
  Serial.print("Connecting MQTT...");
  if (client.connect(clientId.c_str())) {
    Serial.println("Connected");
  }
  else {
    Serial.print("Failed, state = ");
    Serial.println(client.state());
  }
}
void countPeople() {
  sensor1Detected =
    (entranceDistance > 2 &&
     entranceDistance < PERSON_DISTANCE);
  sensor2Detected =
    (insideDistance > 2 &&
     insideDistance < PERSON_DISTANCE);
  if (sequenceState == 0) {
    if (sensor1Detected && !sensor2Detected) {
      sequenceState = 1;
      sequenceStartTime = millis();
      Serial.println("Possible ENTRY...");
    }
    else if (sensor2Detected && !sensor1Detected) {
      sequenceState = 2;
      sequenceStartTime = millis();

      Serial.println("Possible EXIT...");
    }
  }
  else if (sequenceState == 1) {
    if (sensor2Detected) {
      totalEntered++;
      peopleInside++;
      Serial.println("*** PERSON ENTERED ***");
      sequenceState = 3;
    }
    else if (
      millis() - sequenceStartTime >
      SEQUENCE_TIMEOUT
    ) {
      sequenceState = 0;
    }
  }
  else if (sequenceState == 2) {
    if (sensor1Detected) {
      totalExited++;
      if (peopleInside > 0) {
        peopleInside--;
      }
      Serial.println("*** PERSON EXITED ***");
      sequenceState = 3;
    }
    else if (
      millis() - sequenceStartTime >
      SEQUENCE_TIMEOUT
    ) {
      sequenceState = 0;
    }
  }
  else if (sequenceState == 3) {
    if (!sensor1Detected &&
        !sensor2Detected) {
      sequenceState = 0;
      Serial.println("Ready for next person");
    }
  }
}
void checkLiftMovement() {
  liftMoving = (pirRaw == HIGH);
  if (!liftCommandON) {
    liftMoving = false;
    liftStuck = false;
    noMovementStart = 0;
    return;
  }
  if (liftMoving) {
    liftStuck = false;
    noMovementStart = millis();
  }
  else {
    if (noMovementStart == 0) {
      noMovementStart = millis();
    }
    if (
      millis() - noMovementStart >=
      STUCK_TIME
    ) {
      liftStuck = true;
    }
  }
}
void updateStatus() {
  doorOpen = (irRaw == HIGH);
  loudSound = (soundValue >= SOUND_THRESHOLD);
  emergencyTouch = (touchRaw == HIGH);
  highTemperature =
    (!isnan(temperature) &&
     temperature > 40.0);
  overcrowded =
    (peopleInside >= MAX_PEOPLE);
  if (!liftCommandON) {
    elevatorStatus = "LIFT OFF";
  }
  else if (emergencyTouch) {
    elevatorStatus = "EMERGENCY";
  }
  else if (liftStuck) {
    elevatorStatus = "LIFT STUCK";
  }
  else if (highTemperature) {
    elevatorStatus = "HIGH TEMP";
  }
  else if (loudSound) {
    elevatorStatus = "ABNORMAL SOUND";
  }
  else if (overcrowded) {
    elevatorStatus = "OVERCROWDED";
  }
  else if (doorOpen) {
    elevatorStatus = "DOOR OPEN";
  }
  else if (liftMoving) {
    elevatorStatus = "LIFT MOVING";
  }
  else {
    elevatorStatus = "LIFT STOPPED";
  }
}
void controlLEDs() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  if (
    emergencyTouch ||
    liftStuck ||
    highTemperature ||
    loudSound
  ) {
    digitalWrite(RED_LED, HIGH);
  }
  else if (
    overcrowded ||
    doorOpen
  ) {
    digitalWrite(YELLOW_LED, HIGH);
  }
  else if (liftMoving) {
    digitalWrite(BLUE_LED, HIGH);
  }
  else {
    digitalWrite(GREEN_LED, HIGH);
  }
}
void updateLCD() {
  if (emergencyTouch) {
    lcd.setCursor(0, 0);
    lcd.print("!!! EMERGENCY !!");
    lcd.setCursor(0, 1);
    lcd.print("HELP REQUESTED  ");
    return;
  }
  if (
    millis() - lastLCDChange >=
    LCD_INTERVAL
  ) {
    lastLCDChange = millis();
    lcdScreen++;
    if (lcdScreen > 7) {
      lcdScreen = 0;
    }
    lcd.clear();
  }
  if (lcdScreen == 0) {
    lcd.setCursor(0, 0);
    lcd.print("IN:");
    lcd.print(totalEntered);
    lcd.print(" OUT:");
    lcd.print(totalExited);
    lcd.print("   ");
    lcd.setCursor(0, 1);
    lcd.print("INSIDE:");
    lcd.print(peopleInside);
    lcd.print("        ");
  }
  else if (lcdScreen == 1) {
    lcd.setCursor(0, 0);
    lcd.print("TEMP:");
    lcd.print(temperature, 1);
    lcd.print(" C    ");
    lcd.setCursor(0, 1);
    lcd.print("HUM:");
    lcd.print(humidity, 1);
    lcd.print(" %     ");
  }
  else if (lcdScreen == 2) {
    lcd.setCursor(0, 0);
    lcd.print("SOUND:");
    lcd.print(soundValue);
    lcd.print("      ");
    lcd.setCursor(0, 1);
    if (loudSound) {
      lcd.print("ABNORMAL        ");
    }
    else {
      lcd.print("NORMAL          ");
    }
  }
  else if (lcdScreen == 3) {
    lcd.setCursor(0, 0);
    lcd.print("EMERGENCY:");
    lcd.setCursor(0, 1);
    if (emergencyTouch) {
      lcd.print("YES             ");
    }
    else {
      lcd.print("NO              ");
    }
  }
  else if (lcdScreen == 4) {
    lcd.setCursor(0, 0);
    lcd.print("LIFT MOVEMENT:");
    lcd.setCursor(0, 1);
    if (liftMoving) {
      lcd.print("MOVING          ");
    }
    else {
      lcd.print("NOT MOVING      ");
    }
  }
  else if (lcdScreen == 5) {
    lcd.setCursor(0, 0);
    lcd.print("DOOR STATUS:");
    lcd.setCursor(0, 1);
    if (doorOpen) {
      lcd.print("OPEN            ");
    }
    else {
      lcd.print("CLOSED          ");
    }
  }
  else if (lcdScreen == 6) {
    lcd.setCursor(0, 0);
    lcd.print("LIFT POWER:");
    lcd.setCursor(0, 1);
    if (liftCommandON) {
      lcd.print("ON              ");
    }
    else {
      lcd.print("OFF             ");
    }
  }
  else if (lcdScreen == 7) {
    lcd.setCursor(0, 0);
    lcd.print("LIFT STUCK:");
    lcd.setCursor(0, 1);
    if (liftStuck) {
      lcd.print("YES             ");
    }
    else {
      lcd.print("NO              ");
    }
  }
}
void printSerialMonitor() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("     SMART ELEVATOR SAFETY SYSTEM");
  Serial.println("========================================");
  Serial.print("Person Entered     : ");
  Serial.println(totalEntered);
  Serial.print("Person Exited      : ");
  Serial.println(totalExited);
  Serial.print("People Inside Lift : ");
  Serial.println(peopleInside);
  Serial.println("----------------------------------------");
  Serial.print("Entrance Distance  : ");
  Serial.print(entranceDistance);
  Serial.println(" cm");
  Serial.print("Inside Distance    : ");
  Serial.print(insideDistance);
  Serial.println(" cm");
  Serial.println("----------------------------------------");
  Serial.print("Temperature        : ");
  Serial.print(temperature);
  Serial.println(" C");
  Serial.print("Humidity           : ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.println("----------------------------------------");
  Serial.print("Sound Value        : ");
  Serial.println(soundValue);
  Serial.print("Sound Threshold    : ");
  Serial.println(SOUND_THRESHOLD);
  Serial.print("Sound Status       : ");
  if (loudSound) {
    Serial.println("ABNORMAL");
  }
  else {
    Serial.println("NORMAL");
  }
  Serial.println("----------------------------------------");
  Serial.print("Emergency          : ");
  Serial.println(
    emergencyTouch ? "YES" : "NO"
  );
  Serial.print("Lift Movement      : ");
  Serial.println(
    liftMoving ? "MOVING" : "NOT MOVING"
  );
  Serial.print("Door Status        : ");
  Serial.println(
    doorOpen ? "OPEN" : "CLOSED"
  );
  Serial.print("Lift Power         : ");
  Serial.println(
    liftCommandON ? "ON" : "OFF"
  );
  Serial.print("Lift Stuck         : ");
  Serial.println(
    liftStuck ? "YES" : "NO"
  );
  Serial.println("----------------------------------------");
  Serial.print("PIR Raw            : ");
  Serial.println(pirRaw);
  Serial.print("IR Raw             : ");
  Serial.println(irRaw);
  Serial.print("Touch Raw          : ");
  Serial.println(touchRaw);
  Serial.println("----------------------------------------");
  Serial.print("ELEVATOR STATUS    : ");
  Serial.println(elevatorStatus);
  Serial.println("========================================");
}
void publishMQTT() {
  if (!client.connected()) {
    return;
  }
  client.publish(
    "elevator/total_entered",
    String(totalEntered).c_str()
  );
  client.publish(
    "elevator/total_exited",
    String(totalExited).c_str()
  );
  client.publish(
    "elevator/people_inside",
    String(peopleInside).c_str()
  );
  client.publish(
    "elevator/temperature",
    String(temperature, 2).c_str()
  );
  client.publish(
    "elevator/humidity",
    String(humidity, 2).c_str()
  );
  client.publish(
    "elevator/sound_value",
    String(soundValue).c_str()
  );
  client.publish(
    "elevator/sound_status",
    loudSound ? "ABNORMAL" : "NORMAL"
  );
  client.publish(
    "elevator/emergency",
    emergencyTouch ? "EMERGENCY" : "NORMAL"
  );
  client.publish(
    "elevator/lift_movement",
    liftMoving ? "MOVING" : "NOT MOVING"
  );
  client.publish(
    "elevator/door",
    doorOpen ? "OPEN" : "CLOSED"
  );
  client.publish(
    "elevator/lift_power",
    liftCommandON ? "ON" : "OFF"
  );
  client.publish(
    "elevator/lift_stuck",
    liftStuck ? "STUCK" : "NOT STUCK"
  );
  client.publish(
    "elevator/status",
    elevatorStatus.c_str()
  );
}
void setup() {
  Serial.begin(115200);
  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT);
  analogReadResolution(12);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  dht.begin();
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SMART ELEVATOR");
  lcd.setCursor(0, 1);
  lcd.print("STARTING...");
  connectWiFi();
  client.setServer(
    mqtt_server,
    mqtt_port
  );
  delay(2000);
  lcd.clear();
  noMovementStart = millis();
  Serial.println(
    "SMART ELEVATOR SYSTEM STARTED"
  );
}
void loop() {
  static unsigned long lastWiFiAttempt = 0;
  if (
    WiFi.status() != WL_CONNECTED &&
    millis() - lastWiFiAttempt >= 10000
  ) {
    lastWiFiAttempt = millis();
    connectWiFi();
  }
  static unsigned long lastMQTTAttempt = 0;
  if (
    WiFi.status() == WL_CONNECTED &&
    !client.connected() &&
    millis() - lastMQTTAttempt >= 5000
  ) {
    lastMQTTAttempt = millis();
    reconnectMQTT();
  }
  if (client.connected()) {
    client.loop();
  }
  entranceDistance =
    getDistance(TRIG1, ECHO1);
  delay(30);
  insideDistance =
    getDistance(TRIG2, ECHO2);
  countPeople();
  static unsigned long lastDHTRead = 0;
  if (millis() - lastDHTRead >= 2000) {
    lastDHTRead = millis();
    float newTemp =
      dht.readTemperature();
    float newHumidity =
      dht.readHumidity();

    if (!isnan(newTemp)) {
      temperature = newTemp;
    }

    if (!isnan(newHumidity)) {
      humidity = newHumidity;
    }
  }

  // ================= OTHER SENSORS =================
  pirRaw = digitalRead(PIR_PIN);

  irRaw = digitalRead(IR_PIN);

  // Read Analog Sound Sensor V2.2
  soundValue = readSoundSensor();

  touchRaw = digitalRead(TOUCH_PIN);

  // ================= PROCESS =================
  checkLiftMovement();

  updateStatus();

  controlLEDs();

  // ================= LCD =================
  updateLCD();

  // ================= SERIAL =================
  if (
    millis() - lastSerialTime >= 1000
  ) {

    lastSerialTime = millis();

    printSerialMonitor();
  }

  // ================= MQTT =================
  if (
    millis() - lastMQTTTime >= 1000
  ) {

    lastMQTTTime = millis();

    publishMQTT();
  }

  delay(50);
}