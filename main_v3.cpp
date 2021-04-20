//  ===========================================
//  Automatic Water Dispenser
//  https://github.com/StorageB/Water-Dispenser
//
    #define version_number "v3.2021-04-19-1112"
//  ===========================================


#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <HTTPSRedirect.h>
#include <NTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Timezone.h>

#define valve_output      D1          // valve output pin
#define ir1_input         D5          // ir 1 sensor input pin
#define ir2_input         D6          // ir 2 sensor input pin
#define switch1_input     D7          // pushbutton input pin
#define led_pin           D8          // NeoPixel ring signal pin

#define led_count         28          // number of LEDs in NeoPixel ring
#define pwm_intervals     20          // number of intervals in the fade in/out for loops for fading LEDs
#define ir_input_delay    100         // how long to wait once an IR sensor is triggered before opening valve (to prevent false triggers)
#define sw_input_delay    30          // how long to wait once switch is pressed before opening valve (debounce)
#define log_delay         240000      // amount of time to wait before publishing data to Google Sheets
#define display_off_delay 3000        // amount of time to wait once valve is closed before turning off the display LEDs
#define error_time        300000      // amount of time valve can be open before automatically turning off and displaying an error (protect against blocked or failed sensor, disconnected or shorted wiring, etc)
#define cycle_time        250         // amount of time valve must remain closed before reopening (allow valve to fully close before attempting to reopen and prevent rapid on/off switching of valve)
#define turn_off_delay    400         // amount of time to wait to turn off valve after sensor no longer detects an object
#define button_hold_time  850         // amount of time to hold button down before next button hold function (used to select different automatic dispense preset amounts: 16oz, 24oz, 32oz, etc.)
#define led_blink         700         // amount of time delay between flashing LEDs during auto dispense mode
#define time_check        300000      // how often to check the time from the NPT server
#define dim_factor        10          // factor by which to dim the LEDs during afterhours times

bool debug_mode = false;              // debug mode disables the valve from turning on and enables green lights when publishing (water usage data will still be calculated and published)
bool display_orange_led = false;      // ***NOTE: need to increase turn_off_delay to ~800 if this is true***     display orange LEDs in IR mode when object is out of sensor range when water is dispensing when set to true

int led_brightness = 255;             // NeoPixel brightness (max = 255)
int ir1_state;                        // state of IR sensor 1: LOW if object detected, HIGH if no object detected
int ir2_state;                        // state of IR sensor 2: LOW if object detected, HIGH if no object detected
int switch1_state;                    // state of pushbutton: HIGH if pressed, LOW if not pressed
int error_status = 0;                 // used to report an error, set to 0 if no errors
int current_hour = 12;                // current hour of the day (0 to 23) (value will be set from )
int total_gallons = 0;                // total gallons of water used  (default value set, but will import value from Google Sheets at startup and after publishing data)
int oz_target = 128;                  // total ounces daily target    (default value set, but will import value from Google Sheets at startup and after publishing data)
int filter_change = 500;              // what value to change filter  (default value set, but will import value from Google Sheets at startup and after publishing data)
int brightness;                       // used in the fade_in and fade_out loops to calculate and set LED brightness
int button_press_multiplier = 1;      // used to determine the next function when holding down the button
int function_1_oz = 0;                // automatic dispense ounces (default value set, but will import value from Google Sheets at startup and after publishing data)
int function_2_oz = 0;                // automatic dispense ounces (default value set, but will import value from Google Sheets at startup and after publishing data)
int function_3_oz = 0;                // automatic dispense ounces (default value set, but will import value from Google Sheets at startup and after publishing data)
int function_4_oz = 0;                // automatic dispense ounces (default value set, but will import value from Google Sheets at startup and after publishing data)
int function_5_oz = 0;                // automatic dispense ounces (default value set, but will import value from Google Sheets at startup and after publishing data)
int automatic_dispense_oz = 0;        // how much water to dispense automatically (based on which amount was selected when the button is held down)
int automatic_dispense_time = 0;      // calculated length of time to keep water on when automatically dispensing
int afterhours_start = -1;            // beginning hour of afterhours time (0 to 23, with 0 being midnight and 23 being 11pm, -1 to disable) (default value set, but will import value from Google Sheets at startup and after publishing data)
int afterhours_stop = -1;             // ending hour of afterhours time    (0 to 23, with 0 being midnight and 23 being 11pm, -1 to disable) (default value set, but will import value from Google Sheets at startup and after publishing data)

