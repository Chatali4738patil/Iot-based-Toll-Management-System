#include <SPI.h>
#include <MFRC522.h>
#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// =====================================================
// PIN DEFINITIONS
// =====================================================

// ---------------- RC522 RFID ----------------
#define RFID_SS_PIN       5
#define RFID_RST_PIN      27

// SPI pins
#define RFID_SCK_PIN      18
#define RFID_MISO_PIN     19
#define RFID_MOSI_PIN     23

// ---------------- HX711 ----------------
#define HX711_DT_PIN      4
#define HX711_SCK_PIN     16

// ---------------- Servo ----------------
#define SERVO_PIN         13

// ---------------- Buzzer ----------------
#define BUZZER_PIN        25

// ---------------- I2C LCD ----------------
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22

// =====================================================
// LCD SETTINGS
// =====================================================

// Most LCD I2C modules use 0x27
// If your LCD does not work, try 0x3F

#define LCD_ADDRESS 0x27

LiquidCrystal_I2C lcd(LCD_ADDRESS,16,2);

// =====================================================
// OBJECTS
// =====================================================

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

HX711 scale;

Servo gateServo;

// =====================================================
// SERVO SETTINGS
// =====================================================

const int GATE_CLOSED = 0;
const int GATE_OPEN   = 90;
String weightClass = "";

// =====================================================
// HX711 CALIBRATION
// =====================================================

// THIS VALUE MUST BE CALIBRATED
// for your actual load cell.
//
// 420 is only an initial example.

float calibration_factor = 420.0;

// =====================================================
// VEHICLE DATABASE
// =====================================================

struct Vehicle {
  String uid;
  String vehicleNumber;
  String vehicleType;
  float maxWeight;
};

// -----------------------------------------------------
// Example registered vehicles
// maxWeight = maximum permitted weight in kg
// -----------------------------------------------------

Vehicle vehicles[] = {
  {
    "07 80 D4 86",
    "MH01AB1234",
    "CAR",
    500
  },

  {
    "F1 11 59 19",
    "MH02CD5678",
    "SUV",
    1500
  },

  {
    "B9 3B 38 B2",
    "MH04EF9012",
    "TRUCK",
    5000
  }
};

