#pragma once

// What SchemaReader needs from whatever runs it: the metamod plugin, or the headless dumper.
#ifdef SCHEMADUMP_HEADLESS
#include "headless.h"
#else
#include "plugin.h"
#endif
