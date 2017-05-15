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

import retrofit2.Call;
import retrofit2.http.*;

/**
 * Describes endpoints of Google APIs.
 */
public interface GoogleApis {
    String BASE_URL = "https://www.googleapis.com/";

    @POST("geolocation/v1/geolocate")
    Call<GeoResponse> geolocate(@Query("key") String key, @Body GeoRequest request);

    @GET("//maps.googleapis.com/maps/api/timezone/json")
    Call<TimezoneResponse> timezone(@Query("key") String key,
                                    @Query("location") String location,
                                    @Query("timestamp") long timestamp);
}