const int NUMBER_OF_VEHICLES = sizeof(vehicles) / sizeof(vehicles[0]);

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("     ESP32 TOLL MANAGEMENT SYSTEM");
  Serial.println("======================================");

  // ===================================================
  // BUZZER
  // ===================================================

  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);

  // ===================================================
  // LCD
  // ===================================================

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("TOLL SYSTEM");

  lcd.setCursor(0, 1);
  lcd.print("Initializing");

  delay(2000);

  // ===================================================
  // SERVO
  // ===================================================

  gateServo.setPeriodHertz(50);

  gateServo.attach(SERVO_PIN, 500, 2400);

  // Make sure gate starts closed
  gateServo.write(GATE_CLOSED);

  Serial.println("Servo initialized");

  // ===================================================
  // HX711
  // ===================================================

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(calibration_factor);

  // Tare the scale at startup
  Serial.println("Taring load cell...");

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Taring Scale");

  lcd.setCursor(0, 1);
  lcd.print("Please Wait...");

  delay(1000);

  if (scale.is_ready()) {
    scale.tare(20);

    Serial.println(
      "Scale tare completed"
    );
  } else {
    Serial.println(
      "WARNING: HX711 NOT READY"
    );
  }

  // ===================================================
  // RC522
  // ===================================================

  SPI.begin(
    RFID_SCK_PIN,
    RFID_MISO_PIN,
    RFID_MOSI_PIN,
    RFID_SS_PIN
  );

  rfid.PCD_Init();

  delay(500);

  Serial.println("RC522 initialized");

  // ===================================================
  // READY
  // ===================================================

  beep(2);

  showWelcome();

  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Scan RFID card...");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  // ---------------------------------------------------
  // Wait for RFID card
  // ---------------------------------------------------

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // ---------------------------------------------------
  // Read UID
  // ---------------------------------------------------

  String uid = getUID();

  Serial.println();
  Serial.println("--------------------------------------");
  Serial.println("RFID CARD DETECTED");

  Serial.print("UID: ");
  Serial.println(uid);

  // ---------------------------------------------------
  // Find registered vehicle
  // ---------------------------------------------------

  int vehicleIndex = findVehicle(uid);

  // ===================================================
  // UNKNOWN RFID
  // ===================================================

  if (vehicleIndex == -1) {
    Serial.println("UNREGISTERED RFID");

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("RFID NOT");
    lcd.setCursor(0, 1);
    lcd.print("REGISTERED");

    errorBeep();

    delay(3000);

    stopRFID();

    showWelcome();

    return;
  }

  // ===================================================
  // REGISTERED VEHICLE
  // ===================================================

  Serial.println("Vehicle registered");
  Serial.print("Vehicle No: ");
  Serial.println(vehicles[vehicleIndex].vehicleNumber);
  Serial.print("Vehicle Type: ");
  Serial.println(vehicles[vehicleIndex].vehicleType);
  Serial.print("Maximum Weight: ");
  Serial.print(vehicles[vehicleIndex].maxWeight);
  Serial.println(" kg");

  // ---------------------------------------------------
  // LCD
  // ---------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("RFID VERIFIED");

  lcd.setCursor(0, 1);

  lcd.print(vehicles[vehicleIndex].vehicleNumber);

  beep(2);

  delay(2000);

  // ===================================================
  // MEASURE WEIGHT
  // ===================================================

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("MEASURING");

  lcd.setCursor(0, 1);
  lcd.print("WEIGHT...");

  Serial.println();
  Serial.println("Measuring vehicle weight...");

  // Allow load to stabilize
  delay(3000);

  float weight = getVehicleWeight();

  Serial.print("Measured Weight: ");

  Serial.print(weight,1);

  Serial.println(" kg");

  // ===================================================
  // MAXIMUM ALLOWED WEIGHT
  // ===================================================

  float maximumWeight = vehicles[vehicleIndex].maxWeight;

  // ===================================================
  // OVERLOAD CHECK
  // ===================================================

  bool overloaded = checkOverload(weight, maximumWeight);

  // ===================================================
  // OVERLOAD CONDITION
  // ===================================================

  if (overloaded){
    handleOverload (weight, maximumWeight);
    stopRFID();
    delay(1000);
    return;
  }

  // ===================================================
  // NORMAL VEHICLE
  // ===================================================

  Serial.println(
    "Vehicle is within permitted weight"
  );

  // ---------------------------------------------------
  // Determine vehicle class
  // ---------------------------------------------------

  weightClass = vehicles[vehicleIndex].vehicleType;

  // ---------------------------------------------------
  // Calculate toll
  // ---------------------------------------------------

  int toll = calculateToll(weight);

  Serial.print( "Weight Class: ");
  Serial.println(weightClass);
  Serial.print("Toll Amount: Rs.");
  Serial.println(toll);

  // ===================================================
  // DISPLAY WEIGHT
  // ===================================================

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WEIGHT:");
  lcd.print(weight, 0);
  lcd.print("kg");
  lcd.setCursor(0, 1);
  lcd.print("MAX:");
  lcd.print(maximumWeight, 0);
  lcd.print("kg");

  delay(2500);
  // ===================================================
  // DISPLAY TOLL
  // ===================================================
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(weightClass);
  lcd.setCursor(0, 1);
  lcd.print("TOLL: Rs.");
  lcd.print(toll);

  delay(2500);
  // ===================================================
  // OPEN GATE
  // ===================================================
  openGate();
  // ===================================================
  // WAIT FOR VEHICLE
  // ===================================================
  delay(5000);
  // ===================================================
  // CLOSE GATE
  // ===================================================
  closeGate();
  delay(1500);
  // ===================================================
  // RETURN TO READY
  // ===================================================
  stopRFID();
  showWelcome();
  Serial.println();
  Serial.println("Ready for next vehicle...");
  delay(1000);
}




// =====================================================
// GET RFID UID
// =====================================================
String getUID(){
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0){
      uid += " ";
    }

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i],HEX);
  }

  uid.toUpperCase();
 return uid;
}
// =====================================================
// FIND VEHICLE
// =====================================================
int findVehicle(String uid){
  uid.trim(); 

  for (int i = 0; i < NUMBER_OF_VEHICLES; i++) {
    if (uid.equalsIgnoreCase(vehicles[i].uid)) {
      return i;
    }
  }
  return -1;
}

