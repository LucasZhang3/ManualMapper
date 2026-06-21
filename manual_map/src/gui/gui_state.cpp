#define NOMINMAX

#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_widgets.hpp"
#include "window_stealth.hpp"

#include <app/errors.hpp>

#include <imgui.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <chrono>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

extern void* g_gui_hwnd;

namespace
{
    gui_app_state* g_state = nullptr;

    std::string wide_to_utf8( const std::wstring& text )
    {
        if ( text.empty( ) )
        {
            return {};
        }

        const auto length = WideCharToMultiByte( CP_UTF8 , 0 , text.c_str( ) , -1 , nullptr , 0 , nullptr , nullptr );

        if ( length <= 0 )
        {
            return {};
        }

        std::string utf8( static_cast< size_t >( length - 1 ) , '\0' );
        WideCharToMultiByte( CP_UTF8 , 0 , text.c_str( ) , -1 , utf8.data( ) , length , nullptr , nullptr );
        return utf8;
    }

    std::wstring utf8_to_wide( const std::string& text )
    {
        if ( text.empty( ) )
        {
            return {};
        }

        const auto length = MultiByteToWideChar( CP_UTF8 , 0 , text.c_str( ) , -1 , nullptr , 0 );

        if ( length <= 0 )
        {
            return {};
        }

        std::wstring wide( static_cast< size_t >( length - 1 ) , L'\0' );
        MultiByteToWideChar( CP_UTF8 , 0 , text.c_str( ) , -1 , wide.data( ) , length );
        return wide;
    }

    std::string timestamp_prefix( )
    {
        const auto now = std::chrono::system_clock::now( );
        const std::time_t time = std::chrono::system_clock::to_time_t( now );
        std::tm local_time {};
        localtime_s( &local_time , &time );

        std::ostringstream stream;
        stream << std::put_time( &local_time , "%H:%M:%S" ) << " ";
        return stream.str( );
    }

    std::wstring now_timestamp( )
    {
        const auto now = std::chrono::system_clock::now( );
        const std::time_t time = std::chrono::system_clock::to_time_t( now );
        std::tm local_time {};
        localtime_s( &local_time , &time );

        std::wostringstream stream;
        stream << std::put_time( &local_time , L"%Y-%m-%d %H:%M:%S" );
        return stream.str( );
    }

    void append_log( gui_app_state& state , const std::string& line )
    {
        std::lock_guard lock( state.pending_mutex );
        state.pending_log.push_back( line );
    }

    void append_log_w( gui_app_state& state , const std::wstring& line )
    {
        append_log( state , wide_to_utf8( line ) );
    }

    void flush_pending_log( gui_app_state& state )
    {
        std::vector< std::string > lines;
        {
            std::lock_guard lock( state.pending_mutex );
            lines.swap( state.pending_log );
        }

        if ( lines.empty( ) )
        {
            return;
        }

        std::lock_guard lock( state.log_mutex );

        for ( const auto& line : lines )
        {
            if ( !state.log.empty( ) )
            {
                state.log += "\n";
            }

            if ( state.log_timestamps )
            {
                state.log += timestamp_prefix( );
            }

            state.log += line;
        }
    }

    void refresh_dll_pe( gui_app_state& state )
    {
        if ( !state.dll_path [ 0 ] )
        {
            state.dll_pe = {};
            return;
        }

        state.dll_pe = analyze_pe_file( utf8_to_wide( state.dll_path ) );
    }

    void refresh_processes( gui_app_state& state )
    {
        state.all_processes = list_processes( );
        append_log( state , "Process list refreshed (" + std::to_string( state.all_processes.size( ) ) + " total)." );
        gui_push_toast( state , "Process list refreshed" , toast_type::info );
    }

    std::vector< process_entry > visible_processes( gui_app_state& state )
    {
        auto visible = filter_processes( state.all_processes , utf8_to_wide( state.search ).c_str( ) );
        sort_processes( visible , state.sort_mode );

        if ( state.show_process_tree )
        {
            std::sort( visible.begin( ) , visible.end( ) , [ ] ( const process_entry& a , const process_entry& b )
            {
                if ( a.parent_pid != b.parent_pid )
                {
                    return a.parent_pid < b.parent_pid;
                }

                return a.pid < b.pid;
            } );
        }

        return visible;
    }

    bool is_favorite( const gui_app_state& state , uint32_t pid )
    {
        return std::find( state.favorite_pids.begin( ) , state.favorite_pids.end( ) , pid ) != state.favorite_pids.end( );
    }

    void toggle_favorite( gui_app_state& state , uint32_t pid )
    {
        const auto it = std::find( state.favorite_pids.begin( ) , state.favorite_pids.end( ) , pid );

        if ( it != state.favorite_pids.end( ) )
        {
            state.favorite_pids.erase( it );
        }
        else
        {
            state.favorite_pids.push_back( pid );
        }

        state.config.favorite_pids = state.favorite_pids;
        save_config( state.config );
    }

    char avatar_letter( const std::wstring& name )
    {
        if ( name.empty( ) )
        {
            return '?';
        }

        const wchar_t c = towupper( name [ 0 ] );
        return static_cast< char >( c > 127 ? '?' : c );
    }

    struct inject_worker_context
    {
        gui_app_state* state = nullptr;
        inject_request request;
        std::wstring history_target;
        std::wstring history_dll;
    };

    DWORD WINAPI inject_worker( void* parameter );

    void record_history( gui_app_state& state , const inject_worker_context& context , bool success )
    {
        injection_history_entry entry {};
        entry.timestamp = now_timestamp( );
        entry.target = context.history_target;
        entry.dll = context.history_dll;
        entry.success = success;
        add_injection_history( state.config , entry );
        save_config( state.config );
    }

    void launch_single_injection( gui_app_state& state , const std::wstring& dll_path , inject_worker_context* context );

    void launch_single_injection( gui_app_state& state , const std::wstring& dll_path , inject_worker_context* context )
    {
        context->request.dll_path = dll_path;
        context->history_dll = dll_path;
        append_log( state , "[inject] Payload DLL: " + wide_to_utf8( dll_path ) );

        if ( !state.config.cli_notes.empty( ) )
        {
            append_log( state , "[cli] " + wide_to_utf8( state.config.cli_notes ) );
        }

        if ( state.selected_pid > 0 )
        {
            context->request.process_id = static_cast< uint32_t >( state.selected_pid );
            context->history_target = L"PID " + std::to_wstring( state.selected_pid );
            append_log( state , "[inject] Target PID " + std::to_string( state.selected_pid ) );
        }
        else if ( state.search [ 0 ] )
        {
            context->request.process_name = utf8_to_wide( state.search );
            context->history_target = context->request.process_name;
            append_log( state , "[inject] Target process name: " + std::string( state.search ) );
        }
        else
        {
            append_log( state , "Search for a process and select it from the list, or type a process name." );
            delete context;
            state.injecting.store( false , std::memory_order_release );
            state.inject_queue_active = false;
            state.inject_done.store( true , std::memory_order_release );
            return;
        }

        const std::wstring target_name = context->request.process_id
            ? [ & ] ( )
            {
                for ( const auto& process : state.all_processes )
                {
                    if ( static_cast< int >( process.pid ) == state.selected_pid )
                    {
                        context->history_target = process.name + L" (PID " + std::to_wstring( process.pid ) + L")";
                        return process.name;
                    }
                }

                return std::wstring {};
            }( )
            : context->request.process_name;

        if ( !target_name.empty( ) && !is_process_allowed( state.config , target_name ) )
        {
            append_log( state , "Process blocked by safety rules." );
            delete context;
            state.injecting.store( false , std::memory_order_release );
            state.inject_queue_active = false;
            state.inject_done.store( true , std::memory_order_release );
            return;
        }

        HANDLE thread = CreateThread( nullptr , 0 , inject_worker , context , 0 , nullptr );

        if ( !thread )
        {
            state.injecting.store( false , std::memory_order_release );
            state.inject_queue_active = false;
            state.inject_done.store( true , std::memory_order_release );
            state.status = "Failed to start worker thread";
            delete context;
            append_log( state , "Failed to start injection thread." );
            return;
        }

        CloseHandle( thread );
    }

