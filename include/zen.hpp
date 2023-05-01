#ifndef ZEN_HPP
#define ZEN_HPP

#include <cstdio>

namespace Zen {

  /**
   * Dumps a structure to stdout.
   *
   * @warning This function is a nop on all compilers but clang.
   */
  template<typename T> auto dump_struct(T& strct) -> void {
#if defined(__clang__)
    __builtin_dump_struct(&strct, printf);
#else
#warning "Zen::dump_struct is a nop on this compiler."
#endif
  }

}

#endif // ZEN_HPP
