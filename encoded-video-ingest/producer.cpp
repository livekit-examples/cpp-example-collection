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

/// EncodedVideoIngestProducer
///
/// Reads encoded frames from a GStreamer tcpserversink and publishes them via
/// VideoSource(..., EncodedVideoSourceOptions). The TCP reader/demuxing lives
/// in the example; the SDK only owns the encoded video source and WebRTC path.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "livekit/livekit.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

enum class CodecArg {
  H264,
  H265,
  VP8,
  AV1,
};

struct Args {
  std::string url;
  std::string token;
  std::string tcp_host = "127.0.0.1";
  std::uint16_t tcp_port = 5005;
  std::uint32_t width = 640;
  std::uint32_t height = 480;
  std::uint64_t max_bitrate_bps = 1'000'000;
  double max_framerate = 30.0;
  CodecArg codec = CodecArg::H264;
  std::optional<std::string> track_name;
};

struct Stats {
  std::uint64_t accepted = 0;
  std::uint64_t dropped = 0;
  std::uint64_t keyframes = 0;
  std::uint64_t bytes = 0;
};

VideoCodec toVideoCodec(CodecArg codec) {
  switch (codec) {
  case CodecArg::H264:
    return VideoCodec::H264;
  case CodecArg::H265:
    return VideoCodec::H265;
  case CodecArg::VP8:
    return VideoCodec::VP8;
  case CodecArg::AV1:
    return VideoCodec::AV1;
  }
  return VideoCodec::H264;
}

const char *codecName(CodecArg codec) {
  switch (codec) {
  case CodecArg::H264:
    return "H.264";
  case CodecArg::H265:
    return "H.265";
  case CodecArg::VP8:
    return "VP8";
  case CodecArg::AV1:
    return "AV1";
  }
  return "unknown";
}

std::string defaultTrackName(CodecArg codec) {
  switch (codec) {
  case CodecArg::H264:
    return "encoded-h264";
  case CodecArg::H265:
    return "encoded-h265";
  case CodecArg::VP8:
    return "encoded-vp8";
  case CodecArg::AV1:
    return "encoded-av1";
  }
  return "encoded-video";
}

void printUsage(const char *program) {
  std::cerr << "Usage:\n"
            << "  " << program << " <ws-url> <token> [flags]\n"
            << "or:\n"
            << "  LIVEKIT_URL=... LIVEKIT_TOKEN=... " << program
            << " [flags]\n\n"
            << "Flags:\n"
            << "  --tcp-host <host>           default: 127.0.0.1\n"
            << "  --tcp-port <port>           default: 5005\n"
            << "  --width <px>                default: 640\n"
            << "  --height <px>               default: 480\n"
            << "  --codec <h264|h265|vp8|av1> default: h264\n"
            << "  --track-name <name>         default: encoded-<codec>\n"
            << "  --max-bitrate-kbps <kbps>   default: 1000\n"
            << "  --max-framerate <fps>       default: 30\n";
}

std::string takeValue(int &index, int argc, char *argv[]) {
  if (index + 1 >= argc) {
    throw std::invalid_argument(std::string("missing value for ") +
                                argv[index]);
  }
  ++index;
  return argv[index];
}

CodecArg parseCodec(const std::string &value) {
  if (value == "h264") {
    return CodecArg::H264;
  }
  if (value == "h265") {
    return CodecArg::H265;
  }
  if (value == "vp8") {
    return CodecArg::VP8;
  }
  if (value == "av1") {
    return CodecArg::AV1;
  }
  throw std::invalid_argument("unsupported codec: " + value);
}

bool parseArgs(int argc, char *argv[], Args &args) {
  args.url = getenvOrEmpty("LIVEKIT_URL");
  args.token = getenvOrEmpty("LIVEKIT_TOKEN");
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return false;
    }
    if (arg == "--tcp-host") {
      args.tcp_host = takeValue(i, argc, argv);
    } else if (arg == "--tcp-port") {
      args.tcp_port =
          static_cast<std::uint16_t>(std::stoul(takeValue(i, argc, argv)));
    } else if (arg == "--width") {
      args.width =
          static_cast<std::uint32_t>(std::stoul(takeValue(i, argc, argv)));
    } else if (arg == "--height") {
      args.height =
          static_cast<std::uint32_t>(std::stoul(takeValue(i, argc, argv)));
    } else if (arg == "--codec") {
      args.codec = parseCodec(takeValue(i, argc, argv));
    } else if (arg == "--track-name") {
      args.track_name = takeValue(i, argc, argv);
    } else if (arg == "--max-bitrate-kbps") {
      args.max_bitrate_bps =
          static_cast<std::uint64_t>(std::stoull(takeValue(i, argc, argv))) *
          1000;
    } else if (arg == "--max-framerate") {
      args.max_framerate = std::stod(takeValue(i, argc, argv));
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.size() >= 2) {
    args.url = positional[0];
    args.token = positional[1];
  }

  return !(args.url.empty() || args.token.empty());
}

