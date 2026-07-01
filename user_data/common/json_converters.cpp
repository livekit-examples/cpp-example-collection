/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "json_converters.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "constants.h"

namespace user_data {

std::string sensorReadingToJson(const SensorReading& reading) {
  nlohmann::json json;
  json[kFrameIdKey] = reading.frame_id;
  json[kTimestampUsKey] = reading.timestamp_us;
  json[kTemperatureCKey] = reading.temperature_c;
  return json.dump();
}

SensorReading sensorReadingFromJson(const std::string& json_text) {
  try {
    const auto json = nlohmann::json::parse(json_text);

    SensorReading reading;
    reading.frame_id = json.at(kFrameIdKey).get<std::uint32_t>();
    reading.timestamp_us = json.at(kTimestampUsKey).get<std::uint64_t>();
    reading.temperature_c = json.at(kTemperatureCKey).get<double>();
    return reading;
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::string("Failed to parse sensor reading JSON: ") + error.what());
  }
}

} // namespace user_data