// =====================================================
// GET VEHICLE WEIGHT
// =====================================================

float getVehicleWeight() {
  if (!scale.is_ready()) {
    Serial.println(
      "ERROR: HX711 NOT READY"
    );
    return 0;
  }

  // Take 10 readings and average
  float weight = scale.get_units(10);

  // Remove negative values
  if (weight < 0) {
    weight = 0;
  }

  // Ignore very small readings
  if (weight < 5) {
    weight = 0;
  }

  return weight;
}

// =====================================================
// OVERLOAD CHECK
// =====================================================

bool checkOverload(float actualWeight,float maximumWeight) {
  if (actualWeight > maximumWeight) {
    return true;
  }
  return false;
}

// =====================================================
// HANDLE OVERLOAD
// =====================================================

void handleOverload(float actualWeight, float maximumWeight) {
  Serial.println();
  Serial.println("**************************************");
  Serial.println("       !!! OVERLOAD DETECTED !!!" );
  Serial.println( "**************************************");
  Serial.print("Actual Weight: ");
  Serial.print(actualWeight, 1);
  Serial.println(" kg");
  Serial.print("Maximum Allowed: ");
  Serial.print(maximumWeight, 1);
  Serial.println(" kg");
  // ===================================================
  // KEEP GATE CLOSED
  // ===================================================
  gateServo.write(
    GATE_CLOSED
  );

  // ===================================================
  // LCD WARNING
  // ===================================================

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("!!! OVERLOAD !!!");
  lcd.setCursor(0, 1);
  lcd.print(actualWeight, 0);
  lcd.print("/");
  lcd.print(maximumWeight, 0);
  lcd.print("kg");
  // ===================================================
  // ALARM
  // ==================================================
  for (int i = 0; i < 10; i++){
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
  // ===================================================
  // GATE LOCKED MESSAGE
  // ===================================================
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("OVERLOAD");
  lcd.setCursor(0, 1);
  lcd.print("GATE LOCKED");

  Serial.println("GATE LOCKED");
  Serial.println("Vehicle cannot pass.");
  delay(5000);

  // ===================================================
  // SHOW WEIGHT AGAIN
  // ===================================================

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("Wt:");
  lcd.print(actualWeight, 0);
  lcd.print("kg");
  lcd.setCursor(0, 1);
  lcd.print("MAX:");
  lcd.print(maximumWeight, 0);
  lcd.print("kg");

  delay(3000);
  showWelcome();
}

// =====================================================
// CALCULATE TOLL
// =====================================================

int calculateToll(float weight) {
  if (weightClass == "CAR"){
    return 50;
  } else if (weightClass == "SUV") {
    return 120;
  } else if (weightClass == "TRUCK") {
    return 250;
  } else {
    return 350;
  }
}

// =====================================================
// OPEN GATE
// =====================================================

void openGate(){
  Serial.println("Opening gate...");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TOLL ACCEPTED");
  lcd.setCursor(0, 1);
  lcd.print("GATE OPENING");

  beep(2);

  gateServo.write(GATE_OPEN);

  delay(1500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PLEASE PROCEED");
  lcd.setCursor(0, 1);
  lcd.print("GATE OPEN");
}

// =====================================================
// CLOSE GATE
// =====================================================

void closeGate(){
  Serial.println("Closing gate...");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GATE CLOSING");

  gateServo.write(GATE_CLOSED);

  beep(1);

  delay(1500);

  Serial.println("Gate closed.");
}

// =====================================================
// WELCOME SCREEN
// =====================================================

void showWelcome(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TOLL MANAGEMENT");
  lcd.setCursor(0, 1);
  lcd.print("SCAN RFID CARD");
}

// =====================================================
// NORMAL BEEP
// =====================================================

void beep(int count){
  for (int i = 0; i < count; i++ ) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW );
    delay(100);
  }
}

// =====================================================
// ERROR BEEP
// =====================================================

void errorBeep(){
  for (int i = 0;i < 3;i++) {
    digitalWrite(BUZZER_PIN,HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN,LOW );
    delay(200);
  }
}

// =====================================================
// STOP RFID COMMUNICATION
// =====================================================

void stopRFID(){
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}