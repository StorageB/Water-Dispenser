//  Automatic Water Dispenser
//  https://github.com/StorageB/Water-Dispenser

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define valve_output    D1      // valve output pin
#define ir_input        D5      // ir sensor input piin
#define switch_input    D7      // pushbutton switch input pin
#define led_pin         D2      // NeoPixel strip signal pin
#define led_count       8       // number of LEDs in NeoPixel strip
#define led_brightness  125     // NeoPixel brightness (max = 255)
#define pwm_intervals   100     // number of intervals in the fade in/out for loops    
#define ir_input_delay  100     // how long to wait once an object is detected by IR sensor before opening valve
#define error_time      120000  // amount of time valve can be open before automatically turning off and displaying an error

int ir_state;                   // state of IR sensor - LOW if object detected, HIGH if no object detected
int switch_state;               // state of pushbutton switch - HIGH if pressed, LOW if not pressed
int error_status = 0;           // set to 0 if no errors

bool light_on = false;          // is NeoPixel strip on?
bool valve_open = false;        // is valve open?

unsigned long current_time = 0; // used to get current time
unsigned long timer_start = 0;  // used to start timer to keep track of how long valve is open
unsigned long run_time = 0;     // used to calculate how long the valve was open

float R = (pwm_intervals * log10(2))/(log10(255)); // used to calculate the 'R' value for fading LEDs

// Declare NeoPixel strip object:
Adafruit_NeoPixel strip(led_count, led_pin, NEO_GRB + NEO_KHZ800);


void setup() {
  Serial.begin(9600);
  
  pinMode(LED_BUILTIN, OUTPUT);         // Initialize on board LED as output  
  pinMode(valve_output, OUTPUT);        // Initialize pin as digital output   (solenoid valve)
  pinMode(ir_input, INPUT);             // Initialize pin as digital input    (infrared sensor)
  pinMode(switch_input, INPUT);         // Initialize pin as digital input    (pushbutton switch)
  
  digitalWrite(LED_BUILTIN, HIGH);      // LED off
  digitalWrite(valve_output, LOW);      // valve closed

  strip.begin();                        // Initialize NeoPixel strip object (required)
  strip.show();                         // Turn OFF all pixels ASAP
  strip.setBrightness(led_brightness);  // Set brightness (max = 255)

  // Show red LEDs for 3 seconds when system is turned on
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,100,0,0);
    strip.show();
    delay(3);
  }
    delay(3000);
  
  // Turn off LEDs
  for(int j = 0; j < strip.numPixels(); j++) {
    strip.setPixelColor(j,0,0,0);
    strip.show();
    delay(3);
  }

}


// Function to fade LEDs on
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

// Function to fade LEDs off
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

// Function to handle errors based on error_status value
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
}

// Function to open valve and turn on NeoPixels
void turn_on() {
  if (!valve_open) {
    digitalWrite(LED_BUILTIN, LOW);   // LED on
    digitalWrite(valve_output, HIGH); // valve open
    timer_start = millis();           // start timer
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
        Serial.print("valve closed at ");
    Serial.println(current_time);
    run_time = (current_time - timer_start); // calculate how long valve was open
    Serial.print("valve was open for ");
    Serial.print(run_time);
    Serial.println(" ms");
  }
  if (light_on) {
    fade_out("blue", 5); // turn off blue NeoPixels
    light_on = false;
  }
  delay(10);
}


// Main loop begins here
void loop() {

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
    delay(ir_input_delay);            // delay before checking to see if the sensor is still triggered (to prevent false triggers)
    ir_state = digitalRead(ir_input); // check again to see if the sensor is still triggered before turning on valve
    if (ir_state == LOW) {
      turn_on();
    }
    else if (ir_state == HIGH) {
      turn_off();
    }
  }
  
  delay(50);
}
