#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdlib>

struct TestCase
{
	std::string name;
	std::function<void()> func;
	std::string file;
	int line;
};

class TestRegistry
{
public:
	static TestRegistry& Instance()
	{
		static TestRegistry reg;
		return reg;
	}

	void Register(const std::string& name, const std::string& file, int line, std::function<void()> func)
	{
		tests_.push_back({ name, func, file, line });
	}

	int RunAll()
	{
		int passed = 0;
		int failed = 0;

		std::cout << "\n==================================================\n";
		std::cout << "  Karamelo - Unit Test Suite Runner\n";
		std::cout << "==================================================\n\n";

		auto total_start = std::chrono::high_resolution_clock::now();

		for (const auto& t : tests_)
		{
			std::cout << "[ RUN      ] " << t.name << " (" << t.file << ":" << t.line << ")\n";
			current_test_failed_ = false;
			current_failure_msg_ = "";

			auto start = std::chrono::high_resolution_clock::now();
			try
			{
				t.func();
			}
			catch (const std::exception& e)
			{
				current_test_failed_ = true;
				current_failure_msg_ = std::string("Unhandled exception: ") + e.what();
			}
			catch (...)
			{
				current_test_failed_ = true;
				current_failure_msg_ = "Unhandled non-std exception";
			}
			auto end = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration<double, std::milli>(end - start).count();

			if (current_test_failed_)
			{
				std::cout << "[     FAIL ] " << t.name << " (" << ms << " ms)\n";
				if (!current_failure_msg_.empty())
				{
					std::cout << "             -> " << current_failure_msg_ << "\n";
				}
				failed++;
			}
			else
			{
				std::cout << "[     PASS ] " << t.name << " (" << ms << " ms)\n";
				passed++;
			}
		}

		auto total_end = std::chrono::high_resolution_clock::now();
		double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

		std::cout << "\n--------------------------------------------------\n";
		std::cout << "Test Results: " << passed << " PASSED, " << failed << " FAILED in " << total_ms << " ms\n";
		std::cout << "==================================================\n\n";

		return (failed == 0) ? 0 : 1;
	}

	void Fail(const std::string& msg, const char* file, int line)
	{
		current_test_failed_ = true;
		current_failure_msg_ = msg + " at " + file + ":" + std::to_string(line);
	}

	bool IsFailed() const { return current_test_failed_; }

private:
	std::vector<TestCase> tests_;
	bool current_test_failed_ = false;
	std::string current_failure_msg_;
};

struct AutoRegister
{
	AutoRegister(const std::string& name, const std::string& file, int line, std::function<void()> func)
	{
		TestRegistry::Instance().Register(name, file, line, func);
	}
};

#define TEST_CASE(test_name) \
	static void test_name(); \
	static AutoRegister auto_reg_##test_name(#test_name, __FILE__, __LINE__, test_name); \
	static void test_name()

#define ASSERT_TRUE(cond) \
	do { \
		if (!(cond)) { \
			TestRegistry::Instance().Fail("ASSERT_TRUE failed: (" #cond ") is false", __FILE__, __LINE__); \
			return; \
		} \
	} while (0)

#define ASSERT_FALSE(cond) \
	do { \
		if ((cond)) { \
			TestRegistry::Instance().Fail("ASSERT_FALSE failed: (" #cond ") is true", __FILE__, __LINE__); \
			return; \
		} \
	} while (0)

#define ASSERT_EQ(a, b) \
	do { \
		if ((a) != (b)) { \
			TestRegistry::Instance().Fail("ASSERT_EQ failed: " #a " != " #b, __FILE__, __LINE__); \
			return; \
		} \
	} while (0)

#define ASSERT_NE(a, b) \
	do { \
		if ((a) == (b)) { \
			TestRegistry::Instance().Fail("ASSERT_NE failed: " #a " == " #b, __FILE__, __LINE__); \
			return; \
		} \
	} while (0)

#define ASSERT_NEAR(a, b, eps) \
	do { \
		if (std::fabs((a) - (b)) > (eps)) { \
			TestRegistry::Instance().Fail("ASSERT_NEAR failed: |" #a " - " #b "| > " #eps, __FILE__, __LINE__); \
			return; \
		} \
	} while (0)

#define ASSERT_STR_EQ(a, b) \
	do { \
		if (std::string(a) != std::string(b)) { \
			TestRegistry::Instance().Fail("ASSERT_STR_EQ failed: \"" + std::string(a) + "\" != \"" + std::string(b) + "\"", __FILE__, __LINE__); \
			return; \
		} \
	} while (0)
