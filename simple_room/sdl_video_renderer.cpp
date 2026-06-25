/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sdl_video_renderer.h"

#include "livekit/livekit.h"
#include <cstring>
#include <iostream>

using namespace livekit;

constexpr int kMaxFPS = 60;

SDL_PixelFormat textureFormatFor(livekit::VideoBufferType type) {
  if (type == livekit::VideoBufferType::I420) {
    return SDL_PIXELFORMAT_IYUV;
  }
  return SDL_PIXELFORMAT_RGBA32;
}

SDLVideoRenderer::SDLVideoRenderer() = default;

SDLVideoRenderer::~SDLVideoRenderer() { shutdown(); }

bool SDLVideoRenderer::init(const char* title, int width, int height) {
  width_ = width;
  height_ = height;

  // Assume SDL_Init(SDL_INIT_VIDEO) already called in main()
  window_ = SDL_CreateWindow(title, width_, height_, 0);
  if (!window_) {
    std::cerr << "[error] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (!renderer_) {
    std::cerr << "[error] SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    return false;
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_STREAMING, width_, height_);
  if (!texture_) {
    std::cerr << "[error] SDL_CreateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }
  texture_format_ = SDL_PIXELFORMAT_RGBA32;

  return true;
}

void SDLVideoRenderer::shutdown() {
  stopReader();

  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(latest_frame_lock_);
    latest_frame_.reset();
  }
}

void SDLVideoRenderer::setStream(std::shared_ptr<livekit::VideoStream> stream) {
  stopReader();
  stream_ = std::move(stream);
  if (!stream_) {
    return;
  }

  reader_running_.store(true, std::memory_order_relaxed);
  reader_thread_ = std::thread(&SDLVideoRenderer::readerLoop, this, stream_);
}

void SDLVideoRenderer::stopReader() {
  reader_running_.store(false, std::memory_order_relaxed);
  if (stream_) {
    stream_->close();
  }
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  stream_.reset();
}

void SDLVideoRenderer::readerLoop(
    std::shared_ptr<livekit::VideoStream> stream) {
  while (reader_running_.load(std::memory_order_relaxed)) {
    auto event = std::make_unique<livekit::VideoFrameEvent>();
    if (!stream->read(*event)) {
      break;
    }

    std::lock_guard<std::mutex> lock(latest_frame_lock_);
    latest_frame_ = std::move(event);
  }
}

void SDLVideoRenderer::render() {
  // 0) Basic sanity
  if (!window_ || !renderer_) {
    return;
  }

  // 1) Pump SDL events on the main thread
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) {
      // TODO: set some global or member flag if you want to quit the app
    }
  }

  // Throttle rendering to kMaxFPS
  const auto now = std::chrono::steady_clock::now();
  if (last_render_time_.time_since_epoch().count() != 0) {
    const auto min_interval = std::chrono::microseconds(1'000'000 / kMaxFPS);
    if (now - last_render_time_ < min_interval) {
      return;
    }
  }
  std::unique_ptr<livekit::VideoFrameEvent> latest_frame;
  {
    std::lock_guard<std::mutex> lock(latest_frame_lock_);
    latest_frame.swap(latest_frame_);
  }

  if (!latest_frame) {
    return;
  }
  last_render_time_ = now;

  livekit::VideoFrame &frame = latest_frame->frame;

  if (frame.type() != livekit::VideoBufferType::RGBA &&
      frame.type() != livekit::VideoBufferType::I420) {
    try {
      frame = frame.convert(livekit::VideoBufferType::RGBA, false);
    } catch (const std::exception &ex) {
      std::cerr << "[error] SDLVideoRenderer: convert to RGBA failed: "
                << ex.what() << "\n";
      return;
    }
  }

  const SDL_PixelFormat frame_texture_format = textureFormatFor(frame.type());
  if (frame.width() != width_ || frame.height() != height_ ||
      frame_texture_format != texture_format_) {
    width_ = frame.width();
    height_ = frame.height();
    texture_format_ = frame_texture_format;

    if (texture_) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
    texture_ = SDL_CreateTexture(renderer_, texture_format_,
                                 SDL_TEXTUREACCESS_STREAMING, width_, height_);
    if (!texture_) {
      std::cerr << "[error] SDLVideoRenderer: SDL_CreateTexture failed: "
                << SDL_GetError() << "\n";
      return;
    }
  }

  if (frame.type() == livekit::VideoBufferType::I420) {
    const int chroma_width = (frame.width() + 1) / 2;
    const int chroma_height = (frame.height() + 1) / 2;
    const std::size_t y_size =
        static_cast<std::size_t>(frame.width()) * frame.height();
    const std::size_t chroma_size =
        static_cast<std::size_t>(chroma_width) * chroma_height;
    if (frame.dataSize() < y_size + 2 * chroma_size) {
      std::cerr << "[error] SDLVideoRenderer: I420 frame buffer is too small\n";
      return;
    }
    const std::uint8_t *y_plane = frame.data();
    const std::uint8_t *u_plane = y_plane + y_size;
    const std::uint8_t *v_plane = u_plane + chroma_size;
    if (!SDL_UpdateYUVTexture(texture_, nullptr, y_plane, frame.width(),
                              u_plane, chroma_width, v_plane,
                              chroma_width)) {
      std::cerr << "[error] SDLVideoRenderer: SDL_UpdateYUVTexture failed: "
                << SDL_GetError() << "\n";
      return;
    }
  } else {
    void *pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
      std::cerr << "[error] SDLVideoRenderer: SDL_LockTexture failed: "
                << SDL_GetError() << "\n";
      return;
    }

    const std::uint8_t *src = frame.data();
    const int src_pitch = frame.width() * 4;

    for (int y = 0; y < frame.height(); ++y) {
      std::memcpy(static_cast<std::uint8_t *>(pixels) + y * pitch,
                  src + y * src_pitch, src_pitch);
    }

    SDL_UnlockTexture(texture_);
  }

  int window_width = width_;
  int window_height = height_;
  SDL_GetWindowSize(window_, &window_width, &window_height);

  const float frame_aspect =
      static_cast<float>(frame.width()) / static_cast<float>(frame.height());
  const float window_aspect =
      static_cast<float>(window_width) / static_cast<float>(window_height);
  SDL_FRect destination{};
  if (window_aspect > frame_aspect) {
    destination.h = static_cast<float>(window_height);
    destination.w = destination.h * frame_aspect;
    destination.x = (static_cast<float>(window_width) - destination.w) / 2.0F;
    destination.y = 0.0F;
  } else {
    destination.w = static_cast<float>(window_width);
    destination.h = destination.w / frame_aspect;
    destination.x = 0.0F;
    destination.y =
        (static_cast<float>(window_height) - destination.h) / 2.0F;
  }

  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, &destination);
  SDL_RenderPresent(renderer_);
}