bool display_on = false;              // is the display on?
bool led_on = false;                  // is the LED ring on?
bool valve_open = false;              // is the valve open?
bool data_published = false;          // has current data been published? 
bool case_off = false;                // is the button function set to off? (the case when the button is held down long enough to cycle through all the preset functions and should now not dispense any water when button is released)
bool restart_clock = true;            // does the timer used to determine when to check the current time need to be restarted? (has time been checked?)
bool afterhours = false;              // used for afterhours settings (dim LEDs)
bool orange_led = false;              // used in fade_out function call if the fade out color should be orange (when using IR sensors) instead of blue (when using push button)

bool button_pressed = false;          // mode of operation: button pressed
bool auto_dispense = false;           // mode of operation: button pressed and held down for automatic operation using a timer
bool sensor_triggered = false;        // mode of operation: IR sensor

unsigned long current_time = 0;       // used to get the current time
unsigned long timer_start = 0;        // used to start timer to keep track of how long the valve is open
unsigned long run_time = 0;           // used to calculate how long the valve was open
unsigned long run_total = 0;          // used to keep track of total run time before publishing time
unsigned long log_timer = 0;          // used to determine when to publish data
unsigned long clock_timer = 0;        // used to determine when to check the current time
unsigned long display_timer = 0;      // used to determine when to turn off the display LEDs
unsigned long turn_off_timer = 0;     // used to determine when to turn off the value when the IR sensors are no longer triggered 
unsigned long blink_time = 0;         // used to determine when to blink the LED during auto dispense mode
unsigned long button_press_time = 0;  // used to determine when the button was pressed

float conversion_factor = 0.0000;     // gallons per second conversion factor (default value set, but will update from Google Sheets at startup and after publishing data)
float R = (pwm_intervals * log10(2))/(log10(255));  // used to calculate the 'R' value for fading LEDs


// Enter network credentials
#ifndef STASSID
#define STASSID "network"
#define STAPSK  "password"
#endif
const char* ssid = STASSID;
const char* password = STAPSK;

// Enter Google Script ID here
const char *GScriptId = "enter_google_script_id_here"";
#define gs_version_number "Version 48" // the version of the Google Scripts deployment listed above (not required, only for printing out version number at boot)

// Enter command and Google Sheets sheet name here
String payload_base =  "{\"command\": \"insert_row\", \"sheet_name\": \"Sheet1\", \"values\": ";
String payload = "";

// Information for reading and writing to Google Sheets (do not edit)
const char* host = "script.google.com";
const int httpsPort = 443;
const char* fingerprint = "";
String url = String("/macros/s/") + GScriptId + "/exec?cal";
String return_string = "";

// Define HTTPSRedirect client
HTTPSRedirect* client = nullptr;

// US Central Time Zone (Chicago, IL)
TimeChangeRule myDST = {"CDT", Second, Sun, Mar, 2, -300}; // Daylight time = UTC - 5 hours
TimeChangeRule mySTD = {"CST", First, Sun, Nov, 2, -360};  // Standard time = UTC - 6 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev

// Declare NeoPixel strip object
Adafruit_NeoPixel strip(led_count, led_pin, NEO_GRB + NEO_KHZ800);


// Function to return the compile date and time as a time_t value
time_t compileTime()
{
    const time_t FUDGE(10); // fudge factor to allow for compile time (seconds, YMMV)
    const char *compDate = __DATE__, *compTime = __TIME__, *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char chMon[4], *m;
    tmElements_t tm;

    strncpy(chMon, compDate, 3);
    chMon[3] = '\0';
    m = strstr(months, chMon);
    tm.Month = ((m - months) / 3 + 1);

    tm.Day = atoi(compDate + 4);
    tm.Year = atoi(compDate + 7) - 1970;
    tm.Hour = atoi(compTime);
    
    tm.Minute = atoi(compTime + 3);
    tm.Second = atoi(compTime + 6);
    time_t t = makeTime(tm);
    return t + FUDGE; // add fudge factor to allow for compile time
}


