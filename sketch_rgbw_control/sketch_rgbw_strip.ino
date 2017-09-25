/*
 * sketch_rgbw_strip.ino
 * Display a web page on a client browser.  Using individual R/G/B/W sliders to control a LED strip.
 * The client uses the web socket to transmit the slider values back to the ESP8266 (server), and the
 * server then updates the home page with the new values.  If a second client connects to the server 
 * or the first client refreshes, the sliders do not reset back to the default.
 * Modified from WebSocketServer_LEDcontrol.ino
 * 
 * To upload .ino.bin file through terminal use: curl -F "image=@firmware.bin" esp8266-xxxxxx.local/update
 *
 * Web Socket Library:
 * https://github.com/Links2004/arduinoWebSockets
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
#ifdef __AVR__
  #include <avr/power.h>
#endif

// Compiler directives, comment out to disable
#define USE_SERIAL Serial             // Valid options: Serial and Serial1

// LED strip settings
#define ENCODING_BYTES 3              // Options: 3 (RGB) or 4 (RGBW)
#define NUM_PIXELS     86             // Kitchen = 55 LEDs, Hallway = 86 LEDs
#define D_PIN          D2
#define BRIGHTNESS     80
#define SLIDER_MIN     32

// To read a max 4.2V from V(bat), a voltage divider is used to drop down to Vref=1.06V for the ADC
const float volt_div_const = 4.50*1.06/1.023; // multiplier = Vin_max*Vref/1.023 (mV)
                                              // WeMos BatShield: (350KΩ+100KΩ) 

// Wi-Fi Settings
const char* ssid     = "San Leandro";   // your wireless network name (SSID)
const char* password = "nintendo";      // your Wi-Fi network password

// Web page variables
uint32_t  rgbwData=0;             // RGBW LED format = [ W | R | G | B ]
String    homeString = "";        // RAM buffer for home page, we need to copy the home page 
                                  // from PROGMEM to here so we can make it a dynamic page

const unsigned int patch_page_interval = 5 * 1000;  // Patch web page interval = 5 sec
unsigned long      lastPatchTime;


// Initialize class objects
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
ESP8266HTTPUpdateServer httpUpdater;
#if ENCODING_BYTES == 3
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, D_PIN, NEO_GRB + NEO_KHZ800);
#endif
#if ENCODING_BYTES == 4
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, D_PIN, NEO_GRBW + NEO_KHZ800);
#endif

// A simple gamma look-up-table, stored in Program Memory
// To access the table, must use: pgm_read_byte()
const uint8_t PROGMEM gamma256LUT[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

const char PROGMEM index_1[] = R"rawliteral(<html><head><script>
var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);
connection.onopen = function() { connection.send('Connect ' + new Date()); };
connection.onerror = function(error) { console.log('WebSocket Error ', error); };
connection.onmessage = function(e) { console.log('Server: ', e.data); };
function sendRGBW() {
 var r = parseInt(document.getElementById('r').value).toString(16);
 var g = parseInt(document.getElementById('g').value).toString(16);
 var b = parseInt(document.getElementById('b').value).toString(16);
)rawliteral";

#if ENCODING_BYTES == 3  // Part of page for RGB (three bytes) Encoding
const char PROGMEM index_2[] = R"rawliteral( if(r.length<2) { r='0'+r; }  if(g.length<2) { g='0'+g; }
 if(b.length<2) { b='0'+b; }
 var rgb = '#'+r+g+b; console.log('RGB: '+rgb); connection.send(rgb); }
</script></head>
<body>LED Control:<br/><br/>
R : <input id="r" type="range" min="32" max="255" step="1" value=")rawliteral";
#endif
#if ENCODING_BYTES == 4  // Part of page for RGBW (four bytes) Encoding
const char PROGMEM index_2[] = R"rawliteral( var w = parseInt(document.getElementById('w').value).toString(16);
 if(r.length<2) { r='0'+r; }  if(g.length<2) { g='0'+g; }
 if(b.length<2) { b='0'+b; }  if(w.length<2) { w='0'+w; }
 var rgbw = '#'+w+r+g+b; console.log('RGBW: '+rgbw); connection.send(rgbw); }
</script></head>
<body>LED Control:<br/><br/>
R : <input id="r" type="range" min="32" max="255" step="1" value=")rawliteral";
#endif

const char PROGMEM index_3[] = R"rawliteral(" oninput="sendRGBW();" /><br/>
G : <input id="g" type="range" min="32" max="255" step="1" value=")rawliteral";

const char PROGMEM index_4[] = R"rawliteral(" oninput="sendRGBW();" /><br/>
B : <input id="b" type="range" min="32" max="255" step="1" value=")rawliteral";

const char PROGMEM index_5[] = R"rawliteral(" oninput="sendRGBW();" /><br/>
W: <input id="w" type="range" min="32" max="255" step="1" value=")rawliteral";

const char PROGMEM index_6[] = R"rawliteral(" oninput="sendRGBW();" /><br/><br/>
<font size="-1">
Battery: )rawliteral";
const char PROGMEM index_7[] = R"rawliteral(<br/>
</body></html>)rawliteral";


// Convert from perceived brightness value to the luminance value
uint32_t gammaCorrection(uint32_t rgbw)
{
  for (int i=0; i<4; i++) {
    uint32_t lut = pgm_read_byte(&gamma256LUT[rgbw&0xff]);
    rgbw = ((rgbw >> 8) & 0xffffff) | ((lut << 24) & 0xff000000);
  }
  return rgbw;
}


int readBatteryVoltage()
{
   int adc_mV = analogRead(A0) * volt_div_const;
    // Take average of two readings to get rid of noise
    delay(1);
    adc_mV = ( adc_mV + analogRead(A0)*volt_div_const ) / 2 ;
    return adc_mV;
}


// Construct the home page with current RGBW values
void constructHomePage()
{
  char ch[6];
  homeString  = FPSTR(index_1);
  homeString += FPSTR(index_2);
  sprintf(ch, "%03d", (rgbwData>>16)&0xff);
  homeString += ch;
  homeString += FPSTR(index_3);
  sprintf(ch, "%03d",(rgbwData>>8)&0xff );
  homeString += ch;
  homeString += FPSTR(index_4);
  sprintf(ch, "%03d",(rgbwData)&0xff );
  homeString += ch;
  #if ENCODING_BYTES == 4
  homeString += FPSTR(index_5);
  sprintf(ch, "%03d",(rgbwData>>24)&0xff );
  homeString += ch;
  #endif
  homeString += FPSTR(index_6); 
  int ftoi = readBatteryVoltage() / 10;
  sprintf( ch, "%d.%02dv",  ftoi/100, ftoi%100);
  #ifdef USE_SERIAL
  USE_SERIAL.printf("Construct Home Page... battery = %s \n", ch);
  #endif
  homeString += ch;
  homeString += FPSTR(index_7); 
}


// Patch the home page with new RGBW values
void patchHomePage()
{
  char ch[6];
  int i = strlen_P(index_1) + strlen_P(index_2);
  sprintf(ch, "%03d", (rgbwData>>16)&0xff);
  for (int j=0; j<3; j++)
    homeString.setCharAt(i+j, ch[j]);
  i = i + 3 + strlen_P(index_3);
  sprintf(ch, "%03d",(rgbwData>>8)&0xff );
  for (int j=0; j<3; j++)
    homeString.setCharAt(i+j, ch[j]);
  i = i + 3 + strlen_P(index_4);
  sprintf(ch, "%03d",(rgbwData)&0xff );
  for (int j=0; j<3; j++)
    homeString.setCharAt(i+j, ch[j]);
  #if ENCODING_BYTES == 4
  i = i + 3 + strlen_P(index_5);
  sprintf(ch, "%03d",(rgbwData>>24)&0xff );
  for (int j=0; j<3; j++)
    homeString.setCharAt(i+j, ch[j]);
  #endif
  i = i + 3 + strlen_P(index_6);
  int ftoi = readBatteryVoltage() / 10;
  sprintf( ch, "%d.%02dv",  ftoi/100, ftoi%100);
  #ifdef USE_SERIAL
  USE_SERIAL.printf("Patch Home Page... battery = %s \n", ch);
  #endif
  for (int j=0; j<5; j++)
    homeString.setCharAt(i+j, ch[j]);
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) 
{
  switch(type) {
    case WStype_DISCONNECTED:
      #ifdef USE_SERIAL
      USE_SERIAL.printf("[%u] Disconnected!\n", num);
      #endif
      break;
    case WStype_CONNECTED: {
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
      USE_SERIAL.printf("[%u] Received Text: %s ", num, payload);
      #endif
      if(payload[0] == '#') {
        // Patch the web page with new RGBW value
        rgbwData = (uint32_t) strtoul((const char *) &payload[1], NULL, 16);
        uint32_t rgbwLUT = gammaCorrection(rgbwData);
        #ifdef USE_SERIAL
        USE_SERIAL.printf("LUT: %08x", rgbwLUT);
        #endif
        for(uint16_t i=0; i<strip.numPixels(); i++) {
          strip.setPixelColor(i, rgbwLUT);
        }
        strip.show();
      }
      #ifdef USE_SERIAL
      USE_SERIAL.println();
      #endif
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
  
  // WiFi auto-connect fix: https://github.com/esp8266/Arduino/issues/2186
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
  constructHomePage();
  server.on("/", []() {
    // send index.html
    server.send(200, "text/html", homeString);
  });

  // update firmware
  httpUpdater.setup(&server);
  
  server.begin();
}


void setup() 
{
    #ifdef USE_SERIAL
    USE_SERIAL.begin(115200);
    USE_SERIAL.println();
    USE_SERIAL.println();
    //USE_SERIAL.setDebugOutput(true);
    #endif
    
    // First thing is to disable the WiFi auto-connect 
    WiFi.persistent(false);

    // Make up new hostname from our Chip ID (The MAC addr)
    // Note: Max length for hostString is 32, increase array if hostname is longer
    char hostString[16] = {0};
    sprintf(hostString, "esp8266_%06x", ESP.getChipId());  
    WiFi.hostname(hostString);
    WiFi.mode(WIFI_OFF); 
    WiFi.mode(WIFI_STA); 
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
    strip.show();   // Initialize all pixels to 'off'
    
    startServer();
    lastPatchTime = millis();
      
    // Add service to MDNS
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);

    #ifdef USE_SERIAL
    USE_SERIAL.printf("HTTPServer ready! Open http://%s.local in your browser\n", hostString);
    USE_SERIAL.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", hostString);
    #endif
}


void loop() 
{
    webSocket.loop();
    server.handleClient();
    if ( millis()-lastPatchTime >= patch_page_interval ) {
      patchHomePage();
      lastPatchTime = millis();
    }
    // Add some delay for better power consumption but still gives good response 
    delay(20);  // (ms)
}

