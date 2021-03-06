// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <ctime>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/archive_systemsavedata.h"
#include "core/file_sys/directory_backend.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/file_backend.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/result.h"
#include "core/hle/service/cecd/cecd.h"
#include "core/hle/service/cecd/cecd_ndm.h"
#include "core/hle/service/cecd/cecd_s.h"
#include "core/hle/service/cecd/cecd_u.h"
#include "core/loader/smdh.h"

namespace Service::CECD {

using CecDataPathType = Module::CecDataPathType;
using CecOpenMode = Module::CecOpenMode;
using CecSystemInfoType = Module::CecSystemInfoType;

void Module::Interface::Open(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x01, 3, 2};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const CecDataPathType path_type{rp.PopEnum<CecDataPathType>()};
    CecOpenMode open_mode{};
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.read_flag.Assign(1);
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    SessionData* session_data = GetSessionData(ctx.Session());
    session_data->ncch_program_id = ncch_program_id;
    session_data->open_mode.raw = open_mode.raw;
    session_data->data_path_type = path_type;
    session_data->path = path;

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 0)};
    switch (path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR: {
        auto dir_result =
            Service::FS::OpenDirectoryFromArchive(cecd->cecd_system_save_data_archive, path);
        if (dir_result.Failed()) {
            if (open_mode.create) {
                Service::FS::CreateDirectoryFromArchive(cecd->cecd_system_save_data_archive, path);
                rb.Push(RESULT_SUCCESS);
            } else {
                LOG_DEBUG(Service_CECD, "Failed to open directory: {}", path.AsString());
                rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC,
                                   ErrorSummary::NotFound, ErrorLevel::Status));
            }
            rb.Push<u32>(0); /// Zero entries
        } else {
            constexpr u32 max_entries = 32; /// reasonable value, just over max boxes 24
            auto directory = dir_result.Unwrap();

            /// Actual reading into vector seems to be required for entry count
            std::vector<FileSys::Entry> entries(max_entries);
            const u32 entry_count = directory->backend->Read(max_entries, entries.data());

            LOG_DEBUG(Service_CECD, "Number of entries found: {}", entry_count);

            rb.Push(RESULT_SUCCESS);
            rb.Push<u32>(entry_count); /// Entry count
            directory->backend->Close();
        }
        break;
    }
    default: { /// If not directory, then it is a file
        auto file_result =
            Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, path, mode);
        if (file_result.Failed()) {
            LOG_DEBUG(Service_CECD, "Failed to open file: {}", path.AsString());
            rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                               ErrorLevel::Status));
            rb.Push<u32>(0); /// No file size
        } else {
            session_data->file = std::move(file_result.Unwrap());
            rb.Push(RESULT_SUCCESS);
            rb.Push<u32>(session_data->file->backend->GetSize()); /// Return file size
        }

        if (path_type == CecDataPathType::CEC_MBOX_PROGRAM_ID) {
            std::vector<u8> program_id(8);
            u64_le le_program_id = Kernel::g_current_process->codeset->program_id;
            std::memcpy(program_id.data(), &le_program_id, sizeof(u64));
            session_data->file->backend->Write(0, sizeof(u64), true, program_id.data());
            session_data->file->backend->Close();
        }
    }
    }

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, static_cast<u32>(path_type), path.AsString(), open_mode.raw,
              open_mode.unknown, open_mode.read, open_mode.write, open_mode.create,
              open_mode.check);
}

void Module::Interface::Read(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x02, 1, 2};
    const u32 write_buffer_size{rp.Pop<u32>()};
    auto& write_buffer{rp.PopMappedBuffer()};

    SessionData* session_data = GetSessionData(ctx.Session());
    LOG_DEBUG(Service_CECD,
              "SessionData: ncch_program_id={:#010x}, data_path_type={:#04x}, "
              "path={}, open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              session_data->ncch_program_id, static_cast<u32>(session_data->data_path_type),
              session_data->path.AsString(), session_data->open_mode.raw,
              session_data->open_mode.unknown, session_data->open_mode.read,
              session_data->open_mode.write, session_data->open_mode.create,
              session_data->open_mode.check);

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 2)};
    switch (session_data->data_path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        rb.Push(ResultCode(ErrorDescription::NotAuthorized, ErrorModule::CEC,
                           ErrorSummary::NotFound, ErrorLevel::Status));
        rb.Push<u32>(0); /// No bytes read
        break;
    default: /// If not directory, then it is a file
        std::vector<u8> buffer(write_buffer_size);
        const u32 bytes_read =
            session_data->file->backend->Read(0, write_buffer_size, buffer.data()).Unwrap();

        write_buffer.Write(buffer.data(), 0, write_buffer_size);
        session_data->file->backend->Close();

        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(bytes_read);
    }
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(Service_CECD, "called, write_buffer_size={:#x}, path={}", write_buffer_size,
              session_data->path.AsString());
}