void setup() {
  
  Serial.begin(9600);
  Serial.flush();

  pinMode(LED_BUILTIN, OUTPUT);         // initialize on-board LED as output
  pinMode(valve_output, OUTPUT);        // initialize pin as digital output   (solenoid valve)
  pinMode(ir1_input, INPUT);            // initialize pin as digital input    (infrared sensor 1)
  pinMode(ir2_input, INPUT);            // initialize pin as digital input    (infrared sensor 2)
  pinMode(switch1_input, INPUT);        // initialize pin as digital input    (pushbutton)
  
  digitalWrite(LED_BUILTIN, HIGH);      // LED off
  digitalWrite(valve_output, LOW);      // valve closed

  strip.begin();                        // initialize NeoPixel ring object (required)
  strip.show();                         // turn off all pixels ASAP
  strip.setBrightness(led_brightness);  // set brightness

  // Show red LEDs while system is connecting to the internet and to Google server
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,255,0,0);
    strip.show();
    delay(3);
  }

  // Print startup info
  Serial.println("");
  Serial.print("Water Dispenser ");
  Serial.println(version_number);
  Serial.print("Google Scripts deployment: ");
  Serial.println(gs_version_number);

  // Set the time
  setTime(myTZ.toUTC(compileTime()));
  

  // ----- Required for OTA programming -----

  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  // ----- Required for writing to Google Sheets -----

  // Use HTTPSRedirect class to create a new TLS connection
  client = new HTTPSRedirect(httpsPort);
  client->setInsecure();
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");
  Serial.print("Connecting to ");
  Serial.println(host);

  // Try to connect for a maximum of 5 times
  bool flag = false;
  for (int i=0; i<5; i++){
    int retval = client->connect(host, httpsPort);
    if (retval == 1) {
       flag = true;
       Serial.println("Connected");
       break;
    }
    else
      Serial.println("Connection failed. Retrying...");
  }

  if (!flag){
    Serial.print("Could not connect to server: ");
    Serial.println(host);
    Serial.println("Exiting...");
    return;
  }

  // Delete HTTPSRedirect object
  delete client;
  client = nullptr;

  // Turn off LEDs at the end of startup
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,0,0,0);
    strip.show();
  }

  Serial.println("Ready");
}


// Format and print a time_t value with a time zone appended, assign the local time zone and daylight savings adjusted hour to current_hour
void printDateTime(time_t t, const char *tz)
{
    char buf[32];
    char m[4];    // temporary storage for month string (DateStrings.cpp uses shared buffer)
    strcpy(m, monthShortStr(month(t)));
    sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d %s",
        hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t), tz);
    Serial.println(buf);
    current_hour =  hour(t);
}


// Fade LEDs on
void fade_in(String fade_color, int wait) {
  for(int i = 0; i <= pwm_intervals; i++) {
    brightness = pow (2, (i / R)) - 1;
    for(int j = 0; j < strip.numPixels(); j++) {
      if(fade_color == "blue") { 
        if (!afterhours) {strip.setPixelColor(j,0,0,brightness);}             // LEDs set to full brightness
        if (afterhours)  {strip.setPixelColor(j,0,0,brightness/dim_factor);}  // LEDs dimmed during afterhours timeframe
      }
      if (fade_color == "red") {
        if (!afterhours) {strip.setPixelColor(j,brightness,0,0);}
        if (afterhours)  {strip.setPixelColor(j,brightness/dim_factor,0,0);}
      }
      if (fade_color == "green") {
        if (!afterhours) {strip.setPixelColor(j,0,brightness,0);}
        if (afterhours)  {strip.setPixelColor(j,0,brightness/dim_factor,0);}
      }
      if (fade_color == "purple") {
        if (!afterhours) {strip.setPixelColor(j,brightness,0,brightness);}
        if (afterhours)  {strip.setPixelColor(j,brightness/dim_factor,0,brightness/dim_factor);}
      }      
    }
    strip.show();
    delay(wait);
  }
  led_on = true;
}


