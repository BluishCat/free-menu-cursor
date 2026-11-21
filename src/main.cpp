#include "Cursor.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true, .logLevel = spdlog::level::info, .trampoline = true, .trampolineSize = 64 });
	logger::info("Free Menu Cursor {} loading", SKSE::GetPluginVersion().string());

	FMC::RestorePointerFromLastRun();
	FMC::InstallHooks();

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
			if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded)
				FMC::OnDataLoaded();
		})) {
		logger::error("Failed to register the SKSE message listener");
		return false;
	}
	return true;
}
