#include "lockbox.hpp"
#include "orpheus.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <soundio/soundio.h>
#include <stdexcept>
#include <optional>
#include <string>
#include <iostream>
#include <utility>
#include <thread>
#include <vector>
#include "subprojects/orpheus/lib/Catch2/single_include/catch2/catch.hpp"
#include "synchronizedbuffer.hpp"
#include "udpsocket.hpp"

#define DEBUG

struct __attribute__((packed)) HandData_S
{
  float distance;
  float indexAngle;
  float middleAngle;
  float ringAngle;
  float pinkyAngle;
};
union HandData { HandData_S asStruct; std::byte asArr[sizeof(HandData_S)]; };

static Parallel::CopyLockbox<HandData> m_dataBox;

static Orpheus::Engine* s_engine;
static Orpheus::Graph::Node* s_outNode;
static Orpheus::Graph::Attenuator* s_outAtten;
static Orpheus::Graph::Attenuator* s_h1Atten;
static Orpheus::Graph::Attenuator* s_h2Atten;
static Orpheus::Graph::Attenuator* s_h3Atten;

auto OrpheusSampleToFloat(Orpheus::Engine::QuantType sample) -> float {
  return sample * (1 / powf(2, 15));
}

static void write_cb(struct SoundIoOutStream* str, int minCount, int maxCount) {
  size_t framesLeft = maxCount;

  while (framesLeft > 0) {
    int frameCount = framesLeft;

    struct SoundIoChannelArea* areas;
    const auto wrErr = soundio_outstream_begin_write(str, &areas, &frameCount);
    if (wrErr != 0) {
      throw std::runtime_error(std::string("Soundio Write Error ") +
          std::string(soundio_strerror(wrErr)));
    }

    if (frameCount == 0) {
      break;
    }

    for (int frame = 0; frame < frameCount; frame++) {
      const auto currentHandData = m_dataBox.Get();
      // Seed the Engine
      s_outAtten->setAtten((currentHandData.asStruct.distance / 200)
          * std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max());
      s_h1Atten->setAtten((currentHandData.asStruct.indexAngle / 360)
          * std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max());
      s_h2Atten->setAtten((currentHandData.asStruct.middleAngle / 360)
          * std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max());
      s_h3Atten->setAtten((currentHandData.asStruct.ringAngle / 360)
          * std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max());

      // Sample the engine
      const float sample = OrpheusSampleToFloat((*s_outNode)());
      for (int channel = 0; channel < str->layout.channel_count; channel++) {
        auto* ptr = (float*)(areas[channel].ptr + areas[channel].step * frame);
        *ptr = sample;
      }
      s_engine->tick();
    }

    const auto endErr = soundio_outstream_end_write(str);
    if (endErr != 0) {
      throw std::runtime_error(std::string("Soundio Write End Error ")
          + std::string(soundio_strerror(endErr)));
    }

    framesLeft -= frameCount;
  }
}

class Device {
public:
  using CopyCallback = void(struct SoundIoOutStream* stream, int fcMin, int fcMax);

  explicit Device(struct SoundIo* soundIo, const int devIdx, CopyCallback writeCallback):
      m_soundioDev(soundio_get_output_device(soundIo, devIdx)) {
    if (m_soundioDev == nullptr) {
      throw std::runtime_error("Could not allocate memory for the output device.");
    }

    m_soundioStream = std::unique_ptr<struct SoundIoOutStream>(
        soundio_outstream_create(m_soundioDev.get()));

    if (m_soundioStream == nullptr) {
      throw std::runtime_error("Could not allocate memory for the output stream.");
    }

    m_soundioStream->format = SoundIoFormatFloat32NE;
    m_soundioStream->write_callback = writeCallback;
  }

  explicit Device(struct SoundIo* soundIo, CopyCallback writeCallback):
    Device(soundIo, soundio_default_output_device_index(soundIo), writeCallback) {
#ifdef DEBUG
    std::cout << "Available Devices:" << std::endl;
    for (size_t i = 0; i < soundio_output_device_count(soundIo); i++) {
      const auto dev = soundio_get_output_device(soundIo, i);
      std::cout << "  - " << dev->id << std::endl;
    }
    std::cout << "Using " << m_soundioDev->id << std::endl;
#endif
  }

  ~Device() {
    soundio_outstream_destroy(m_soundioStream.get());
    soundio_device_unref(m_soundioDev.get());
  }

  Device(Device& other) =delete;
  Device(Device&& other) =default;

