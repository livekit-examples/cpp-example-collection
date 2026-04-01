add_executable(SimpleRoom
  main.cpp
  fallback_capture.cpp
  fallback_capture.h
  sdl_media.cpp
  sdl_media.h
  sdl_media_manager.cpp
  sdl_media_manager.h
  sdl_video_renderer.cpp
  sdl_video_renderer.h
  wav_audio_source.cpp
  wav_audio_source.h
)

target_include_directories(SimpleRoom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SimpleRoom PRIVATE ${LIVEKIT_CORE_TARGET} SDL3::SDL3)

if(LIVEKIT_DATA_DIR)
  add_custom_command(TARGET SimpleRoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${LIVEKIT_DATA_DIR}
            $<TARGET_FILE_DIR:SimpleRoom>/data
  )
endif()
