# Copyright 2026 LiveKit, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include(FetchContent)

set(_schema_mcap_sdk_prefixes
  "${LIVEKIT_LOCAL_SDK_DIR}"
  "${LIVEKIT_SDK_EXTRACTED_ROOT}"
)
set(_schema_mcap_schema_header_found FALSE)
foreach(_schema_mcap_sdk_prefix IN LISTS _schema_mcap_sdk_prefixes)
  if(_schema_mcap_sdk_prefix AND EXISTS "${_schema_mcap_sdk_prefix}/include/livekit/data_track_schema.h")
    set(_schema_mcap_schema_header_found TRUE)
  endif()
endforeach()

if(NOT _schema_mcap_schema_header_found)
  message(FATAL_ERROR
    "schema_mcap requires a LiveKit C++ SDK with schema-metadata support.\n"
    "The configured SDK does not provide include/livekit/data_track_schema.h.\n"
    "Install a schema-enabled SDK or select an SDK release that includes schema metadata."
  )
endif()

FetchContent_Declare(
  foxglove_mcap
  URL https://github.com/foxglove/mcap/archive/fcd77ea7ce9a2f0b951a1392f473d8830e9ab472.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(foxglove_mcap)

add_library(schema_mcap_mcap INTERFACE)
target_include_directories(schema_mcap_mcap SYSTEM INTERFACE
  ${foxglove_mcap_SOURCE_DIR}/cpp/mcap/include
)
target_compile_definitions(schema_mcap_mcap INTERFACE
  MCAP_COMPRESSION_NO_LZ4
  MCAP_COMPRESSION_NO_ZSTD
)

add_subdirectory(publisher)
add_subdirectory(recorder)

unset(_schema_mcap_schema_header_found)
unset(_schema_mcap_sdk_prefix)
unset(_schema_mcap_sdk_prefixes)