// Fade LEDs off
void fade_out(String fade_color, int wait) {
  for(int i = pwm_intervals; i >= 0; i--){
    brightness = pow (2, (i / R)) - 1;
    for(int j = 0; j < strip.numPixels(); j++) {
      if(fade_color == "blue") {
        if (!afterhours) {strip.setPixelColor(j,0,0,brightness);}             // LEDs set to full brightness
        if (afterhours)  {strip.setPixelColor(j,0,0,brightness/dim_factor);}  // LEDs dimmed during afterhours timeframe
      }
      if (fade_color == "red") {
        if (!afterhours) {strip.setPixelColor(j,brightness,0,0);}
        if (afterhours)  {strip.setPixelColor(j,brightness/dim_factor,0,0);}
      }
      if (fade_color == "green") {
        if (!afterhours) {strip.setPixelColor(j,0,brightness,0);}
        if (afterhours)  {strip.setPixelColor(j,0,brightness/dim_factor,0);}
      }
      if (fade_color == "purple") {
        if (!afterhours) {strip.setPixelColor(j,brightness,0,brightness);}
        if (afterhours)  {strip.setPixelColor(j,brightness/dim_factor,0,brightness/dim_factor);}
      }
      if (fade_color == "orange") {
        if (!afterhours) {strip.setPixelColor(j,  brightness*0.75,              brightness*0.25,             0);}
        if (afterhours)  {strip.setPixelColor(j, (brightness/dim_factor)*0.75, (brightness/dim_factor)*0.25, 0);}
      }         
    }
    strip.show();
    delay(wait);
  }
  strip.clear();
  strip.show();
  led_on = false;
  orange_led = false;
}


/*// Show LED animations or flashing lights if button is held down long enough just for fun
void LED_animation() {
  for (int i = 0; i < 10; i++) {
    fade_in("red", 7);
    fade_out("red", 7);
    fade_in("green", 7);
    fade_out("green", 7);
    fade_in("purple", 7);
    fade_out("purple", 7);
    fade_in("blue", 7);
    fade_out("blue", 7);
  }
}*/


// Check the time (get current hour of 0 to 23)
void check_time() {
  if (restart_clock) {
    clock_timer = millis(); // used to check how long it has been since last checked the time
    restart_clock = false;
  }
  current_time = millis();
  if (current_time - clock_timer > time_check) { // time_check is how often to check the time
    restart_clock = true;
    time_t utc = now();                     // gets current UTC time
    time_t local = myTZ.toLocal(utc, &tcr); // gets current local time
    //printDateTime(utc, "UTC");            // sets current_hour and prints UTC time
    printDateTime(local, tcr -> abbrev);    // sets current_hour and prints local time
    //Serial.print("current hour: "); Serial.println(current_hour); //current_hour assigned in printDateTime function
    
    // Turn afterhours on or off based on current time and inputs from Google Sheets with the following if/elseif block
    // if afterhours start time or afterhours stop time == -1 disable afterhours functions
    if (afterhours_start == -1 || afterhours_stop == -1) {
      afterhours = false;
      Serial.println("afterhours mode: disabled"); 
    }
    // if afterhours start time > afterhours stop time (example: start at hour 23 and end at 8)
    else if (afterhours_start > afterhours_stop) {
      if (current_hour >= afterhours_start || current_hour < afterhours_stop) { // check to see if time is during afterhours
        afterhours = true;
        Serial.println("afterhours mode: ON"); 
      }
      else {
        afterhours = false;
        Serial.println("afterhours mode: OFF"); 
      }
    }
    // if afterhours start time == afterhours stop time (example: start at hour 2 and end at 2)
    else if (afterhours_start == afterhours_stop) {
      afterhours = false;
      Serial.println("afterhours mode: OFF"); 
     
    }
    // if afterhours start time < afterhours stop time (example: start at hour 0 and end at 8)
    else if (afterhours_start < afterhours_stop) {
      if (current_hour >= afterhours_start && current_hour < afterhours_stop) { // check to see if time is during afterhours
        afterhours = true;
        Serial.println("afterhours mode: ON"); 
      }
      else {
        afterhours = false;
        Serial.println("afterhours mode: OFF"); 
      }
    }
  } 
}


// Handle errors based on error_status value
void error() {

  Serial.print("error status ");
  Serial.println(error_status);

  // error_status 1: water running for too long
  // close valve and flash red LEDs, board must be reset manually before used again
  while (error_status == 1) {
    digitalWrite(valve_output, LOW);
    fade_in("red", 10);
    digitalWrite(LED_BUILTIN, LOW);
    fade_out("red", 10);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
  }

  // error_status 2: could not connect to Google Sheets (only enabled when debug mode is on)
  // allow program to continue and try to publish data again later
  if(error_status == 2 && debug_mode == true) {
    // flash onboard LED and green NeoPixels
    for(int k = 0; k <= 3; k++) {
      fade_out("green", 1);
      digitalWrite(LED_BUILTIN, LOW);
      fade_in("green", 1);
      digitalWrite(LED_BUILTIN, HIGH);
    }
    fade_out("green", 1);
    delay(10);
  }

  // error_status 3: filter change warning
  // flash red LEDs then allow program to continue, reset error_status back to zero because the turn_off function will check for change filter each time valve is turned off
  if (error_status == 3) {
    // flash onboard LED and red NeoPixels
    for(int k = 0; k <= 4; k++) {
      fade_in("red", 7);
      digitalWrite(LED_BUILTIN, HIGH);
      fade_out("red", 7);
      digitalWrite(LED_BUILTIN, LOW);
    }
    error_status = 0;   
  }
}


