#pragma once
// ADR-033 / spec 4: --role=setup (SetupWorker). main.cpp yukseltme + ayni EXE ust surec + broker_trust kontrollerini
// yaptiktan sonra cagrilir. Sira: makine kilidi -> kurulum journal'i -> recover_interrupted -> pipe (oturum kullanicisi
// ACL'i, ust surec kimligi birebir) -> Hello -> TEK SetupRequest (Install / Uninstall / Repair; yol/deger yok) ->
// her journal kaydindan sonra SetupProgress -> SetupReply -> Bye. Bu oturumda calistirilmaz.

#include "core/roles.h"
#include "platform/windows/elevation.h"

namespace ef::platform::install {

int run_setup_worker(const core::LaunchArgs& args, const ParentIdentity& parent);

}  // namespace ef::platform::install