void Module::Interface::ReadMessage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x03, 4, 4};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const bool is_outbox{rp.Pop<bool>()};
    const u32 message_id_size{rp.Pop<u32>()};
    const u32 buffer_size{rp.Pop<u32>()};
    auto& message_id_buffer{rp.PopMappedBuffer()};
    auto& write_buffer{rp.PopMappedBuffer()};

    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path;
    if (is_outbox) {
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_OUTBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    } else { /// otherwise inbox
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_INBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    }

    auto message_result =
        Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, message_path, mode);

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 4)};
    if (message_result.Succeeded()) {
        auto message = message_result.Unwrap();
        std::vector<u8> buffer(buffer_size);

        const u32 bytes_read = message->backend->Read(0, buffer_size, buffer.data()).Unwrap();
        write_buffer.Write(buffer.data(), 0, buffer_size);
        message->backend->Close();

        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(bytes_read);
    } else {
        rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
        rb.Push<u32>(0); /// zero bytes read
    }
    rb.PushMappedBuffer(message_id_buffer);
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::ReadMessageWithHMAC(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x04, 4, 6};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const bool is_outbox{rp.Pop<bool>()};
    const u32 message_id_size{rp.Pop<u32>()};
    const u32 buffer_size{rp.Pop<u32>()};
    auto& message_id_buffer{rp.PopMappedBuffer()};
    auto& hmac_key_buffer{rp.PopMappedBuffer()};
    auto& write_buffer{rp.PopMappedBuffer()};

    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path;
    if (is_outbox) {
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_OUTBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    } else { /// otherwise inbox
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_INBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    }

    auto message_result =
        Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, message_path, mode);

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 6)};
    if (message_result.Succeeded()) {
        auto message = message_result.Unwrap();
        std::vector<u8> buffer(buffer_size);

        const u32 bytes_read = message->backend->Read(0, buffer_size, buffer.data()).Unwrap();
        write_buffer.Write(buffer.data(), 0, buffer_size);
        message->backend->Close();

        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(bytes_read);
    } else {
        rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
        rb.Push<u32>(0); /// zero bytes read
    }

    rb.PushMappedBuffer(message_id_buffer);
    rb.PushMappedBuffer(hmac_key_buffer);
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::Write(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x05, 1, 2};
    const u32 read_buffer_size{rp.Pop<u32>()};
    auto& read_buffer{rp.PopMappedBuffer()};

    SessionData* session_data = GetSessionData(ctx.Session());
    LOG_DEBUG(Service_CECD,
              "SessionData: ncch_program_id={:#010x}, data_path_type={:#04x}, "
              "path={}, open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              session_data->ncch_program_id, static_cast<u32>(session_data->data_path_type),
              session_data->path.AsString(), session_data->open_mode.raw,
              session_data->open_mode.unknown, session_data->open_mode.read,
              session_data->open_mode.write, session_data->open_mode.create,
              session_data->open_mode.check);

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    switch (session_data->data_path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        rb.Push(ResultCode(ErrorDescription::NotAuthorized, ErrorModule::CEC,
                           ErrorSummary::NotFound, ErrorLevel::Status));
        break;
    default: /// If not directory, then it is a file
        std::vector<u8> buffer(read_buffer_size);
        read_buffer.Read(buffer.data(), 0, read_buffer_size);

        if (session_data->open_mode.check) {
            cecd->CheckAndUpdateFile(session_data->data_path_type, session_data->ncch_program_id,
                                     buffer);
        }

        const u32 bytes_written =
            session_data->file->backend->Write(0, read_buffer_size, true, buffer.data()).Unwrap();
        session_data->file->backend->Close();

        rb.Push(RESULT_SUCCESS);
    }
    rb.PushMappedBuffer(read_buffer);

    LOG_DEBUG(Service_CECD, "called, read_buffer_size={:#x}", read_buffer_size);
}

