#ifndef _INC_RGBW_STRIP_HOMEPAGE_H_
#define _INC_RGBW_STRIP_HOMEPAGE_H_

const char PROGMEM index_1[] = R"rawliteral(<html><head><script>
var effectEnable = false;
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
function ledEffect () {
 effectEnable = ! effectEnable;
 if (effectEnable) {
  connection.send("Effect ON");
  document.getElementById('effect').style.backgroundColor = '#00878F';
  document.getElementById('r').className = 'disabled';
  document.getElementById('g').className = 'disabled';
  document.getElementById('b').className = 'disabled';
  document.getElementById('r').disabled = true;
  document.getElementById('g').disabled = true;
  document.getElementById('b').disabled = true;
  console.log('LED Effect ON');
 } else {
  connection.send("Normal Mode");
  document.getElementById('effect').style.backgroundColor = '#999';
  document.getElementById('r').className = 'enabled';
  document.getElementById('g').className = 'enabled';
  document.getElementById('b').className = 'enabled';
  document.getElementById('r').disabled = false;
  document.getElementById('g').disabled = false;
  document.getElementById('b').disabled = false;
  console.log('LED Effect OFF');
 }
})rawliteral";
#endif
#if ENCODING_BYTES == 4  // Part of page for RGBW (four bytes) Encoding
const char PROGMEM index_2[] = R"rawliteral( var w = parseInt(document.getElementById('w').value).toString(16);
 if(r.length<2) { r='0'+r; }  if(g.length<2) { g='0'+g; }
 if(b.length<2) { b='0'+b; }  if(w.length<2) { w='0'+w; }
 var rgbw = '#'+w+r+g+b; console.log('RGBW: '+rgbw); connection.send(rgbw); }
function ledEffect () {
 effectEnable = ! effectEnable;
 if (effectEnable) {
  connection.send("Effect ON");
  document.getElementById('effect').style.backgroundColor = '#00878F';
  document.getElementById('r').className = 'disabled';
  document.getElementById('g').className = 'disabled';
  document.getElementById('b').className = 'disabled';  
  document.getElementById('w').className = 'disabled';
  document.getElementById('r').disabled = true;
  document.getElementById('g').disabled = true;
  document.getElementById('b').disabled = true;
  document.getElementById('w').disabled = true;
  console.log('LED Effect ON');
 } else {
  connection.send("Normal Mode");
  document.getElementById('effect').style.backgroundColor = '#999';
  document.getElementById('r').className = 'enabled';
  document.getElementById('g').className = 'enabled';
  document.getElementById('b').className = 'enabled';
  document.getElementById('w').className = 'enabled';
  document.getElementById('r').disabled = false;
  document.getElementById('g').disabled = false;
  document.getElementById('b').disabled = false;
  document.getElementById('w').disabled = false;
  console.log('LED Effect OFF');
 }
})rawliteral";
#endif

const char PROGMEM index_3[] = R"rawliteral(</script></head>
<body><center><h2>LED Control:</h2>
<table><tr>
<td>R: </td><td><input id="r" type="range" min="08" max="255" step="1" value=")rawliteral";

const char PROGMEM index_4[] = R"rawliteral(" oninput="sendRGBW();" /></td></tr>
<td>G: </td><td><input id="g" type="range" min="08" max="255" step="1" value=")rawliteral";

const char PROGMEM index_5[] = R"rawliteral(" oninput="sendRGBW();" /></td></tr>
<td>B: </td><td><input id="b" type="range" min="08" max="255" step="1" value=")rawliteral";

const char PROGMEM index_6[] = R"rawliteral(" oninput="sendRGBW();" /></td></tr>
<td>W: </td><td><input id="w" type="range" min="08" max="255" step="1" value=")rawliteral";

const char PROGMEM index_7[] = R"rawliteral(" oninput="sendRGBW();" /></td></tr></table><br/>
<button id="effect" class="button" style="background-color:#999" onclick="ledEffect();">Effect</button><br/><br/>
<font size="1">
Hostname: )rawliteral";

const char PROGMEM index_8[] = R"rawliteral(<br/>
</center></body></html>)rawliteral";

// Construct the home page with current RGBW values
String constructHomePage(uint32_t values)
{
  String buffer;
  char ch[6];
  buffer  = FPSTR(index_1);
  buffer += FPSTR(index_2);
  buffer += FPSTR(index_3);
  sprintf(ch, "%03d", (values>>16)&0xff);
  buffer += ch;
  buffer += FPSTR(index_4);
  sprintf(ch, "%03d",(values>>8)&0xff );
  buffer += ch;
  buffer += FPSTR(index_5);
  sprintf(ch, "%03d",(values)&0xff );
  buffer += ch;
  #if ENCODING_BYTES == 4
  buffer += FPSTR(index_6);
  sprintf(ch, "%03d",(values>>24)&0xff );
  buffer += ch;
  #endif
  buffer += FPSTR(index_7); 
  buffer += WiFi.hostname();
  buffer += FPSTR(index_8); 
  return buffer;
}


// Patch the home page with new RGBW values
// note: buffer will be modified, pass the String object in by reference
void patchHomePage(String& buffer, uint32_t values)
{
  char ch[6];
  int i = strlen_P(index_1) + strlen_P(index_2) + strlen_P(index_3);
  sprintf(ch, "%03d", (values>>16)&0xff);
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
  i = i + 3 + strlen_P(index_4);
  sprintf(ch, "%03d",(values>>8)&0xff );
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
  i = i + 3 + strlen_P(index_5);
  sprintf(ch, "%03d",(values)&0xff );
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
  #if ENCODING_BYTES == 4
  i = i + 3 + strlen_P(index_6);
  sprintf(ch, "%03d",(values>>24)&0xff );
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
  #endif
}

#endif