std::optional<std::pair<std::size_t, std::size_t>>
findStartCode(const std::vector<std::uint8_t> &data, std::size_t offset) {
  for (std::size_t i = offset; i + 3 <= data.size(); ++i) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      return std::make_pair(i, 3);
    }
    if (i + 4 <= data.size() && data[i] == 0 && data[i + 1] == 0 &&
        data[i + 2] == 0 && data[i + 3] == 1) {
      return std::make_pair(i, 4);
    }
  }
  return std::nullopt;
}

std::uint8_t nalType(CodecArg codec, std::uint8_t first_byte) {
  if (codec == CodecArg::H264) {
    return first_byte & 0x1F;
  }
  if (codec == CodecArg::H265) {
    return (first_byte >> 1) & 0x3F;
  }
  return 0;
}

bool isAud(CodecArg codec, std::uint8_t nal_type) {
  return (codec == CodecArg::H264 && nal_type == 9) ||
         (codec == CodecArg::H265 && nal_type == 35);
}

bool isAnnexBKeyframe(CodecArg codec, const std::vector<std::uint8_t> &data) {
  std::size_t offset = 0;
  while (auto start = findStartCode(data, offset)) {
    const std::size_t nal_offset = start->first + start->second;
    if (nal_offset >= data.size()) {
      break;
    }
    const std::uint8_t type = nalType(codec, data[nal_offset]);
    if (codec == CodecArg::H264 && type == 5) {
      return true;
    }
    if (codec == CodecArg::H265 && type >= 16 && type <= 23) {
      return true;
    }
    offset = nal_offset + 1;
  }
  return false;
}

bool readLeb128(const std::vector<std::uint8_t> &data, std::size_t &offset,
                std::uint32_t &value) {
  value = 0;
  for (int i = 0; i < 8 && offset < data.size(); ++i) {
    const std::uint8_t byte = data[offset++];
    value |= static_cast<std::uint32_t>(byte & 0x7F) << (i * 7);
    if ((byte & 0x80) == 0) {
      return true;
    }
  }
  return false;
}

bool isAv1Keyframe(const std::vector<std::uint8_t> &data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::uint8_t header = data[offset++];
    const std::uint8_t obu_type = (header >> 3) & 0x0F;
    const bool has_extension = (header & 0x04) != 0;
    const bool has_size = (header & 0x02) != 0;
    if (has_extension) {
      if (offset >= data.size()) {
        return false;
      }
      ++offset;
    }
    std::uint32_t payload_size = 0;
    if (has_size) {
      if (!readLeb128(data, offset, payload_size)) {
        return false;
      }
    } else {
      payload_size = static_cast<std::uint32_t>(data.size() - offset);
    }
    if (obu_type == 1) {
      return true;
    }
    offset += std::min<std::size_t>(payload_size, data.size() - offset);
  }
  return false;
}

bool isKeyframe(CodecArg codec, const std::vector<std::uint8_t> &data) {
  switch (codec) {
  case CodecArg::H264:
  case CodecArg::H265:
    return isAnnexBKeyframe(codec, data);
  case CodecArg::VP8:
    return !data.empty() && ((data[0] & 0x01) == 0);
  case CodecArg::AV1:
    return isAv1Keyframe(data);
  }
  return false;
}

class Demuxer {
public:
  explicit Demuxer(CodecArg codec) : codec_(codec) {}

  void feed(const std::uint8_t *data, std::size_t size,
            std::vector<std::vector<std::uint8_t>> &frames) {
    buffer_.insert(buffer_.end(), data, data + size);
    if (codec_ == CodecArg::H264 || codec_ == CodecArg::H265) {
      feedAnnexB(frames);
    } else {
      feedIvf(frames);
    }
  }

private:
  void feedAnnexB(std::vector<std::vector<std::uint8_t>> &frames) {
    std::size_t search = 0;
    while (auto start = findStartCode(buffer_, search)) {
      const std::size_t nal_offset = start->first + start->second;
      if (nal_offset >= buffer_.size()) {
        break;
      }
      const bool aud = isAud(codec_, nalType(codec_, buffer_[nal_offset]));
      if (aud) {
        if (au_start_ && start->first > *au_start_) {
          frames.emplace_back(buffer_.begin() + static_cast<std::ptrdiff_t>(*au_start_),
                              buffer_.begin() + static_cast<std::ptrdiff_t>(start->first));
          buffer_.erase(buffer_.begin(),
                        buffer_.begin() + static_cast<std::ptrdiff_t>(start->first));
          au_start_ = 0;
          search = start->second + 1;
          continue;
        }
        au_start_ = start->first;
      } else if (!au_start_) {
        au_start_ = start->first;
      }
      search = nal_offset + 1;
    }
  }