void Module::Interface::WriteMessage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x06, 4, 4};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const bool is_outbox{rp.Pop<bool>()};
    const u32 message_id_size{rp.Pop<u32>()};
    const u32 buffer_size{rp.Pop<u32>()};
    auto& read_buffer{rp.PopMappedBuffer()};
    auto& message_id_buffer{rp.PopMappedBuffer()};

    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path;
    if (is_outbox) {
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_OUTBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    } else { /// otherwise inbox
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_INBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    }

    auto message_result =
        Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, message_path, mode);

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 4)};
    if (message_result.Succeeded()) {
        auto message = message_result.Unwrap();
        std::vector<u8> buffer(buffer_size);

        read_buffer.Read(buffer.data(), 0, buffer_size);
        const u32 bytes_written =
            message->backend->Write(0, buffer_size, true, buffer.data()).Unwrap();
        message->backend->Close();

        rb.Push(RESULT_SUCCESS);
    } else {
        rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
    }

    rb.PushMappedBuffer(read_buffer);
    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::WriteMessageWithHMAC(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x07, 4, 6};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const bool is_outbox{rp.Pop<bool>()};
    const u32 message_id_size{rp.Pop<u32>()};
    const u32 buffer_size{rp.Pop<u32>()};
    auto& read_buffer{rp.PopMappedBuffer()};
    auto& hmac_key_buffer{rp.PopMappedBuffer()};
    auto& message_id_buffer{rp.PopMappedBuffer()};

    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path;
    if (is_outbox) {
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_OUTBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    } else { /// otherwise inbox
        message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_INBOX_MSG,
                                                        ncch_program_id, id_buffer)
                           .data();
    }

    auto message_result =
        Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, message_path, mode);

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 6)};
    if (message_result.Succeeded()) {
        auto message = message_result.Unwrap();
        std::vector<u8> buffer(buffer_size);

        read_buffer.Read(buffer.data(), 0, buffer_size);
        const u32 bytes_written =
            message->backend->Write(0, buffer_size, true, buffer.data()).Unwrap();
        message->backend->Close();

        rb.Push(RESULT_SUCCESS);
    } else {
        rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
    }

    rb.PushMappedBuffer(read_buffer);
    rb.PushMappedBuffer(hmac_key_buffer);
    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::Delete(Kernel::HLERequestContext& ctx) { /// DeleteMessage?
    IPC::RequestParser rp{ctx, 0x08, 4, 2};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const CecDataPathType path_type{rp.PopEnum<CecDataPathType>()};
    const bool is_outbox{rp.Pop<bool>()};
    const u32 message_id_size{rp.Pop<u32>()};
    auto& message_id_buffer{rp.PopMappedBuffer()};

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.write_flag.Assign(1);

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    switch (path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        rb.Push(Service::FS::DeleteDirectoryRecursivelyFromArchive(
            cecd->cecd_system_save_data_archive, path));
        break;
    default: /// If not directory, then it is a file
        if (message_id_size == 0) {
            rb.Push(Service::FS::DeleteFileFromArchive(cecd->cecd_system_save_data_archive, path));
        } else {
            std::vector<u8> id_buffer(message_id_size);
            message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

            FileSys::Path message_path;
            if (is_outbox) {
                message_path =
                    cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_OUTBOX_MSG,
                                                     ncch_program_id, id_buffer)
                        .data();
            } else { /// otherwise inbox
                message_path = cecd->GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_INBOX_MSG,
                                                                ncch_program_id, id_buffer)
                                   .data();
            }
            rb.Push(Service::FS::DeleteFileFromArchive(cecd->cecd_system_save_data_archive,
                                                       message_path));
        }
    }

    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, "
              "is_outbox={}, message_id_size={:#x}",
              ncch_program_id, static_cast<u32>(path_type), path.AsString(), is_outbox,
              message_id_size);
}

void Module::Interface::SetData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x09, 3, 2};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const u32 buffer_size{rp.Pop<u32>()};
    const u32 option{rp.Pop<u32>()};
    auto& message_id_buffer{rp.PopMappedBuffer()};

    if (buffer_size > 0) {
        FileSys::Path path("/SetData.out");
        FileSys::Mode mode;
        mode.write_flag.Assign(1);
        mode.create_flag.Assign(1);

        auto file_result =
            Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, path, mode);
        if (file_result.Succeeded()) {
            auto file = file_result.Unwrap();
            std::vector<u8> buffer(buffer_size);
            message_id_buffer.Read(buffer.data(), 0, buffer_size);

            file->backend->Write(0, buffer_size, true, buffer.data());
            file->backend->Close();
        }
    }

    SessionData* session_data = GetSessionData(ctx.Session());
    if (session_data->file)
        LOG_TRACE(
            Service_CECD,
            "SessionData: ncch_program_id={:#010x}, data_path_type={:#04x}, "
            "path={}, open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
            session_data->ncch_program_id, static_cast<u32>(session_data->data_path_type),
            session_data->path.AsString(), session_data->open_mode.raw,
            session_data->open_mode.unknown, session_data->open_mode.read,
            session_data->open_mode.write, session_data->open_mode.create,
            session_data->open_mode.check);

    if (session_data->file)
        session_data->file->backend->Close();

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(Service_CECD, "called, ncch_program_id={:#010x}, buffer_size={:#x}, option={:#x}",
              ncch_program_id, buffer_size, option);
}

void Module::Interface::ReadData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0A, 3, 4};
    const u32 dest_buffer_size{rp.Pop<u32>()};
    const CecSystemInfoType info_type{rp.PopEnum<CecSystemInfoType>()};
    const u32 param_buffer_size{rp.Pop<u32>()};
    auto& param_buffer{rp.PopMappedBuffer()};
    auto& dest_buffer{rp.PopMappedBuffer()};

    /// TODO: Other CecSystemInfoTypes
    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 4)};
    std::vector<u8> buffer;
    switch (info_type) {
    case CecSystemInfoType::EulaVersion: /// TODO: Read config Eula version
        buffer = {0xFF, 0xFF};
        dest_buffer.Write(buffer.data(), 0, buffer.size());
        break;
    case CecSystemInfoType::Eula:
        buffer = {0x01}; /// Eula agreed
        dest_buffer.Write(buffer.data(), 0, buffer.size());
        break;
    case CecSystemInfoType::ParentControl:
        buffer = {0x00}; /// No parent control
        dest_buffer.Write(buffer.data(), 0, buffer.size());
        break;
    default:
        LOG_ERROR(Service_CECD, "Unknown system info type={:#x}", static_cast<u32>(info_type));
    }

    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(param_buffer);
    rb.PushMappedBuffer(dest_buffer);

    LOG_DEBUG(Service_CECD,
              "called, dest_buffer_size={:#x}, info_type={:#x}, param_buffer_size={:#x}",
              dest_buffer_size, static_cast<u32>(info_type), param_buffer_size);
}

