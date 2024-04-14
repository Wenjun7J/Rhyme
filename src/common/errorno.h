#ifndef ERRRO_H
#define ERRRO_H

namespace ryhme {
namespace common {
  constexpr int R_SUCCESS = 0;
  constexpr int R_ERROR = -4096;
  constexpr int R_SIZE_OVERFLOW = -4097;
  constexpr int R_EAGAIN = -4098;
  constexpr int R_ALLOC_FAIL = -4098;



  #define _EXPECTED(x)  __builtin_expect(!!(x),!!1)
  #define _UNEXPECTED(x)  __builtin_expect(!!(x),!!0)
  #define E_SUCC(x) (_EXPECTED((x) == R_SUCCESS))
  #define UE_SUCC(x) (_UNEXPECTED((x) == R_SUCCESS))
} //end common
}

#endif
