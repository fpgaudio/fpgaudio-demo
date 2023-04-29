#include "orpheus.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <soundio/soundio.h>
#include <stdexcept>
#include <optional>
#include <string>
#include <iostream>
#include <utility>

static void write_cb(struct SoundIoOutStream* str, int minCount, int maxCount) {
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
        soundio_outstream_create(m_soundioDev.get())
    );

    if (m_soundioStream == nullptr) {
      throw std::runtime_error("Could not allocate memory for the output stream.");
    }

    m_soundioStream->format = SoundIoFormatU16LE;
    m_soundioStream->write_callback = writeCallback;
  }

  explicit Device(struct SoundIo* soundIo, CopyCallback writeCallback):
    Device(soundIo, soundio_default_output_device_index(soundIo), writeCallback) {
  }

  ~Device() {
    std::cout << "~Device()" << std::endl;
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
    std::cout << "PlaybackDevice()" << std::endl;
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

    std::cout << "Moving Device" << std::endl;
    m_dev = std::make_unique<Device>(Device(m_soundio.get(), write_cb));
    std::cout << "Moved" << std::endl;
  }

  PlaybackDevice(PlaybackDevice& other) =delete;

  ~PlaybackDevice() {
    soundio_destroy(m_soundio.get());
  }

  void Begin() {
    std::cout << "Begin()" << std::endl;
    m_dev->Begin();
    while (true) {
      soundio_wait_events(m_soundio.get());
    }
  }

private:
  std::unique_ptr<struct SoundIo> m_soundio;
  std::unique_ptr<Device> m_dev;
};

auto main(int argc, char** argv) -> int {
  PlaybackDevice m_device;
  m_device.Begin();

  std::cout << "Main Done" << std::endl;

  return 0;
}
