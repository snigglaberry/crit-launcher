// Crit Launcher Native — feature pages + the install/launch engine.
#pragma once

#include <memory>

#include "app/app.h"

namespace crit {

std::unique_ptr<PageBase> MakeHomePage();
std::unique_ptr<PageBase> MakeInstancesPage();
std::unique_ptr<PageBase> MakeAddonsPage();
std::unique_ptr<PageBase> MakeSkinsPage();
std::unique_ptr<PageBase> MakeScreenshotsPage();
std::unique_ptr<PageBase> MakeServersPage();
std::unique_ptr<PageBase> MakeAccountsPage();
std::unique_ptr<PageBase> MakeSettingsPage();
std::unique_ptr<PageBase> MakeConsolePage();

// install + launch engine (minecraft/)
void StartInstall(App& app, const string& instanceName);
void StartLaunch(App& app, const string& instanceName);
void StopInstanceProcess(App& app, const string& instanceName);
std::vector<string> DetectJavaRuntimes(App& app);
string ResolveJavaFor(App& app, const string& version);

}  // namespace crit