  static std::uint32_t readLe32(const std::vector<std::uint8_t> &data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
  }

  void feedIvf(std::vector<std::vector<std::uint8_t>> &frames) {
    if (!ivf_header_checked_) {
      if (buffer_.size() >= 32 && std::memcmp(buffer_.data(), "DKIF", 4) == 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + 32);
      }
      ivf_header_checked_ = true;
    }

    while (buffer_.size() >= 12) {
      const std::uint32_t frame_size = readLe32(buffer_);
      if (frame_size == 0 || frame_size > 16 * 1024 * 1024) {
        throw std::runtime_error("invalid IVF frame size");
      }
      if (buffer_.size() < static_cast<std::size_t>(12 + frame_size)) {
        return;
      }
      frames.emplace_back(buffer_.begin() + 12,
                          buffer_.begin() + 12 + frame_size);
      buffer_.erase(buffer_.begin(), buffer_.begin() + 12 + frame_size);
    }
  }

  CodecArg codec_;
  std::vector<std::uint8_t> buffer_;
  std::optional<std::size_t> au_start_;
  bool ivf_header_checked_ = false;
};

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void closeSocket(SocketHandle socket) { close(socket); }
#endif

class NetworkRuntime {
public:
  NetworkRuntime() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
#endif
  }

  ~NetworkRuntime() {
#if defined(_WIN32)
    WSACleanup();
#endif
  }
};

class TcpSocket {
public:
  TcpSocket() = default;
  explicit TcpSocket(SocketHandle socket) : socket_(socket) {}
  ~TcpSocket() { reset(); }

  TcpSocket(const TcpSocket &) = delete;
  TcpSocket &operator=(const TcpSocket &) = delete;

  TcpSocket(TcpSocket &&other) noexcept : socket_(other.socket_) {
    other.socket_ = kInvalidSocket;
  }

  TcpSocket &operator=(TcpSocket &&other) noexcept {
    if (this != &other) {
      reset();
      socket_ = other.socket_;
      other.socket_ = kInvalidSocket;
    }
    return *this;
  }

  int read(std::uint8_t *buffer, int length) {
#if defined(_WIN32)
    return recv(socket_, reinterpret_cast<char *>(buffer), length, 0);
#else
    return static_cast<int>(recv(socket_, buffer, static_cast<std::size_t>(length), 0));
#endif
  }

  explicit operator bool() const noexcept { return socket_ != kInvalidSocket; }

private:
  void reset() {
    if (socket_ != kInvalidSocket) {
      closeSocket(socket_);
      socket_ = kInvalidSocket;
    }
  }

  SocketHandle socket_ = kInvalidSocket;
};

TcpSocket connectTcp(const std::string &host, std::uint16_t port) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  addrinfo *results = nullptr;
  const std::string service = std::to_string(port);
  const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
  if (rc != 0) {
    throw std::runtime_error("getaddrinfo failed for " + host);
  }

  for (addrinfo *addr = results; addr != nullptr; addr = addr->ai_next) {
    SocketHandle socket =
        ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (socket == kInvalidSocket) {
      continue;
    }
    if (::connect(socket, addr->ai_addr,
                  static_cast<int>(addr->ai_addrlen)) == 0) {
      freeaddrinfo(results);
      return TcpSocket(socket);
    }
    closeSocket(socket);
  }

  freeaddrinfo(results);
  throw std::runtime_error("connect failed for " + host + ":" + service);
}

class LoggingObserver : public EncodedVideoSourceObserver {
public:
  explicit LoggingObserver(std::atomic<std::uint64_t> &target_bitrate_bps)
      : target_bitrate_bps_(target_bitrate_bps) {}

  void onKeyframeRequested() override {
    std::cout << "[producer] keyframe requested by WebRTC\n";
  }

  void onTargetBitrate(std::uint32_t bitrate_bps,
                       double framerate_fps) override {
    target_bitrate_bps_.store(bitrate_bps, std::memory_order_relaxed);
    std::cout << "[producer] target bitrate " << (bitrate_bps / 1000)
              << " kbps @ " << framerate_fps << " fps\n";
  }

private:
  std::atomic<std::uint64_t> &target_bitrate_bps_;
};

} // namespace

