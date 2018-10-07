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

#ifdef DEBUG_ESP_PORT     //this is Arduino IDE's debug option, use it if defined
#define DEBUG_PORT DEBUG_ESP_PORT
#else
//#define DEBUG_PORT Serial //comment out to disable serial, or choose Serial1
#endif

#ifdef DEBUG_PORT
#define DEBUG_PRINTF(...) DEBUG_PORT.printf_P( __VA_ARGS__ )
#else
#define DEBUG_PRINTF(...)
#endif

#ifdef DEBUG_PORT
#define DEBUG_PRINT(...) DEBUG_PORT.print( __VA_ARGS__ )
#else
#define DEBUG_PRINT(...)
#endif

// LED strip settings
#define D_PIN          D2
#define ENCODING_BYTES 3          // Options: 3 (RGB) or 4 (RGBW)
                                  // Floor-Strip = RGB, Counter-Strip = RGBW
#define NUM_PIXELS     126        // Kitchen = 55 LEDs, Hallway = 86 LEDs, Entryway = 126 LEDs
#define BRIGHTNESS     80         // WWA-Floor-Strip = 80, RGB-Counter-Strip = 120
#define SLIDER_MIN     08         // Note: Patch this literal by hand inside html code because this
                                  //       macro is not recongnised inside the literal declaration
#include "homepage.h"
#include "gamma8.h"

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
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
  rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  lut = pgm_read_byte(&gamma8[rgbw&0xff]);
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
      DEBUG_PRINTF(PSTR("[%u] Disconnected!\n"), num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        DEBUG_PRINTF(PSTR("[%u] Connected from %d.%d.%d.%d url: %s\n"), num, ip[0], ip[1], ip[2], ip[3], payload);
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      DEBUG_PRINTF(PSTR("[%u] Received Text: %s\n"), num, payload);
      if(payload[0] == '#') {
        // Patch the web page with new RGBW value
        rgbwData = (uint32_t) strtoul((const char *) &payload[1], NULL, 16);
        uint32_t rgbwLUT = gammaCorrection(rgbwData);
        DEBUG_PRINTF(PSTR("    Corrected RGB value: %08x\n"), rgbwLUT);
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
  DEBUG_PRINT(F("Hostname: "));
  DEBUG_PRINT(WiFi.hostname());
  DEBUG_PRINT(F("\nConnecting ."));

  if (WiFi.status() != WL_CONNECTED) 
    WiFi.begin(ssid, password);
      
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_PRINT(".");
  }

  IPAddress ip = WiFi.localIP();
  DEBUG_PRINTF(PSTR("\nConnected to %s\n"), ssid);
  DEBUG_PRINTF(PSTR("IP address: %u.%u.%u.%u\n"), ip[0], ip[1], ip[2], ip[3]);
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
  #ifdef DEBUG_PORT
  DEBUG_PORT.begin(115200);
  delay(10);
  DEBUG_PORT.println();
  DEBUG_PORT.println();
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
    DEBUG_PRINTF(PSTR("MDNS responder started\n"));
  }

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  // Initialize all pixels the default value
  uint32_t rgbwLUT = gammaCorrection(rgbwData);
  DEBUG_PRINTF(PSTR("Startup corrected RGB value: %08x\n"), rgbwLUT);
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, rgbwLUT);
  }
  strip.show();
  
  startServer();
      
  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  
  DEBUG_PRINTF(PSTR("HTTPServer ready! Open http://%s.local in your browser\n"), hostString);
  DEBUG_PRINTF(PSTR("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n"), hostString);

  // Start timer
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
