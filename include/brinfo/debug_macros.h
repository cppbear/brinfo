#pragma once

#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace std;
using namespace llvm;

#ifdef DEBUG
#define DEBUG_PRINT(...)                                                       \
  do {                                                                         \
    string Str;                                                                \
    raw_string_ostream OS(Str);                                                \
    OS << "DEBUG: " << __FILE__ << ":" << __LINE__ << " - " << __VA_ARGS__;    \
    outs() << Str << "\n";                                                     \
  } while (0)
#else
#define DEBUG_PRINT(...)
#endif

#ifdef DEBUG
#define DEBUG_EXEC(...)                                                        \
  do {                                                                         \
    __VA_ARGS__;                                                               \
  } while (0)
#else
#define DEBUG_EXEC(...)
#endif
