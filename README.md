## Automatic Filtered Water Dispenser

### Project Description 

This project is an for an electronic water dispenser connected to an under counter filtration system. The most current version includes the following features:

- Automatically dispenses water when a glass is placed under the tap using two short range infrared sensors
- Dispenses water when a button is pressed
- Logs water usage data to a Google Sheets document to keep track of daily and total water usage
- Bottle fill function
  - Press and hold button to select from a preset amount of water to dispense (16oz, 24oz, 32oz, etc.)
  - Purple LEDs flash to indicate which preset amount is being selected
  - Preset values are loaded from Google Sheets document so they are easily adjustable
- Filter change alert
  - Red LEDs flash to indicate the filter needs to be changed
  - Total water usage is loaded from Google Sheets document
- Automatic LED dimming at night based on time of day scheduling 
- 3D printed enclosure for hidden mounting under a cabinet

### Project Information

* [wiring diagram](https://github.com/StorageB/Water-Dispenser/blob/master/wiring-diagram.pdf)

* [parts list](https://github.com/StorageB/Water-Dispenser/blob/master/parts-list.md)

* [3D printed enclosure stl file](https://github.com/StorageB/Water-Dispenser/blob/master/Enclosure.stl)

* [picture gallery](http://imgur.com/a/mHQLtMX)

### Code

There are three different versions of the code posted for the ESP8266:

1. [Version 1](https://github.com/StorageB/Water-Dispenser/blob/master/main_v1.cpp) - Basic Functionality 

   This includes the most basic operation for one sensor and can be used without an internet connection. It will open the valve and fade on the lights when an object is detected or when the button is pressed.

2. [Version 2](https://github.com/StorageB/Water-Dispenser/blob/master/main_v2.cpp) - Google Sheets logging

   This version has the same functionality as version 1, but includes code for over the air (OTA) programming of the ESP8266 and for logging water usage data directly to Google Sheets

3. [Version 3](https://github.com/StorageB/Water-Dispenser/blob/master/main_v3.cpp) - Current version 

   This is the most recent version, and includes the code for all of the features listed above in the Project Description.

A tutorial for how to log data to Google Sheets without the use of a third party service can be found here:
https://github.com/StorageB/Google-Sheets-Logging






### Build Notes and Considerations

#### Google Sheets Logging

Google sheets is used to log water usage data and for the system to read inputs such as what level the filter should be changed and what preset values to use for the automatic dispensing mode 

A tutorial for how to log data to Google Sheets with an ESP8266 module without the use of a third party service can be found here:
https://github.com/StorageB/Google-Sheets-Logging

#### Controller

A NodeMCU controller was used mainly because a WiFi connection was required for logging data and for the desire to use over the air programming. 

#### Valve

1.  The water valve should be a fail close valve. If the power goes out or signal is otherwise lost from the microcontroller, the valve should close or remain closed.
2.  You can find cheaper solenoid valves on Amazon or other sites, but keep in mind the following when selecting a valve:
      - Safety: It needs to be safe to use for beverage applications (should use lead free brass and safe to drink from plastics). 
      - Reliability: This is a commercial valve made specifically for this application. I would not trust a $3 valve from Amazon when installing this permanently in my house.
3.  It is a good idea to have a delay after the valve is open before it is allowed to close. This will prevent rapid or partial switching of the valve that may lead to early failure.

#### IR Sensor

1. I chose infrared sensors over an ultrasonic sensor for this application for a few reasons. 
   - Simplicity: The sensor used simply outputs a low signal when an object is detected, and a high signal otherwise.
   - Size: The sensor is much smaller than an ultrasonic sensor module and will be easier to hide under the cabinet where it is mounted.
   - Health: There is a bit of research that suggests long term exposure to ultrasonic waves, although out of our hearing range, may have a negative impact on people. And since this will be running 24/7, I prefer using an IR sensor. Additionally, the frequency of an ultrasonic sensor module is in the hearing range of dogs.  
2. Sensor range: I chose a sensor with a short range of between 2 cm and 10 cm (0.8" and 4"). Because it is mounted above the kitchen sink, I did not want it to be accidentally triggered when using the sink.
3. Ghost Detection: Occasionally an IR sensor may give you false triggers based on what the light may be reflecting on (such as dust). I was having this problem but solved it but adding a simple 100 ms delay after it was triggered. After the delay, the system checks again to see if the sensor is still triggered. This also reduced rapid on/off switching of the valve if an object was just on the edge of detection.
4. IR sensors do not work well with glass. The sensors had to be positioned to detect a hand holding a glass. 
5. Code has been added for the valve to stay open for a short amount of time after an object is no longer detected. This help prevent the valve from rapidly opening and closing if the sensor is not continuously triggered when an object is on the edge of the detection zone of the sensor.

#### NeoPixel LED ring

Because the ESP module uses 3.3V for the GPIO pins, a level shifter was required for the 5V NeoPixel ring. Although you can get by without a level shifter for some applications, to do very quick changes such as fading all of the LEDs on/off the level shifter was required. Without it, the LEDs had unstable and erratic behavior.

Adafruit has an excellent guide for how to get started with NeoPixels: https://learn.adafruit.com/adafruit-neopixel-uberguide

#### Fading LEDs

When fading LEDs with PWM, the light output levels do not scale linearly. Therefore, a logarithm curve is required. This post gives a great explanation on this along with some example Arduino code that I used in this project: https://diarmuid.ie/blog/pwm-exponential-led-fading-on-arduino-or-other-platforms

#### Handling Malfunctions

I added code to shut off the valve in the case that it was open for an abnormally long amount of time. This could be the result of a faulty sensor, disconnected sensor signal wire, stuck switch, electrical short, blocked sensor, etc. In addition, the LEDs will display an error (flashing red), and the system will have to be manually reset before it is allowed to be used again.

