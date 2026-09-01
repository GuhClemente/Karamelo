#include "test_framework.h"
#include "updater.h"

TEST_CASE(UpdaterVersionComparison)
{
	// Remote is newer
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0", "1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0", "2.0"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0.0", "1.0.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("v1.0", "v1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0-alpha", "1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.9.9", "2.0.0"));

	// Remote is equal
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("v1.0", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0.0", "1.0"));

	// Remote is older
	ASSERT_FALSE(UpdaterIsNewerVersion("1.1", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("2.0", "1.9"));
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0.2", "1.0.1"));
}