  auto operator=(Device&& other) noexcept -> Device& {
    if (&other == this) {
      return *this;
    }

    m_soundioDev = std::move(other.m_soundioDev);
    m_soundioStream = std::move(other.m_soundioStream);

    return *this;
  }

  void Begin() {
    auto err = soundio_outstream_open(m_soundioStream.get());
    if (err != 0) {
      throw std::runtime_error(std::string("Could not open the stream: ")
          + std::string(soundio_strerror(err)));
    }

    if (m_soundioStream->layout_error != 0) {
      throw std::runtime_error(std::string("Unable to set channel layout: ")
          + std::string(soundio_strerror(m_soundioStream->layout_error)));
    }

    err = soundio_outstream_start(m_soundioStream.get());
    if (err != 0) {
      throw std::runtime_error(std::string("Unable to start writing to the outstream: ")
          + std::string(soundio_strerror(err)));
    }
  }

private:
  std::unique_ptr<struct SoundIoDevice> m_soundioDev;
  std::unique_ptr<struct SoundIoOutStream> m_soundioStream;
};

class PlaybackDevice {
public:
  PlaybackDevice() {
    auto* const soundio = soundio_create();
    if (soundio == nullptr) {
      throw std::runtime_error("Could not allocate memory for sound device.");
      return;
    }
    m_soundio = std::unique_ptr<struct SoundIo>(soundio);

    const int err = soundio_connect(m_soundio.get());
    if (err != 0) {
      soundio_destroy(m_soundio.get());
      throw std::runtime_error(std::string("Could not connect: ")
          + std::string(soundio_strerror(err)));
    }

    soundio_flush_events(m_soundio.get());

    m_dev = std::make_unique<Device>(Device(m_soundio.get(), write_cb));
  }

  PlaybackDevice(PlaybackDevice& other) =delete;

  ~PlaybackDevice() {
    soundio_destroy(m_soundio.get());
  }

  void Begin() {
    m_dev->Begin();
    while (true) {
      soundio_wait_events(m_soundio.get());
    }
  }

private:
  std::unique_ptr<struct SoundIo> m_soundio;
  std::unique_ptr<Device> m_dev;
};

void ipserver() {
  Socc::UDPServ<> server {6000, [](auto begin, auto end) {
    const auto dataSize = std::distance(begin, end);
    if (dataSize != sizeof(HandData)) {
      std::cout << "Received corrupted data of length " << dataSize << ". Ignoring" << std::endl;
      return;
    }

    HandData deserialized = {};
    // TODO(markovejnovic): Yikes
    std::memcpy(&deserialized.asArr[0], begin, dataSize);

    m_dataBox.Set(deserialized);
  }};
  server.Begin();
}

auto main(int argc, char** argv) -> int {
  m_dataBox.Set({.asStruct = {
      .distance = 0.0,
      .indexAngle = 0.0,
      .middleAngle = 0.0,
      .ringAngle = 0.0,
      .pinkyAngle = 0.0,
  }});
  const auto m_serveThread = std::thread(ipserver);
  auto eng = Orpheus::EngineFactory().ofSampleRate(Orpheus::EngineFactory::SampleRate::kHz48).build();
  s_engine = &eng;

  Orpheus::Graph::SineSource m_base { &eng };
  m_base.setPeriodTicks(456);

  Orpheus::Graph::SineSource m_h1 { &eng };
  m_h1.setPeriodTicks(227);
  Orpheus::Graph::Attenuator m_h1a { &eng, m_h1 };
  m_h1a.setAtten(std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max() / 4);

  Orpheus::Graph::SineSource m_h2 { &eng };
  m_h2.setPeriodTicks(113);
  Orpheus::Graph::Attenuator m_h2a { &eng, m_h2 };
  m_h2a.setAtten(std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max() / 8);

  Orpheus::Graph::SineSource m_h3 { &eng };
  m_h3.setPeriodTicks(56);
  Orpheus::Graph::Attenuator m_h3a { &eng, m_h3 };
  m_h3a.setAtten(std::numeric_limits<Orpheus::Graph::Attenuator::AttenFactor>::max() / 16);

  Orpheus::Graph::Sum sum { &eng, m_base, m_h1a, m_h2a };

  Orpheus::Graph::Attenuator outAtten { &eng, sum };

  s_h1Atten = &m_h1a;
  s_h2Atten = &m_h2a;
  s_h3Atten = &m_h3a;
  s_outAtten = &outAtten;
  s_outNode = &outAtten;

  PlaybackDevice m_out;
  m_out.Begin();

  return 0;
}
