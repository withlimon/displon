# llvm-mingw (UCRT, x86_64) toolchain file.
# Usage:
#   cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-toolchain.cmake
# Either put llvm-mingw's bin/ on PATH, or set LLVM_MINGW_ROOT (env or -D).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW_ROOT AND DEFINED ENV{LLVM_MINGW_ROOT})
  set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()

set(FT_TRIPLE x86_64-w64-mingw32)
if(LLVM_MINGW_ROOT)
  set(FT_BIN "${LLVM_MINGW_ROOT}/bin/")
else()
  set(FT_BIN "")
endif()

set(CMAKE_C_COMPILER   "${FT_BIN}${FT_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${FT_BIN}${FT_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER  "${FT_BIN}${FT_TRIPLE}-windres")
set(CMAKE_AR           "${FT_BIN}llvm-ar")
set(CMAKE_RANLIB       "${FT_BIN}llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
