/*
 * Copyright (C) 2017-2019 Leonid Bogdanov
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
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Ticker.h>
#include <Time.h>

const char NIXIECLOCK[] PROGMEM = "nixieclock";

typedef struct { char name[9]; } ConfigKey;
const uint8_t CONFIG_KEYS_COUNT = 4;
const ConfigKey CONFIG_KEYS[] PROGMEM = {
  {"ssid"}, {"ssid-psk"}, {"api-key"}, {"tz"}
};
const char CONFIG_FILE[] PROGMEM = "/config.cfg";

const char MIME_TYPE_JSON[] PROGMEM = "application/json";
const char MIME_TYPE_TEXT[] PROGMEM = "text/plain";

const char GEOLOCATE_API_URL[] PROGMEM = "https://www.googleapis.com/geolocation/v1/geolocate?key=";
const char TIMEZONE_API_URL[] PROGMEM = "https://maps.googleapis.com/maps/api/timezone/json?key=";

struct Location {
  double_t lat;
  double_t lng;

  bool isValid() {
    return !(isnan(lat) || isnan(lng));
  }
};
const Location INVALID_LOCATION = {nan("loc"), nan("loc")};

/*
 * Describes ESP8266 controller behavior.
 */
class IBehavior {
  public:
    virtual ~IBehavior() {}
    virtual void doLoop() = 0;

  protected:
    static String readNextValue(Stream &configFile) {
      String value = configFile.readStringUntil('\r');
      configFile.readStringUntil('\n');
      return value;
    }
};

/*
 * Stores a behavior instance and delegates doLoop() calls to it.
 */
class Context : public IBehavior {
  IBehavior *behavior;

  public:
    ~Context() {
      setBehavior(nullptr);
    }

    void doLoop() override {
      behavior->doLoop();
    }

    void setBehavior(IBehavior *behavior) {
      delete this->behavior;
      this->behavior = behavior;
    }
};

/*
 * Clocks mode behavior.
 */
class ClocksBehavior : public IBehavior {
    // RFC7231 date is "Tue, 15 Nov 1994 08:12:31 GMT"
    static time_t parseRFC7231Date(const String &date) {
      String weekDays = F("SunMonTueWedThuFriSat");
      String months = F("JanFebMarAprMayJunJulAugSepOctNovDec");
      uint8_t year = CalendarYrToTm(date.substring(12, 16).toInt());
      uint8_t month = months.indexOf(date.substring(8, 11)) / 3 + 1;
      uint8_t day = date.substring(5, 7).toInt();
      uint8_t weekDay = weekDays.indexOf(date.substring(0, 3)) / 3 + 1;
      uint8_t hours = date.substring(17, 19).toInt();
      uint8_t minutes = date.substring(20, 22).toInt();
      uint8_t seconds = date.substring(23, 25).toInt();

      TimeElements time = {
        seconds, minutes, hours, weekDay, day, month, year
      };
      return makeTime(time);
    }

    void init() {
      initialized = true;

      File configFile = SPIFFS.open(FPSTR(CONFIG_FILE), "r");
      if (!configFile) {
        return;
      }
      String ssid = readNextValue(configFile);
      String ssidPsk = readNextValue(configFile);
      apiKey = readNextValue(configFile);
      String tz = readNextValue(configFile);
      configFile.close();

      // make sure only STA mode is enabled
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, ssidPsk);
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        return;
      }

      if (tz == F("auto")) {
        int8_t networksFound = WiFi.scanNetworks(false, true);
        if (networksFound > 1) {
          location = geolocate(networksFound);
        }
      } else {
        // tz is a ±hh:mm offset
        String hours = tz.substring(1, 3);
        String minutes = tz.substring(4, 6);
        tzOffset = hours.toInt() * SECS_PER_HOUR + minutes.toInt() * SECS_PER_MIN;
        if (tz[0] == '-') {
          tzOffset = -tzOffset;
        }
      }

      setSyncInterval(SECS_PER_DAY);
      setSyncProvider(
        [](void *arg) {
          return reinterpret_cast<ClocksBehavior*>(arg)->getTime();
        },
        this
      );
    }

    Location geolocate(int8_t networksCount) {
      DynamicJsonDocument jsonDoc(900);
      jsonDoc[F("considerIp")] = F("true");
      JsonArray wifiAPs = jsonDoc.createNestedArray(F("wifiAccessPoints"));
      for (int i = 0, limit = min(networksCount, int8_t(7)); i < limit; ++i) {
        JsonObject ap = wifiAPs.createNestedObject();
        ap[F("channel")] = WiFi.channel(i);
        ap[F("macAddress")] = WiFi.BSSIDstr(i);
        ap[F("signalStrength")] = WiFi.RSSI(i);
      }
      String jsonStr;
      if (!serializeJson(jsonDoc, jsonStr)) {
        return INVALID_LOCATION;
      }

      HTTPClient https;
      String geolocateUrl = String(FPSTR(GEOLOCATE_API_URL)) + apiKey;
      https.begin(wifiClient, geolocateUrl);
      https.addHeader(F("Content-Type"), FPSTR(MIME_TYPE_JSON));
      https.setUserAgent(FPSTR(NIXIECLOCK));
      if (https.POST(jsonStr) != HTTP_CODE_OK) {
        https.end();
        return INVALID_LOCATION;
      }

      jsonDoc.clear();
      DeserializationError parseResult = deserializeJson(jsonDoc, https.getStream());
      https.end();
      if (parseResult == DeserializationError::Ok) {
        JsonObject location = jsonDoc[F("location")];
        return {
          location[F("lat")],
          location[F("lng")]
        };
      }

      return INVALID_LOCATION;
    }

    time_t getTime() {
      HTTPClient https;
      // https.setReuse(true);
      https.setUserAgent(FPSTR(NIXIECLOCK));

      const char *dateHeader[] = {"Date"};
      String timezoneUrl = String(FPSTR(TIMEZONE_API_URL)) + apiKey;
      https.begin(wifiClient, timezoneUrl);
      https.collectHeaders(dateHeader, 1);
      if (https.sendRequest("HEAD") != HTTP_CODE_OK) {
        https.end();
        return 0;
      }

      String date = https.header(dateHeader[0]);
      https.end();
      time_t time = parseRFC7231Date(date);

      if (location.isValid()) {
        timezoneUrl += String(F("&location=")) + location.lat + ',' + location.lng + F("&timestamp=") + time;

        https.begin(wifiClient, timezoneUrl);
        https.collectHeaders(nullptr, 0);
        if (https.GET() != HTTP_CODE_OK) {
          https.end();
          return time;
        }

        StaticJsonDocument<350> jsonDoc;
        DeserializationError parseResult = deserializeJson(jsonDoc, https.getStream());
        https.end();
        if (parseResult == DeserializationError::Ok) {
          int32_t rawOffset = jsonDoc[F("rawOffset")];
          int32_t dstOffset = jsonDoc[F("dstOffset")];
          tzOffset = rawOffset + dstOffset;
        }
      }

      return time;
    }

    WiFiClientSecure wifiClient;
    String apiKey;
    int32_t tzOffset = 0;
    Location location = INVALID_LOCATION;
    bool initialized = false;

  public:
    ClocksBehavior() {
      wifiClient.setInsecure();
    }

    ~ClocksBehavior() {
      wifiClient.stopAll();
    }

    void doLoop() override {
      // init() has blocking operations and should be performed in the loop()
      if (initialized) {
        Serial.println(now() + tzOffset);
        delay(5000);
      } else {
        init();
      }
    }
};

