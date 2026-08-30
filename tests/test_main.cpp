#include "test_framework.h"

int MenuGetN64Core() { return 0; }

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	return TestRegistry::Instance().RunAll();
}
