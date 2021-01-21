//  Automatic Water Dispenser
//  with Google Sheets logging
//  https://github.com/StorageB/Water-Dispenser

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "HTTPSRedirect.h"

#define valve_output    D1      // valve output pin
#define ir_input        D5      // ir sensor input piin
#define switch_input    D7      // pushbutton switch input pin
#define led_pin         D2      // NeoPixel strip signal pin
#define led_count       8       // number of LEDs in NeoPixel strip
#define led_brightness  125     // NeoPixel brightness (max = 255)
#define pwm_intervals   100     // number of intervals in the fade in/out for loops    
#define ir_input_delay  100     // how long to wait once an object is detected by IR sensor before opening valve
#define log_delay       60000   // amount of time to wait before logging data
#define error_time      120000  // amount of time valve can be open before automatically turning off and displaying an error

int ir_state;                   // state of IR sensor - LOW if object detected, HIGH if no object detected
int switch_state;               // state of pushbutton switch - HIGH if pressed, LOW if not pressed
int error_status = 0;           // set to 0 if no errors

bool light_on = false;          // is NeoPixel strip on?
bool valve_open = false;        // is valve open?
bool data_published = false;    // has data been published? 
bool log_timer_started = false; // has the log timer been started?

unsigned long current_time = 0; // used to get current time
unsigned long timer_start = 0;  // used to start timer to keep track of how long valve is open
unsigned long run_time = 0;     // used to calculate how long the valve was open
unsigned long run_total = 0;    // used to keep track of total run time before publishing time
unsigned long log_timer = 0;    // used to start timer to determine when to publish data

float R = (pwm_intervals * log10(2))/(log10(255)); // used to calculate the 'R' value for fading LEDs

// Declare NeoPixel strip object:
Adafruit_NeoPixel strip(led_count, led_pin, NEO_GRB + NEO_KHZ800);

// Enter network credentials
#ifndef STASSID
#define STASSID "network"
#define STAPSK  "password"
#endif
const char* ssid = STASSID;
const char* password = STAPSK;

// Enter Google Script ID here
const char *GScriptId = "enter_google_script_id_here";

// Enter command and Google Sheets sheet name here:
String payload_base =  "{\"command\": \"insert_row\", \"sheet_name\": \"Sheet1\", \"values\": ";
String payload = "";

// Information to write to Google Sheets
const char* host = "script.google.com";
const int httpsPort = 443;
const char* fingerprint = "";
String url = String("/macros/s/") + GScriptId + "/exec?cal";

HTTPSRedirect* client = nullptr;


void setup() {
  Serial.begin(9600);
  Serial.flush();

  // ----- Project setup -----

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize on board LED as output  
  pinMode(valve_output, OUTPUT);    // Initialize pin as digital output   (solenoid valve)
  pinMode(ir_input, INPUT);         // Initialize pin as digital input    (infrared sensor)
  pinMode(switch_input, INPUT);     // Initialize pin as digital input    (pushbutton switch)
  
  digitalWrite(LED_BUILTIN, HIGH);  // LED off
  digitalWrite(valve_output, LOW);  // valve closed

  strip.begin();                        // Initialize NeoPixel strip object (required)
  strip.show();                         // Turn OFF all pixels ASAP
  strip.setBrightness(led_brightness);  // Set brightness (max = 255)

  // Show red LEDs while system is connecting to the internet and to Google server
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,100,0,0);
    strip.show();
    delay(3);
  }
  
  // ----- Required for OTA programming -----

  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");
  // No authentication by default
  // ArduinoOTA.setPassword("admin");
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
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
  Serial.println("Ready");
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
    //Serial.println("Exiting...");
    return;
  }

  // delete HTTPSRedirect object
  delete client;
  client = nullptr;

  // Turn off LEDs
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,0,0,0);
    strip.show();
    delay(3);
  }

}

// Fade LEDs on
void fade_in(String fade_color, int wait) {
  int brightness = 0;
  for(int i = 0; i <= pwm_intervals; i++){
    brightness = pow (2, (i / R)) - 1;
    for(int j = 0; j < strip.numPixels(); j++) {
      if(fade_color == "blue") {
        strip.setPixelColor(j,0,0,brightness);
      }
      if (fade_color == "red") {
        strip.setPixelColor(j,brightness,0,0);
      }
      if (fade_color == "green") {
        strip.setPixelColor(j,0,brightness,0);
      }
    }
    strip.show();
    delay(wait);
  }
}

