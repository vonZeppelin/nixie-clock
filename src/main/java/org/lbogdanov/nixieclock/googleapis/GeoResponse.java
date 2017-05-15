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
 * Represents a geolocation response from Google APIs.
 */
public class GeoResponse {
    /**
     * Represents a location.
     */
    public static class Location {
        private float lat;
        private float lng;

        public float getLat() {
            return lat;
        }

        public float getLng() {
            return lng;
        }
    }

    private Location location;

    public Location getLocation() {
        return location;
    }
}
