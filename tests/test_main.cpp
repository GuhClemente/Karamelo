#include "test_framework.h"
#include "netplay.h"

int MenuGetN64Core() { return 0; }
void CoreSetToast(const char* msg, int duration) { (void)msg; (void)duration; }
// The production Tr() lives in menu.cpp, which the test build does not link;
// messages are checked in Portuguese, the app's default language.
const char* Tr(const char* pt, const char* en) { (void)en; return pt; }
bool CoreIsRunning() { return false; }
const char* CoreGetGameName() { return "Street Fighter III: 3rd Strike"; }
const char* NetplayGetLocalIp() { return "127.0.0.1"; }
const char* NetplayGetPeerIp() { return ""; }
std::vector<NetplayLocalInterface> NetplayGetLocalInterfaces() { return {}; }
bool NetplayStartHost(int port) { (void)port; return true; }
bool NetplayStartClient(const char* ip, int port) { (void)ip; (void)port; return true; }
NetplayRole NetplayGetRole() { return NETPLAY_NONE; }
NetplayState NetplayGetState() { return NETPLAY_DISCONNECTED; }
void MenuRequestQuit() {}
void MenuSaveSettingsExport() {}

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	return TestRegistry::Instance().RunAll();
}