/*
 * Configuration mode behavior.
 */
class ConfigBehavior : public IBehavior {
    /*
     * A RequestHandler that sets current behavior to clocks mode if no client
     * requested the configraion web page for a specified amount of seconds.
     */
    class BehaviorSwitcher : public RequestHandler {
      Ticker ticker;

      public:
        BehaviorSwitcher(Context &context, uint8_t idleFor = 60) {
          ticker.once(idleFor, [&]() {
            context.setBehavior(new ClocksBehavior());
          });
        }

        bool canHandle(HTTPMethod method, String uri) override {
          ticker.detach();
          return false;
        }
    };

    ESP8266WebServer webServer;
    ESP8266HTTPUpdateServer updateServer;
    DNSServer dnsServer;
    bool initialized = false;

  public:
    ConfigBehavior(Context &context) {
      uint8_t apMacAddr[WL_MAC_ADDR_LENGTH];
      WiFi.softAPmacAddress(apMacAddr);
      char ssid[16];
      sprintf_P(ssid, PSTR("NixieClock %02X%02X"), apMacAddr[0], apMacAddr[1]);
      // make sure only AP mode is enabled
      WiFi.mode(WIFI_AP);
      if (!WiFi.softAP(ssid, FPSTR(NIXIECLOCK))) {
        return;
      }

      webServer.addHandler(new BehaviorSwitcher(context));
      // overrides ESP8266HTTPUpdateServer GET route, because we have custom UI
      // must be added before calling updateServer.setup()
      webServer.on(F("/update"), HTTP_GET, [&]() {
        webServer.send_P(404, MIME_TYPE_TEXT, PSTR("Not found: /update"));
      });
      String settingsPath = F("/settings");
      webServer.on(settingsPath, HTTP_GET, [&]() {
        String jsonStr;
        File configFile = SPIFFS.open(FPSTR(CONFIG_FILE), "r");
        if (configFile) {
          StaticJsonDocument<250> jsonDoc;
          for (int i = 0; i < CONFIG_KEYS_COUNT && configFile.available(); ++i) {
            ConfigKey key;
            memcpy_P(&key, &CONFIG_KEYS[i], sizeof key);
            jsonDoc[key.name] = readNextValue(configFile);
          }
          configFile.close();
          serializeJson(jsonDoc, jsonStr);
        } else {
          jsonStr = F("{}");
        }
        webServer.send(200, FPSTR(MIME_TYPE_JSON), jsonStr);
      });
      webServer.on(settingsPath, HTTP_POST, [&]() {
        File configFile = SPIFFS.open(FPSTR(CONFIG_FILE), "w+");
        if (configFile) {
          for (int i = 0; i < CONFIG_KEYS_COUNT; ++i) {
            ConfigKey key;
            memcpy_P(&key, &CONFIG_KEYS[i], sizeof key);
            configFile.println(webServer.arg(key.name));
          }
          configFile.close();

          webServer.send_P(200, MIME_TYPE_TEXT, PSTR("OK"));
        } else {
          webServer.send_P(500, MIME_TYPE_TEXT, PSTR("Couldn't write config file"));
        }
      });
      webServer.serveStatic("/", SPIFFS, "/", "max-age=86400");

      dnsServer.setTTL(300);
      // it's OK if DNS server can't start - IP should do fine
      dnsServer.start(53, FPSTR(NIXIECLOCK), WiFi.softAPIP());

      updateServer.setup(&webServer);

      webServer.begin();

      initialized = true;
    }

    ~ConfigBehavior() {
      webServer.stop();
      dnsServer.stop();
    }

    void doLoop() override {
      if (initialized) {
        dnsServer.processNextRequest();
        webServer.handleClient();
      }
    }
};

// encapsulates current behavior
Context context;

void setup()
{
  if (SPIFFS.begin()) {
    context.setBehavior(new ConfigBehavior(context));
  }
}

void loop()
{
  context.doLoop();
}