// Fade LEDs off
void fade_out(String fade_color, int wait) {
  int brightness = 255;
  for(int i = pwm_intervals; i >= 0; i--){
    brightness = pow (2, (i / R)) - 1;
    for(int j = 0; j < strip.numPixels(); j++) {
      if(fade_color == "blue") {
        strip.setPixelColor(j,0,0,brightness);
      }
      if (fade_color == "red") {
        strip.setPixelColor(j,brightness,0,0);
      }
      if (fade_color == "green") {
        strip.setPixelColor(j,0,brightness,0);
      }
    }
    strip.show();
    delay(wait);
  }
  strip.clear();
  strip.show();
}

// Handle errors based on error_status value
void error() {
  digitalWrite(valve_output, LOW); // close valve
  Serial.print("error status ");
  Serial.println(error_status);

  // error_status 1: water running for too long
  // board must be reset manually before used again
  while (error_status == 1) {
    // flash onboard LED and red NeoPixels until board is reset
    fade_in("red", 3);
    digitalWrite(LED_BUILTIN, LOW);
    fade_out("red", 3);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
  }

  // error_status 2: could not connect to Google Sheets
  // allow program to continue and try to publish data again later
  if(error_status == 2) {
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
}

// Open valve and turn on NeoPixels
void turn_on() {
  if (!valve_open) {
    digitalWrite(LED_BUILTIN, LOW);   // LED on
    digitalWrite(valve_output, HIGH); // valve open
    timer_start = millis();           // start timer
    if (log_timer_started == false) {
      log_timer = millis();           // start timer
      log_timer_started = true;
    }
    valve_open = true;
    Serial.print("valve open at ");
    Serial.println(timer_start);
  }
  if (!light_on) {
    fade_in("blue", 5); // turn on blue NeoPixels
    light_on = true;
  }
  // Report error if valve has been open for longer than the specified error_time
  if (valve_open) {
    current_time = millis();
    if (current_time - timer_start > error_time) {
      error_status = 1;
      error();
    }
  }
  delay(10);
}

// Close valve and turn off NeoPixels
void turn_off() {
  if (valve_open) {
    digitalWrite(LED_BUILTIN, HIGH); // LED off
    digitalWrite(valve_output, LOW); // valve closed
    current_time = millis();         // get current time
    valve_open = false;
    data_published = false;
    Serial.print("valve closed at ");
    Serial.println(current_time);
    run_time = (current_time - timer_start); // calculate how long valve was open
    Serial.print("valve was open for ");
    Serial.print(run_time);
    Serial.println(" ms");
    run_total = run_total + run_time; // keep track of total time valve has been open until data is published
  }
  if (light_on) {
    fade_out("blue", 5); // turn off blue NeoPixels
    light_on = false;
  }
  delay(10);
}



void loop() {
  ArduinoOTA.handle(); // Required for OTA programming

  // Publish data to Google Sheets
  if (!data_published) {
    current_time = millis();
    if (current_time - log_timer > log_delay) {
      static bool flag = false;
      if (!flag){
          client = new HTTPSRedirect(httpsPort);
          client->setInsecure();
          flag = true;
          client->setPrintResponseBody(true);
          client->setContentTypeHeader("application/json");
        }
        if (client != nullptr){
          if (!client->connected()){
            client->connect(host, httpsPort);
          }
        }
        else{
          Serial.println("Error creating client object!");
        }
        // publish data
        fade_in("green", 3);
        digitalWrite(LED_BUILTIN, LOW);
        payload = payload_base + "\"" + run_total + "\"}";
        if(client->POST(url, host, payload)){ // attempt to publish
          Serial.print("total run time: ");
          Serial.println(run_total);
          Serial.println("----------------------------");
          data_published = true;
          log_timer_started = false;
          run_total = 0;
          digitalWrite(LED_BUILTIN, HIGH);
          fade_out("green", 3);
        }
        else{ // publish has failed
          Serial.println("Error while connecting: ");
          error_status = 2;
          error();
          log_timer = millis(); //restart the timer and try to publish again later
        }
    }
  }

  ir_state = digitalRead(ir_input);         // get status of IR sensor
  switch_state = digitalRead(switch_input); // get status of switch
  
  // button pressed
  while (switch_state == HIGH) {
    turn_on();
    switch_state = digitalRead(switch_input);
    if (switch_state == LOW) {
      turn_off();
    }
  }

  // object detected
  while (ir_state == LOW) {
    delay(ir_input_delay);
    ir_state = digitalRead(ir_input);
    if (ir_state == LOW) {
      turn_on();
    }
    else if (ir_state == HIGH) {
      turn_off();
    }
  }
  
  delay(50);
}