int main(int argc, char *argv[]) {
  Args args;
  try {
    if (!parseArgs(argc, argv, args)) {
      printUsage(argv[0]);
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "[producer] argument error: " << error.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  try {
    NetworkRuntime network;
    livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

    Room room;
    RoomOptions room_options;
    room_options.auto_subscribe = false;
    room_options.dynacast = false;

    std::cout << "[producer] connecting to " << args.url << "\n";
    if (!room.Connect(args.url, args.token, room_options)) {
      std::cerr << "[producer] failed to connect\n";
      livekit::shutdown();
      return 1;
    }

    std::cout << "[producer] connected as "
              << room.localParticipant()->identity() << " to room '"
              << room.room_info().name << "'\n";

    auto source = std::make_shared<VideoSource>(
        static_cast<int>(args.width), static_cast<int>(args.height),
        EncodedVideoSourceOptions{toVideoCodec(args.codec)});
    std::atomic<std::uint64_t> target_bitrate_bps{0};
    source->setEncodedObserver(
        std::make_shared<LoggingObserver>(target_bitrate_bps));

    const std::string track_name =
        args.track_name.value_or(defaultTrackName(args.codec));
    auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;
    publish_options.video_codec = toVideoCodec(args.codec);
    publish_options.video_encoding =
        VideoEncodingOptions{args.max_bitrate_bps, args.max_framerate};
    room.localParticipant()->publishTrack(track, publish_options);

    std::cout << "[producer] published " << codecName(args.codec)
              << " track name=\"" << track_name << "\" at " << args.width
              << "x" << args.height << "\n";

    Stats total;
    Stats last;
    auto last_log = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> read_buffer(64 * 1024);
    std::vector<std::vector<std::uint8_t>> frames;

    while (g_running.load(std::memory_order_relaxed)) {
      const std::string endpoint =
          args.tcp_host + ":" + std::to_string(args.tcp_port);
      try {
        std::cout << "[producer] connecting to " << endpoint << " for "
                  << codecName(args.codec)
                  << ((args.codec == CodecArg::H264 ||
                       args.codec == CodecArg::H265)
                          ? " Annex-B"
                          : " IVF")
                  << " bytestream\n";
        TcpSocket socket = connectTcp(args.tcp_host, args.tcp_port);
        std::cout << "[producer] connected to " << endpoint << "\n";

        Demuxer demuxer(args.codec);
        while (g_running.load(std::memory_order_relaxed)) {
          const int n = socket.read(read_buffer.data(),
                                    static_cast<int>(read_buffer.size()));
          if (n <= 0) {
            std::cerr << "[producer] TCP stream closed\n";
            break;
          }

          frames.clear();
          demuxer.feed(read_buffer.data(), static_cast<std::size_t>(n),
                       frames);
          for (const auto &frame : frames) {
            const bool keyframe = isKeyframe(args.codec, frame);
            EncodedVideoFrameInfo info;
            info.is_keyframe = keyframe;
            info.has_sps_pps = false;
            info.width = args.width;
            info.height = args.height;
            info.capture_time_us = 0;

            total.bytes += frame.size();
            if (keyframe) {
              ++total.keyframes;
            }
            if (source->captureEncodedFrame(frame, info)) {
              ++total.accepted;
            } else {
              ++total.dropped;
            }
          }

          const auto now = std::chrono::steady_clock::now();
          const double elapsed =
              std::chrono::duration<double>(now - last_log).count();
          if (elapsed >= 2.0) {
            const std::uint64_t accepted = total.accepted - last.accepted;
            const std::uint64_t dropped = total.dropped - last.dropped;
            const std::uint64_t keyframes = total.keyframes - last.keyframes;
            const std::uint64_t bytes = total.bytes - last.bytes;
            if (accepted + dropped > 0) {
              const double kbps =
                  static_cast<double>(bytes) * 8.0 / elapsed / 1000.0;
              std::cout << "[producer] ingest: "
                        << (static_cast<double>(accepted) / elapsed)
                        << " fps accepted, "
                        << (static_cast<double>(dropped) / elapsed)
                        << " fps dropped, " << kbps
                        << " kbps encoded (target "
                        << target_bitrate_bps.load(std::memory_order_relaxed) /
                               1000
                        << " kbps), " << keyframes << " keyframes\n";
            }
            last = total;
            last_log = now;
          }
        }
      } catch (const std::exception &error) {
        std::cerr << "[producer] " << error.what() << "; retrying in 1s\n";
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    livekit::shutdown();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[producer] fatal error: " << error.what() << "\n";
    livekit::shutdown();
    return 1;
  }
}
