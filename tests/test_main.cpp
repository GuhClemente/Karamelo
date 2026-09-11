#include "test_framework.h"

int MenuGetN64Core() { return 0; }
void CoreSetToast(const char* msg, int duration) { (void)msg; (void)duration; }

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	return TestRegistry::Instance().RunAll();
}
