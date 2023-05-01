#ifndef LOCKBOX_HPP
#define LOCKBOX_HPP

#include <mutex>

namespace Parallel
{

template<typename T>
class CopyLockbox
{

public:
  void Set(T val) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_val = val;
  };

  T Get() {
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_val;
  }

private:
  T m_val;
  std::mutex m_mutex;
};

}

#endif // LOCKBOX_HPP
