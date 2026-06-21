#pragma once

#include <app/config.hpp>
#include <app/inject_service.hpp>
#include <payload/payload_shared.hpp>

#include <windows.h>

#include <functional>
#include <string>

struct payload_inject_session
{
    uint32_t target_pid = 0;
    payload_config config {};
    payload_shared_status local_status {};
    HANDLE status_mapping = nullptr;
    payload_shared_status* status_view = nullptr;
    bool payload_enabled = false;
};

payload_inject_session prepare_payload_session( uint32_t target_pid , const app_config& config , const inject_request& request );
void cleanup_payload_session( payload_inject_session& session );
bool verify_payload_handshake(
    payload_inject_session& session ,
    uint32_t timeout_ms ,
    const std::function< void( const std::wstring& ) >& log );
bool send_payload_ipc_command(
    const payload_inject_session& session ,
    payload_ipc_command command ,
    const std::function< void( const std::wstring& ) >& log );
payload_config build_payload_config( uint32_t target_pid , const app_config& config , const inject_request& request );
