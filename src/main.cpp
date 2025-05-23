#include <Arduino.h>
//--------- lib---------------
#include <WiFi.h>
#include <WebServer.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>

#include <ESPmDNS.h> // Include the ESPmDNS library
#include <esp_task_wdt.h>
#include <EEPROM.h>
#define EEPROM_SIZE 512 // حجم الذاكرة المطلوبة (يمكن تعديله حسب الحاجة)
//---------------struct Schedule---------------
struct Schedule
{
  String time;      // Time in HH:MM format
  int duration;     // Duration in seconds
  String direction; // Direction ("east", "west", or "sleep")
};

// Declare an array to hold 5 schedules
Schedule schedules[5] = {
    {"00:00", 0, "east"}, // Example: Move east at 08:00 for 300 seconds
    {"00:00", 0, "west"}, // Example: Move west at 10:00 for 200 seconds
    {"00:00", 0, "east"}, // Example: Move east at 12:00 for 150 seconds
    {"00:00", 0, "west"}, // Example: Move west at 14:00 for 250 seconds
    {"00:00", 0, "east"}  // Example: Move east at 16:00 for 300 seconds
};

// Now the schedules array is globally accessible

// ----------------------- Configuration -----------------------
bool autoMode = true;
int morningStartHour = 7;
int nightReturnHour = 18;
int stepInterval = 30;
int motorStepTime = 30; // Variable now represents time in seconds
// ----------------------- ----------------------- 
int actualMotionTime = 0; // Variable to store the actual motion time

//--------saveCustomSettingsToEEPROM-------------
void saveCustomSettingsToEEPROM()
{
  int address = 10; // Start address for custom settings in EEPROM
  for (int i = 0; i < 5; i++)
  {
    // Save time (HH:MM format)
    for (int j = 0; j < schedules[i].time.length(); j++)
    {
      EEPROM.write(address++, schedules[i].time[j]);
    }
    EEPROM.write(address++, '\0'); // Null-terminate the string

    // Save duration
    EEPROM.write(address++, schedules[i].duration & 0xFF);        // Low byte
    EEPROM.write(address++, (schedules[i].duration >> 8) & 0xFF); // High byte

    // Save direction
    for (int j = 0; j < schedules[i].direction.length(); j++)
    {
      EEPROM.write(address++, schedules[i].direction[j]);
    }
    EEPROM.write(address++, '\0'); // Null-terminate the string
  }
  EEPROM.commit();
  Serial.println("✅ Custom settings saved to EEPROM");
}
//------loadCustomSettingsFromEEPROM()-----
void loadCustomSettingsFromEEPROM()
{
  int address = 10; // Start address for custom settings in EEPROM
  for (int i = 0; i < 5; i++)
  {
    // Load time (HH:MM format)
    char timeBuffer[6];
    int j = 0;
    while (true)
    {
      char c = EEPROM.read(address++);
      if (c == '\0' || j >= 5)
        break;
      timeBuffer[j++] = c;
    }
    timeBuffer[j] = '\0';
    schedules[i].time = String(timeBuffer);

    // Load duration
    int lowByte = EEPROM.read(address++);
    int highByte = EEPROM.read(address++);
    schedules[i].duration = (highByte << 8) | lowByte;

    // Load direction
    char directionBuffer[10];
    j = 0;
    while (true)
    {
      char c = EEPROM.read(address++);
      if (c == '\0' || j >= 9)
        break;
      directionBuffer[j++] = c;
    }
    directionBuffer[j] = '\0';
    schedules[i].direction = String(directionBuffer);
  }
  Serial.println("✅ Custom settings loaded from EEPROM");
}
// ----------------------- saveSettingsToEEPROM() -----------------------
void saveSettingsToEEPROM()
{
  EEPROM.writeBool(0, autoMode);
  EEPROM.write(1, morningStartHour);
  EEPROM.write(2, nightReturnHour);
  EEPROM.write(3, stepInterval);
  EEPROM.write(4, motorStepTime & 0xFF);        // حفظ الجزء  الاعلى
  EEPROM.write(5, (motorStepTime >> 8) & 0xFF); //حفظ الجزء الانى
  EEPROM.commit();
  Serial.println("✅ Settings saved to EEPROM");
}
//-----------loadSettingsFromEEPROM()-------------------
void loadSettingsFromEEPROM()
{
  autoMode = EEPROM.readBool(0);
  morningStartHour = EEPROM.read(1);
  nightReturnHour = EEPROM.read(2);
  stepInterval = EEPROM.read(3);
  motorStepTime = EEPROM.read(4) | (EEPROM.read(5) << 8);
  actualMotionTime = EEPROM.read(8) | (EEPROM.read(9) << 8);  // New: Load actual motion time
  Serial.println("✅ Settings loaded from EEPROM");
  
  EEPROM.commit();
}
//---------------
void saveMotionTimeToEEPROM() {
  EEPROM.write(8, actualMotionTime & 0xFF);
  EEPROM.write(9, (actualMotionTime >> 8) & 0xFF);
  EEPROM.commit();
}
// ----------------------- void validateOrResetSettings() -----------------------
void validateOrResetSettings()
{
  if (morningStartHour > 23)
    morningStartHour = 6;
  if (nightReturnHour > 23)
    nightReturnHour = 18;
  if (stepInterval < 1 || stepInterval > 60)
    stepInterval = 30;
  if (motorStepTime < 20 || motorStepTime > 3000)
    motorStepTime = 30;
}