// Open valve and turn on NeoPixels
void turn_on() {
  if (!valve_open) {
    if (debug_mode == false) {digitalWrite(valve_output, HIGH);} // valve open
    digitalWrite(LED_BUILTIN, LOW);   // LED on
    timer_start = millis();           // time when valve turned on
    blink_time = timer_start;
    valve_open = true;
    if (debug_mode == true) {Serial.println("**DEBUG MODE**");}
    Serial.print("valve open at ");
    Serial.println(timer_start);
  }
  if (!led_on) {  // turn on blue LEDs
    fade_in("blue", 5);
    display_on = true;
  }  
}


// Close valve
void turn_off() {
  if (valve_open) {
    digitalWrite(LED_BUILTIN, HIGH); // LED off
    digitalWrite(valve_output, LOW); // valve closed
    current_time = millis();         // get current time
    valve_open = false;
    button_pressed = false;
    sensor_triggered = false;
    button_press_multiplier = 1; // reset back to 1 after valve is off
    auto_dispense = false;
    Serial.print("valve closed at ");
    Serial.println(current_time);
    run_time = (current_time - timer_start); // calculate how long valve was open
    Serial.print("valve was open for ");
    Serial.print(run_time);
    Serial.println(" ms");
    run_total = run_total + run_time; // keep track of total time valve has been open until data is published
    display_timer = current_time;
    delay(cycle_time); // allow valve to fully close before continuing
  }
}


// Publish and receive data from Google Sheets
void publish_data() {
  current_time = millis();
  if (current_time - log_timer > log_delay) {
    static bool flag = false;
    if (!flag) {
        client = new HTTPSRedirect(httpsPort);
        client->setInsecure();
        flag = true;
        client->setPrintResponseBody(true);
        client->setContentTypeHeader("application/json");
    }
    if (client != nullptr) {
      if (!client->connected()) {
        client->connect(host, httpsPort);
      }
    }
    else {
      Serial.println("Error creating client object!");
    }

    if (debug_mode == true) {fade_in("green", 5);}
    payload = payload_base + "\"" + run_total + "\"}"; 
    Serial.println("");
    if (debug_mode == true) {Serial.println("**DEBUG MODE**");}
    Serial.print("payload received: ");
    if(client->POST(url, host, payload)){ // attempt to publish
      Serial.print("total run time published: ");
      Serial.println(run_total);
      data_published = true;
      run_total = 0;
      digitalWrite(LED_BUILTIN, HIGH);
      return_string = client->getResponseBody();
      const size_t capacity = JSON_OBJECT_SIZE(11) + 150; //create json doc and allocate memory (use https://arduinojson.org/v6/assistant/ to determine memory)
      DynamicJsonDocument doc(capacity);
      deserializeJson(doc, return_string ); // get data from Google Sheets json string and assign values to appropriate variables
      total_gallons = doc["gallons"];
      conversion_factor = doc["conversion"];
      oz_target = doc["target"];
      filter_change = doc["filter"];
      function_1_oz = doc["a"];
      function_2_oz = doc["b"];
      function_3_oz = doc["c"];
      function_4_oz = doc["d"];
      function_5_oz = doc["e"];
      afterhours_start = doc["afterhours_start"];
      afterhours_stop = doc["afterhours_stop"];
      Serial.print("total gallons: ");
      Serial.println(total_gallons);
      Serial.print("filter change: ");
      Serial.println(filter_change);      
      Serial.print("conversion factor: ");
      Serial.println(conversion_factor, 4);
      Serial.print("oz_target: ");
      Serial.println(oz_target);
      Serial.print("automatic dispense presets: ");
      Serial.print(function_1_oz);  
      Serial.print(",");
      Serial.print(function_2_oz);  
      Serial.print(",");
      Serial.print(function_3_oz);  
      Serial.print(",");
      Serial.print(function_4_oz);  
      Serial.print(",");
      Serial.println(function_5_oz);  
      Serial.print("afterhours: from "); Serial.print(afterhours_start); Serial.print(" to "); Serial.println(afterhours_stop);
      Serial.print("payload sent: ");
      Serial.println(payload);
      Serial.println("");
      if (debug_mode == true) {fade_out("green", 5);}
    }
    else { // publish has failed
      error_status = 2;
      log_timer = millis(); //restart the timer and try to publish again later
      error();
    }                                                                            
  }
}


