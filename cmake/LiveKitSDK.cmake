# LiveKitSDK.cmake
#
# Helper for example repos:
# - Downloads the appropriate prebuilt LiveKit C++ SDK asset for the host OS/arch
#     * From a tagged GitHub Release (VERSION mode), or
#     * From a per-commit "Builds" workflow artifact (COMMIT mode)
# - Extracts it into a local directory (default: <build>/_deps/livekit-sdk)
# - Prepends the extracted prefix to CMAKE_PREFIX_PATH so:
#     find_package(LiveKit CONFIG REQUIRED)
#   works out of the box.
#
# Usage (release):
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#   include(LiveKitSDK)
#   livekit_sdk_setup(VERSION "latest" SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk")
#
# Usage (pin to an upstream commit on main):
#   livekit_sdk_setup(
#     COMMIT "f7a8df1b72207d0a81390e64e73f4eb5a4811c56"
#     SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
#     GITHUB_TOKEN "$ENV{GITHUB_TOKEN}")
#
# Optional:
#   livekit_sdk_setup(VERSION "latest" REPO "livekit/client-sdk-cpp" GITHUB_TOKEN "$ENV{GITHUB_TOKEN}")

include_guard(GLOBAL)

# -------------------- Host detection --------------------
function(_lk_detect_host out_os out_arch)
  if(WIN32)
    set(_os "windows")
  elseif(APPLE)
    set(_os "macos")
  elseif(UNIX)
    set(_os "linux")
  else()
    message(FATAL_ERROR "LiveKitSDK: unsupported host OS")
  endif()

  # Use host processor; normalize common variants (case-insensitive)
  set(_proc "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  string(TOLOWER "${_proc}" _proc_l)

  if(_proc_l MATCHES "^(x86_64|amd64)$")
    set(_arch "x64")
  elseif(_proc_l MATCHES "^(arm64|aarch64)$")
    set(_arch "arm64")
  else()
    message(FATAL_ERROR "LiveKitSDK: unsupported host arch: ${_proc}")
  endif()

  set(${out_os}   "${_os}"   PARENT_SCOPE)
  set(${out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

function(_lk_default_triple out_triple)
  _lk_detect_host(_os _arch)
  set(${out_triple} "${_os}-${_arch}" PARENT_SCOPE)
endfunction()

function(_lk_archive_ext out_ext)
  _lk_detect_host(_os _arch)
  if(_os STREQUAL "windows")
    set(${out_ext} "zip" PARENT_SCOPE)
  else()
    set(${out_ext} "tar.gz" PARENT_SCOPE)
  endif()
endfunction()

# -------------------- Commit-mode helpers --------------------
# Build a list of curl-style HTTPHEADER args for the GitHub REST API.
# Always sets a User-Agent and (when token is non-empty) Authorization.
function(_lk_github_headers out_headers github_token)
  set(_headers
    "User-Agent: cmake-livekit-sdk/1.0"
    "Accept: application/vnd.github+json"
    "X-GitHub-Api-Version: 2022-11-28"
  )
  if(NOT "${github_token}" STREQUAL "")
    string(STRIP "${github_token}" github_token)
  endif()
  if(NOT "${github_token}" STREQUAL "")
    list(APPEND _headers "Authorization: Bearer ${github_token}")
  endif()
  set(${out_headers} "${_headers}" PARENT_SCOPE)
endfunction()

# Find the most recent successful "Builds" workflow run for the given commit
# on main, and return its artifact id matching `livekit-sdk-<triple>`.
function(_lk_resolve_commit_artifact
         out_artifact_id
         out_resolved_sha
         repo
         commit
         triple
         download_dir
         github_token)
  if(NOT download_dir)
    set(download_dir "${CMAKE_BINARY_DIR}/_downloads")
  endif()
  file(MAKE_DIRECTORY "${download_dir}")

  _lk_github_headers(_headers "${github_token}")
  if("${github_token}" STREQUAL "")
    message(STATUS "LiveKitSDK: no GITHUB_TOKEN provided; "
                   "GitHub API may rate-limit and artifact downloads will fail.")
  endif()

  # GitHub API requires the full 40-char SHA for the head_sha filter to work,
  # so resolve any prefix to a full SHA via the commits endpoint first.
  string(LENGTH "${commit}" _commit_len)
  set(_full_sha "${commit}")
  if(NOT _commit_len EQUAL 40)
    set(_commit_url "https://api.github.com/repos/${repo}/commits/${commit}")
    set(_commit_json "${download_dir}/livekit_commit_${commit}.json")
    set(_dl_args TLS_VERIFY ON STATUS _st)
    foreach(_h IN LISTS _headers)
      list(APPEND _dl_args HTTPHEADER "${_h}")
    endforeach()
    file(DOWNLOAD "${_commit_url}" "${_commit_json}" ${_dl_args})
    list(GET _st 0 _code)
    list(GET _st 1 _msg)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR
        "LiveKitSDK: failed to resolve commit ${commit} via GitHub API\n"
        "URL: ${_commit_url}\nStatus: ${_code}\nMessage: ${_msg}")
    endif()
    file(READ "${_commit_json}" _commit_body)
    string(JSON _full_sha GET "${_commit_body}" sha)
  endif()

  # Find a successful run of the "Builds" workflow for this exact commit.
  set(_runs_url
      "https://api.github.com/repos/${repo}/actions/workflows/builds.yml/runs?head_sha=${_full_sha}&status=success&per_page=20")
  set(_runs_json "${download_dir}/livekit_runs_${_full_sha}.json")
  set(_dl_args TLS_VERIFY ON STATUS _st LOG _log)
  foreach(_h IN LISTS _headers)
    list(APPEND _dl_args HTTPHEADER "${_h}")
  endforeach()
  file(DOWNLOAD "${_runs_url}" "${_runs_json}" ${_dl_args})
  list(GET _st 0 _code)
  list(GET _st 1 _msg)
  if(NOT _code EQUAL 0)
    message(STATUS "LiveKitSDK: GitHub API log:\n${_log}")
    message(FATAL_ERROR
      "LiveKitSDK: failed to query Builds runs for ${_full_sha}\n"
      "URL: ${_runs_url}\nStatus: ${_code}\nMessage: ${_msg}")
  endif()
  file(READ "${_runs_json}" _runs_body)
  string(JSON _total_count ERROR_VARIABLE _err GET "${_runs_body}" total_count)
  if(NOT _err STREQUAL "NOTFOUND" OR _total_count LESS_EQUAL 0)
    if(_err STREQUAL "NOTFOUND")
      # _total_count is set to the value
    else()
      message(FATAL_ERROR
        "LiveKitSDK: GitHub API response missing total_count: ${_runs_body}")
    endif()
  endif()
  if(_total_count LESS_EQUAL 0)
    message(FATAL_ERROR
      "LiveKitSDK: No successful Builds runs found for commit ${_full_sha} on ${repo}.\n"
      "Either the commit is too old (artifacts expire after ~7 days), the build hasn't "
      "completed yet, or the commit isn't on a branch CI runs against.")
  endif()

  # Pick the newest run (the API returns newest first by default).
  string(JSON _run_id GET "${_runs_body}" workflow_runs 0 id)

  # List artifacts for that run.
  set(_artifacts_url "https://api.github.com/repos/${repo}/actions/runs/${_run_id}/artifacts?per_page=100")
  set(_artifacts_json "${download_dir}/livekit_artifacts_${_run_id}.json")
  set(_dl_args TLS_VERIFY ON STATUS _st LOG _log)
  foreach(_h IN LISTS _headers)
    list(APPEND _dl_args HTTPHEADER "${_h}")
  endforeach()
  file(DOWNLOAD "${_artifacts_url}" "${_artifacts_json}" ${_dl_args})
  list(GET _st 0 _code)
  list(GET _st 1 _msg)
  if(NOT _code EQUAL 0)
    message(STATUS "LiveKitSDK: GitHub API log:\n${_log}")
    message(FATAL_ERROR
      "LiveKitSDK: failed to list artifacts for run ${_run_id}\n"
      "URL: ${_artifacts_url}\nStatus: ${_code}\nMessage: ${_msg}")
  endif()
  file(READ "${_artifacts_json}" _artifacts_body)

  set(_target_name "livekit-sdk-${triple}")
  string(JSON _artifact_count GET "${_artifacts_body}" total_count)
  set(_found_id "")
  set(_found_expired TRUE)
  if(_artifact_count GREATER 0)
    math(EXPR _last "${_artifact_count} - 1")
    foreach(_i RANGE 0 ${_last})
      string(JSON _name GET "${_artifacts_body}" artifacts ${_i} name)
      if(_name STREQUAL "${_target_name}")
        string(JSON _id GET "${_artifacts_body}" artifacts ${_i} id)
        string(JSON _expired GET "${_artifacts_body}" artifacts ${_i} expired)
        set(_found_id "${_id}")
        if(_expired)
          set(_found_expired TRUE)
        else()
          set(_found_expired FALSE)
        endif()
        break()
      endif()
    endforeach()
  endif()

  if(_found_id STREQUAL "")
    message(FATAL_ERROR
      "LiveKitSDK: artifact '${_target_name}' not found in Builds run ${_run_id} for ${_full_sha}.\n"
      "Available artifacts may have expired (~7 day retention).")
  endif()
  if(_found_expired)
    message(FATAL_ERROR
      "LiveKitSDK: artifact '${_target_name}' for ${_full_sha} has expired.\n"
      "Pin to a more recent commit, or use a tagged VERSION instead.")
  endif()

  set(${out_artifact_id} "${_found_id}" PARENT_SCOPE)
  set(${out_resolved_sha} "${_full_sha}" PARENT_SCOPE)
endfunction()

# Synthesize the LiveKit CMake package config files inside an extracted
# per-commit artifact (which lacks the lib/cmake/LiveKit/ tree the tagged
# release tarballs ship with).
function(_lk_write_cmake_config extracted_root)
  _lk_detect_host(_os _arch)
  if(_os STREQUAL "windows")
    set(_lib_name "livekit.lib")
    set(_runtime_name "livekit.dll")
    set(_imp_kind "SHARED")
  elseif(_os STREQUAL "macos")
    set(_lib_name "liblivekit.dylib")
    set(_runtime_name "${_lib_name}")
    set(_imp_kind "SHARED")
  else()
    set(_lib_name "liblivekit.so")
    set(_runtime_name "${_lib_name}")
    set(_imp_kind "SHARED")
  endif()

  set(_cmake_dir "${extracted_root}/lib/cmake/LiveKit")
  file(MAKE_DIRECTORY "${_cmake_dir}")

  file(WRITE "${_cmake_dir}/LiveKitConfig.cmake"
"# Auto-generated by LiveKitSDK.cmake for per-commit artifact builds.
get_filename_component(PACKAGE_PREFIX_DIR \"\${CMAKE_CURRENT_LIST_DIR}/../../../\" ABSOLUTE)
include(\"\${CMAKE_CURRENT_LIST_DIR}/LiveKitTargets.cmake\")
")

  if(_os STREQUAL "windows")
    set(_lib_path "\${_IMPORT_PREFIX}/lib/${_lib_name}")
    set(_imploc_lines
"  IMPORTED_IMPLIB_RELEASE \"\${_IMPORT_PREFIX}/lib/${_lib_name}\"
  IMPORTED_LOCATION_RELEASE \"\${_IMPORT_PREFIX}/bin/${_runtime_name}\"")
  else()
    set(_imploc_lines
"  IMPORTED_LOCATION_RELEASE \"\${_IMPORT_PREFIX}/lib/${_runtime_name}\"")
  endif()

  file(WRITE "${_cmake_dir}/LiveKitTargets.cmake"
"# Auto-generated by LiveKitSDK.cmake.
if(TARGET LiveKit::livekit)
  return()
endif()

get_filename_component(_IMPORT_PREFIX \"\${CMAKE_CURRENT_LIST_FILE}\" PATH)
get_filename_component(_IMPORT_PREFIX \"\${_IMPORT_PREFIX}\" PATH)
get_filename_component(_IMPORT_PREFIX \"\${_IMPORT_PREFIX}\" PATH)
get_filename_component(_IMPORT_PREFIX \"\${_IMPORT_PREFIX}\" PATH)

add_library(LiveKit::livekit ${_imp_kind} IMPORTED)
set_target_properties(LiveKit::livekit PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES \"\${_IMPORT_PREFIX}/include\"
)

file(GLOB _cmake_config_files \"\${CMAKE_CURRENT_LIST_DIR}/LiveKitTargets-*.cmake\")
foreach(_cmake_config_file IN LISTS _cmake_config_files)
  include(\"\${_cmake_config_file}\")
endforeach()
unset(_cmake_config_file)
unset(_cmake_config_files)
unset(_IMPORT_PREFIX)
")

  file(WRITE "${_cmake_dir}/LiveKitTargets-release.cmake"
"# Auto-generated by LiveKitSDK.cmake.
set_property(TARGET LiveKit::livekit APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LiveKit::livekit PROPERTIES
${_imploc_lines}
)
")

  file(WRITE "${_cmake_dir}/LiveKitConfigVersion.cmake"
"# Auto-generated stub. Per-commit artifact builds report version 0.0.0.
set(PACKAGE_VERSION \"0.0.0\")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT FALSE)
")
endfunction()

# -------------------- GitHub API helpers --------------------
# Resolve VERSION="latest" via GitHub API, returning version without leading "v".
function(_lk_resolve_latest_version out_version repo download_dir github_token)
  if(NOT download_dir)
    set(download_dir "${CMAKE_BINARY_DIR}/_downloads")
  endif()
  file(MAKE_DIRECTORY "${download_dir}")

  set(_api "https://api.github.com/repos/${repo}/releases/latest")

  # Sanitize repo for filename
  string(REPLACE "/" "_" _repo_sanitized "${repo}")
  set(_json "${download_dir}/livekit_latest_release_${_repo_sanitized}.json")

  # Build headers as a proper LIST (each element is one full header line)
  set(_headers
    "User-Agent: cmake-livekit-sdk/1.0"
    "Accept: application/vnd.github+json"
    "X-GitHub-Api-Version: 2022-11-28"
  )

  # Strip token (defensive: avoids accidental newline causing header splitting)
  if(NOT "${github_token}" STREQUAL "")
    string(STRIP "${github_token}" github_token)
  endif()

  # Use Authorization only if token is non-empty
  if(NOT "${github_token}" STREQUAL "")
    # "token" is broadly compatible
    list(APPEND _headers "Authorization: Bearer ${github_token}")
  else()
    message(STATUS "LiveKitSDK: no GITHUB_TOKEN provided; GitHub API may rate-limit.")
  endif()

  # Capture LOG for actionable failure output
  set(_dl_args
    TLS_VERIFY ON
    STATUS _st
    LOG _log
  )
  foreach(_h IN LISTS _headers)
    list(APPEND _dl_args HTTPHEADER "${_h}")
  endforeach()
  file(DOWNLOAD "${_api}" "${_json}" ${_dl_args})

  list(GET _st 0 _code)
  list(GET _st 1 _msg)
  if(NOT _code EQUAL 0)
    message(STATUS "LiveKitSDK: GitHub API download log:\n${_log}")
    if(EXISTS "${_json}")
      file(READ "${_json}" _body)
      message(STATUS "LiveKitSDK: GitHub API response body:\n${_body}")
    endif()
    message(FATAL_ERROR
      "LiveKitSDK: failed to query latest release from GitHub API\n"
      "API: ${_api}\n"
      "Status: ${_code}\n"
      "Message: ${_msg}\n"
      "Tip: set GITHUB_TOKEN to avoid rate limits, or use VERSION=<fixed>."
    )
  endif()

  file(READ "${_json}" _content)

  # CMake >= 3.19 supports string(JSON ...)
  string(JSON _tag GET "${_content}" tag_name)
  if(_tag STREQUAL "")
    message(FATAL_ERROR "LiveKitSDK: GitHub API response missing tag_name")
  endif()

  # Strip leading "v" if present
  string(REGEX REPLACE "^v" "" _ver "${_tag}")
  set(${out_version} "${_ver}" PARENT_SCOPE)
endfunction()

# -------------------- Public entrypoint --------------------
# livekit_sdk_setup(
#   [VERSION <ver|latest>]            tagged release (e.g. "0.3.4" or "latest")
#   [COMMIT <sha>]                    OR pin to per-commit Builds artifact
#   SDK_DIR <dir>
#   [REPO <org/repo>]                 default: livekit/client-sdk-cpp
#   [SHA256 <hash>]                   optional: verify download (only works for fixed VERSION)
#   [TRIPLE <os-arch>]                optional override
#   [DOWNLOAD_DIR <dir>]              default: <build>/_downloads
#   [GITHUB_TOKEN <token>]            optional/required: auth for GitHub API
#                                     - VERSION=latest: optional, avoids rate limits
#                                     - COMMIT mode:    required (artifact downloads need auth)
#   [NO_DOWNLOAD]                     error if not already present
# )
#
# Exactly one of VERSION or COMMIT must be provided. COMMIT mode downloads the
# `livekit-sdk-<triple>` artifact uploaded by the upstream "Builds" workflow
# for that commit on `main`. Note: GitHub Actions artifacts have a 7-day
# retention by default; pinning to old commits will fail.
function(livekit_sdk_setup)
  set(options NO_DOWNLOAD)
  set(oneValueArgs VERSION COMMIT SDK_DIR REPO SHA256 TRIPLE DOWNLOAD_DIR GITHUB_TOKEN)
  cmake_parse_arguments(LK "${options}" "${oneValueArgs}" "" ${ARGN})

  if(LK_VERSION AND LK_COMMIT)
    message(FATAL_ERROR
      "livekit_sdk_setup: pass either VERSION or COMMIT, not both")
  endif()
  if(NOT LK_VERSION AND NOT LK_COMMIT)
    message(FATAL_ERROR
      "livekit_sdk_setup: VERSION or COMMIT is required "
      "(VERSION=\"latest\" picks the newest release)")
  endif()
  if(NOT LK_SDK_DIR)
    message(FATAL_ERROR "livekit_sdk_setup: SDK_DIR is required")
  endif()

  if(NOT LK_REPO)
    set(LK_REPO "livekit/client-sdk-cpp")
  endif()
  if(NOT LK_DOWNLOAD_DIR)
    set(LK_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_downloads")
  endif()
  if(NOT LK_TRIPLE)
    _lk_default_triple(LK_TRIPLE)
  endif()

  # -------------------- COMMIT mode --------------------
  if(LK_COMMIT)
    if(NOT LK_GITHUB_TOKEN)
      set(LK_GITHUB_TOKEN "$ENV{GITHUB_TOKEN}")
    endif()
    if("${LK_GITHUB_TOKEN}" STREQUAL "")
      message(FATAL_ERROR
        "LiveKitSDK: COMMIT mode requires GITHUB_TOKEN (artifact API needs auth).\n"
        "Provide it via livekit_sdk_setup(... GITHUB_TOKEN ...) or env var GITHUB_TOKEN.")
    endif()
    if(LK_SHA256)
      message(WARNING "LiveKitSDK: SHA256 ignored when COMMIT is set "
                      "(per-commit artifact contents change with each rebuild).")
    endif()

    string(SUBSTRING "${LK_COMMIT}" 0 12 _short_sha)
    set(_extracted_root "${LK_SDK_DIR}/livekit-sdk-${LK_TRIPLE}-commit-${_short_sha}")
    set(_archive_path "${LK_DOWNLOAD_DIR}/livekit-sdk-${LK_TRIPLE}-commit-${_short_sha}.zip")

    file(MAKE_DIRECTORY "${LK_DOWNLOAD_DIR}")
    file(MAKE_DIRECTORY "${LK_SDK_DIR}")

    if(NOT EXISTS "${_extracted_root}/lib/cmake/LiveKit/LiveKitConfig.cmake")
      if(LK_NO_DOWNLOAD)
        message(FATAL_ERROR
          "LiveKitSDK: SDK not found at:\n  ${_extracted_root}\n"
          "and NO_DOWNLOAD was set.")
      endif()

      _lk_resolve_commit_artifact(_artifact_id _full_sha
        "${LK_REPO}" "${LK_COMMIT}" "${LK_TRIPLE}"
        "${LK_DOWNLOAD_DIR}" "${LK_GITHUB_TOKEN}")
      message(STATUS "LiveKitSDK: pinning to commit ${_full_sha}")
      message(STATUS "LiveKitSDK: artifact id = ${_artifact_id}")

      _lk_github_headers(_headers "${LK_GITHUB_TOKEN}")
      set(_artifact_url
          "https://api.github.com/repos/${LK_REPO}/actions/artifacts/${_artifact_id}/zip")
      set(_dl_args SHOW_PROGRESS TLS_VERIFY ON STATUS _st LOG _log)
      foreach(_h IN LISTS _headers)
        list(APPEND _dl_args HTTPHEADER "${_h}")
      endforeach()
      message(STATUS "LiveKitSDK: downloading ${_artifact_url}")
      file(DOWNLOAD "${_artifact_url}" "${_archive_path}" ${_dl_args})
      list(GET _st 0 _code)
      list(GET _st 1 _msg)
      if(NOT _code EQUAL 0)
        message(STATUS "LiveKitSDK: artifact download log:\n${_log}")
        message(FATAL_ERROR
          "LiveKitSDK: failed to download artifact ${_artifact_id}\n"
          "URL: ${_artifact_url}\nStatus: ${_code}\nMessage: ${_msg}")
      endif()

      file(REMOVE_RECURSE "${_extracted_root}")
      file(MAKE_DIRECTORY "${_extracted_root}")
      message(STATUS "LiveKitSDK: extracting ${_archive_path}")
      file(ARCHIVE_EXTRACT
        INPUT "${_archive_path}"
        DESTINATION "${_extracted_root}")

      _lk_write_cmake_config("${_extracted_root}")
    endif()

    if(NOT EXISTS "${_extracted_root}/include/livekit/livekit.h")
      message(FATAL_ERROR
        "LiveKitSDK: extracted artifact looks invalid (missing include/livekit/livekit.h)\n"
        "Path: ${_extracted_root}")
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${_extracted_root}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    set(LiveKit_DIR "${_extracted_root}/lib/cmake/LiveKit" PARENT_SCOPE)

    set(LIVEKIT_SDK_EXTRACTED_ROOT "${_extracted_root}" CACHE PATH "LiveKit SDK extracted root" FORCE)
    set(LIVEKIT_SDK_URL_USED "${_artifact_url}" CACHE STRING "LiveKit SDK URL used" FORCE)
    set(LIVEKIT_SDK_VERSION_RESOLVED "commit-${_short_sha}" CACHE STRING "LiveKit SDK resolved version" FORCE)
    set(LIVEKIT_SDK_TRIPLE_USED "${LK_TRIPLE}" CACHE STRING "LiveKit SDK triple used" FORCE)
    set(LIVEKIT_SDK_COMMIT_RESOLVED "${LK_COMMIT}" CACHE STRING "LiveKit SDK commit resolved" FORCE)

    message(STATUS "LiveKitSDK: using SDK at ${_extracted_root}")
    return()
  endif()

  # Resolve latest tag if requested
  set(_resolved_version "${LK_VERSION}")
  if(LK_VERSION STREQUAL "latest")
    if(LK_SHA256)
      message(WARNING "LiveKitSDK: SHA256 was provided but VERSION=latest; ignoring SHA256.")
      set(LK_SHA256 "")
    endif()

    if(NOT LK_GITHUB_TOKEN)
      # Common in CI if you set env: GITHUB_TOKEN: ${{ github.token }}
      set(LK_GITHUB_TOKEN "$ENV{GITHUB_TOKEN}")
    endif()

    _lk_resolve_latest_version(_resolved_version "${LK_REPO}" "${LK_DOWNLOAD_DIR}" "${LK_GITHUB_TOKEN}")
    message(STATUS "LiveKitSDK: resolved latest version = ${_resolved_version}")
  endif()

  _lk_archive_ext(_ext)
  set(_archive "livekit-sdk-${LK_TRIPLE}-${_resolved_version}.${_ext}")
  set(_url "https://github.com/${LK_REPO}/releases/download/v${_resolved_version}/${_archive}")

  set(_archive_path "${LK_DOWNLOAD_DIR}/${_archive}")

  # The archive is expected to contain a top-level folder named:
  #   livekit-sdk-<triple>-<version>/
  set(_extracted_root "${LK_SDK_DIR}/livekit-sdk-${LK_TRIPLE}-${_resolved_version}")

  file(MAKE_DIRECTORY "${LK_DOWNLOAD_DIR}")
  file(MAKE_DIRECTORY "${LK_SDK_DIR}")

  if(NOT EXISTS "${_extracted_root}")
    if(LK_NO_DOWNLOAD)
      message(FATAL_ERROR
        "LiveKitSDK: SDK not found at:\n  ${_extracted_root}\n"
        "and NO_DOWNLOAD was set."
      )
    endif()

    message(STATUS "LiveKitSDK: downloading ${_url}")

    if(LK_SHA256)
      file(DOWNLOAD "${_url}" "${_archive_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        EXPECTED_HASH "SHA256=${LK_SHA256}"
        STATUS _st
        LOG _log
      )
    else()
      file(DOWNLOAD "${_url}" "${_archive_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS _st
        LOG _log
      )
    endif()

    list(GET _st 0 _code)
    list(GET _st 1 _msg)
    if(NOT _code EQUAL 0)
      message(STATUS "LiveKitSDK: download log:\n${_log}")
      message(FATAL_ERROR "LiveKitSDK: download failed\nURL: ${_url}\nStatus: ${_code}\nMessage: ${_msg}")
    endif()

    # Remove any previous partial extraction
    file(REMOVE_RECURSE "${_extracted_root}")

    message(STATUS "LiveKitSDK: extracting ${_archive_path}")
    file(ARCHIVE_EXTRACT
      INPUT "${_archive_path}"
      DESTINATION "${LK_SDK_DIR}"
    )
  endif()

  if(NOT EXISTS "${_extracted_root}/lib/cmake")
    message(FATAL_ERROR
      "LiveKitSDK: extracted SDK does not look valid (missing lib/cmake)\n"
      "Expected: ${_extracted_root}\n"
      "If your archive root folder name differs, adjust _extracted_root logic."
    )
  endif()

  # Make find_package(LiveKit CONFIG REQUIRED) work.
  list(PREPEND CMAKE_PREFIX_PATH "${_extracted_root}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)

  # Direct hint to the package config dir
  set(LiveKit_DIR "${_extracted_root}/lib/cmake/LiveKit" PARENT_SCOPE)

  # Export a few useful variables for callers (optional).
  set(LIVEKIT_SDK_EXTRACTED_ROOT "${_extracted_root}" CACHE PATH "LiveKit SDK extracted root" FORCE)
  set(LIVEKIT_SDK_URL_USED "${_url}" CACHE STRING "LiveKit SDK URL used" FORCE)
  set(LIVEKIT_SDK_VERSION_RESOLVED "${_resolved_version}" CACHE STRING "LiveKit SDK resolved version" FORCE)
  set(LIVEKIT_SDK_TRIPLE_USED "${LK_TRIPLE}" CACHE STRING "LiveKit SDK triple used" FORCE)

  message(STATUS "LiveKitSDK: using SDK at ${_extracted_root}")
endfunction()