void Module::Interface::Start(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0B, 1, 0};
    const CecCommand command{rp.PopEnum<CecCommand>()};

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 0)};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_CECD, "(STUBBED) called, command={}", cecd->GetCecCommandAsString(command));
}

void Module::Interface::Stop(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0C, 1, 0};
    const CecCommand command{rp.PopEnum<CecCommand>()};

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 0)};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_CECD, "(STUBBED) called, command={}", cecd->GetCecCommandAsString(command));
}

void Module::Interface::GetCecInfoBuffer(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0D, 2, 2};
    const u32 buffer_size{rp.Pop<u32>()};
    const u32 possible_info_type{rp.Pop<u32>()};
    auto& buffer{rp.PopMappedBuffer()};

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(buffer);

    LOG_DEBUG(Service_CECD, "called, buffer_size={}, possible_info_type={}", buffer_size,
              possible_info_type);
}

void Module::Interface::GetCecdState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0E, 0, 0};

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 0)};
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(CecdState::NDM_STATUS_IDLE);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::GetCecInfoEventHandle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x0F, 0, 0};

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(cecd->cecinfo_event);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::GetChangeStateEventHandle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x10, 0, 0};

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(cecd->change_state_event);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::OpenAndWrite(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x11, 4, 4};
    const u32 buffer_size{rp.Pop<u32>()};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const CecDataPathType path_type{rp.PopEnum<CecDataPathType>()};
    CecOpenMode open_mode{};
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();
    auto& read_buffer{rp.PopMappedBuffer()};

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    IPC::ResponseBuilder rb{rp.MakeBuilder(1, 2)};
    switch (path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        rb.Push(ResultCode(ErrorDescription::NotAuthorized, ErrorModule::CEC,
                           ErrorSummary::NotFound, ErrorLevel::Status));
        break;
    default: /// If not directory, then it is a file
        auto file_result =
            Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, path, mode);
        if (file_result.Succeeded()) {
            auto file = file_result.Unwrap();
            std::vector<u8> buffer(buffer_size);
            read_buffer.Read(buffer.data(), 0, buffer_size);

            if (open_mode.check) {
                cecd->CheckAndUpdateFile(path_type, ncch_program_id, buffer);
            }

            const u32 bytes_written =
                file->backend->Write(0, buffer_size, true, buffer.data()).Unwrap();
            file->backend->Close();

            rb.Push(RESULT_SUCCESS);
        } else {
            rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                               ErrorLevel::Status));
        }
    }
    rb.PushMappedBuffer(read_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, buffer_size={:#x} "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, static_cast<u32>(path_type), path.AsString(), buffer_size,
              open_mode.raw, open_mode.unknown, open_mode.read, open_mode.write, open_mode.create,
              open_mode.check);
}

void Module::Interface::OpenAndRead(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx, 0x12, 4, 4};
    const u32 buffer_size{rp.Pop<u32>()};
    const u32 ncch_program_id{rp.Pop<u32>()};
    const CecDataPathType path_type{rp.PopEnum<CecDataPathType>()};
    CecOpenMode open_mode{};
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();
    auto& write_buffer{rp.PopMappedBuffer()};

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    IPC::ResponseBuilder rb{rp.MakeBuilder(2, 2)};
    switch (path_type) {
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        rb.Push(ResultCode(ErrorDescription::NotAuthorized, ErrorModule::CEC,
                           ErrorSummary::NotFound, ErrorLevel::Status));
        rb.Push<u32>(0); /// No entries read
        break;
    default: /// If not directory, then it is a file
        auto file_result =
            Service::FS::OpenFileFromArchive(cecd->cecd_system_save_data_archive, path, mode);
        if (file_result.Succeeded()) {
            auto file = file_result.Unwrap();
            std::vector<u8> buffer(buffer_size);

            const u32 bytes_read = file->backend->Read(0, buffer_size, buffer.data()).Unwrap();
            write_buffer.Write(buffer.data(), 0, buffer_size);
            file->backend->Close();

            rb.Push(RESULT_SUCCESS);
            rb.Push<u32>(bytes_read);
        } else {
            rb.Push(ResultCode(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                               ErrorLevel::Status));
            rb.Push<u32>(0); /// No bytes read
        }
    }
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, buffer_size={:#x} "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, static_cast<u32>(path_type), path.AsString(), buffer_size,
              open_mode.raw, open_mode.unknown, open_mode.read, open_mode.write, open_mode.create,
              open_mode.check);
}

std::string Module::EncodeBase64(const std::vector<u8>& in, const std::string& dictionary) const {
    std::string out;
    out.reserve((in.size() * 4) / 3);
    int b;
    for (int i = 0; i < in.size(); i += 3) {
        b = (in[i] & 0xFC) >> 2;
        out += dictionary[b];
        b = (in[i] & 0x03) << 4;
        if (i + 1 < in.size()) {
            b |= (in[i + 1] & 0xF0) >> 4;
            out += dictionary[b];
            b = (in[i + 1] & 0x0F) << 2;
            if (i + 2 < in.size()) {
                b |= (in[i + 2] & 0xC0) >> 6;
                out += dictionary[b];
                b = in[i + 2] & 0x3F;
                out += dictionary[b];
            } else {
                out += dictionary[b];
            }
        } else {
            out += dictionary[b];
        }
    }
    return out;
}

