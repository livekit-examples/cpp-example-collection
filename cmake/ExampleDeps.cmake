include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(SDL3 CONFIG QUIET)
set(_need_sdl3_fetch TRUE)
if(TARGET SDL3::SDL3)
  get_target_property(_sdl3_include_dirs SDL3::SDL3 INTERFACE_INCLUDE_DIRECTORIES)
  if(_sdl3_include_dirs)
    set(_need_sdl3_fetch FALSE)
  endif()
endif()

if(_need_sdl3_fetch)
  FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.26/SDL3-3.2.26.tar.gz
  )
  FetchContent_MakeAvailable(SDL3)
endif()

unset(_need_sdl3_fetch)
unset(_sdl3_include_dirs)