    DWORD WINAPI inject_worker( void* parameter )
    {
        auto* context = static_cast< inject_worker_context* >( parameter );
        inject_result result = run_injection( context->request , context->state->config );

        if ( g_state )
        {
            g_state->inject_result = result;
            record_history( *g_state , *context , result.code == 0 );

            if ( g_state->inject_queue_active && g_state->inject_queue_index + 1 < g_state->inject_queue.size( ) )
            {
                ++g_state->inject_queue_index;
                const std::wstring next_dll = g_state->inject_queue [ g_state->inject_queue_index ];
                auto* next_context = new inject_worker_context {};
                next_context->state = g_state;
                next_context->request.wait_for_ms = g_state->wait_for_process ? 30000u : 0u;
                next_context->request.delay_ms = static_cast< uint32_t >( g_state->inject_delay_sec ) * 1000u;
                next_context->request.inject_all = g_state->inject_all;
                next_context->request.log = [ state = g_state ] ( const std::wstring& line )
                {
                    append_log_w( *state , line );
                };
                launch_single_injection( *g_state , next_dll , next_context );
                delete context;
                return 0;
            }

            g_state->inject_done.store( true , std::memory_order_release );
            g_state->inject_queue_active = false;
        }

        delete context;
        return 0;
    }

    void launch_injection( gui_app_state& state )
    {
        if ( state.injecting.load( ) )
        {
            return;
        }

        std::vector< std::wstring > dlls;

        if ( !state.inject_queue.empty( ) )
        {
            dlls = state.inject_queue;
        }
        else if ( state.dll_path [ 0 ] )
        {
            dlls.push_back( utf8_to_wide( state.dll_path ) );
        }

        if ( dlls.empty( ) )
        {
            append_log( state , "Select a DLL first." );
            gui_push_toast( state , "Select a DLL first" , toast_type::error_type );
            return;
        }

        state.inject_queue = dlls;
        state.inject_queue_index = 0;
        state.inject_queue_active = dlls.size( ) > 1;

        auto* context = new inject_worker_context {};
        context->state = &state;
        context->request.wait_for_ms = state.wait_for_process ? 30000u : 0u;
        context->request.delay_ms = static_cast< uint32_t >( state.inject_delay_sec ) * 1000u;
        context->request.inject_all = state.inject_all;
        context->request.log = [ &state ] ( const std::wstring& line )
        {
            append_log_w( state , line );
        };

        state.inject_done.store( false , std::memory_order_release );
        state.injecting.store( true , std::memory_order_release );
        state.inject_progress = 0.15f;
        state.status = "Injecting...";

        launch_single_injection( state , dlls.front( ) , context );
    }

    void start_injection( gui_app_state& state )
    {
        if ( state.confirm_inject )
        {
            state.show_confirm_popup = true;
            return;
        }

        launch_injection( state );
    }

    void poll_injection( gui_app_state& state )
    {
        if ( state.injecting.load( ) )
        {
            state.inject_progress = ( std::min )( 0.92f , state.inject_progress + ImGui::GetIO( ).DeltaTime * 0.18f );
        }

        if ( !state.inject_done.load( std::memory_order_acquire ) )
        {
            return;
        }

        state.inject_done.store( false , std::memory_order_release );
        state.injecting.store( false , std::memory_order_release );
        state.inject_progress = 1.0f;

        state.last_error_code = state.inject_result.code;

        if ( state.inject_result.code == 0 )
        {
            state.status = "Injection succeeded";
            append_log( state , "Injection succeeded." );
            gui_push_toast( state , "Injection succeeded" , toast_type::success );
        }
        else
        {
            state.status = "Injection failed (0x" + std::to_string( state.inject_result.code ) + ")";
            append_log(
                state ,
                "Injection failed (0x" + std::to_string( state.inject_result.code ) + "): "
                + wide_to_utf8( inject_error_message_w( state.inject_result.code ) ) );
            gui_push_toast( state , "Injection failed" , toast_type::error_type );
        }
    }

    void poll_auto_inject( gui_app_state& state )
    {
        if ( !state.auto_inject || state.injecting.load( ) || !state.search [ 0 ] || !state.dll_path [ 0 ] )
        {
            return;
        }

        const auto matches = find_processes_by_name( utf8_to_wide( state.search ).c_str( ) );

        if ( matches.empty( ) || matches.front( ).pid == state.last_auto_inject_pid )
        {
            return;
        }

        state.selected_pid = static_cast< int >( matches.front( ).pid );
        state.last_auto_inject_pid = matches.front( ).pid;
        append_log( state , "[auto] Process appeared — injecting PID " + std::to_string( matches.front( ).pid ) );
        launch_injection( state );
    }

    void poll_watch_folder( gui_app_state& state )
    {
        if ( state.config.watch_folder.empty( ) )
        {
            return;
        }

        state.watch_folder_poll_timer += ImGui::GetIO( ).DeltaTime;

        if ( state.watch_folder_poll_timer < 0.5f )
        {
            return;
        }

        state.watch_folder_poll_timer = 0.0f;

        try
        {
            std::filesystem::path newest;
            std::filesystem::file_time_type newest_time {};

            for ( const auto& entry : std::filesystem::directory_iterator( state.config.watch_folder ) )
            {
                if ( !entry.is_regular_file( ) )
                {
                    continue;
                }

                if ( _wcsicmp( entry.path( ).extension( ).c_str( ) , L".dll" ) != 0 )
                {
                    continue;
                }

                const auto write_time = entry.last_write_time( );

                if ( newest.empty( ) || write_time > newest_time )
                {
                    newest = entry.path( );
                    newest_time = write_time;
                }
            }

            if ( !newest.empty( ) && _wcsicmp( newest.wstring( ).c_str( ) , state.last_watched_dll.c_str( ) ) != 0 )
            {
                state.last_watched_dll = newest.wstring( );
                std::strncpy( state.dll_path , wide_to_utf8( state.last_watched_dll ).c_str( ) , sizeof( state.dll_path ) - 1 );
                refresh_dll_pe( state );
                append_log( state , "[watch] Auto-selected DLL: " + wide_to_utf8( state.last_watched_dll ) );
                gui_push_toast( state , "Watch folder: new DLL selected" , toast_type::info );
            }
        }
        catch ( ... )
        {
        }
    }

