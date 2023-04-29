#ifndef SYNCHRONIZED_BUFFER_HPP
#define SYNCHRONIZED_BUFFER_HPP

#include <algorithm>
#include <deque>
#include <mutex>
#include <iostream>
#include <array>

/**
 * This buffer represents a self-filling buffer.
 */
template<typename T, size_t MetricsWindowSize = 10, bool Debug = false>
class SynchronizedBuffer {
private:
  std::deque<T> m_data;
  std::mutex m_mutex;

  /**
   * Contains the historical data of the buffer sizes.
   */
  std::array<size_t, MetricsWindowSize> m_metricsBufferSize;

  void PerformMetrics() {
    // First, shift the rolling average window.
    std::copy(m_metricsBufferSize.cbegin(), m_metricsBufferSize.cend() - 1,
              m_metricsBufferSize.begin());
    m_metricsBufferSize[m_metricsBufferSize.size() - 1] = m_data.size();

    if (Debug) {
      std::cout << "SynchronizedBuffer: " << "Current Size: " << m_data.size() << std::endl;
    }
  }

public:
  template<typename It>
  void Read(It into, size_t count) {
    std::lock_guard<std::mutex> lock { m_mutex };
    std::copy(m_data.begin(), m_data.begin() + count, into);
    m_data.erase(m_data.begin(), m_data.begin() + count);
    PerformMetrics();
  }

  template<typename It>
  void Write(const It begin, const It end) {
    std::lock_guard<std::mutex> lock { m_mutex };
    m_data.insert(m_data.end(), begin, end);
    PerformMetrics();
  }
};

#endif
