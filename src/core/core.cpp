// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include "audio_core/dsp_interface.h"
#include "audio_core/hle/hle.h"
#include "common/logging/log.h"
#include "core/arm/arm_interface.h"
#ifdef ARCHITECTURE_x86_64
#include "core/arm/dynarmic/arm_dynarmic.h"
#endif
#include "core/arm/dyncom/arm_dyncom.h"
#include "core/cheat_core.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hw/hw.h"
#include "core/loader/loader.h"
#include "core/memory_setup.h"
#include "core/movie.h"
#include "core/rpc/rpc_server.h"
#include "core/settings.h"
#include "network/network.h"
#include "video_core/video_core.h"

namespace Core {

/*static*/ System System::s_instance;

System::ResultStatus System::RunLoop() {
    if (!cpu_core) {
        return ResultStatus::ErrorNotInitialized;
    }

    // If we don't have a currently active thread then don't execute instructions,
    // instead advance to the next event and try to yield to the next thread
    if (Kernel::GetCurrentThread() == nullptr) {
        LOG_TRACE(Core_ARM11, "Idling");
        CoreTiming::Idle();
        CoreTiming::Advance();
        PrepareReschedule();
    } else {
        CoreTiming::Advance();
        cpu_core->Run();
    }

    HW::Update();
    Reschedule();

    if (jump_requested.exchange(false)) {
        Jump();
    } else if (shutdown_requested.exchange(false)) {
        return ResultStatus::ShutdownRequested;
    }

    return ResultStatus::Success;
}

System::ResultStatus System::Load(EmuWindow& emu_window, const std::string& filepath) {
    app_loader = Loader::GetLoader(filepath);

    if (!app_loader) {
        LOG_CRITICAL(Core, "Failed to obtain loader for {}!", filepath);
        return ResultStatus::ErrorGetLoader;
    }
    std::pair<boost::optional<u32>, Loader::ResultStatus> system_mode{
        app_loader->LoadKernelSystemMode()};

    if (system_mode.second != Loader::ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to determine system mode (Error {})!",
                     static_cast<int>(system_mode.second));

        switch (system_mode.second) {
        case Loader::ResultStatus::ErrorEncrypted:
            return ResultStatus::ErrorLoader_ErrorEncrypted;
        case Loader::ResultStatus::ErrorInvalidFormat:
            return ResultStatus::ErrorLoader_ErrorInvalidFormat;
        default:
            return ResultStatus::ErrorSystemMode;
        }
    }

    ResultStatus init_result{Init(emu_window, system_mode.first.get())};
    if (init_result != ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to initialize system (Error {})!",
                     static_cast<u32>(init_result));
        System::Shutdown();
        return init_result;
    }

    const Loader::ResultStatus load_result{app_loader->Load(Kernel::g_current_process)};
    if (Loader::ResultStatus::Success != load_result) {
        LOG_CRITICAL(Core, "Failed to load ROM (Error {})!", static_cast<u32>(load_result));
        System::Shutdown();

        switch (load_result) {
        case Loader::ResultStatus::ErrorEncrypted:
            return ResultStatus::ErrorLoader_ErrorEncrypted;
        case Loader::ResultStatus::ErrorInvalidFormat:
            return ResultStatus::ErrorLoader_ErrorInvalidFormat;
        default:
            return ResultStatus::ErrorLoader;
        }
    }
    Memory::SetCurrentPageTable(&Kernel::g_current_process->vm_manager.page_table);
    status = ResultStatus::Success;
    m_emu_window = &emu_window;
    m_filepath = filepath;
    return status;
}

void System::PrepareReschedule() {
    cpu_core->PrepareReschedule();
    reschedule_pending = true;
}

PerfStats::Results System::GetAndResetPerfStats() {
    return perf_stats.GetAndResetStats(CoreTiming::GetGlobalTimeUs());
}

void System::Reschedule() {
    if (!reschedule_pending) {
        return;
    }

    reschedule_pending = false;
    Kernel::Reschedule();
}

System::ResultStatus System::Init(EmuWindow& emu_window, u32 system_mode) {
    LOG_DEBUG(HW_Memory, "initialized OK");

    CoreTiming::Init();

    if (Settings::values.use_cpu_jit) {
#ifdef ARCHITECTURE_x86_64
        cpu_core = std::make_unique<ARM_Dynarmic>(USER32MODE);
#else
        cpu_core = std::make_unique<ARM_DynCom>(USER32MODE);
        LOG_WARNING(Core, "CPU JIT requested, but Dynarmic not available");
#endif
    } else {
        cpu_core = std::make_unique<ARM_DynCom>(USER32MODE);
    }

    qt_callbacks = std::make_unique<QtCallbacks>();

    dsp_core = std::make_unique<AudioCore::DspHle>();
    dsp_core->SetSink(Settings::values.sink_id, Settings::values.audio_device_id);
    dsp_core->EnableStretching(Settings::values.enable_audio_stretching);

    rpc_server = std::make_unique<RPC::RPCServer>();
    service_manager = std::make_shared<Service::SM::ServiceManager>();
    shared_page_handler = std::make_shared<SharedPage::Handler>();

    HW::Init();
    Kernel::Init(system_mode);
    Service::Init(service_manager);
    CheatCore::Init();

    ResultStatus result = VideoCore::Init(emu_window);
    if (result != ResultStatus::Success) {
        return result;
    }

    LOG_DEBUG(Core, "Initialized OK");

    // Reset counters and set time origin to current frame
    GetAndResetPerfStats();
    perf_stats.BeginSystemFrame();

    return ResultStatus::Success;
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *service_manager;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *service_manager;
}

void System::Shutdown() {
    // Shutdown emulation session
    CheatCore::Shutdown();
    VideoCore::Shutdown();
    Service::Shutdown();
    Kernel::Shutdown();
    HW::Shutdown();
    service_manager.reset();
    dsp_core.reset();
    cpu_core.reset();
    CoreTiming::Shutdown();
    app_loader.reset();
    qt_callbacks.reset();
    rpc_server.reset();

    LOG_DEBUG(Core, "Shutdown OK");
}

void System::Jump() {
    Shutdown();
    if (jump_tid == 0) {
        Load(*m_emu_window, m_filepath);
        return;
    }
    std::string path{Service::AM::GetTitleContentPath(jump_media, jump_tid)};
    Load(*m_emu_window, path);
}

} // namespace Core
