#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(ORPHEUS_BUILD_SHARED)
    #if defined(ORPHEUS_BUILDING_SHARED)
      #define ORPHEUS_API __declspec(dllexport)
    #else
      #define ORPHEUS_API __declspec(dllimport)
    #endif
  #else
    #define ORPHEUS_API
  #endif
#else
  #if defined(ORPHEUS_BUILD_SHARED)
    #define ORPHEUS_API __attribute__((visibility("default")))
  #else
    #define ORPHEUS_API
  #endif
#endif
