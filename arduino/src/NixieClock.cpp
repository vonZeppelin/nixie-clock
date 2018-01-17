/*
 * Copyright (C) 2017-2018 Leonid Bogdanov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>

enum Mode { config, clocks };
Mode currentMode;

// config mode services
DNSServer *dnsServer;
ESP8266HTTPUpdateServer *updateServer;
ESP8266WebServer *webServer;

void initConfigMode() {
  currentMode = config;

  SPIFFS.begin();

  WiFi.mode(WIFI_AP) && WiFi.softAP("NixieClock", "nixieclock");

  dnsServer = new DNSServer();
  dnsServer->setTTL(300);
  dnsServer->start(53, "nixieclock", WiFi.softAPIP());

  webServer = new ESP8266WebServer();
  webServer->serveStatic("/", SPIFFS, "/index.html", "max-age=86400");
  webServer->serveStatic("/chota.css", SPIFFS, "/chota.css", "max-age=86400");
  webServer->serveStatic("/zepto.js", SPIFFS, "/zepto.js", "max-age=86400");
  // override ESP8266HTTPUpdateServer GET route
  webServer->on("/update", HTTP_GET, []() {
    webServer->send(404, "text/plain", "Not found: /update");
  });
  webServer->on("/settings", HTTP_POST, []() {
    webServer->send(200, "text/plain", "OK");
  });
  webServer->begin();

  updateServer = new ESP8266HTTPUpdateServer();
  updateServer->setup(webServer);
}


void handleConfigMode() {
  dnsServer->processNextRequest();
  webServer->handleClient();
}

void destroyConfigMode() {
  delete updateServer;

  webServer->stop();
  delete webServer;

  dnsServer->stop();
  delete dnsServer;

  SPIFFS.end();
}

void setup()
{
  initConfigMode();
}

void loop()
{
  switch (currentMode) {
    case config:
      handleConfigMode();
      break;
    case clocks:
      break;
  }
}
