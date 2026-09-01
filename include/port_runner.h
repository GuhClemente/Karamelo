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

#endif
