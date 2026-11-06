//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Rtt_FileSystem.h"
#include "Rtt_LinuxSimulator.h"
#include "Rtt_LinuxUtils.h"
#include "Rtt_LinuxCEF.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

smart_ptr<Rtt::SolarApp> app;

// IME under GNOME (and other) Wayland desktops. When XMODIFIERS asks for ibus, SDL2 turns XIM off and
// talks to ibus over D-Bus, finding its address in ~/.config/ibus/bus/<machine-id>-<host>-<display>,
// named after DISPLAY (":0" -> "<id>-unix-0"). Under Wayland, ibus writes only "<id>-unix-<WAYLAND_DISPLAY>"
// ("<id>-unix-wayland-0"), so SDL apps on XWayland got no IME at all (and SDL2's IBUS_ADDRESS override is
// read as a file name). When only the Wayland file exists, link the name SDL looks for to it; ibus rewrites
// that file in place when it restarts, so the link stays right. Must run before SDL initializes video.
static void LinkIBusAddressForXWayland()
{
	const char* xmods = getenv("XMODIFIERS");
	const char* imModule = getenv("SDL_IM_MODULE");
	const char* display = getenv("DISPLAY");
	const char* wayland = getenv("WAYLAND_DISPLAY");
	if (!xmods || !strstr(xmods, "@im=ibus") || (imModule && strcmp(imModule, "ibus") != 0) || getenv("IBUS_ADDRESS"))
		return;
	if (!display || !*display || !wayland || !*wayland || strchr(wayland, '/'))
		return;

	string machineId;
	for (const char* f : { "/var/lib/dbus/machine-id", "/etc/machine-id" })   // dbus_get_local_machine_id's order
	{
		if (FILE* fp = fopen(f, "r"))
		{
			char buf[64] = {};
			if (fgets(buf, sizeof(buf), fp)) machineId = buf;
			fclose(fp);
		}
		while (!machineId.empty() && (machineId.back() == '\n' || machineId.back() == '\r')) machineId.pop_back();
		if (!machineId.empty()) break;
	}
	if (machineId.empty() || machineId.find('/') != string::npos)
		return;

	// As SDL_ibus.c: "host:number.screen" -> host ("unix" when empty) and number.
	string disp = display;
	const size_t colon = disp.rfind(':');
	if (colon == string::npos)
		return;
	string host = disp.substr(0, colon);
	string number = disp.substr(colon + 1);
	number = number.substr(0, number.find('.'));
	if (host.empty()) host = "unix";
	if (number.empty() || host.find('/') != string::npos || number.find('/') != string::npos)
		return;

	const char* xdg = getenv("XDG_CONFIG_HOME");
	const char* home = getenv("HOME");
	string dir = (xdg && *xdg) ? string(xdg) : (home && *home) ? string(home) + "/.config" : string();
	if (dir.empty())
		return;
	dir += "/ibus/bus/";
	const string sdlName = machineId + "-" + host + "-" + number;
	const string waylandName = machineId + "-unix-" + wayland;
	struct stat st;
	if (lstat((dir + sdlName).c_str(), &st) == 0)   // a real X session's file, or our link: leave it
		return;
	if (stat((dir + waylandName).c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return;
	if (symlink(waylandName.c_str(), (dir + sdlName).c_str()) != 0)
		Rtt_LogException("IME: could not link %s to %s (%s)\n", sdlName.c_str(), waylandName.c_str(), strerror(errno));
}

int main(int argc, char* argv[])
{
	LinkIBusAddressForXWayland();
	Rtt::InitCEF(argc, argv);

	string resourcesDir = GetStartupPath(NULL);
	resourcesDir.append("/Resources");

	// --shell <project>: run the project like the Windows Corona Shell -- the app window only (no
	// Simulator menu, Console window or relaunch-on-file-change), print() on stdout, plugins and
	// sandbox as in the Simulator. Used by editors that run a project in their own child process.
	bool shellMode = false;
	string arg;
	bool isNotCommandOption = false;
	for (int i = 1; i < argc; ++i)
	{
		arg = argv[i];
		if (arg == "--shell")
		{
			shellMode = true;
			continue;
		}
		if (arg.compare(0, 1, "-") != 0)
		{
			arg = argv[i];
			isNotCommandOption = true;
			break;
		}
	}

	if (shellMode && isNotCommandOption && Rtt_FileExists(arg.c_str()))
	{
		// stdout is usually a pipe read by the editor: flush each line, not each 4 KB block.
		setvbuf(stdout, NULL, _IOLBF, 0);
		string projectDir = std::filesystem::is_regular_file(arg) ? std::filesystem::path(arg).parent_path().string() : arg;
		app = new Rtt::SolarApp(std::filesystem::absolute(projectDir));
	}
	else if (argc > 1 && isNotCommandOption && Rtt_FileExists(arg.c_str()))
	{
		if (std::filesystem::is_regular_file(arg))
		{
			string fileParent = std::filesystem::path(arg).parent_path().string();
			app = new Rtt::SolarSimulator(std::filesystem::absolute(fileParent));
		}
		else
		{
			app = new Rtt::SolarSimulator(std::filesystem::absolute(arg));
		}
	}

	// look for welcomescereen
	else if (Rtt_FileExists((resourcesDir + "/homescreen/main.lua").c_str()))
	{
		resourcesDir.append("/homescreen");
		app = new Rtt::SolarSimulator(resourcesDir);
	}
	else if (Rtt_IsDirectory(resourcesDir.c_str()))
	{
		app = new Rtt::SolarApp(resourcesDir);
	}
	else
	{
		return -1;
	}

	if (app->Init())
	{
		app->Run();
	}

	Rtt::FinalizeCEF();
	app = NULL;

	return 0;
}