std::string Module::GetCecDataPathTypeAsString(const CecDataPathType type, const u32 program_id,
                                               const std::vector<u8>& msg_id) const {
    switch (type) {
    case CecDataPathType::CEC_PATH_MBOX_LIST:
        return "/CEC/MBoxList____";
    case CecDataPathType::CEC_PATH_MBOX_INFO:
        return Common::StringFromFormat("/CEC/%08x/MBoxInfo____", program_id);
    case CecDataPathType::CEC_PATH_INBOX_INFO:
        return Common::StringFromFormat("/CEC/%08x/InBox___/BoxInfo_____", program_id);
    case CecDataPathType::CEC_PATH_OUTBOX_INFO:
        return Common::StringFromFormat("/CEC/%08x/OutBox__/BoxInfo_____", program_id);
    case CecDataPathType::CEC_PATH_OUTBOX_INDEX:
        return Common::StringFromFormat("/CEC/%08x/OutBox__/OBIndex_____", program_id);
    case CecDataPathType::CEC_PATH_INBOX_MSG:
        return Common::StringFromFormat("/CEC/%08x/InBox___/_%08x", program_id,
                                        EncodeBase64(msg_id, base64_dict).data());
    case CecDataPathType::CEC_PATH_OUTBOX_MSG:
        return Common::StringFromFormat("/CEC/%08x/OutBox__/_%08x", program_id,
                                        EncodeBase64(msg_id, base64_dict).data());
    case CecDataPathType::CEC_PATH_ROOT_DIR:
        return "/CEC";
    case CecDataPathType::CEC_PATH_MBOX_DIR:
        return Common::StringFromFormat("/CEC/%08x", program_id);
    case CecDataPathType::CEC_PATH_INBOX_DIR:
        return Common::StringFromFormat("/CEC/%08x/InBox___", program_id);
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        return Common::StringFromFormat("/CEC/%08x/OutBox__", program_id);
    case CecDataPathType::CEC_MBOX_DATA:
    case CecDataPathType::CEC_MBOX_ICON:
    case CecDataPathType::CEC_MBOX_TITLE:
    default:
        return Common::StringFromFormat("/CEC/%08x/MBoxData.%03d", program_id,
                                        static_cast<u32>(type) - 100);
    }
}

std::string Module::GetCecCommandAsString(const CecCommand command) const {
    switch (command) {
    case CecCommand::CEC_COMMAND_NONE:
        return "NONE";
    case CecCommand::CEC_COMMAND_START:
        return "START";
    case CecCommand::CEC_COMMAND_RESET_START:
        return "RESET_START";
    case CecCommand::CEC_COMMAND_READYSCAN:
        return "READYSCAN";
    case CecCommand::CEC_COMMAND_READYSCANWAIT:
        return "READYSCANWAIT";
    case CecCommand::CEC_COMMAND_STARTSCAN:
        return "STARTSCAN";
    case CecCommand::CEC_COMMAND_RESCAN:
        return "RESCAN";
    case CecCommand::CEC_COMMAND_NDM_RESUME:
        return "RESUME";
    case CecCommand::CEC_COMMAND_NDM_SUSPEND:
        return "NDM_SUSPEND";
    case CecCommand::CEC_COMMAND_NDM_SUSPEND_IMMEDIATE:
        return "NDM_SUSPEND_IMMEDIATE";
    case CecCommand::CEC_COMMAND_STOPWAIT:
        return "STOPWAIT";
    case CecCommand::CEC_COMMAND_STOP:
        return "STOP";
    case CecCommand::CEC_COMMAND_STOP_FORCE:
        return "STOP_FORCE";
    case CecCommand::CEC_COMMAND_STOP_FORCE_WAIT:
        return "STOP_FORCE_WAIT";
    case CecCommand::CEC_COMMAND_RESET_FILTER:
        return "RESET_FILTER";
    case CecCommand::CEC_COMMAND_DAEMON_STOP:
        return "DAEMON_STOP";
    case CecCommand::CEC_COMMAND_DAEMON_START:
        return "DAEMON_START";
    case CecCommand::CEC_COMMAND_EXIT:
        return "EXIT";
    case CecCommand::CEC_COMMAND_OVER_BOSS:
        return "OVER_BOSS";
    case CecCommand::CEC_COMMAND_OVER_BOSS_FORCE:
        return "OVER_BOSS_FORCE";
    case CecCommand::CEC_COMMAND_OVER_BOSS_FORCE_WAIT:
        return "OVER_BOSS_FORCE_WAIT";
    case CecCommand::CEC_COMMAND_END:
        return "END";
    default:
        return "Unknown";
    }
}

