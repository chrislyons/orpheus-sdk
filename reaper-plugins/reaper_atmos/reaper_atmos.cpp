#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include "atmos_engine.h"

#include "../../sdk/reaper_plugin.h"
#include "../../sdk/reaper_plugin_functions.h"

namespace {
bool g_api_registered = false;
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec)
{
  if (rec)
  {
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
      return 0;
    if (!REAPERAPI_LoadAPI(rec->GetFunc))
      return 0;
    if (!plugin_register)
      return 0;

    AtmosEngine_Initialize();

    if (!plugin_register("API_Atmos_AssignTrackObject",
                         (void*)Atmos_AssignTrackObject))
    {
      AtmosEngine_Shutdown();
      return 0;
    }

    if (!plugin_register("API_Atmos_GetTrackObject",
                         (void*)Atmos_GetTrackObject))
    {
      plugin_register("-API_Atmos_AssignTrackObject",
                      (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      return 0;
    }

    if (!plugin_register("API_Atmos_GetSpeakerFormat",
                         (void*)Atmos_GetSpeakerFormat))
    {
      plugin_register("-API_Atmos_GetTrackObject",
                      (void*)Atmos_GetTrackObject);
      plugin_register("-API_Atmos_AssignTrackObject",
                      (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      return 0;
    }

    if (!plugin_register("API_Atmos_GetSpeakerFormatCount",
                         (void*)Atmos_GetSpeakerFormatCount))
    {
      plugin_register("-API_Atmos_GetSpeakerFormat",
                      (void*)Atmos_GetSpeakerFormat);
      plugin_register("-API_Atmos_GetTrackObject",
                      (void*)Atmos_GetTrackObject);
      plugin_register("-API_Atmos_AssignTrackObject",
                      (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      return 0;
    }

    if (!plugin_register("API_Atmos_ExportADM", (void*)Atmos_ExportADM))
    {
      plugin_register("-API_Atmos_GetSpeakerFormatCount",
                      (void*)Atmos_GetSpeakerFormatCount);
      plugin_register("-API_Atmos_GetSpeakerFormat",
                      (void*)Atmos_GetSpeakerFormat);
      plugin_register("-API_Atmos_GetTrackObject",
                      (void*)Atmos_GetTrackObject);
      plugin_register("-API_Atmos_AssignTrackObject",
                      (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      return 0;
    }

    if (!plugin_register("API_Atmos_ExportBWF", (void*)Atmos_ExportBWF))
    {
      plugin_register("-API_Atmos_ExportADM", (void*)Atmos_ExportADM);
      plugin_register("-API_Atmos_GetSpeakerFormatCount",
                      (void*)Atmos_GetSpeakerFormatCount);
      plugin_register("-API_Atmos_GetSpeakerFormat",
                      (void*)Atmos_GetSpeakerFormat);
      plugin_register("-API_Atmos_GetTrackObject",
                      (void*)Atmos_GetTrackObject);
      plugin_register("-API_Atmos_AssignTrackObject",
                      (void*)Atmos_AssignTrackObject);
      AtmosEngine_Shutdown();
      return 0;
    }

    g_api_registered = true;
    return 1;
  }

  if (g_api_registered && plugin_register)
  {
    plugin_register("-API_Atmos_ExportBWF", (void*)Atmos_ExportBWF);
    plugin_register("-API_Atmos_ExportADM", (void*)Atmos_ExportADM);
    plugin_register("-API_Atmos_GetSpeakerFormatCount",
                    (void*)Atmos_GetSpeakerFormatCount);
    plugin_register("-API_Atmos_GetSpeakerFormat",
                    (void*)Atmos_GetSpeakerFormat);
    plugin_register("-API_Atmos_GetTrackObject",
                    (void*)Atmos_GetTrackObject);
    plugin_register("-API_Atmos_AssignTrackObject",
                    (void*)Atmos_AssignTrackObject);
  }

  AtmosEngine_Shutdown();
  g_api_registered = false;
  return 0;
}
