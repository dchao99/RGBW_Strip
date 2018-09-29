/*
 * sketch_rgbw_strip.ino
 * Display a web page on a client browser.  Using individual R/G/B/W sliders to control a LED strip.
 * The client uses the web socket to transmit the slider values back to the ESP8266 (server), and the
 * server then updates the home page with the new values.  If a second client connects to the server 
 * or the first client refreshes, the sliders do not reset back to the default.
 * Modified from WebSocketServer_LEDcontrol.ino
 * 
 * To upload .ino.bin file through terminal use: curl -F "image=@firmware.bin" 
 * Web browser:  esp8266-xxxxxx.local/update
 *
 * Web Socket Library:
 *   https://github.com/Links2004/arduinoWebSockets
 *
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
//#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Hash.h>
#include <Adafruit_NeoPixel.h>


#define ENCODING_BYTES 3          // Options: 3 (RGB) or 4 (RGBW)
                                  // Floor-Strip = RGB, Counter-Strip = RGBW
#include "homepage.h"
#include "gamma8.h"

//#define USE_SERIAL Serial       // DEBUG options: Serial and Serial1

// LED strip settings
#define D_PIN          D2
#define NUM_PIXELS     126        // Kitchen = 55 LEDs, Hallway = 86 LEDs, Entryway = 126 LEDs
#define BRIGHTNESS     80         // WWA-Floor-Strip = 80, RGB-Counter-Strip = 120
#define SLIDER_MIN     08         // Note: Patch this literal by hand inside html code because this
                                  //       macro wont be recongnised inside the literal declaration

// Choose one special effect, only one effect for now
#define EFFECT_DEEPSLEEP
//#define EFFECT_PLASMA

// LED Strip variables 
#if (ENCODING_BYTES==3)           // RGBW LED format = [ WW | W | A ]
uint32_t  rgbwData=0x00600060;    // WWA-Floor-Strip = 0x00600060
#endif
#if (ENCODING_BYTES==4)           // RGBW LED format = [ W | R | G | B ]
uint32_t  rgbwData=0x80000000;    // RGB-Counter-Strip = 0x80000000
#endif

bool      effectEnable = false;   // Run special LED Effect
String    homeString = "";        // we need to copy the home page from PROGMEM to here 

const unsigned int WebPatchInterval = 1000;  //= 1 sec, web page patching interval 
unsigned long      lastPatchTime;

// Wi-Fi Settings
const char* ssid     = "San Leandro";   // your wireless network name (SSID)
const char* password = "nintendo";      // your Wi-Fi network password

// Initialize network class objects
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
ESP8266HTTPUpdateServer httpUpdater;
#if (ENCODING_BYTES==3)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, D_PIN, NEO_GRB + NEO_KHZ800);
#endif
#if (ENCODING_BYTES==4)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, D_PIN, NEO_GRBW + NEO_KHZ800);
#endif

#if (ENCODING_BYTES==3) 
// Convert from perceived brightness value to the luminance value
uint32_t gammaCorrection(uint32_t rgbw)
{
  uint32_t lut;
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = rgbw & 0xff;
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  return rgbw;
}
#endif
#if (ENCODING_BYTES==4) 
// Convert from perceived brightness value to the luminance value
uint32_t gammaCorrection(uint32_t rgbw)
{
  uint32_t lut;
  lut = rgbw & 0xff;
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = rgbw & 0xff;
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  return rgbw;
}
#endif
//********** WEB SERVER AND WEB SOCKET CODE BEGINS

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) 
{
  switch(type) {
    case WStype_DISCONNECTED:
      #ifdef USE_SERIAL
      USE_SERIAL.printf("[%u] Disconnected!\n", num);
      #endif
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        #ifdef USE_SERIAL        
        USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        #endif
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      #ifdef USE_SERIAL
      USE_SERIAL.printf("[%u] Received Text: %s\n", num, payload);
      #endif
      if(payload[0] == '#') {
        // Patch the web page with new RGBW value
        rgbwData = (uint32_t) strtoul((const char *) &payload[1], NULL, 16);
        uint32_t rgbwLUT = gammaCorrection(rgbwData);
        #ifdef USE_SERIAL
        USE_SERIAL.printf("    Gamma corrected RGB value: %08x\n", rgbwLUT);
        #endif
        for(uint16_t i=0; i<strip.numPixels(); i++) {
          strip.setPixelColor(i, rgbwLUT);
        }
        strip.show();
      } 
      else if (payload[0] == 'E') {   // the browser sends an E when the LED effect is enabled
        effectEnable = true;
      } 
      else if (payload[0] == 'N') {   // the browser sends an N when the LED effect is disabled
        effectEnable = false;
      }
      break;
  }
}


void startWiFi()
{
  #ifdef USE_SERIAL
  USE_SERIAL.print(F("Hostname: "));
  USE_SERIAL.println(WiFi.hostname());
  USE_SERIAL.print(F("Connecting ."));
  #endif

  if (WiFi.status() != WL_CONNECTED) 
    WiFi.begin(ssid, password);
      
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    #ifdef USE_SERIAL
    USE_SERIAL.print(".");
    #endif
  }

  #ifdef USE_SERIAL
  USE_SERIAL.println();
  USE_SERIAL.print(F("Connected to "));
  USE_SERIAL.println(ssid);
  USE_SERIAL.print(F("IP address: "));
  USE_SERIAL.println(WiFi.localIP());
  #endif
}


void startServer()
{
  // handle index
  homeString = constructHomePage(rgbwData);
  server.on("/", []() {
    // send index.html
    server.send(200, "text/html", homeString);
  });

  // update firmware
  httpUpdater.setup(&server);
  
  server.begin();
}

//********** ARDUINO MAIN CODE BEGINS

void setup() 
{
  #ifdef USE_SERIAL
  USE_SERIAL.begin(115200);
  delay(10);
  USE_SERIAL.println();
  USE_SERIAL.println();
  USE_SERIAL.setDebugOutput(false);  // Use extra debugging details?
  #endif
    
  // Make up new hostname from our Chip ID (The MAC addr)
  // Note: Max length for hostString is 32, increase array if hostname is longer
  char hostString[16] = {0};
  sprintf(hostString, "esp8266_%06x", ESP.getChipId());  
  WiFi.hostname(hostString);
    
  //** no longer required **
  //WiFi unable to re-connect fix: https://github.com/esp8266/Arduino/issues/2186
  //WiFi.persistent(false);  
  //WiFi.mode(WIFI_OFF); 
  //WiFi.mode(WIFI_STA); 
  startWiFi();
    
  // Start webSocket service
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Start MDNS Service
  if(MDNS.begin(hostString)) {
    #ifdef USE_SERIAL
    USE_SERIAL.println("MDNS responder started");
    #endif
  }

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  // Initialize all pixels the default value
  uint32_t rgbwLUT = gammaCorrection(rgbwData);
  #ifdef USE_SERIAL
  USE_SERIAL.printf("Startup Gamma corrected RGB value: %08x\n", rgbwLUT);
  #endif
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, rgbwLUT);
  }
  strip.show();
  
  startServer();
      
  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  #ifdef USE_SERIAL
  USE_SERIAL.printf("HTTPServer ready! Open http://%s.local in your browser\n", hostString);
  USE_SERIAL.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", hostString);
  #endif

  lastPatchTime = millis();
}


void loop() 
{
  webSocket.loop();
  server.handleClient();
  unsigned long now = millis();
  if ( now - lastPatchTime >= WebPatchInterval ) {
    lastPatchTime = now;
    patchHomePage(homeString,rgbwData);
  }
  if (effectEnable == true) {
    #ifdef EFFECT_DEEPSLEEP 
    effectAllLedOff();
    ESP.deepSleep(0);   // Put ESP8266 into sleep infinitely
    #endif
  }
  // Add some delay to reduce power consumption but still give a good slider response 
  delay(20);  // (ms)
}

#ifdef EFFECT_DEEPSLEEP 
void effectAllLedOff() 
{
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, 0x00000000);
  }
  strip.show();
}
#endif
