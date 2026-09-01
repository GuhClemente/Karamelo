#ifndef PORT_RUNNER_H_INCLUDED
#define PORT_RUNNER_H_INCLUDED

#include <string>
#include <vector>

struct PortGameInfo {
    std::string id;
    std::string name;
    std::string exe_path;
    std::string working_dir;
    std::string rom_pattern;
    bool is_installed;
};

// Initializes ports subsystem
void PortInit();

// Scans available ports in ports/ directory
std::vector<PortGameInfo> PortGetAvailableList();

// Checks if Dr Mario 64 or other ports are installed
bool PortIsInstalled(const std::string& port_id);

// Searches roms/ and copies matching ROM to port directory if needed
bool PortAutoSetupRom(const std::string& port_id);

// Launches the port executable, minimizes/hides MiSTer, and restores on exit
bool PortLaunch(const std::string& port_id);

// Checks if an external port process is currently active
bool PortIsRunning();

// Must be called once per frame from the UI thread (the main message loop).
// Performs the actual launch of a port whose background download just
// finished - PortLaunch()'s install thread only signals that a launch is
// pending, it never performs one itself, so CoreShutdown()/window/process
// calls stay on the UI thread the rest of the app already assumes owns them.
void PortPumpPendingLaunch();

// Downloads and extracts a known port without launching it. Used by the
// --install-port headless command-line mode so ports/ can be pre-populated
// without going through the menu UI. Returns true immediately if the port
// is already installed. Sets out_error on failure.
bool PortInstallOnly(const std::string& port_id, std::string& out_error);

#endif