    bool copy_text_to_clipboard( const std::string& text )
    {
        if ( !OpenClipboard( static_cast< HWND >( g_gui_hwnd ) ) )
        {
            return false;
        }

        EmptyClipboard( );

        const size_t bytes = text.size( ) + 1;
        HGLOBAL memory = GlobalAlloc( GMEM_MOVEABLE , bytes );

        if ( !memory )
        {
            CloseClipboard( );
            return false;
        }

        void* locked = GlobalLock( memory );
        std::memcpy( locked , text.c_str( ) , bytes );
        GlobalUnlock( memory );
        SetClipboardData( CF_TEXT , memory );
        CloseClipboard( );
        return true;
    }

    ImVec4 log_color_for_line( const std::string& line , bool light_mode )
    {
        if ( line.find( "[inject]" ) != std::string::npos )
        {
            return light_mode ? ImVec4( 0.10f , 0.35f , 0.75f , 1.0f ) : ImVec4( 0.45f , 0.72f , 1.00f , 1.0f );
        }

        if ( line.find( "[pe]" ) != std::string::npos || line.find( "[map]" ) != std::string::npos )
        {
            return light_mode ? ImVec4( 0.10f , 0.50f , 0.25f , 1.0f ) : ImVec4( 0.45f , 0.90f , 0.55f , 1.0f );
        }

        if ( line.find( "[handle]" ) != std::string::npos || line.find( "[loader]" ) != std::string::npos )
        {
            return light_mode ? ImVec4( 0.55f , 0.35f , 0.05f , 1.0f ) : ImVec4( 1.00f , 0.78f , 0.35f , 1.0f );
        }

        if ( line.find( "failed" ) != std::string::npos || line.find( "Error" ) != std::string::npos )
        {
            return light_mode ? ImVec4( 0.75f , 0.15f , 0.15f , 1.0f ) : ImVec4( 1.00f , 0.45f , 0.45f , 1.0f );
        }

        return ImGui::GetStyleColorVec4( ImGuiCol_Text );
    }

    bool set_stealth_capture( gui_app_state& state , bool enabled )
    {
        std::wstring error;
        const bool applied = apply_capture_stealth( g_gui_hwnd , enabled , &error );

        if ( !applied )
        {
            append_log(
                state ,
                "Stealth capture failed: "
                + ( error.empty( ) ? std::string( "unknown error" ) : wide_to_utf8( error ) ) );
            return false;
        }

        state.stealth_capture = enabled;
        state.config.stealth_capture = enabled;
        save_config( state.config );

        append_log(
            state ,
            enabled
                ? "[stealth] Window hidden from Discord/OBS capture (SetWindowDisplayAffinity)."
                : "[stealth] Window visible to Discord/OBS capture again." );

        return true;
    }

    int search_input_callback( ImGuiInputTextCallbackData* data )
    {
        ( void )data;
        return 0;
    }

    std::string selected_process_label( const gui_app_state& state )
    {
        if ( state.selected_pid <= 0 )
        {
            return {};
        }

        for ( const auto& process : state.all_processes )
        {
            if ( static_cast< int >( process.pid ) == state.selected_pid )
            {
                return wide_to_utf8( process.name ) + " (PID " + std::to_string( process.pid ) + ")";
            }
        }

        return "PID " + std::to_string( state.selected_pid );
    }

    void draw_favorites_strip( gui_app_state& state )
    {
        if ( state.favorite_pids.empty( ) )
        {
            return;
        }

        ImGui::TextDisabled( "Favorites" );
        ImGui::SameLine( );

        for ( const uint32_t pid : state.favorite_pids )
        {
            std::string label = "PID " + std::to_string( pid );

            for ( const auto& process : state.all_processes )
            {
                if ( process.pid == pid )
                {
                    label = wide_to_utf8( process.name );
                    break;
                }
            }

            ImGui::PushID( static_cast< int >( pid ) );

            if ( ImGui::SmallButton( label.c_str( ) ) )
            {
                state.selected_pid = static_cast< int >( pid );
            }

            ImGui::SameLine( );
            ImGui::PopID( );
        }

        ImGui::NewLine( );
    }

