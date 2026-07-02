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

#include "constants.h"

namespace frame_metadata {

std::string sensorReadingToJson(const SensorReading& reading) {
  nlohmann::json json;
  json[kTemperatureCKey] = reading.temperature_c;
  return json.dump();
}

std::optional<SensorReading> sensorReadingFromJson(const std::string& json_text) {
  const auto json = nlohmann::json::parse(json_text, nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    return std::nullopt;
  }

  const auto temperature_it = json.find(kTemperatureCKey);
  if (temperature_it == json.end() || !temperature_it->is_number()) {
    return std::nullopt;
  }

  SensorReading reading;
  reading.temperature_c = temperature_it->get<double>();
  return reading;
}

} // namespace frame_metadata
