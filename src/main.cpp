#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <FS.h>
#include <SPI.h>

//Pre-defines


//Global variables
volatile uint8_t current_mask = 0;
volatile uint8_t times[] = {0,0,0,0};

//Global functions and class initialization
DNSServer dns;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
void update_mask(uint8_t mask);
void get_mask();
void get_timeout();


void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup() {

    Serial.begin(115200);
    Serial2.begin(115200);

    while (!Serial2) {
      Serial.println("STM connection error!");
      delay(1000); // wait for serial port to connect. Needed for native USB
    }

    // Make sure we can read the file system
    if( !SPIFFS.begin()){
      Serial.println("Error mounting SPIFFS");
      while(1);
    }
    
    //Wi-Fi manager
    AsyncWiFiManager wifiManager(&server,&dns);
    //wifiManager.resetSettings();
    wifiManager.autoConnect("UltraBot AP", "disinfection", 3, 3000);

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", "text/html");
    });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.onNotFound([](AsyncWebServerRequest *request){
      Serial.printf("NOT_FOUND: ");
      if(request->method() == HTTP_GET)
        Serial.printf("GET");
      else if(request->method() == HTTP_POST)
        Serial.printf("POST");
      else if(request->method() == HTTP_DELETE)
        Serial.printf("DELETE");
      else if(request->method() == HTTP_PUT)
        Serial.printf("PUT");
      else if(request->method() == HTTP_PATCH)
        Serial.printf("PATCH");
      else if(request->method() == HTTP_HEAD)
        Serial.printf("HEAD");
      else if(request->method() == HTTP_OPTIONS)
        Serial.printf("OPTIONS");
      else
        Serial.printf("UNKNOWN");
      Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

      if(request->contentLength()){
        Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
        Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
      }

      int headers = request->headers();
      int i;
      for(i=0;i<headers;i++){
        AsyncWebHeader* h = request->getHeader(i);
        Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
      }

      int params = request->params();
      for(i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isFile()){
          Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        } else if(p->isPost()){
          Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        } else {
          Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }

      request->send(404);
    });
    times[0] = 0;
    times[1] = 0;
    times[2] = 0;
    times[3] = 0;
    current_mask = 0;

    byte msg[2];
    msg[0] = 0x07;
    msg[1] = 0x03;
    Serial2.write(msg,2);

    server.begin();
}

void loop() 
{
  get_mask();
  get_timeout();
  delay(100);
  ws.cleanupClients();
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  String msg = "";
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if(info->final && info->index == 0 && info->len == len){
      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
        
    } 
    else {
      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        }
      }
    }
  }

  if(msg.charAt(0) == 'm'){
    uint8_t new_mask = msg.substring(1, 3).toInt();
    Serial.println(new_mask);
    update_mask(new_mask);
    
  } else if(msg.equals(String("g"))){
    String message = "m:" + String(current_mask);
    client->text(message);

  } else if(msg.equals(String("t"))) {
    String message = "t:";
    for (size_t i = 0; i < 4; i++)
    {
      message += String(times[i]) + ' ';
    }
    client->text(message);
  }
}

void update_mask(uint8_t mask)
{
  Serial.println(mask, HEX);
  byte msg[2];
  msg[0] = 0x1A;
  msg[1] = mask;
  Serial2.write(msg,2);
  delay(1000);
  Serial2.write(0x1B);
  if (Serial2.available() > 0) {
    uint8_t ans = Serial2.read();
    Serial.println(ans, HEX);
    current_mask = ans;
  }
  Serial.print("Updated mask: ");
  Serial.println(current_mask, HEX);
}

void get_mask()
{
    Serial2.write(0x1B);
    if (Serial2.available() > 0) {
      uint8_t ans = Serial2.read();
      current_mask = ans;
    }
}

void get_timeout()
{
  Serial2.write(0x1C);
  int i = 0;
  while (Serial2.available()) {
    times[i] = Serial2.read();
    i++;
  }
    
}