    void draw_process_list( gui_app_state& state , const std::vector< process_entry >& visible , float height )
    {
        const auto& tokens = gui_theme_tokens_for( state );
        ImGui::BeginChild( "ProcessList" , ImVec2( -1.0f , height ) , ImGuiChildFlags_None );

        const float content_width = ImGui::GetContentRegionAvail( ).x;
        const float row_height = tokens.row_height;
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImU32 text_color = ImGui::GetColorU32( ImGuiCol_Text );
        const ImU32 header_bg = ImGui::GetColorU32( state.light_mode ? ImVec4( 0.92f , 0.93f , 0.95f , 1.0f ) : ImVec4( 0.11f , 0.12f , 0.14f , 1.0f ) );

        const ImVec2 header_min = ImGui::GetCursorScreenPos( );
        draw->AddRectFilled( header_min , ImVec2( header_min.x + content_width , header_min.y + row_height ) , header_bg );
        const float header_text_y = header_min.y + ( row_height - ImGui::GetTextLineHeight( ) ) * 0.5f;
        draw->AddText( ImVec2( header_min.x + 40.0f , header_text_y ) , text_color , "Process" );
        draw->AddText( ImVec2( header_min.x + content_width - 120.0f , header_text_y ) , text_color , "PID / Arch" );
        ImGui::Dummy( ImVec2( 0.0f , row_height ) );

        if ( visible.empty( ) )
        {
            ImGui::Spacing( );
            ImGui::TextDisabled( "No matching processes. Press F5 to refresh." );
            ImGui::EndChild( );
            return;
        }

        ImGuiListClipper clipper;
        clipper.Begin( static_cast< int >( visible.size( ) ) , row_height );

        while ( clipper.Step( ) )
        {
            for ( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
            {
                process_entry process = visible [ static_cast< size_t >( row ) ];
                enrich_process_details( process );

                ImGui::PushID( process.pid );

                const float indent = state.show_process_tree && process.parent_pid > 0 ? 16.0f : 0.0f;
                const ImVec2 row_min = ImGui::GetCursorScreenPos( );
                ImGui::SetCursorScreenPos( ImVec2( row_min.x + indent , row_min.y ) );
                ImGui::InvisibleButton( "##row" , ImVec2( content_width - indent , row_height ) );

                const bool hovered = ImGui::IsItemHovered( );
                const bool clicked = ImGui::IsItemClicked( ImGuiMouseButton_Left );
                const bool right_clicked = ImGui::IsItemClicked( ImGuiMouseButton_Right );
                const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
                const ImVec2 item_min = ImGui::GetItemRectMin( );
                const ImVec2 item_max = ImGui::GetItemRectMax( );
                const bool selected = state.selected_pid == static_cast< int >( process.pid );
                const bool focused = state.list_focus == row;

                if ( selected )
                {
                    draw->AddRectFilled( item_min , item_max , ImGui::GetColorU32( ImGuiCol_Header ) );
                    draw->AddRectFilled( ImVec2( item_min.x , item_min.y ) , ImVec2( item_min.x + 3.0f , item_max.y ) , ImGui::GetColorU32( gui_theme_accent( state.config.accent_index ) ) );
                }
                else if ( focused || hovered )
                {
                    draw->AddRectFilled( item_min , item_max , ImGui::GetColorU32( ImGuiCol_HeaderHovered ) );
                }
                else if ( row % 2 == 1 )
                {
                    draw->AddRectFilled( item_min , item_max , ImGui::GetColorU32( ImGuiCol_TableRowBgAlt ) );
                }

                const float text_y = item_min.y + ( item_max.y - item_min.y - ImGui::GetTextLineHeight( ) ) * 0.5f;
                const char letter = avatar_letter( process.name );
                const ImVec4 accent = gui_theme_accent( state.config.accent_index );
                draw->AddCircleFilled( ImVec2( item_min.x + 16.0f , item_min.y + row_height * 0.5f ) , 12.0f , ImGui::GetColorU32( ImVec4( accent.x , accent.y , accent.z , 0.35f ) ) );
                char letter_buf [ 2 ] = { letter , '\0' };
                draw->AddText( ImVec2( item_min.x + 11.0f , text_y ) , text_color , letter_buf );

                const std::string name_utf8 = wide_to_utf8( process.name );
                draw->AddText( ImVec2( item_min.x + 36.0f , text_y ) , text_color , name_utf8.c_str( ) );

                const std::string meta = std::to_string( process.pid ) + "  " + ( process.is_wow64 ? "x86" : "x64" );
                const ImVec2 meta_size = ImGui::CalcTextSize( meta.c_str( ) );
                draw->AddText( ImVec2( item_max.x - meta_size.x - 8.0f , text_y ) , ImGui::GetColorU32( ImGuiCol_TextDisabled ) , meta.c_str( ) );

                if ( clicked )
                {
                    state.list_focus = row;
                    state.selected_pid = static_cast< int >( process.pid );
                }

                if ( right_clicked )
                {
                    toggle_favorite( state , process.pid );
                }

                if ( double_clicked )
                {
                    state.list_focus = row;
                    state.selected_pid = static_cast< int >( process.pid );
                    start_injection( state );
                }

                ImGui::PopID( );
            }
        }

        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && !ImGui::IsAnyItemActive( ) )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) && state.list_focus > 0 )
            {
                --state.list_focus;
            }

            if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) && state.list_focus + 1 < static_cast< int >( visible.size( ) ) )
            {
                ++state.list_focus;
            }

            if ( state.list_focus >= 0 && state.list_focus < static_cast< int >( visible.size( ) ) )
            {
                const auto& focused_process = visible [ static_cast< size_t >( state.list_focus ) ];
                state.selected_pid = static_cast< int >( focused_process.pid );
            }
        }

        ImGui::EndChild( );
    }

    void draw_injection_summary( gui_app_state& state )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg , gui_theme_accent_muted( state.config.accent_index , state.light_mode ) );
        ImGui::BeginChild( "InjectSummary" , ImVec2( -1.0f , 88.0f ) , ImGuiChildFlags_None );
        ImGui::TextUnformatted( "Injection Summary" );
        ImGui::Text( "Target: %s" , state.selected_pid > 0 ? selected_process_label( state ).c_str( ) : ( state.search [ 0 ] ? state.search : "None" ) );

        bool arch_match = true;

        if ( state.dll_pe.valid && state.selected_pid > 0 )
        {
            for ( const auto& process : state.all_processes )
            {
                if ( static_cast< int >( process.pid ) == state.selected_pid )
                {
                    const bool dll_x64 = state.dll_pe.is_64bit;
                    const bool proc_x64 = !process.is_wow64;
                    arch_match = dll_x64 == proc_x64;
                    ImGui::TextColored( arch_match ? ImVec4( 0.3f , 0.85f , 0.45f , 1.0f ) : ImVec4( 0.95f , 0.35f , 0.35f , 1.0f ) , "Arch match: %s" , arch_match ? "Yes" : "No" );
                    break;
                }
            }
        }

        ImGui::TextWrapped( "DLL: %s" , state.dll_path [ 0 ] ? state.dll_path : "(none)" );
        ImGui::EndChild( );
        ImGui::PopStyleColor( );
        ImGui::Spacing( );
    }

    void draw_recent_dll_cards( gui_app_state& state )
    {
        if ( state.config.recent_dlls.empty( ) )
        {
            return;
        }

        ImGui::TextUnformatted( "Recent Payloads" );
        ImGui::Spacing( );

        const std::wstring current_dll = utf8_to_wide( state.dll_path );

        for ( int idx = 0; idx < static_cast< int >( state.config.recent_dlls.size( ) ); ++idx )
        {
            const auto& dll_path = state.config.recent_dlls [ static_cast< size_t >( idx ) ];
            const std::filesystem::path path_obj( dll_path );
            const std::string filename = wide_to_utf8( path_obj.filename( ).wstring( ) );
            const bool is_active = !current_dll.empty( ) && _wcsicmp( current_dll.c_str( ) , dll_path.c_str( ) ) == 0;
            const pe_info pe = analyze_pe_file( dll_path );

            ImGui::PushID( idx );
            const float card_width = ImGui::GetContentRegionAvail( ).x;
            const float card_height = 56.0f;
            const ImVec2 card_pos = ImGui::GetCursorScreenPos( );

            ImGui::InvisibleButton( "##dll_card" , ImVec2( card_width - 36.0f , card_height ) );

            if ( ImGui::IsItemClicked( ) )
            {
                std::strncpy( state.dll_path , wide_to_utf8( dll_path ).c_str( ) , sizeof( state.dll_path ) - 1 );
                refresh_dll_pe( state );
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList( );
            const ImU32 bg = ImGui::GetColorU32( is_active ? gui_theme_accent_muted( state.config.accent_index , state.light_mode ) : ImGui::GetStyleColorVec4( ImGuiCol_ChildBg ) );
            draw_list->AddRectFilled( card_pos , ImVec2( card_pos.x + card_width - 36.0f , card_pos.y + card_height ) , bg , 6.0f );
            draw_list->AddText( ImVec2( card_pos.x + 10.0f , card_pos.y + 8.0f ) , ImGui::GetColorU32( ImGuiCol_Text ) , filename.c_str( ) );

            if ( pe.valid )
            {
                const std::string badge = wide_to_utf8( pe.machine_label );
                draw_list->AddText( ImVec2( card_pos.x + card_width - 90.0f , card_pos.y + 10.0f ) , ImGui::GetColorU32( gui_theme_accent( state.config.accent_index ) ) , badge.c_str( ) );
            }

            ImGui::SameLine( 0.0f , 0.0f );
            ImGui::SetCursorScreenPos( ImVec2( card_pos.x + card_width - 32.0f , card_pos.y + 12.0f ) );

            if ( ImGui::Button( "X" , ImVec2( 28.0f , 0.0f ) ) )
            {
                remove_recent_dll( state.config , dll_path );
                save_config( state.config );
            }

            ImGui::Dummy( ImVec2( 0.0f , 4.0f ) );
            ImGui::PopID( );
        }

        ImGui::Spacing( );
    }

    void draw_dll_queue( gui_app_state& state )
    {
        ImGui::TextUnformatted( "DLL Queue" );
        ImGui::SetNextItemWidth( -80.0f );
        ImGui::InputText( "##queue_input" , state.queue_dll_input , sizeof( state.queue_dll_input ) );
        ImGui::SameLine( );

        if ( ImGui::Button( "Add" ) && state.queue_dll_input [ 0 ] )
        {
            state.config.dll_queue.push_back( utf8_to_wide( state.queue_dll_input ) );
            state.inject_queue = state.config.dll_queue;
            save_config( state.config );
            state.queue_dll_input [ 0 ] = '\0';
        }

        for ( size_t idx = 0; idx < state.config.dll_queue.size( ); ++idx )
        {
            ImGui::PushID( static_cast< int >( idx ) );
            ImGui::BulletText( "%s" , wide_to_utf8( state.config.dll_queue [ idx ] ).c_str( ) );
            ImGui::SameLine( );

            if ( ImGui::SmallButton( "Remove" ) )
            {
                state.config.dll_queue.erase( state.config.dll_queue.begin( ) + static_cast< ptrdiff_t >( idx ) );
                state.inject_queue = state.config.dll_queue;
                save_config( state.config );
            }

            ImGui::PopID( );
        }
    }

    void draw_pe_preview( gui_app_state& state )
    {
        if ( !state.dll_path [ 0 ] )
        {
            return;
        }

        if ( !state.dll_pe.valid )
        {
            ImGui::TextColored( ImVec4( 0.9f , 0.35f , 0.35f , 1.0f ) , "PE: %s" , wide_to_utf8( state.dll_pe.error ).c_str( ) );
            return;
        }

        ImGui::Text( "PE: %s | Hash: %s" , wide_to_utf8( state.dll_pe.machine_label ).c_str( ) , state.dll_pe.hash_hex.c_str( ) );
    }

    bool export_log( gui_app_state& state );

    void draw_log_panel( gui_app_state& state , float height )
    {
        ImGui::TextUnformatted( "Output Log" );
        ImGui::SetNextItemWidth( 160.0f );
        ImGui::InputTextWithHint( "##logfilter" , "Filter tags..." , state.log_filter , sizeof( state.log_filter ) );

        if ( state.focus_log_filter_next_frame )
        {
            ImGui::SetKeyboardFocusHere( -1 );
            state.focus_log_filter_next_frame = false;
        }
        gui_draw_help_marker( "Shows only log lines containing the entered text." );
        ImGui::SameLine( );

        if ( ImGui::Button( "Copy Log" ) )
        {
            std::string text;
            {
                std::lock_guard lock( state.log_mutex );
                text = state.log;
            }

            if ( copy_text_to_clipboard( text ) )
            {
                append_log( state , "Log copied to clipboard." );
            }
        }

        std::string log_copy;
        {
            std::lock_guard lock( state.log_mutex );
            log_copy = state.log;
        }

        ImGui::BeginChild( "LogScroll" , ImVec2( -1.0f , height ) , ImGuiChildFlags_None );

        if ( state.font_mono )
        {
            ImGui::PushFont( state.font_mono );
        }

        std::istringstream stream( log_copy );
        std::string line;
        const float scroll_y_before = ImGui::GetScrollY( );
        const float scroll_max_before = ImGui::GetScrollMaxY( );
        const bool was_at_bottom = scroll_max_before <= 0.0f || scroll_y_before >= scroll_max_before - 4.0f;

        while ( std::getline( stream , line ) )
        {
            if ( state.log_filter [ 0 ] && line.find( state.log_filter ) == std::string::npos )
            {
                continue;
            }

            ImGui::PushStyleColor( ImGuiCol_Text , log_color_for_line( line , state.light_mode ) );
            ImGui::TextWrapped( "%s" , line.c_str( ) );
            ImGui::PopStyleColor( );
        }

        if ( state.font_mono )
        {
            ImGui::PopFont( );
        }

        if ( state.log_follow_tail && was_at_bottom )
        {
            ImGui::SetScrollHereY( 1.0f );
        }
        else if ( !state.log_follow_tail && ImGui::GetScrollMaxY( ) > 0.0f )
        {
            ImGui::SetCursorScreenPos( ImVec2( ImGui::GetWindowPos( ).x + ImGui::GetWindowWidth( ) - 120.0f , ImGui::GetWindowPos( ).y + 8.0f ) );

            if ( ImGui::SmallButton( "Jump to bottom" ) )
            {
                state.log_follow_tail = true;
            }
        }

        if ( ImGui::GetScrollY( ) < ImGui::GetScrollMaxY( ) - 4.0f && was_at_bottom == false )
        {
            state.log_follow_tail = false;
        }

        ImGui::EndChild( );
    }

    void draw_injection_page( gui_app_state& state , ImGuiIO& io )
    {
        const auto& tokens = gui_theme_tokens_for( state );
        const float left_width = ( io.DisplaySize.x - tokens.sidebar_width ) * state.panel_split;

        ImGui::BeginChild( "LeftPanel" , ImVec2( left_width , -tokens.status_bar_height - 52.0f ) , ImGuiChildFlags_None );
        draw_injection_summary( state );

        if ( gui_begin_section_card( "TargetCard" , "Target Process" , true , nullptr ) )
        {
            ImGui::SetNextItemWidth( -240.0f );
            ImGui::InputTextWithHint( "##search" , "Search by name or PID..." , state.search , sizeof( state.search ) , ImGuiInputTextFlags_CallbackEdit , search_input_callback );

            if ( state.focus_search_next_frame )
            {
                ImGui::SetKeyboardFocusHere( -1 );
                state.focus_search_next_frame = false;
            }

            ImGui::SameLine( );
            const char* sort_labels [ ] = { "Name" , "PID" , "Session" };
            int sort_index = static_cast< int >( state.sort_mode );
            ImGui::SetNextItemWidth( 90.0f );

            if ( ImGui::Combo( "##sort" , &sort_index , sort_labels , IM_ARRAYSIZE( sort_labels ) ) )
            {
                state.sort_mode = static_cast< process_sort_mode >( sort_index );
            }

            ImGui::SameLine( );

            if ( ImGui::Button( "Refresh" , ImVec2( -1.0f , 0.0f ) ) )
            {
                refresh_processes( state );
            }

            if ( gui_draw_pill_toggle( state , "tree_toggle" , &state.show_process_tree , "Tree" , "Flat" , "Group processes by parent PID with indent." ) )
            {
                state.config.show_process_tree = state.show_process_tree;
                save_config( state.config );
            }

            draw_favorites_strip( state );
            const auto visible = visible_processes( state );
            ImGui::TextDisabled( "%d process(es) shown" , static_cast< int >( visible.size( ) ) );
            draw_process_list( state , visible , io.DisplaySize.y * 0.28f );
            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "PayloadCard" , "DLL Payload" , true , nullptr ) )
        {
            draw_recent_dll_cards( state );
            ImGui::SetNextItemWidth( -90.0f );
            ImGui::InputText( "##dll_path" , state.dll_path , sizeof( state.dll_path ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                refresh_dll_pe( state );
            }

            ImGui::SameLine( );

            if ( ImGui::Button( "Browse" , ImVec2( -1.0f , 0.0f ) ) )
            {
                const auto selected = pick_dll_path( utf8_to_wide( state.dll_path ) );

                if ( !selected.empty( ) )
                {
                    std::strncpy( state.dll_path , wide_to_utf8( selected ).c_str( ) , sizeof( state.dll_path ) - 1 );
                    refresh_dll_pe( state );
                }
            }

            draw_dll_queue( state );
            draw_pe_preview( state );
            gui_end_section_card( );
        }

        ImGui::EndChild( );

        ImGui::SameLine( );
        ImGui::InvisibleButton( "##splitter" , ImVec2( 4.0f , -tokens.status_bar_height - 52.0f ) );

        if ( ImGui::IsItemActive( ) )
        {
            const float total = io.DisplaySize.x - tokens.sidebar_width;
            state.panel_split = ( std::clamp )( state.panel_split + io.MouseDelta.x / total , 0.28f , 0.62f );
            state.config.panel_split = state.panel_split;
        }

        if ( ImGui::IsItemHovered( ) || ImGui::IsItemActive( ) )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
        }

        ImGui::SameLine( );
        ImGui::BeginChild( "RightPanel" , ImVec2( 0.0f , -tokens.status_bar_height - 52.0f ) , ImGuiChildFlags_None );
        draw_log_panel( state , ImGui::GetContentRegionAvail( ).y );
        ImGui::EndChild( );

        const bool busy = state.injecting.load( );

        if ( busy )
        {
            ImGui::BeginDisabled( );
        }

        ImGui::PushStyleColor( ImGuiCol_Button , gui_theme_accent( state.config.accent_index ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding , ImVec2( 18.0f , 10.0f ) );

        if ( ImGui::Button( "Inject" , ImVec2( 160.0f , 0.0f ) ) )
        {
            start_injection( state );
        }

        ImGui::PopStyleVar( );
        ImGui::PopStyleColor( );

        if ( busy )
        {
            ImGui::EndDisabled( );
            ImGui::SameLine( );
            ImGui::ProgressBar( state.inject_progress , ImVec2( 180.0f , 0.0f ) );
        }

        ImGui::SameLine( );

        if ( ImGui::Button( "Run as Admin" ) )
        {
            if ( relaunch_as_admin( L"--gui" ) )
            {
                PostMessageW( static_cast< HWND >( g_gui_hwnd ) , WM_CLOSE , 0 , 0 );
            }
        }

        ImGui::SameLine( );

        if ( ImGui::Button( "Clear Log" ) )
        {
            std::lock_guard lock( state.log_mutex );
            state.log.clear( );
        }

        ImGui::SameLine( );

        if ( ImGui::Button( "Export Log" ) )
        {
            export_log( state );
        }
    }

    void draw_history_page( gui_app_state& state )
    {
        const auto& tokens = gui_theme_tokens_for( state );
        ImGui::BeginChild( "HistoryPage" , ImVec2( 0.0f , -tokens.status_bar_height ) , ImGuiChildFlags_None );
        ImGui::TextUnformatted( "Injection History" );
        ImGui::Separator( );

        if ( state.config.injection_history.empty( ) )
        {
            ImGui::TextDisabled( "No injection history yet." );
        }

        for ( size_t idx = 0; idx < state.config.injection_history.size( ); ++idx )
        {
            const auto& entry = state.config.injection_history [ idx ];
            ImGui::PushID( static_cast< int >( idx ) );
            ImGui::TextColored( entry.success ? ImVec4( 0.3f , 0.85f , 0.45f , 1.0f ) : ImVec4( 0.95f , 0.35f , 0.35f , 1.0f ) , entry.success ? "[OK]" : "[FAIL]" );
            ImGui::SameLine( );
            ImGui::Text( "%s | %s | %s" , wide_to_utf8( entry.timestamp ).c_str( ) , wide_to_utf8( entry.target ).c_str( ) , wide_to_utf8( entry.dll ).c_str( ) );
            ImGui::SameLine( );

            if ( ImGui::SmallButton( "Re-inject" ) )
            {
                std::strncpy( state.dll_path , wide_to_utf8( entry.dll ).c_str( ) , sizeof( state.dll_path ) - 1 );
                refresh_dll_pe( state );
                state.current_page = gui_page::injection;
                start_injection( state );
            }

            ImGui::PopID( );
        }

        ImGui::EndChild( );
    }

    void sync_process_rules( gui_app_state& state )
    {
        state.config.process_rules.clear( );
        std::wstring text = utf8_to_wide( state.process_rules_text );
        size_t start = 0;

        while ( start < text.size( ) )
        {
            const auto end = text.find_first_of( L"\r\n;,|" , start );
            const auto token = text.substr( start , end == std::wstring::npos ? std::wstring::npos : end - start );

            if ( !token.empty( ) )
            {
                state.config.process_rules.push_back( token );
            }

            if ( end == std::wstring::npos )
            {
                break;
            }

            start = end + 1;
        }
    }

    void draw_settings_page( gui_app_state& state )
    {
        const auto& tokens = gui_theme_tokens_for( state );
        ImGui::BeginChild( "SettingsScroll" , ImVec2( 0.0f , -tokens.status_bar_height ) , ImGuiChildFlags_None );

        if ( gui_begin_section_card( "AppearanceSection" , "Appearance" , true , &state.config.settings_appearance_open ) )
        {
            if ( gui_draw_pill_toggle( state , "theme_toggle" , &state.light_mode , "Light" , "Dark" , "Switch between light and dark interface themes." ) )
            {
                state.config.light_mode = state.light_mode;
                gui_theme_apply( state );
                save_config( state.config );
            }

            ImGui::TextUnformatted( "Accent color" );

            for ( int idx = 0; idx < 4; ++idx )
            {
                ImGui::PushID( idx );
                ImGui::PushStyleColor( ImGuiCol_Button , gui_theme_accent( idx ) );

                if ( ImGui::Button( "##accent" , ImVec2( 28.0f , 28.0f ) ) )
                {
                    state.config.accent_index = idx;
                    gui_theme_apply( state );
                    save_config( state.config );
                }

                ImGui::PopStyleColor( );

                if ( state.config.accent_index == idx )
                {
                    ImGui::GetWindowDrawList( )->AddRect( ImGui::GetItemRectMin( ) , ImGui::GetItemRectMax( ) , IM_COL32( 255 , 255 , 255 , 255 ) , 4.0f );
                }

                ImGui::SameLine( );
                ImGui::PopID( );
            }

            ImGui::NewLine( );

            if ( gui_draw_pill_toggle( state , "compact_toggle" , &state.config.compact_mode , "Compact" , "Comfortable" , "Tighter spacing and smaller fonts." ) )
            {
                gui_theme_init( state );
                save_config( state.config );
            }

            if ( gui_draw_pill_toggle( state , "tray_toggle" , &state.config.min_to_tray , "Min to tray" , "Minimize normally" , "Minimize button sends app to system tray." ) )
            {
                save_config( state.config );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "CaptureSection" , "Capture" , true , &state.config.settings_capture_open ) )
        {
            const bool supported = capture_stealth_supported( );

            if ( !supported )
            {
                ImGui::BeginDisabled( );
            }

            if ( gui_draw_pill_toggle( state , "stealth_toggle" , &state.stealth_capture , "Stealth" , "Visible" , "Capture visibility for Discord/OBS." ) )
            {
                set_stealth_capture( state , state.stealth_capture );
            }

            if ( !supported )
            {
                ImGui::EndDisabled( );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "InjectionSection" , "Injection" , true , &state.config.settings_injection_open ) )
        {
            if ( ImGui::Checkbox( "Confirm before inject" , &state.confirm_inject ) )
            {
                state.config.confirm_inject = state.confirm_inject;
                save_config( state.config );
            }

            ImGui::Checkbox( "Wait up to 30 seconds for process" , &state.wait_for_process );
            ImGui::Checkbox( "Inject all matching instances" , &state.inject_all );
            ImGui::Checkbox( "Auto-inject when process appears" , &state.auto_inject );
            ImGui::SetNextItemWidth( 120.0f );
            ImGui::InputInt( "Delay before inject (seconds)" , &state.inject_delay_sec );
            state.inject_delay_sec = ( std::max )( 0 , state.inject_delay_sec );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputText( "Watch folder" , state.watch_folder_text , sizeof( state.watch_folder_text ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                state.config.watch_folder = utf8_to_wide( state.watch_folder_text );
                save_config( state.config );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "LoggingSection" , "Logging" , true , &state.config.settings_logging_open ) )
        {
            if ( ImGui::Checkbox( "Log timestamps" , &state.log_timestamps ) )
            {
                state.config.log_timestamps = state.log_timestamps;
                save_config( state.config );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "SafetySection" , "Safety" , true , &state.config.settings_safety_open ) )
        {
            if ( ImGui::Checkbox( "Allowlist mode (off = blocklist)" , &state.use_allowlist ) )
            {
                state.config.use_allowlist = state.use_allowlist;
                save_config( state.config );
            }

            ImGui::InputTextMultiline( "Process rules (one per line)" , state.process_rules_text , sizeof( state.process_rules_text ) , ImVec2( -1.0f , 72.0f ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                sync_process_rules( state );
                save_config( state.config );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "ProfilesSection" , "Profiles" , true , &state.config.settings_profiles_open ) )
        {
            ImGui::InputText( "Profile name" , state.new_profile_name , sizeof( state.new_profile_name ) );
            ImGui::SameLine( );

            if ( ImGui::Button( "Save current as profile" ) && state.new_profile_name [ 0 ] )
            {
                inject_profile profile {};
                profile.name = utf8_to_wide( state.new_profile_name );
                profile.dll_path = utf8_to_wide( state.dll_path );
                profile.process_name = utf8_to_wide( state.search );
                profile.wait_for_process = state.wait_for_process;
                profile.inject_all = state.inject_all;
                profile.inject_delay_sec = state.inject_delay_sec;
                state.config.profiles.push_back( profile );
                save_config( state.config );
                gui_push_toast( state , "Profile saved" , toast_type::success );
            }

            for ( size_t idx = 0; idx < state.config.profiles.size( ); ++idx )
            {
                const auto& profile = state.config.profiles [ idx ];
                ImGui::PushID( static_cast< int >( idx ) );
                ImGui::BulletText( "%s" , wide_to_utf8( profile.name ).c_str( ) );
                ImGui::SameLine( );

                if ( ImGui::SmallButton( "Load" ) )
                {
                    std::strncpy( state.dll_path , wide_to_utf8( profile.dll_path ).c_str( ) , sizeof( state.dll_path ) - 1 );
                    std::strncpy( state.search , wide_to_utf8( profile.process_name ).c_str( ) , sizeof( state.search ) - 1 );
                    state.wait_for_process = profile.wait_for_process;
                    state.inject_all = profile.inject_all;
                    state.inject_delay_sec = profile.inject_delay_sec;
                    refresh_dll_pe( state );
                }

                ImGui::SameLine( );

                if ( ImGui::SmallButton( "Delete" ) )
                {
                    state.config.profiles.erase( state.config.profiles.begin( ) + static_cast< ptrdiff_t >( idx ) );
                    save_config( state.config );
                }

                ImGui::PopID( );
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "AdvancedSection" , "Advanced" , true , &state.config.settings_advanced_open ) )
        {
            ImGui::InputText( "CLI notes (logged on inject)" , state.cli_notes , sizeof( state.cli_notes ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                state.config.cli_notes = utf8_to_wide( state.cli_notes );
                save_config( state.config );
            }

            if ( ImGui::Button( "Export Settings" , ImVec2( 140.0f , 0.0f ) ) )
            {
                wchar_t path [ MAX_PATH ] = L"manual_map_settings.ini";
                OPENFILENAMEW dialog {};
                dialog.lStructSize = sizeof( dialog );
                dialog.hwndOwner = static_cast< HWND >( g_gui_hwnd );
                dialog.lpstrFilter = L"INI Files (*.ini)\0*.ini\0All Files (*.*)\0*.*\0";
                dialog.lpstrFile = path;
                dialog.nMaxFile = MAX_PATH;
                dialog.Flags = OFN_OVERWRITEPROMPT;
                dialog.lpstrDefExt = L"ini";

                if ( GetSaveFileNameW( &dialog ) )
                {
                    sync_process_rules( state );
                    save_config_to_path( path , state.config );
                    append_log( state , "Settings exported." );
                }
            }

            ImGui::SameLine( );

            if ( ImGui::Button( "Import Settings" , ImVec2( 140.0f , 0.0f ) ) )
            {
                wchar_t path [ MAX_PATH ] = {};
                OPENFILENAMEW dialog {};
                dialog.lStructSize = sizeof( dialog );
                dialog.hwndOwner = static_cast< HWND >( g_gui_hwnd );
                dialog.lpstrFilter = L"INI Files (*.ini)\0*.ini\0All Files (*.*)\0*.*\0";
                dialog.lpstrFile = path;
                dialog.nMaxFile = MAX_PATH;
                dialog.Flags = OFN_FILEMUSTEXIST;

                if ( GetOpenFileNameW( &dialog ) )
                {
                    if ( load_config_from_path( path , state.config ) )
                    {
                        gui_state_init( state );
                        gui_theme_apply( state );
                        append_log( state , "Settings imported." );
                    }
                }
            }

            gui_end_section_card( );
        }

        if ( gui_begin_section_card( "AboutSection" , "About" , true , nullptr ) )
        {
            ImGui::TextUnformatted( "Manual Map Injector v2.0" );
            ImGui::TextDisabled( "Build: GUI overhaul with profiles, history, tray, command palette" );
            gui_end_section_card( );
        }

        ImGui::EndChild( );
    }

    bool export_log( gui_app_state& state )
    {
        std::string text;
        {
            std::lock_guard lock( state.log_mutex );
            text = state.log;
        }

        if ( text.empty( ) )
        {
            append_log( state , "Log is empty — nothing to export." );
            return false;
        }

        wchar_t path [ MAX_PATH ] = L"manual_map_log.txt";
        OPENFILENAMEW dialog {};
        dialog.lStructSize = sizeof( dialog );
        dialog.hwndOwner = static_cast< HWND >( g_gui_hwnd );
        dialog.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        dialog.lpstrDefExt = L"txt";
        dialog.lpstrTitle = L"Export Log";

        if ( !GetSaveFileNameW( &dialog ) )
        {
            return false;
        }

        std::ofstream file( path , std::ios::binary );

        if ( !file.is_open( ) )
        {
            append_log( state , "Failed to write log file." );
            return false;
        }

        file.write( text.data( ) , static_cast< std::streamsize >( text.size( ) ) );
        append_log( state , "Log exported to " + wide_to_utf8( path ) );
        return true;
    }

    void draw_confirm_popup( gui_app_state& state )
    {
        if ( state.show_confirm_popup )
        {
            ImGui::OpenPopup( "Confirm Injection" );
            state.show_confirm_popup = false;
        }

        if ( ImGui::BeginPopupModal( "Confirm Injection" , nullptr , ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Proceed with manual map injection?" );
            ImGui::Text( "DLL: %s" , state.dll_path );

            if ( state.selected_pid > 0 )
            {
                ImGui::Text( "Target PID: %d" , state.selected_pid );
            }
            else
            {
                ImGui::Text( "Target name: %s" , state.search );
            }

            if ( ImGui::Button( "Inject" , ImVec2( 120.0f , 0.0f ) ) )
            {
                launch_injection( state );
                ImGui::CloseCurrentPopup( );
            }

            ImGui::SameLine( );

            if ( ImGui::Button( "Cancel" , ImVec2( 120.0f , 0.0f ) ) )
            {
                ImGui::CloseCurrentPopup( );
            }

            ImGui::EndPopup( );
        }
    }
}

void gui_state_init( gui_app_state& state )
{
    g_state = &state;
    load_config( state.config );

    state.confirm_inject = state.config.confirm_inject;
    state.log_timestamps = state.config.log_timestamps;
    state.use_allowlist = state.config.use_allowlist;
    state.panel_split = state.config.panel_split;
    state.stealth_capture = state.config.stealth_capture;
    state.light_mode = state.config.light_mode;
    state.show_process_tree = state.config.show_process_tree;
    state.favorite_pids = state.config.favorite_pids;
    state.inject_queue = state.config.dll_queue;
    state.show_wizard = !state.config.first_run_complete;

    std::strncpy( state.cli_notes , wide_to_utf8( state.config.cli_notes ).c_str( ) , sizeof( state.cli_notes ) - 1 );
    std::strncpy( state.watch_folder_text , wide_to_utf8( state.config.watch_folder ).c_str( ) , sizeof( state.watch_folder_text ) - 1 );

    std::wstring rules;

    for ( const auto& rule : state.config.process_rules )
    {
        if ( !rules.empty( ) )
        {
            rules += L"\n";
        }

        rules += rule;
    }

    std::strncpy( state.process_rules_text , wide_to_utf8( rules ).c_str( ) , sizeof( state.process_rules_text ) - 1 );

    if ( !state.config.last_dll.empty( ) )
    {
        std::strncpy( state.dll_path , wide_to_utf8( state.config.last_dll ).c_str( ) , sizeof( state.dll_path ) - 1 );
        refresh_dll_pe( state );
    }

    refresh_processes( state );
    append_log( state , "Manual Map Injector ready." );

    if ( state.stealth_capture )
    {
        if ( !set_stealth_capture( state , true ) )
        {
            state.stealth_capture = false;
            state.config.stealth_capture = false;
        }
    }

    if ( !is_process_elevated( ) )
    {
        append_log( state , "Note: not running as administrator — some targets may fail." );
        state.status = "Ready (not elevated)";
    }
}

void gui_state_shutdown( gui_app_state& state )
{
    state.config.favorite_pids = state.favorite_pids;
    state.config.dll_queue = state.inject_queue.empty( ) ? state.config.dll_queue : state.inject_queue;
    save_config( state.config );
    g_state = nullptr;
}

void gui_state_on_resize( gui_app_state& state , unsigned int width , unsigned int height )
{
    ( void )state;
    ( void )width;
    ( void )height;
}

void gui_state_on_drag_enter( gui_app_state& state )
{
    state.drag_highlight_timer = 1.0f;
    gui_push_toast( state , "DLL loaded from drop" , toast_type::success );
}

bool gui_state_handle_shortcuts( gui_app_state& state )
{
    const ImGuiIO& io = ImGui::GetIO( );

    if ( io.WantCaptureKeyboard && !io.KeyCtrl )
    {
        if ( !state.command_palette_open )
        {
            return false;
        }
    }

    if ( ImGui::IsKeyPressed( ImGuiKey_F5 ) )
    {
        refresh_processes( state );
        return true;
    }

    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_F ) )
    {
        state.focus_search_next_frame = true;
        state.current_page = gui_page::injection;
        return true;
    }

    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_L ) )
    {
        state.focus_log_filter_next_frame = true;
        state.current_page = gui_page::injection;
        return true;
    }

    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_K ) )
    {
        state.command_palette_open = true;
        return true;
    }

    if ( ImGui::IsKeyPressed( ImGuiKey_Enter ) && !io.WantTextInput && state.current_page == gui_page::injection )
    {
        start_injection( state );
        return true;
    }

    return false;
}

