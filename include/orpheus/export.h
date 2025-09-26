#pragma once

#ifdef _WIN32
  #ifdef ORPHEUS_EXPORTS
    #define ORPHEUS_API __declspec(dllexport)
  #else
    #define ORPHEUS_API __declspec(dllimport)
  #endif
#else
  #define ORPHEUS_API
#endif