void Module::CheckAndUpdateFile(const CecDataPathType path_type, const u32 ncch_program_id,
                                std::vector<u8>& file_buffer) {
    constexpr u32 max_num_boxes = 24;
    constexpr u32 name_size = 16;      /// fixed size 16 characters long
    constexpr u32 valid_name_size = 8; /// 8 characters are valid, the rest are null
    const u32 file_size = file_buffer.size();

    switch (path_type) {
    case CecDataPathType::CEC_PATH_MBOX_LIST: {
        CecMBoxListHeader mbox_list_header = {};
        std::memcpy(&mbox_list_header, file_buffer.data(), sizeof(CecMBoxListHeader));

        if (file_size != sizeof(CecMBoxListHeader)) { /// 0x18C
            LOG_DEBUG(Service_CECD, "CecMBoxListHeader size is incorrect: {}", file_size);
        }

        if (mbox_list_header.magic != 0x6868) { /// 'hh'
            if (mbox_list_header.magic == 0 || mbox_list_header.magic == 0xFFFF) {
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader magic number is not set");
            } else {
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader magic number is incorrect: {}",
                          mbox_list_header.magic);
            }
            std::memset(&mbox_list_header, 0, sizeof(CecMBoxListHeader));
            mbox_list_header.magic = 0x6868;
        }

        if (mbox_list_header.version != 0x01) { /// Not quite sure if it is a version
            if (mbox_list_header.version == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader version is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader version is incorrect: {}",
                          mbox_list_header.version);
            mbox_list_header.version = 0x01;
        }

        if (mbox_list_header.num_boxes > 24) {
            LOG_DEBUG(Service_CECD, "CecMBoxListHeader number of boxes is too large: {}",
                      mbox_list_header.num_boxes);
        } else {
            std::vector<u8> name_buffer(name_size);
            std::memset(name_buffer.data(), 0, name_size);

            if (ncch_program_id != 0) {
                std::string name = Common::StringFromFormat("%08x", ncch_program_id);
                std::memcpy(name_buffer.data(), name.data(), name.size());

                bool already_activated = false;
                for (auto i = 0; i < mbox_list_header.num_boxes; i++) {
                    LOG_DEBUG(Service_CECD, "{}", i);
                    /// Box names start at offset 0xC, are 16 char long, first 8 id, last 8 null
                    if (std::memcmp(name_buffer.data(), &mbox_list_header.box_names[i * name_size],
                                    valid_name_size) == 0) {
                        LOG_DEBUG(Service_CECD, "Title already activated");
                        already_activated = true;
                    }
                };

                if (!already_activated) {
                    if (mbox_list_header.num_boxes < max_num_boxes) { /// max boxes
                        LOG_DEBUG(Service_CECD, "Adding title to mboxlist____: {}", name);
                        std::memcpy(
                            &mbox_list_header.box_names[mbox_list_header.num_boxes * name_size],
                            name_buffer.data(), name_size);
                        mbox_list_header.num_boxes++;
                    }
                }
            } else { /// ncch_program_id == 0, remove/update activated boxes
                /// We need to read the /CEC directory to find out which titles, if any,
                /// are activated. The num_of_titles = (total_read_count) - 1, to adjust for
                /// the MBoxList____ file that is present in the directory as well.
                FileSys::Path root_path(
                    GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_ROOT_DIR, 0).data());

                auto dir_result =
                    Service::FS::OpenDirectoryFromArchive(cecd_system_save_data_archive, root_path);

                auto root_dir = dir_result.Unwrap();
                std::vector<FileSys::Entry> entries(max_num_boxes + 1); // + 1 mboxlist
                const u32 entry_count = root_dir->backend->Read(max_num_boxes + 1, entries.data());
                root_dir->backend->Close();

                LOG_DEBUG(Service_CECD, "Number of entries found in /CEC: {}", entry_count);

                std::string mbox_list_name("MBoxList____");
                std::string file_name;
                std::u16string u16_filename;

                /// Loop through entries but don't add mboxlist____ to itself.
                for (u32 i = 0; i < entry_count; i++) {
                    u16_filename = std::u16string(entries[i].filename);
                    file_name = Common::UTF16ToUTF8(u16_filename);

                    if (mbox_list_name.compare(file_name) != 0) {
                        LOG_DEBUG(Service_CECD, "Adding title to mboxlist____: {}", file_name);
                        std::memcpy(&mbox_list_header.box_names[16 * mbox_list_header.num_boxes++],
                                    file_name.data(), valid_name_size);
                    }
                }
            }
        }
        std::memcpy(file_buffer.data(), &mbox_list_header, sizeof(CecMBoxListHeader));
        break;
    }
    case CecDataPathType::CEC_PATH_MBOX_INFO: {
        CecMBoxInfoHeader mbox_info_header = {};
        std::memcpy(&mbox_info_header, file_buffer.data(), sizeof(CecMBoxInfoHeader));

        if (file_size != sizeof(CecMBoxInfoHeader)) { /// 0x60
            LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader size is incorrect: {}", file_size);
        }

        if (mbox_info_header.magic != 0x6363) { /// 'cc'
            if (mbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader magic number is incorrect: {}",
                          mbox_info_header.magic);
            mbox_info_header.magic = 0x6363;
        }

        if (mbox_info_header.program_id != ncch_program_id) {
            if (mbox_info_header.program_id == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader program id is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader program id doesn't match current id: {}",
                          mbox_info_header.program_id);
        }

        std::memcpy(file_buffer.data(), &mbox_info_header, sizeof(CecMBoxInfoHeader));
        break;
    }
    case CecDataPathType::CEC_PATH_INBOX_INFO: {
        CecInOutBoxInfoHeader inbox_info_header = {};
        std::memcpy(&inbox_info_header, file_buffer.data(), sizeof(CecInOutBoxInfoHeader));

        if (inbox_info_header.magic != 0x6262) { /// 'bb'
            if (inbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader magic number is incorrect: {}",
                          inbox_info_header.magic);
            inbox_info_header.magic = 0x6262;
        }

        if (inbox_info_header.box_info_size != file_size) {
            if (inbox_info_header.box_info_size == 0)
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader box info size is not set");
            else
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader box info size is incorrect:",
                          inbox_info_header.box_info_size);
            inbox_info_header.box_info_size = sizeof(CecInOutBoxInfoHeader);
        }

        if (inbox_info_header.max_box_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max box size is not set");
        } else if (inbox_info_header.max_box_size > 0x100000) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max box size is too large: {}",
                      inbox_info_header.max_box_size);
        }

        if (inbox_info_header.max_message_num == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message number is not set");
        } else if (inbox_info_header.max_message_num > 99) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message number is too large: {}",
                      inbox_info_header.max_message_num);
        }

        if (inbox_info_header.max_message_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message size is not set");
        } else if (inbox_info_header.max_message_size > 0x019000) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message size is too large");
        }

        if (inbox_info_header.max_batch_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max batch size is not set");
            inbox_info_header.max_batch_size = inbox_info_header.max_message_num;
        } else if (inbox_info_header.max_batch_size != inbox_info_header.max_message_num) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max batch size != max message number");
        }

        std::memcpy(file_buffer.data(), &inbox_info_header, sizeof(CecInOutBoxInfoHeader));
        break;
    }
    case CecDataPathType::CEC_PATH_OUTBOX_INFO: {
        CecInOutBoxInfoHeader outbox_info_header = {};
        std::memcpy(&outbox_info_header, file_buffer.data(), sizeof(CecInOutBoxInfoHeader));

        if (outbox_info_header.magic != 0x6262) { /// 'bb'
            if (outbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader magic number is incorrect: {}",
                          outbox_info_header.magic);
            outbox_info_header.magic = 0x6262;
        }

        if (outbox_info_header.box_info_size != file_size) {
            if (outbox_info_header.box_info_size == 0)
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader box info size is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader box info size is incorrect:",
                          outbox_info_header.box_info_size);
            outbox_info_header.box_info_size = sizeof(CecInOutBoxInfoHeader);
        }

        if (outbox_info_header.max_box_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max box size is not set");
        } else if (outbox_info_header.max_box_size > 0x100000) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max box size is too large");
        }

        if (outbox_info_header.max_message_num == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message number is not set");
        } else if (outbox_info_header.max_message_num > 99) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message number is too large");
        }

        if (outbox_info_header.max_message_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message size is not set");
        } else if (outbox_info_header.max_message_size > 0x019000) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message size is too large");
        }

        if (outbox_info_header.max_batch_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max batch size is not set");
            outbox_info_header.max_batch_size = outbox_info_header.max_message_num;
        } else if (outbox_info_header.max_batch_size != outbox_info_header.max_message_num) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max batch size != max message number");
        }

        std::memcpy(file_buffer.data(), &outbox_info_header, sizeof(CecInOutBoxInfoHeader));
        break;
    }
    case CecDataPathType::CEC_PATH_OUTBOX_INDEX: {
        CecOBIndexHeader obindex_header = {};
        std::memcpy(&obindex_header, file_buffer.data(), sizeof(CecOBIndexHeader));

        if (file_size < sizeof(CecOBIndexHeader)) { /// 0x08, minimum size
            LOG_DEBUG(Service_CECD, "CecOBIndexHeader size is too small: {}", file_size);
        }

        if (obindex_header.magic != 0x6767) { /// 'gg'
            if (obindex_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader magic number is incorrect: {}",
                          obindex_header.magic);
            obindex_header.magic = 0x6767;
        }

        if (obindex_header.message_num == 0) {
            if (file_size > sizeof(CecOBIndexHeader)) {
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader message number is not set");
                obindex_header.message_num = (file_size % 8) - 1; /// 8 byte message id - 1 header
            }
        } else if (obindex_header.message_num != (file_size % 8) - 1) {
            LOG_DEBUG(Service_CECD, "CecOBIndexHeader message number is incorrect: {}",
                      obindex_header.message_num);
        }

        std::memcpy(file_buffer.data(), &obindex_header, sizeof(CecOBIndexHeader));
        break;
    }
    case CecDataPathType::CEC_PATH_INBOX_MSG:
        break;
    case CecDataPathType::CEC_PATH_OUTBOX_MSG:
        break;
    case CecDataPathType::CEC_PATH_ROOT_DIR:
    case CecDataPathType::CEC_PATH_MBOX_DIR:
    case CecDataPathType::CEC_PATH_INBOX_DIR:
    case CecDataPathType::CEC_PATH_OUTBOX_DIR:
        break;
    case CecDataPathType::CEC_MBOX_DATA:
    case CecDataPathType::CEC_MBOX_ICON:
    case CecDataPathType::CEC_MBOX_TITLE:
    default: {}
    }
}