void gui_state_new_frame( gui_app_state& state )
{
    flush_pending_log( state );
    poll_injection( state );
    poll_auto_inject( state );
    poll_watch_folder( state );
    gui_state_handle_shortcuts( state );

    if ( state.pending_palette_refresh )
    {
        refresh_processes( state );
        state.pending_palette_refresh = false;
    }

    if ( state.pending_palette_inject )
    {
        start_injection( state );
        state.pending_palette_inject = false;
    }
}

void gui_state_set_dll_path( gui_app_state& state , const wchar_t* path )
{
    if ( !path || !path [ 0 ] )
    {
        return;
    }

    std::strncpy( state.dll_path , wide_to_utf8( path ).c_str( ) , sizeof( state.dll_path ) - 1 );
    refresh_dll_pe( state );
    append_log( state , "[payload] DLL set via drag-and-drop." );
}

void gui_state_save_window( gui_app_state& state , void* hwnd )
{
    RECT rect {};
    GetWindowRect( static_cast< HWND >( hwnd ) , &rect );
    state.config.window_x = rect.left;
    state.config.window_y = rect.top;
    state.config.window_w = rect.right - rect.left;
    state.config.window_h = rect.bottom - rect.top;
    state.config.panel_split = state.panel_split;
    save_config( state.config );
}

void gui_state_render( gui_app_state& state )
{
    ImGuiIO& io = ImGui::GetIO( );
    const auto& tokens = gui_theme_tokens_for( state );

    ImGui::SetNextWindowPos( ImVec2( 0.0f , 0.0f ) );
    ImGui::SetNextWindowSize( io.DisplaySize );
    ImGui::Begin(
        "ManualMapInjector" ,
        nullptr ,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus );

    gui_draw_title_bar( state , g_gui_hwnd );
    gui_draw_sidebar( state );

    ImGui::BeginChild( "MainContent" , ImVec2( 0.0f , -tokens.status_bar_height ) , ImGuiChildFlags_None );

    switch ( state.current_page )
    {
    case gui_page::injection:
        draw_injection_page( state , io );
        break;
    case gui_page::history:
        draw_history_page( state );
        break;
    case gui_page::settings:
        draw_settings_page( state );
        break;
    }

    ImGui::EndChild( );

    gui_draw_status_bar( state );
    draw_confirm_popup( state );
    gui_draw_command_palette( state );
    gui_draw_first_run_wizard( state );
    gui_draw_toasts( state );
    gui_draw_drag_overlay( state );

    ImGui::End( );
}
