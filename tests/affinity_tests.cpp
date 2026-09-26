// CPU affinity around Direct3DCreate9. DS2 pins its process to CPU 0, and the
// RTX Remix bridge's Direct3DCreate9 starts its server, which inherits that
// affinity; the server's busy-waiting threads then starve the game. With
// remix_server_all_cpus = 1 QindieGL runs Direct3DCreate9 with every CPU and
// then restores the game's pin. The child pins itself before the context is
// created; the parent checks what QindieGL.log reports.

#include <windows.h>
#include <stdio.h>
#include <string>

#include "tests.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {
	DWORD_PTR SystemAffinity()
	{
		DWORD_PTR process = 0, system = 0;
		GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
		return system;
	}

	// The lowest CPU, as DS2's SetProcessAffinityMask(GetCurrentProcess(), 1).
	DWORD_PTR PinnedAffinity()
	{
		const DWORD_PTR system = SystemAffinity();
		return system & (~system + 1);
	}

	std::string ReadFile( const std::string &path )
	{
		std::string text;
		FILE *file = nullptr;
		if (!fopen_s(&file, path.c_str(), "rb") && file) {
			char buffer[4096];
			size_t read;
			while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, read);
			fclose(file);
		}
		return text;
	}
}

bool affinity_can_be_widened()
{
	return PinnedAffinity() != SystemAffinity();
}

// Runs in the child before the context is created.
void pin_to_one_cpu()
{
	if (!SetProcessAffinityMask(GetCurrentProcess(), PinnedAffinity()))
		printf("SetProcessAffinityMask failed (%lu)\n", GetLastError());
}

// Runs in the child after the context was created: the game keeps its pin.
void do_affinity_tests()
{
	DWORD_PTR process = 0, system = 0;
	GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
	CHECK(process == PinnedAffinity(), "the process keeps its CPU affinity 0x%llX (0x%llX)",
		static_cast<unsigned long long>(PinnedAffinity()), static_cast<unsigned long long>(process));
}

// Runs in the parent.
void check_affinity_log( const std::string &logPath, bool widened )
{
	const std::string log = ReadFile(logPath);
	char during[160], restored[96];
	sprintf_s(during, "Direct3DCreate9 runs with CPU affinity 0x%llX instead of the game's 0x%llX",
		static_cast<unsigned long long>(SystemAffinity()), static_cast<unsigned long long>(PinnedAffinity()));
	sprintf_s(restored, "CPU affinity restored to the game's 0x%llX", static_cast<unsigned long long>(PinnedAffinity()));
	if (widened) {
		CHECK(log.find(during) != std::string::npos, "%s: contains \"%s\"", logPath.c_str(), during);
		CHECK(log.find(restored) != std::string::npos, "%s: contains \"%s\"", logPath.c_str(), restored);
		CHECK(log.find("RemixServerAllCPUs: 1") != std::string::npos, "%s: settings list RemixServerAllCPUs: 1", logPath.c_str());
	} else {
		CHECK(log.find("Direct3DCreate9 runs with CPU affinity") == std::string::npos,
			"%s: Direct3DCreate9 keeps the pinned affinity", logPath.c_str());
	}
}
