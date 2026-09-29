#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include "Plugin.h"

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface *a_skse) {
  SKSE::Init(a_skse);
  return true;
}