#include "lockbox.hpp"
#include "orpheus.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <math.h>
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
#include "orpheus.hpp"
#include "udpsocket.hpp"

#define DEBUG

static float secondsOffset = 0.0;
static Orpheus::Engine* s_engine;
static Orpheus::Graph::Node* s_outNode;

float OrpheusSampleToFloat(Orpheus::Engine::QuantType sample) {
  return static_cast<float>(sample) * (1 / pow(2, 15));
}

static void write_cb(struct SoundIoOutStream* str, int minCount, int maxCount) {
  size_t framesLeft = maxCount;
  float secondsPerFame = 1.0F / str->sample_rate;

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
      const float sample = OrpheusSampleToFloat((*s_outNode)());
      for (int channel = 0; channel < str->layout.channel_count; channel++) {
        float* ptr = (float*)(areas[channel].ptr + areas[channel].step * frame);
        *ptr = sample;
      }
      s_engine->tick();
    }

    secondsOffset = fmodf(secondsOffset + secondsPerFame * frameCount, 1.0F);

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

void populateRunnable(SynchronizedBuffer<float, true>& buf) {
  while (true) {
    std::vector<float> test = { 1, 2, 3, 4, 5 };
    buf.Write(test.cbegin(), test.cend());
  }
}

struct __attribute__((packed)) HandData_S
{
  float distance;
};
union HandData { HandData_S asStruct; std::byte asArr[sizeof(HandData_S)]; };

static Parallel::CopyLockbox<HandData> m_dataBox;

void ipserver() {
  Socc::UDPServ<> server {6000, [](auto begin, auto end) {
    m_dataBox.Set({ .asStruct = { .distance = 0.3F } });
  }};
  server.Begin();
}

auto main(int argc, char** argv) -> int {
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

  Orpheus::Graph::Sum out_source { &eng, m_base, m_h1a, m_h2a };
  s_outNode = &out_source;

  PlaybackDevice m_out;
  m_out.Begin();

  return 0;
}
