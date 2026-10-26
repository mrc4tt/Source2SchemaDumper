// Headless schema dumper: dumps the schema straight from the CS2 server binaries,
// without running a game server.
//
// Loads tier0 + schemasystem from a CS2 install, lets every module register its schema
// bindings (InstallSchemaBindings, what the engine does on module load), then runs the
// same SchemaReader as the dump_schema command.
//
// Usage: schemadump_headless <cs2 dir> <out dir> [dump flags, default "all"]
// <cs2 dir> holds game/ (a server install or a depot download), and the binary has to be
// started with LD_LIBRARY_PATH=<cs2 dir>/game/bin/linuxsteamrt64 so libtier0.so resolves.

#include "headless.h"
#include "schemareader.h"

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

HeadlessHost g_HeadlessHost;

std::optional<std::string> RunIsolated( const std::function<std::string()> &fn )
{
	int fds[2];
	if(pipe( fds ) != 0)
		return std::nullopt;

	fflush( stdout );

	pid_t pid = fork();
	if(pid < 0)
	{
		close( fds[0] );
		close( fds[1] );
		return std::nullopt;
	}

	if(pid == 0)
	{
		close( fds[0] );

		// A getter stuck on something that never comes up gets killed
		alarm( 30 );

		std::string result = fn();
		for(size_t written = 0; written < result.size();)
		{
			ssize_t n = write( fds[1], result.data() + written, result.size() - written );
			if(n <= 0)
				_exit( 1 );
			written += n;
		}

		_exit( 0 );
	}

	close( fds[1] );

	std::string result;
	char buf[4096];
	ssize_t n;
	while((n = read( fds[0], buf, sizeof( buf ) )) > 0)
		result.append( buf, n );

	close( fds[0] );

	int status;
	if(waitpid( pid, &status, 0 ) != pid || !WIFEXITED( status ) || WEXITSTATUS( status ) != 0)
		return std::nullopt;

	return result;
}

// Client-only modules a dedicated server never loads; their types don't belong in a server dump.
static const char *s_ClientOnlyModules[] = {
	"libpanorama.so",
	"libpanorama_text_pango.so",
	"libpanoramauiclient.so",
	"librendersystemvulkan.so",
	"libsteamaudio.so",
};

typedef bool (*InstallSchemaBindingsFn)( const char *interface_name, void *schema_system );

static std::vector<std::filesystem::path> ListModules( const std::filesystem::path &dir )
{
	std::vector<std::filesystem::path> modules;

	std::error_code ec;
	for(auto &entry : std::filesystem::directory_iterator( dir, ec ))
	{
		if(entry.path().extension() != ".so")
			continue;

		auto name = entry.path().filename();
		if(std::find( std::begin( s_ClientOnlyModules ), std::end( s_ClientOnlyModules ), name ) != std::end( s_ClientOnlyModules ))
			continue;

		modules.push_back( entry.path() );
	}

	std::sort( modules.begin(), modules.end() );
	return modules;
}

int main( int argc, char **argv )
{
	// Keep the log in order with the game modules' own output
	setvbuf( stdout, nullptr, _IOLBF, 0 );

	if(argc < 3)
	{
		printf( "Usage: %s <cs2 dir> <out dir> [dump flags, default \"all\"]\n", argv[0] );
		return 1;
	}

	auto game_dir = std::filesystem::absolute( argv[1] ) / "game";
	auto out_dir = std::filesystem::absolute( argv[2] );

	std::string flags = "all";
	if(argc > 3)
	{
		flags.clear();
		for(int i = 3; i < argc; i++)
			flags += std::string( argv[i] ) + " ";
	}

	auto engine_bin = game_dir / "bin" / "linuxsteamrt64";
	auto game_bin = game_dir / "csgo" / "bin" / "linuxsteamrt64";

	std::string base_dir = (game_dir / "csgo").string();
	g_HeadlessHost.m_pszBaseDir = base_dir.c_str();

	void *schema_module = dlopen( (engine_bin / "libschemasystem.so").c_str(), RTLD_NOW );
	if(!schema_module)
	{
		printf( "Failed to load libschemasystem.so: %s\n", dlerror() );
		return 1;
	}

	auto factory = (CreateInterfaceFn)dlsym( schema_module, "CreateInterface" );
	g_pSchemaSystem = factory ? (ISchemaSystem *)factory( SCHEMASYSTEM_INTERFACE_VERSION, nullptr ) : nullptr;
	if(!g_pSchemaSystem)
	{
		printf( "Failed to get %s\n", SCHEMASYSTEM_INTERFACE_VERSION );
		return 1;
	}

	if(!g_pSchemaSystem->Connect( factory ) || g_pSchemaSystem->Init() != INIT_OK)
	{
		printf( "Failed to initialize the schema system\n" );
		return 1;
	}

	int installed = 0;
	for(auto &dir : { engine_bin, game_bin })
	{
		for(auto &path : ListModules( dir ))
		{
			printf( "Loading %s\n", path.filename().c_str() );
			void *module = dlopen( path.c_str(), RTLD_LAZY | RTLD_GLOBAL );
			if(!module)
			{
				printf( "Skipping %s: %s\n", path.filename().c_str(), dlerror() );
				continue;
			}

			auto install = (InstallSchemaBindingsFn)dlsym( module, "InstallSchemaBindings" );

			// dlsym also searches the module's dependencies, only take the module's own export
			Dl_info info;
			if(!install || !dladdr( (void *)install, &info ) || std::filesystem::path( info.dli_fname ).filename() != path.filename())
				continue;

			if(!install( SCHEMASYSTEM_INTERFACE_VERSION, g_pSchemaSystem ))
			{
				printf( "InstallSchemaBindings failed for %s\n", path.filename().c_str() );
				return 1;
			}

			installed++;
		}
	}

	printf( "Installed schema bindings of %d modules\n", installed );

	if(!g_pSchemaSystem->SchemaSystemIsReady())
	{
		printf( "Schema system is not ready\n" );
		return 1;
	}

	SchemaReader sr;
	sr.SetOutDir( out_dir / "" );
	sr.ReadSchema( SchemaReader::ParseDumpFlags( flags.c_str() ) );

	return sr.WriteToOutDir() ? 0 : 1;
}
