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
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Ticker.h>

/*
 * Describes ESP8266 controller behavior.
 */
class BehaviorInterface {
  public:
    virtual ~BehaviorInterface() {}
    virtual void doLoop() = 0;
};

/*
 * Stores a behavior instance and delegates doLoop() calls to it.
 */
class Context : public BehaviorInterface {
  public:
    ~Context() {
      setBehavior(nullptr);
    }

    void doLoop() override {
      behavior->doLoop();
    }

    void setBehavior(BehaviorInterface *newBehavior) {
      delete behavior;
      behavior = newBehavior;
    }

  private:
    BehaviorInterface *behavior;
};

// is used to switch between behaviors
Context context;

/*
 * Clocks mode behavior.
 */
class ClocksBehavior : public BehaviorInterface {
  public:
    ClocksBehavior() {
       pinMode(BUILTIN_LED, OUTPUT);
    }

    void doLoop() override {
      while (true) {
        digitalWrite(BUILTIN_LED, LOW);
        delay(100);
        digitalWrite(BUILTIN_LED, HIGH);
        delay(100);
      }
    }
};

/*
 * Configuration mode behavior.
 */
class ConfigBehavior : public BehaviorInterface {
  public:
    ConfigBehavior() {
      if (!WiFi.mode(WIFI_AP)) {
        return;
      }

      uint8_t macAddr[6];
      WiFi.softAPmacAddress(macAddr);
      char ssid[16];
      sprintf(ssid, "NixieClock %02X%02X", macAddr[0], macAddr[1]);
      if (!WiFi.softAP(ssid, "nixieclock")) {
        return;
      }

      if (!SPIFFS.begin()) {
        return;
      }

      webServer.addHandler(new BehaviorSwitcher());
      // overrides ESP8266HTTPUpdateServer GET route, because we have own UI
      // must be added before calling updateServer.setup()
      webServer.on("/update", HTTP_GET, [this]() {
        webServer.send(404, "text/plain", "Not found: /update");
      });
      webServer.on("/settings", HTTP_GET, [this]() {
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.createObject();
        File configFile = SPIFFS.open("/config.txt", "r");
        if (configFile) {
          String settings[] = {"ssid", "ssid-psk", "server-url", "tz", "tz-override"};
          for (int i = 0; i < 5 && configFile.available() > 0; ++i) {
            json[settings[i]] = configFile.readStringUntil('\r');
            configFile.readStringUntil('\n');
          }
          configFile.close();
        }
        String jsonStr;
        json.printTo(jsonStr);
        webServer.send(200, "application/json", jsonStr);
      });
      webServer.on("/settings", HTTP_POST, [this]() {
        File configFile = SPIFFS.open("/config.txt", "w+");
        if (configFile) {
          configFile.println(webServer.arg("ssid"));
          configFile.println(webServer.arg("ssid-psk"));
          configFile.println(webServer.arg("server-url"));
          String tz = webServer.arg("tz");
          if (tz.length() > 0) {
              configFile.println(tz);
              configFile.println(webServer.arg("tz-override"));
          } else {
            configFile.println("manual");
            configFile.println("+00:00");
          }
          configFile.close();

          webServer.send(200, "text/plain", "OK");
        } else {
          webServer.send(500, "text/plain", "Couldn't read config file");
        }
      });
      webServer.serveStatic("/", SPIFFS, "/index.html", "max-age=86400");
      webServer.serveStatic("/chota.css", SPIFFS, "/chota.css", "max-age=86400");
      webServer.serveStatic("/zepto.js", SPIFFS, "/zepto.js", "max-age=86400");

      dnsServer.setTTL(300);
      // it's OK if DNS server can't start - IP should do fine
      dnsServer.start(53, "nixieclock", WiFi.softAPIP());

      updateServer.setup(&webServer);

      webServer.begin();

      initialized = true;
    }

    ~ConfigBehavior() {
      dnsServer.stop();
      SPIFFS.end();
    }

    void doLoop() override {
      if (initialized) {
        dnsServer.processNextRequest();
        webServer.handleClient();
      }
    }

  private:
    /*
     * A RequestHandler that sets current behavior to clocks mode if no client
     * requested configraion web page for a specified amount of seconds.
     */
    class BehaviorSwitcher : public RequestHandler {
      public:
        BehaviorSwitcher(int idleFor = 90) {
          ticker.once(idleFor, []() {
            context.setBehavior(new ClocksBehavior());
          });
        }

        bool canHandle(HTTPMethod method, String uri) override {
          ticker.detach();
          return false;
        }

      private:
        Ticker ticker;
    };

    ESP8266WebServer webServer;
    ESP8266HTTPUpdateServer updateServer;
    DNSServer dnsServer;
    bool initialized = false;
};

void setup()
{
  context.setBehavior(new ConfigBehavior());
}

void loop()
{
  context.doLoop();
}
