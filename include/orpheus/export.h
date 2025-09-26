#pragma once

#ifdef _WIN32
  #ifdef ORPHEUS_EXPORTS
    #define ORPHEUS_API __declspec(dllexport)
  #else
    #define ORPHEUS_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__)
  #ifdef ORPHEUS_EXPORTS
    #define ORPHEUS_API __attribute__((visibility("default")))
  #else
    #define ORPHEUS_API
  #endif
#else
  #define ORPHEUS_API
#endif