// ----------------------- Configuration -----------------------
const char *ssid = "solar_track";
const char *password = "admin653";

const int RELAY_EAST = 26;
const int RELAY_WEST = 14;
const int SENSOR_EAST = 22;
const int SENSOR_WEST = 23;

// ----------------------- Global Variables -----------------------
bool isMovingEast = false;
bool isMovingWest = false;

unsigned long lastMoveTime = 0; // Stores last movement time
bool returningToEast = false;   // Track if we returned to East

ThreeWire myWire(15, 2, 4); //  DAT = GPIO15, CLK= GPIO2, RST = GPIO4
RtcDS1302<ThreeWire> Rtc(myWire);
WebServer server(80);

const char *adminPassword = "kb70503"; // Change this to your desired password
bool isAuthenticated = false;

// ----------------------- WiFi Access Point Setup -----------------------
void setupWiFi()
{
  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// ----------------------- Motor Control Functions -----------------------
void stopMotor()
{
  digitalWrite(RELAY_EAST, HIGH); // Set to HIGH to deactivate
  digitalWrite(RELAY_WEST, HIGH); // Set to HIGH to deactivate
  isMovingEast = false;
  isMovingWest = false;
  Serial.println("Motor stopped.");
}

void moveEast()
{
  if (digitalRead(SENSOR_EAST) == HIGH)
  {
    Serial.println("East limit reached");
    return;
  }
  if (isMovingWest)
  {
    Serial.println("Cannot move East while West is active!");
    stopMotor();
  }

  Serial.println("Moving East");
  digitalWrite(RELAY_EAST, LOW);  // Set to LOW to activate
  digitalWrite(RELAY_WEST, HIGH); // Set to HIGH to deactivate
  isMovingEast = true;
  isMovingWest = false;
}

void moveWest()
{
  if (digitalRead(SENSOR_WEST) == HIGH)
  {
    Serial.println("West limit reached");
    return;
  }
  if (isMovingEast)
  {
    Serial.println("Cannot move West while East is active!");
    stopMotor();
  }

  Serial.println("Moving West");
  digitalWrite(RELAY_WEST, LOW);  // Set to LOW to activate
  digitalWrite(RELAY_EAST, HIGH); // Set to HIGH to deactivate
  isMovingWest = true;
  isMovingEast = false;
}
//------------------handleUnlock()-------------
void handleUnlock()
{
  String password = server.arg("password");
  if (password == adminPassword)
  {
    isAuthenticated = true;
    Serial.println("✅ Password correct. Settings unlocked.");
  }
  else
  {
    isAuthenticated = false;
    Serial.println("❌ Incorrect password.");
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}

// ----------------------- Web Server Handlers -----------------------
void handleRoot()
{
  RtcDateTime now = Rtc.GetDateTime();
  char timeBuffer[20];
  sprintf(timeBuffer, "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());

  String html = "<!DOCTYPE html><html><head><title>وحدة التحكم بالطاقة الشمسية</title>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                // Replace the existing <style> section with the following:
                "<style>"
                "body {font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f8ff; color: #333; margin: 0; padding: 0;}"
                "h1 {color: #007bff; text-align: center; margin: 20px 0; font-size: 28px;}"
                ".container {max-width: 800px; margin: 20px auto; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 4px 10px rgba(0, 0, 0, 0.1);}"
                ".settings-box {margin-top: 20px; padding: 20px; background: #f9f9f9; border-radius: 10px; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);}"
                ".settings-box h2 {margin-bottom: 15px; font-size: 20px; color: #333; border-bottom: 2px solid #007bff; padding-bottom: 5px;}"
                ".schedule-row {border: 1px solid #ddd; padding: 15px; margin-bottom: 15px; border-radius: 8px; background: #fefefe; box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);}"
                "label {font-weight: bold; display: block; margin-bottom: 8px; color: #555;}"
                "input[type='text'], input[type='password'], input[type='number'], input[type='time'], input[type='checkbox'] {width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box; font-size: 14px;}"
                "input[type='checkbox'] {width: auto; margin-left: 10px;}"
                ".inline {display: flex; align-items: center; justify-content: space-between; margin-bottom: 15px;}"
                ".inline label {margin: 0; font-size: 14px; color: #555;}"
                ".btn-container {display: flex; justify-content: space-around; margin: 20px 0; gap: 15px; flex-wrap: wrap;}"
                ".btn {display: inline-block; padding: 15px 30px; background: linear-gradient(135deg, #007bff, #0056b3); color: #fff; border: none; border-radius: 10px; font-size: 18px; font-weight: bold; cursor: pointer; text-align: center; text-decoration: none; transition: all 0.3s ease; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);}"
                ".btn:hover {background: linear-gradient(135deg, #0056b3, #003f7f); transform: scale(1.1); box-shadow: 0 6px 8px rgba(0, 0, 0, 0.2);}"
                ".btn:active {transform: scale(0.95); box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);}"
                ".btn.stop {background: linear-gradient(135deg, #dc3545, #b02a37);}"
                ".btn.stop:hover {background: linear-gradient(135deg, #b02a37, #7a1d28);}"
                ".btn.stop:active {transform: scale(0.95); box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);}"
                ".time-display {text-align: center; font-size: 24px; font-weight: bold; color: #007bff; margin: 20px 0;}"
                "@media (max-width: 600px) {"
                "  .btn {padding: 10px 20px; font-size: 16px;}"
                "}"

                "</style></head><body>"
                "<div class='container'>"
                "<h1>وحدة التحكم بالطاقة الشمسية</h1>";
  // Display current time
  html += "<div class='time-display'>"
          "<p>الوقت الحالي: " +
          String(timeBuffer) + "</p>"
                               "</div>";

  // Movement buttons
  html += "<div class='btn-container'>"
          "<a href='/move?dir=east' class='btn'>تحرك شرقًا</a>"
          "<a href='/move?dir=west' class='btn'>تحرك غربًا</a>"
          "<a href='/move?dir=stop' class='btn stop'>إيقاف</a>"
          "</div>";

  // Add this section to the `handleRoot` function after the "General settings form" section
  html += "<form action='/settime' method='POST'>"
          "<div class='settings-box'>"
          "<h2>ضبط الوقت</h2>"
          "<label>الساعة:</label>"
          "<input type='number' name='hour' min='0' max='23' required>"
          "<label>الدقيقة:</label>"
          "<input type='number' name='minute' min='0' max='59' required>"
          "<label>الثانية:</label>"
          "<input type='number' name='second' min='0' max='59' required>"
          "<label>اليوم:</label>"
          "<input type='number' name='day' min='1' max='31' required>"
          "<label>الشهر:</label>"
          "<input type='number' name='month' min='1' max='12' required>"
          "<label>السنة:</label>"
          "<input type='number' name='year' min='2020' max='2099' required>"
          "<button type='submit' class='btn'>ضبط الوقت</button>"
          "</div>"
          "</form>";

  // General settings form
  html += "<form action='/settings' method='POST'>"
          "<div class='settings-box'>"
          "<h2>الإعدادات العامة</h2>"
          "<label>كلمة المرور:</label>"
          "<input type='password' name='password' required>"
          "<div class='inline'>"
          "<label>الوضع الألي:</label>"
          "<input type='checkbox' name='autoMode' " +
          String(autoMode ? "checked" : "") + "> "
                                              "</div>"
                                              "<label>ساعة بدء الصباح:</label>"
                                              "<input type='number' name='morningStart' value='" +
          String(morningStartHour) + "' min='0' max='23'>"
                                     "<label>ساعة العودة الليلية:</label>"
                                     "<input type='number' name='nightReturn' value='" +
          String(nightReturnHour) + "' min='0' max='23'>"
                                    "<label>فاصل الخطوة (دقيقة):</label>"
                                    "<input type='number' name='stepInterval' value='" +
          String(stepInterval) + "' min='1' max='60'>"
                                 "<label>زمن خطوة المحرك (ثانية):</label>"
                                 "<input type='number' name='motorStepTime' value='" +
          String(motorStepTime) + "' min='20' max='3000'>"
                                  "</div>";

  // Custom movement settings
  html += "<div class='settings-box'>"
          "<h2>إعدادات الحركة المخصصة</h2>";
  for (int i = 0; i < 5; i++)
  {
    html += "<div class='schedule-row'>"
            "<label>الصف " +
            String(i + 1) + ":</label>"
                            "<label>الوقت (HH:MM): <input type='time' name='time" +
            String(i) + "' value='" + String(schedules[i].time) + "' required></label>"
                                                                  "<label>المدة (ثانية): <input type='number' name='duration" +
            String(i) + "' min='1' max='3000' value='" + String(schedules[i].duration) + "' required></label>"
                                                                                         "<label>الاتجاه: <select name='direction" +
            String(i) + "'>"
                        "<option value='east'" +
            (schedules[i].direction == "east" ? " selected" : "") + ">شرق</option>"
                                                                    "<option value='west'" +
            (schedules[i].direction == "west" ? " selected" : "") + ">غرب</option>"
                                                                    "<option value='sleep'" +
            (schedules[i].direction == "sleep" ? " selected" : "") + ">نوم</option>"
                                                                     "</select></label>"
                                                                     "<label>نشط: <input type='checkbox' name='active" +
            String(i) + "' " + (schedules[i].duration > 0 ? "checked" : "") + "></label>"
                                                                              "</div>";
  }
  html += "</div>";

  // Save button
  html += "<button type='submit' class='btn'>حفظ الإعدادات</button>"
          "</form>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}
//---------------MovementSetting----------------
struct MovementSetting
{
  String parameter;
  String time;
  int duration;
};

MovementSetting movementSettings[5];
//------------------handleCustomMovement()-------------
void handleCustomMovement()
{
  for (int i = 0; i < 5; i++)
  {
    String timeKey = "time" + String(i);
    String durationKey = "duration" + String(i);
    String directionKey = "direction" + String(i);

    if (server.hasArg(timeKey) && server.hasArg(durationKey) && server.hasArg(directionKey))
    {
      schedules[i].time = server.arg(timeKey); // Time in HH:MM format
      schedules[i].duration = server.arg(durationKey).toInt();
      schedules[i].direction = server.arg(directionKey);
    }
  }

  saveCustomSettingsToEEPROM(); // Save updated settings to EEPROM

  Serial.println("✅ Custom movement settings updated:");
  for (int i = 0; i < 5; i++)
  {
    Serial.print("Row ");
    Serial.print(i + 1);
    Serial.print(": Time=");
    Serial.print(schedules[i].time);
    Serial.print(", Duration=");
    Serial.print(schedules[i].duration);
    Serial.print(", Direction=");
    Serial.println(schedules[i].direction);
  }

  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
}
//---------------- handleMove()--------------
void handleMove()
{
  String direction = server.arg("dir");
  if (direction == "east")
    moveEast();
  else if (direction == "west")
    moveWest();
  else if (direction == "stop")
    stopMotor();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}
//--------------------handleSettings()------------
void handleSettings()
{
  String password = server.arg("password");
  if (password != adminPassword)
  {
    server.sendHeader("Location", "/?error=1", true);
    server.send(302, "text/plain", "Redirecting...");
    return;
  }

  // Process general settings
  autoMode = server.arg("autoMode") == "on";
  morningStartHour = server.arg("morningStart").toInt();
  nightReturnHour = server.arg("nightReturn").toInt();
  stepInterval = server.arg("stepInterval").toInt();
  motorStepTime = server.arg("motorStepTime").toInt();

  // Process custom movement settings
  for (int i = 0; i < 5; i++)
  {
    String timeKey = "time" + String(i);
    String durationKey = "duration" + String(i);
    String directionKey = "direction" + String(i);
    String activeKey = "active" + String(i);

    if (server.hasArg(timeKey) && server.hasArg(durationKey) && server.hasArg(directionKey))
    {
      schedules[i].time = server.arg(timeKey);
      schedules[i].duration = server.arg(activeKey) == "on" ? server.arg(durationKey).toInt() : 0;
      schedules[i].direction = server.arg(directionKey);
    }
  }

  // Save all settings
  saveSettingsToEEPROM();
  saveCustomSettingsToEEPROM();

  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
}
//--------------------handleSetTime()------------
void handleSetTime()
{
  // Retrieve time values from the form
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  int second = server.arg("second").toInt();
  int day = server.arg("day").toInt();
  int month = server.arg("month").toInt();
  int year = server.arg("year").toInt();

  // Validate the input values
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
      day < 1 || day > 31 || month < 1 || month > 12 || year < 2020 || year > 2099)
  {
    Serial.println("❌ Invalid time input.");
    server.sendHeader("Location", "/?error=1", true);
    server.send(302, "text/plain", "Redirecting...");
    return;
  }

  // Set the RTC with the new time
  RtcDateTime newTime(year, month, day, hour, minute, second);
  Rtc.SetDateTime(newTime);

  Serial.println("✅ Time updated successfully.");
  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
}
//------------------------locationAcurately----------------
void locationAccurately() {
  if (digitalRead(SENSOR_EAST) == HIGH) {
    actualMotionTime = 0;
    saveMotionTimeToEEPROM();
    return;
  }

  RtcDateTime now = Rtc.GetDateTime();
  int currentHour = now.Hour();
  int currentMinute = now.Minute();
  
  if (currentHour >= morningStartHour && currentHour < nightReturnHour) {
    int totalMinutes = (currentHour - morningStartHour) * 60 + currentMinute;
    int expectedSteps = totalMinutes / stepInterval;
   // THIS IS WHERE THE NEW CODE GOES:
   int stepsNeeded = expectedSteps - actualMotionTime;
    
   if (stepsNeeded > 0 && digitalRead(SENSOR_WEST) == LOW) {
     Serial.printf("Need to move %d steps west\n", stepsNeeded);
     
     for(int i = 0; i < stepsNeeded && digitalRead(SENSOR_WEST) == LOW; i++) {
       moveWest();
       delay(motorStepTime * 1000);
       stopMotor();
       actualMotionTime++;  // Track this step
       saveMotionTimeToEEPROM(); // Save after each step
       delay(500); // Brief pause between steps
     }
   }
 }
 // 4. Reset at night
 else if (currentHour >= nightReturnHour) {
   actualMotionTime = 0;
   saveMotionTimeToEEPROM();
 }
}  
//--------logCurrentPosition()------------------
void logCurrentPosition() {
  Serial.print("Current position: ");
  Serial.print(actualMotionTime);
  Serial.print(" steps from east (");
  Serial.print(actualMotionTime * motorStepTime);
  Serial.println(" seconds of movement)");

}
//----safeMoveWest----------------------
void safeMoveWest(int duration) {
  unsigned long start = millis();
  while (millis() - start < duration && digitalRead(SENSOR_WEST) == LOW) {
    moveWest();
    delay(100); // Small delay for sensor checking
  }
  stopMotor();
}
// ----------------------- Setup  -----------------------
void setup()
{
  esp_task_wdt_init(10, true); // Reset if frozen for 10 seconds
  

  Serial.begin(115200);
  delay(5000); // Wait 5 seconds for power stabilization

  EEPROM.begin(EEPROM_SIZE);

  // Clear old motionTime values if they exist
if (EEPROM.read(6) != 255 || EEPROM.read(7) != 255) {
  EEPROM.write(6, 255);
  EEPROM.write(7, 255);
  EEPROM.commit();
}

  loadSettingsFromEEPROM();
  loadCustomSettingsFromEEPROM(); // Load custom settings from EEPROM
  validateOrResetSettings();

  pinMode(RELAY_EAST, OUTPUT);
  pinMode(RELAY_WEST, OUTPUT);

  // Explicitly set relays to inactive state before enabling them
  digitalWrite(RELAY_EAST, HIGH); // Ensure RELAY_EAST is inactive
  digitalWrite(RELAY_WEST, HIGH); // Ensure RELAY_WEST is inactive

  pinMode(SENSOR_EAST, INPUT_PULLDOWN);
  pinMode(SENSOR_WEST, INPUT_PULLDOWN);
  //------------------------------
  
    // ↓↓↓ ADD POSITION LOGGING HERE ↓↓↓
    Serial.print("Initial position: ");
    if(digitalRead(SENSOR_EAST) == HIGH) {
      Serial.println("EAST (home)");
    }
    else if(digitalRead(SENSOR_WEST) == HIGH) {
      Serial.println("WEST (max)");
    }
    else {
      Serial.printf("MIDDLE (step %d)\n", actualMotionTime);
    }
    // ↑↑↑ END OF ADDITION ↑↑↑
  
    
  //----------------------------------------------

  setupWiFi();
  Rtc.Begin();

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/settime", HTTP_POST, handleSetTime);
  server.on("/unlock", HTTP_POST, handleUnlock);

  server.begin();

  Serial.println("✅ Web server started");
}
//------------------processCustomMovements()-------------
void processCustomMovements()
{
  RtcDateTime now = Rtc.GetDateTime();
  char currentTime[6];
  sprintf(currentTime, "%02d:%02d", now.Hour(), now.Minute());

  for (int i = 0; i < 5; i++)
  {
    // Skip inactive rows (duration <= 0)
    if (schedules[i].duration <= 0)
    {
      continue;
    }

    if (schedules[i].time == String(currentTime)) // Compare as String
    {
      Serial.print("Executing custom movement: ");
      Serial.println(schedules[i].direction);

      if (schedules[i].direction == "east")
      {
        moveEast();
        delay(schedules[i].duration * 1000);
        stopMotor();
      }
      else if (schedules[i].direction == "west")
      {
        moveWest();
        delay(schedules[i].duration * 1000);
        stopMotor();
      }
      else if (schedules[i].direction == "sleep")
      {
        stopMotor(); // Explicitly stop motors before sleeping
        Serial.println("Sleep mode - no movement");
        delay(schedules[i].duration * 1000);
      }
    }
  }
}
//-----------------------------loop ----------
void loop()
{

  static bool firstRun = true;
  if (firstRun) {
    locationAccurately();
    firstRun = false;
  }

  server.handleClient();    // Always handle client requests
  processCustomMovements(); // Process custom movements first

  // Update time from RTC
  RtcDateTime now = Rtc.GetDateTime();
  int currentHour = now.Hour();
  unsigned long currentMillis = millis();

  // Prevent both relays from being active simultaneously
  if (isMovingEast && isMovingWest)
  {
    stopMotor();
    Serial.println("⚠️ Error: Both relays active! Stopping motor.");
  }

  // **Automatic Mode**
  if (autoMode)
  {
    // 🌞 **Morning: Move West at intervals**
    if (currentHour >= morningStartHour && currentHour < nightReturnHour)
    {
      returningToEast = false; // Reset nighttime return stat

      // Only execute general settings if no custom movement is active
      //if (!isMovingEast && !isMovingWest && (currentMillis - lastMoveTime >= (stepInterval * 60000)))
      //{  
        //------------------
        // In auto mode west movement section:
      if (!isMovingEast && !isMovingWest && (millis() - lastMoveTime >= (stepInterval * 60000))) {
       if (digitalRead(SENSOR_WEST) == LOW) {
         moveWest();
         delay(motorStepTime * 1000);
         stopMotor();
         // In loop():
         actualMotionTime += 1; // Correct increment
         saveMotionTimeToEEPROM();
         lastMoveTime = millis();
        }
      }
    }
        

    // 🌙 **Night: Return to East until reaching the sensor**
    else if (currentHour >= nightReturnHour && !returningToEast)
    {
      Serial.println("🌙 Auto Mode: Returning to East");

      // Continue moving East until the East sensor is triggered
      if (digitalRead(SENSOR_EAST) == LOW)
      {
        Serial.println("🌙 Moving East to return to start position...");
        moveEast(); // Ensure we are moving East, not West
      }
      else
      {
        Serial.println("✅ Reached East Position - Stopping motor");
        stopMotor();
        returningToEast = true; // Confirm return and prevent repetition
      }
    }
  }
}