#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include "atmos_engine.h"

#include "../../sdk/reaper_plugin.h"

namespace {
bool g_api_registered = false;
int (*g_register_fn)(const char*, void*) = nullptr;
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec)
{
  if (rec)
  {
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->Register)
      return 0;

    g_register_fn = rec->Register;
    if (!g_register_fn)
      return 0;

    AtmosEngine_Initialize();

    if (!g_register_fn("API_Atmos_AssignTrackObject",
                       (void*)Atmos_AssignTrackObject))
    {
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn("API_Atmos_GetTrackObject",
                       (void*)Atmos_GetTrackObject))
    {
      g_register_fn("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn("API_Atmos_GetSpeakerFormat",
                       (void*)Atmos_GetSpeakerFormat))
    {
      g_register_fn("-API_Atmos_GetTrackObject",
                    (void*)Atmos_GetTrackObject);
      g_register_fn("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn("API_Atmos_GetSpeakerFormatCount",
                       (void*)Atmos_GetSpeakerFormatCount))
    {
      g_register_fn("-API_Atmos_GetSpeakerFormat",
                    (void*)Atmos_GetSpeakerFormat);
      g_register_fn("-API_Atmos_GetTrackObject",
                    (void*)Atmos_GetTrackObject);
      g_register_fn("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn("API_Atmos_ExportADM", (void*)Atmos_ExportADM))
    {
      g_register_fn("-API_Atmos_GetSpeakerFormatCount",
                    (void*)Atmos_GetSpeakerFormatCount);
      g_register_fn("-API_Atmos_GetSpeakerFormat",
                    (void*)Atmos_GetSpeakerFormat);
      g_register_fn("-API_Atmos_GetTrackObject",
                    (void*)Atmos_GetTrackObject);
      g_register_fn("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    if (!g_register_fn("API_Atmos_ExportBWF", (void*)Atmos_ExportBWF))
    {
      g_register_fn("-API_Atmos_ExportADM", (void*)Atmos_ExportADM);
      g_register_fn("-API_Atmos_GetSpeakerFormatCount",
                    (void*)Atmos_GetSpeakerFormatCount);
      g_register_fn("-API_Atmos_GetSpeakerFormat",
                    (void*)Atmos_GetSpeakerFormat);
      g_register_fn("-API_Atmos_GetTrackObject",
                    (void*)Atmos_GetTrackObject);
      g_register_fn("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      g_register_fn = nullptr;
      return 0;
    }

    g_api_registered = true;
    return 1;
  }

  if (g_api_registered && g_register_fn)
  {
    g_register_fn("-API_Atmos_ExportBWF", (void*)Atmos_ExportBWF);
    g_register_fn("-API_Atmos_ExportADM", (void*)Atmos_ExportADM);
    g_register_fn("-API_Atmos_GetSpeakerFormatCount",
                  (void*)Atmos_GetSpeakerFormatCount);
    g_register_fn("-API_Atmos_GetSpeakerFormat",
                  (void*)Atmos_GetSpeakerFormat);
    g_register_fn("-API_Atmos_GetTrackObject",
                  (void*)Atmos_GetTrackObject);
    g_register_fn("-API_Atmos_AssignTrackObject",
                  (void*)Atmos_AssignTrackObject);
  }

  AtmosEngine_Shutdown();
  g_api_registered = false;
  g_register_fn = nullptr;
  return 0;
}
