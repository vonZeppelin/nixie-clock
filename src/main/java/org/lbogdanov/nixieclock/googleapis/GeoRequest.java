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

package org.lbogdanov.nixieclock.googleapis;

/**
 * Represents a geolocation request to Google APIs.
 */
public class GeoRequest {
    private final WiFiAccessPoint[] wifiAccessPoints;
    private final boolean considerIp;

    public GeoRequest(WiFiAccessPoint[] wifiAccessPoints, boolean considerIp) {
        this.wifiAccessPoints = wifiAccessPoints;
        this.considerIp = considerIp;
    }

    public GeoRequest(WiFiAccessPoint[] wifiAccessPoints) {
        this(wifiAccessPoints, false);
    }

    public WiFiAccessPoint[] getWifiAccessPoints() {
        return wifiAccessPoints;
    }

    public boolean isConsiderIp() {
        return considerIp;
    }
}