void loop() {
  ArduinoOTA.handle(); // required for OTA programming

  // Publish data to Google Sheets
  if (!valve_open && !display_on && !data_published) {
    publish_data();
  }

  // Read status of sensors and pushbutton
  ir1_state = digitalRead(ir1_input);         // get status of IR sensor 1
  ir2_state = digitalRead(ir2_input);         // get status of IR sensor 2
  switch1_state = digitalRead(switch1_input); // get status of pushbutton
  

  // Button has been pressed (press on, press off, hold down for automatic dispense functions)
  if (switch1_state == HIGH && !sensor_triggered) {
    delay(sw_input_delay);
    switch1_state = digitalRead(switch1_input);
    if (switch1_state == HIGH) {
      if (!valve_open) {
        button_pressed = true;
        button_press_time = millis();
        
        while (!valve_open && switch1_state == HIGH) { // enter while loop if button is still pressed
          switch1_state = digitalRead(switch1_input);  // check if button still held down
          current_time = millis();
          if (current_time - button_press_time > (button_hold_time * button_press_multiplier)) { // execute the cases below when button has been held down for correct amount of time (ex: case 1 executed when button held down for 1 second, case 2 at 2 seconds, etc.)
            if (function_1_oz != 0) { // only run automatic dispense function if data has been imported from google sheets, otherwise auto shut off won't work as the function_x_oz variables will all still be set to zero
              switch (button_press_multiplier) {
                case 1:
                  Serial.print("Function 1: ");
                  Serial.print(function_1_oz);
                  Serial.println("oz");
                  auto_dispense = true;
                  fade_in("purple", 7);
                  fade_out("purple", 7);
                  automatic_dispense_oz = function_1_oz;
                  button_press_multiplier ++;
                  break;
                case 2:                  
                  Serial.print("Function 2: ");
                  Serial.print(function_2_oz);
                  Serial.println("oz");
                  auto_dispense = true;
                  fade_in("purple", 7);
                  fade_out("purple", 7);
                  automatic_dispense_oz = function_2_oz;
                  button_press_multiplier ++;
                  break;
                case 3:                    
                  Serial.print("Function 3: ");
                  Serial.print(function_3_oz);
                  Serial.println("oz");
                  auto_dispense = true;
                  fade_in("purple", 7);
                  fade_out("purple", 7);
                  automatic_dispense_oz = function_3_oz;
                  button_press_multiplier ++;
                  break;
                case 4:                
                  Serial.print("Function 4: ");
                  Serial.print(function_4_oz);
                  Serial.println("oz");
                  auto_dispense = true;
                  fade_in("purple", 7);
                  fade_out("purple", 7);
                  automatic_dispense_oz = function_4_oz;
                  button_press_multiplier ++;
                  break;    
                case 5:      
                  Serial.print("Function 5: ");
                  Serial.print(function_5_oz);
                  Serial.println("oz");
                  auto_dispense = true;
                  fade_in("purple", 7);
                  fade_out("purple", 7);
                  automatic_dispense_oz = function_5_oz;
                  button_press_multiplier ++;
                  break;   
                case 6:
                  Serial.println("Function 6: Off");
                  auto_dispense = false;
                  case_off = true;
                  button_press_multiplier ++;
                  break;
                case 7:
                  Serial.println("Function 7: empty");            
                  button_press_multiplier ++;
                  break;
                case 8:
                  Serial.println("Function 8: publish/retrieve data");
                  fade_in("green", 5);
                  log_timer = log_delay + 1; // set the log timer greater than the log delay
                  publish_data();
                  fade_out("green", 5);
                  log_timer = millis(); // reset the log timer                     
                  button_press_multiplier ++;
                  break;                              
                default: // default case if none of the above cases match
                  break;
              }
            }
            else{ // publish data if the button has been held down but data has not yet been imported from Google Sheets
              fade_in("green", 5);
              case_off = true;
              log_timer = log_delay + 1; // set the log timer greater than the log delay
              publish_data();
              fade_out("green", 5);
              log_timer = millis(); // reset the log timer
            }
          }
          yield(); // required to keep from crashing in while loop
        } // end while
        
        // when button is released, turn on water unless button was held down to the 'off' function 
        if (case_off) { 
          button_pressed = false;
          button_press_multiplier = 1;
          Serial.println("automatic dispense off");
          case_off = false;
        }
        else {
          turn_on();  
        }
        
        // if automatically dispensing, calculate how long to leave water on
        if (auto_dispense) {
          automatic_dispense_time = automatic_dispense_oz / (conversion_factor * 0.001 * 128); 
          Serial.print("automatically dispensing ");
          Serial.print(automatic_dispense_oz);
          Serial.print("oz (");
          Serial.print(automatic_dispense_time);
          Serial.println("ms)");
        }

      }
      else if (valve_open) {  
        turn_off();
      }
    }
  } // end of button press
  

  // If automatically dispensing, turn valve off automatically based on calculated dispense time
  if (valve_open && auto_dispense) {
    current_time = millis();
    if (current_time - timer_start > automatic_dispense_time) {
      turn_off();
    }
  }


  // If button pressed, check for error if valve has been left open too long (if pressed and forgotten about)
  if (valve_open && button_pressed) {
    current_time = millis();
    if (auto_dispense) { // if automatically dispensing, flash LEDs instead of LEDs being solid on to indicate automatic dispense mode is activated
      if (current_time - blink_time > led_blink) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        if (led_on) {fade_out("blue", 1); } else { fade_in("blue", 1);}
        blink_time = current_time;
      }
    }
  }


  // IR sensor has been triggered
  if ((ir1_state == LOW || ir2_state == LOW) && !button_pressed) {
    if (display_orange_led){ // if displaying orange LEDs when object is out of sensor range, this is required to turn LEDs blue when back in range
      if (led_on) {
        for(int j = 0; j < strip.numPixels(); j++) {
          if (!afterhours) {strip.setPixelColor(j,0,0,led_brightness);}
          if (afterhours)  {strip.setPixelColor(j,0,0,led_brightness/dim_factor);}
        }
        strip.show();
      }
    }
    if (!sensor_triggered) { // only delay if the water isn't on yet (prevent false triggers), no need for delay once water is on as that is handled by turn_off_delay
      delay(ir_input_delay);
      ir1_state = digitalRead(ir1_input);
      ir2_state = digitalRead(ir2_input);      
    }
    if (ir1_state == LOW || ir2_state == LOW) {
      turn_on();
      turn_off_timer = millis();
      sensor_triggered = true;
      if(display_orange_led) {orange_led = true;}
    }    
  }


  // Turn off water when in IR sensor mode
  if (sensor_triggered && (ir1_state == HIGH && ir2_state == HIGH)) {
    if(display_orange_led){ // display orange LEDs if object out of sensor range when water is on
      for(int j = 0; j < strip.numPixels(); j++) {
        if (!afterhours) {strip.setPixelColor(j,  led_brightness*0.75,              led_brightness*0.25,             0);}
        if (afterhours)  {strip.setPixelColor(j, (led_brightness/dim_factor)*0.75, (led_brightness/dim_factor)*0.25, 0);}
      }    
      strip.show();
    }
    current_time = millis();
    if (current_time - turn_off_timer > turn_off_delay) {
      turn_off();
    }
  }


  // Turn display LEDs off after valve is closed and after display_off_delay amount of time has passed after valve cloased
  if (!valve_open && display_on) {
    current_time = millis();
    if (current_time - display_timer > display_off_delay) {
      if (led_on) { // turn off LEDs if they are currently on (could be off if flashing in automatic dispense mode)
        if (orange_led) {fade_out("orange", 10);}
        else {fade_out("blue", 10);}
      } 
      display_on = false;
      data_published = false;
      log_timer = millis(); // start log timer
      if(total_gallons > filter_change) { // check to see if filter needs to be changed
        error_status = 3;
        error();
      }
    }
  }


  // Report error if valve has been open for longer than the specified error_time
  if (valve_open){
    current_time = millis();
    if (current_time - timer_start > error_time) {
      error_status = 1;
      error();
    }
  }


  // check current time when system not in use
  if (!display_on && !sensor_triggered && !button_pressed) {
    check_time();
  }

}
