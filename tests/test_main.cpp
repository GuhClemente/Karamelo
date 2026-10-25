#include "test_framework.h"

int MenuGetN64Core() { return 0; }
void CoreSetToast(const char* msg, int duration) { (void)msg; (void)duration; }
// The production Tr() lives in menu.cpp, which the test build does not link;
// messages are checked in Portuguese, the app's default language.
const char* Tr(const char* pt, const char* en) { (void)en; return pt; }

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	return TestRegistry::Instance().RunAll();
}
