#pragma once

// Stand-ins for the metamod globals SchemaReader uses, so the same reader runs in the
// headless dumper (src/headless.cpp) without a game server or metamod.

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include "version_gen.h"

struct HeadlessHost
{
	// Game folder that holds steam.inf (game/csgo)
	const char *m_pszBaseDir = ".";

	const char *GetBaseDir() const { return m_pszBaseDir; }

	void ConPrintf( const char *fmt, ... ) const
	{
		va_list args;
		va_start( args, fmt );
		vprintf( fmt, args );
		va_end( args );
	}

	const char *GetVersion() const { return PLUGIN_FULL_VERSION; }
};

extern HeadlessHost g_HeadlessHost;

// Metadata getters run game code, which can reach systems that only exist in a running game
// (the resource system, for one). RunIsolated runs fn in a forked child, so a crash there
// can't take the dumper down or leave a lock held, and returns nullopt if the child failed.
std::optional<std::string> RunIsolated( const std::function<std::string()> &fn );

#define g_SMAPI (&g_HeadlessHost)
#define META_CONPRINTF g_SMAPI->ConPrintf
#define g_ThisPlugin g_HeadlessHost
