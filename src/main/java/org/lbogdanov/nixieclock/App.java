/*
 * Copyright (C) 2017 Leonid Bogdanov
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

package org.lbogdanov.nixieclock;

import static java.lang.System.getProperty;
import static java.lang.System.getenv;
import static java.lang.System.nanoTime;
import static java.util.concurrent.TimeUnit.NANOSECONDS;
import static javax.servlet.http.HttpServletResponse.*;
import static spark.Spark.*;

import java.util.Arrays;
import java.util.Optional;
import java.util.regex.Pattern;
import java.util.stream.Stream;

import org.lbogdanov.nixieclock.googleapis.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;

import retrofit2.Response;
import retrofit2.Retrofit;
import retrofit2.converter.gson.GsonConverterFactory;

/**
 * Nixie Clock server can respond with:
 * <ul>
 *   <li> Current UTC time;
 *   <li> UTC offset based on 2 or more WiFi BSSIDs taken as input from a client.
 * </ul>
 */
public class App {
    private static final String GEOLOCATION_API_KEY = "GL_KEY";
    private static final String TIMEZONE_API_KEY = "TZ_KEY";
    private static final String AUTH_TOKEN = "AUTH_TOKEN";
    private static final String OPENSHIFT_IP = "OPENSHIFT_DIY_IP";
    private static final String OPENSHIFT_PORT = "OPENSHIFT_DIY_PORT";
    private static final String BSSID_QUERY_PARAM = "bssid";
    private static final String KEY_QUERY_PARAM = "key";
    private static final String IP_ATTR = "ip";
    private static final String START_ATTR = "start";

    private static final Logger log = LoggerFactory.getLogger(App.class);

    /**
     * Application entry point.
     *
     * @param args the command line args
     */
    public static void main(String[] args) {
        Pattern commaSplitter = Pattern.compile(",");
        Pattern bssidValidator = Pattern.compile("^(?:\\p{XDigit}{2}:){5}\\p{XDigit}{2}$");
        String authToken = Optional.ofNullable(getenv(AUTH_TOKEN))
                                   .orElseGet(() -> getProperty(AUTH_TOKEN));
        String geoKey = Optional.ofNullable(getenv(GEOLOCATION_API_KEY))
                                .orElseGet(() -> getProperty(GEOLOCATION_API_KEY));
        String tzKey = Optional.ofNullable(getenv(TIMEZONE_API_KEY))
                               .orElseGet(() -> getProperty(TIMEZONE_API_KEY));
        GoogleApis googleApis = new Retrofit.Builder()
                                            .baseUrl(GoogleApis.BASE_URL)
                                            .addConverterFactory(GsonConverterFactory.create())
                                            .build()
                                            .create(GoogleApis.class);

        port(Optional.ofNullable(getenv(OPENSHIFT_PORT))
                     .map(Integer::valueOf)
                     .orElse(8080));
        ipAddress(Optional.ofNullable(getenv(OPENSHIFT_IP))
                          .orElse("0.0.0.0"));
        before((req, res) -> {
            MDC.put(IP_ATTR, req.ip());
            res.type("text/plain");
            if (!(authToken == null || authToken.equals(req.queryParams(KEY_QUERY_PARAM)))) {
                log.warn("Unauthorized request to {}, rejecting", req.pathInfo());
                throw halt(SC_UNAUTHORIZED, "401 Access denied");
            }
            log.info("Request to {}, processing...", req.pathInfo());
            req.attribute(START_ATTR, nanoTime());
        });
        after((req, res) -> {
            long duration = NANOSECONDS.toMillis(nanoTime() - (long) req.attribute(START_ATTR));
            log.info("Request completed in {} ms", duration);
            MDC.remove(IP_ATTR);
        });
        notFound((req, res) -> "404 Not found");
        internalServerError((req, res) -> "500 Internal Error");
        get("/about", (req, res) -> "Nixie Clock Server v1.0");
        get("/timezone", (req, res) -> {
            String[] bssids = req.queryParamsValues(BSSID_QUERY_PARAM);
            if (bssids == null) {
                throw halt(SC_BAD_REQUEST, BSSID_QUERY_PARAM + " query parameter is absent");
            }
            WiFiAccessPoint[] wifiAPs = Arrays.stream(bssids)
                                              .flatMap(commaSplitter::splitAsStream)
                                              .map(bssid -> {
                                                  if (bssid != null && bssidValidator.matcher(bssid).matches()) {
                                                      return new WiFiAccessPoint(bssid);
                                                  }
                                                  log.warn("Ivalid BSSID {}", bssid);
                                                  throw halt(SC_BAD_REQUEST, "Invalid BSSID " + bssid);
                                              })
                                             .toArray(WiFiAccessPoint[]::new);
            if (wifiAPs.length < 2) {
                throw halt(SC_BAD_REQUEST, "2 or more WiFi BSSIDs are required");
            }

            Response<GeoResponse> geoOutput = googleApis.geolocate(geoKey, new GeoRequest(wifiAPs)).execute();
            if (!geoOutput.isSuccessful()) {
                String msg = geoOutput.code() + " " + geoOutput.message();
                log.error("Couldn't perform geolocation: {}", msg);
                throw halt(SC_INTERNAL_SERVER_ERROR, msg);
            }

            GeoResponse.Location location = geoOutput.body().getLocation();
            String locationParam = location.getLat() + "," + location.getLng();
            long timestamp = geoOutput.headers().getDate("Date").toInstant().getEpochSecond();
            Response<TimezoneResponse> tzOutput = googleApis.timezone(tzKey, locationParam, timestamp).execute();
            if (!tzOutput.isSuccessful()) {
                String msg = tzOutput.code() + " " + tzOutput.message();
                log.error("Couldn't find time zone: {}", msg);
                throw halt(SC_INTERNAL_SERVER_ERROR, msg);
            }

            TimezoneResponse timezone = tzOutput.body();
            return timezone.getRawOffset() + timezone.getDstOffset();
        });
        get("/time", (req, res) -> {
            long time = Stream.of("pool.ntp.org", "time.nist.gov", "time.windows.com")
                              .parallel()
                              .map(host -> {
                                  SntpClient client = new SntpClient();
                                  return new Object[] {
                                      client,
                                      client.requestTime(host, 1000)
                                  };
                              })
                             .filter(pair -> Boolean.TRUE.equals(pair[1]))
                             .findAny()
                             .map(pair -> {
                                 SntpClient client = (SntpClient) pair[0];
                                 long diff = NANOSECONDS.toMillis(System.nanoTime()) - client.getNtpTimeReference();
                                 return client.getNtpTime() + diff;
                             })
                             .orElseThrow(() -> {
                                 log.error("Couldn't retrieve NTP time");
                                 return halt(SC_BAD_GATEWAY, "Couldn't retrieve NTP time");
                             });
            return time / 1000;
        });
    }
}