Module::SessionData::SessionData() {}

Module::SessionData::~SessionData() {
    if (file)
        file->backend->Close();
}

Module::Interface::Interface(std::shared_ptr<Module> cecd, const char* name, u32 max_session)
    : ServiceFramework(name, max_session), cecd(std::move(cecd)) {}

Module::Module() {
    using namespace Kernel;
    cecinfo_event = Event::Create(Kernel::ResetType::OneShot, "CECD::cecinfo_event");
    change_state_event = Event::Create(Kernel::ResetType::OneShot, "CECD::change_state_event");

    // Open the SystemSaveData archive 0x00010026
    FileSys::Path archive_path(cecd_system_savedata_id);
    auto archive_result =
        Service::FS::OpenArchive(Service::FS::ArchiveIdCode::SystemSaveData, archive_path);

    // If the archive didn't exist, create the files inside
    if (archive_result.Code() == FileSys::ERR_NOT_FORMATTED) {
        // Format the archive to create the directories
        Service::FS::FormatArchive(Service::FS::ArchiveIdCode::SystemSaveData,
                                   FileSys::ArchiveFormatInfo(), archive_path);

        // Open it again to get a valid archive now that the folder exists
        archive_result =
            Service::FS::OpenArchive(Service::FS::ArchiveIdCode::SystemSaveData, archive_path);

        /// Now that the archive is formatted, we need to create the root CEC directory,
        /// eventlog.dat, and CEC/MBoxList____
        const FileSys::Path root_dir_path(
            GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_ROOT_DIR, 0).data());
        Service::FS::CreateDirectoryFromArchive(*archive_result, root_dir_path);

        FileSys::Mode mode;
        mode.write_flag.Assign(1);
        mode.create_flag.Assign(1);

        /// eventlog.dat resides in the root of the archive beside the CEC directory
        /// Initially created, at offset 0x0, are bytes 0x01 0x41 0x12, followed by
        /// zeroes until offset 0x1000, where it changes to 0xDD until the end of file
        /// at offset 0x30d53
        FileSys::Path eventlog_path("/eventlog.dat");

        auto eventlog_result =
            Service::FS::OpenFileFromArchive(*archive_result, eventlog_path, mode);

        constexpr u32 eventlog_size = 0x30d54;
        auto eventlog = eventlog_result.Unwrap();
        std::vector<u8> eventlog_buffer(eventlog_size);

        std::memset(&eventlog_buffer[0], 0, 0x1000);
        eventlog_buffer[0] = 0x01;
        eventlog_buffer[1] = 0x41;
        eventlog_buffer[2] = 0x12;
        std::memset(&eventlog_buffer[0x1000], 0xDD, eventlog_size - 0x1000);

        eventlog->backend->Write(0, eventlog_size, true, eventlog_buffer.data());
        eventlog->backend->Close();

        /// MBoxList____ resides within the root CEC/ directory.
        /// Initially created, at offset 0x0, are bytes 0x68 0x68 0x00 0x00 0x01, with 0x6868 'hh',
        /// being the magic number. The rest of the file is filled with zeroes, until the end of
        /// file at offset 0x18b
        FileSys::Path mboxlist_path(
            GetCecDataPathTypeAsString(CecDataPathType::CEC_PATH_MBOX_LIST, 0).data());

        auto mboxlist_result =
            Service::FS::OpenFileFromArchive(*archive_result, mboxlist_path, mode);

        constexpr u32 mboxlist_size = 0x18c;
        auto mboxlist = mboxlist_result.Unwrap();
        std::vector<u8> mboxlist_buffer(mboxlist_size);

        std::memset(&mboxlist_buffer[0], 0, mboxlist_size);
        mboxlist_buffer[0] = 0x68;
        mboxlist_buffer[1] = 0x68;
        /// mboxlist_buffer[2-3] are already zeroed
        mboxlist_buffer[4] = 0x01;

        mboxlist->backend->Write(0, mboxlist_size, true, mboxlist_buffer.data());
        mboxlist->backend->Close();
    }
    ASSERT_MSG(archive_result.Succeeded(), "Could not open the CECD SystemSaveData archive!");

    cecd_system_save_data_archive = *archive_result;
}

Module::~Module() {
    if (cecd_system_save_data_archive)
        Service::FS::CloseArchive(cecd_system_save_data_archive);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto cecd = std::make_shared<Module>();
    std::make_shared<CECD_NDM>(cecd)->InstallAsService(service_manager);
    std::make_shared<CECD_S>(cecd)->InstallAsService(service_manager);
    std::make_shared<CECD_U>(cecd)->InstallAsService(service_manager);
}

} // namespace Service::CECD
