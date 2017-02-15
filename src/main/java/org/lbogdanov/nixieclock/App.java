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
import java.util.List;
import java.util.Optional;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import java.util.stream.Stream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;

import us.monoid.json.JSONArray;
import us.monoid.json.JSONException;
import us.monoid.json.JSONObject;
import us.monoid.web.JSONResource;
import us.monoid.web.Resty;
import us.monoid.web.Resty.Option;

/**
 * Nixie Clock server can respond with:
 * <ul>
 *   <li> Current UTC time;
 *   <li> UTC offset based on 2 or more WiFi BSSIDs taken as input from a client.
 * </ul>
 */
public class App {
    private static final String GEOLOCATION_API_URL = "https://www.googleapis.com/geolocation/v1/geolocate";
    private static final String TIMEZONE_API_URL = "https://maps.googleapis.com/maps/api/timezone/json";
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
        Option restyTimeout = Option.timeout(5000);
        String authToken = Optional.ofNullable(getenv(AUTH_TOKEN))
                                   .orElseGet(() -> getProperty(AUTH_TOKEN));
        String geoKey = Optional.ofNullable(getenv(GEOLOCATION_API_KEY))
                                .orElseGet(() -> getProperty(GEOLOCATION_API_KEY));
        String tzKey = Optional.ofNullable(getenv(TIMEZONE_API_KEY))
                               .orElseGet(() -> getProperty(TIMEZONE_API_KEY));

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
            JSONObject[] wifiAPs = Arrays.stream(bssids)
                                         .flatMap(commaSplitter::splitAsStream)
                                         .map(bssid -> {
                                             try {
                                                 if (bssidValidator.matcher(bssid).matches()) {
                                                     return new JSONObject().put("macAddress", bssid);
                                                 }
                                             } catch (JSONException | NullPointerException ignore) {}
                                             log.warn("Ivalid BSSID {}", bssid);
                                             throw halt(SC_BAD_REQUEST, "Invalid BSSID " + bssid);
                                         })
                                         .toArray(JSONObject[]::new);
            if (wifiAPs.length < 2) {
                throw halt(SC_BAD_REQUEST, "2 or more WiFi BSSIDs are required");
            }

            int rawOffset = 0;
            int dstOffset = 0;
            try {
                Resty resty = new Resty(restyTimeout);

                JSONObject geoInput = new JSONObject().put("considerIp", false)
                                                      .put("wifiAccessPoints", new JSONArray(wifiAPs));
                String geoParams = toParams("key", geoKey);
                JSONResource geoOutput = resty.json(GEOLOCATION_API_URL + "?" + geoParams, Resty.content(geoInput));
                if (!geoOutput.status(SC_OK)) {
                    throw new Exception(geoOutput.http().getResponseMessage());
                }

                String tzParams = toParams("key", tzKey,
                                           "location", geoOutput.get("location.lat") + "," + geoOutput.get("location.lng"),
                                           "timestamp", geoOutput.http().getDate() / 1000);
                JSONResource tzOutput = resty.json(TIMEZONE_API_URL + "?" + tzParams);
                if (!geoOutput.status(SC_OK)) {
                    throw new Exception(geoOutput.http().getResponseMessage());
                }

                rawOffset = ((Number) tzOutput.get("rawOffset")).intValue();
                dstOffset = ((Number) tzOutput.get("dstOffset")).intValue();
            } catch (Exception e) {
                log.error("Couldn't find out timezone, will use UTC", e);
            }

            return rawOffset + dstOffset;
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

    private static String toParams(Object... kvs) {
        assert kvs.length % 2 == 0;
        List<Object> params = Arrays.asList(kvs);
        return IntStream.range(0, params.size() / 2)
                        .mapToObj(i -> params.subList(i * 2, (i + 1) * 2))
                        .map(kv -> kv.stream()
                                     .map(s -> Resty.enc(String.valueOf(s)))
                                     .collect(Collectors.joining("=")))
                        .collect(Collectors.joining("&"));
    }
}
