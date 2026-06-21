#include "gui_app.hpp"

#include <app/config.hpp>
#include <app/errors.hpp>
#include <app/inject_service.hpp>
#include <app/process_list.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <windows.h>
#include <commdlg.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#pragma comment( lib , "d3d11.lib" )
#pragma comment( lib , "dxgi.lib" )
#pragma comment( lib , "comdlg32.lib" )

void* g_gui_hwnd = nullptr;

namespace
{
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swap_chain = nullptr;
    ID3D11RenderTargetView* g_render_target = nullptr;
    bool g_swap_occluded = false;
    unsigned int g_resize_w = 0;
    unsigned int g_resize_h = 0;

    app_config g_config {};
    std::vector< process_entry > g_all_processes;

    char g_search [ 256 ] = {};
    char g_process_name [ 260 ] = {};
    char g_dll_path [ 1024 ] = {};
    bool g_wait_for_process = false;
    int g_selected_pid = 0;

    std::string g_log;
    std::mutex g_log_mutex;
    std::vector< std::string > g_pending_log;
    std::mutex g_pending_mutex;

    std::string g_status = "Ready";
    std::atomic< bool > g_injecting { false };
    std::atomic< bool > g_inject_done { false };
    inject_result g_inject_result {};

    int g_recent_process = 0;
    int g_recent_dll = 0;
    bool g_light_mode = false;

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

    void append_log( const std::string& line )
    {
        std::lock_guard lock( g_pending_mutex );
        g_pending_log.push_back( line );
    }

    void append_log_w( const std::wstring& line )
    {
        append_log( wide_to_utf8( line ) );
    }

    void flush_pending_log( )
    {
        std::vector< std::string > lines;
        {
            std::lock_guard lock( g_pending_mutex );
            lines.swap( g_pending_log );
        }

        if ( lines.empty( ) )
        {
            return;
        }

        std::lock_guard lock( g_log_mutex );

        for ( const auto& line : lines )
        {
            if ( !g_log.empty( ) )
            {
                g_log += "\n";
            }

            g_log += line;
        }
    }

    void refresh_processes( )
    {
        g_all_processes = list_processes( );
        append_log( "Process list refreshed (" + std::to_string( g_all_processes.size( ) ) + " total)." );
    }

    std::vector< process_entry > filtered_processes( )
    {
        return filter_processes( g_all_processes , utf8_to_wide( g_search ).c_str( ) );
    }

    struct inject_worker_context
    {
        inject_request request;
    };

    DWORD WINAPI inject_worker( void* parameter )
    {
        auto* context = static_cast< inject_worker_context* >( parameter );
        inject_result result = run_injection( context->request , g_config );
        delete context;

        g_inject_result = result;
        g_inject_done.store( true , std::memory_order_release );
        return 0;
    }

    void start_injection( )
    {
        if ( g_injecting.load( ) )
        {
            return;
        }

        if ( !g_dll_path [ 0 ] )
        {
            append_log( "Select a DLL first." );
            return;
        }

        auto* context = new inject_worker_context {};
        context->request.dll_path = utf8_to_wide( g_dll_path );
        context->request.wait_for_ms = g_wait_for_process ? 30000u : 0u;
        context->request.log = append_log_w;

        append_log( "[inject] Payload DLL: " + std::string( g_dll_path ) );

        if ( g_selected_pid > 0 )
        {
            context->request.process_id = static_cast< uint32_t >( g_selected_pid );
            append_log( "[inject] Target PID " + std::to_string( g_selected_pid ) + " (manual map via remote loader thread)." );
        }
        else if ( g_process_name [ 0 ] )
        {
            context->request.process_name = utf8_to_wide( g_process_name );
            append_log( "[inject] Target process name: " + std::string( g_process_name ) );
        }
        else
        {
            delete context;
            append_log( "Select a process from the list or enter a process name." );
            return;
        }

        g_inject_done.store( false , std::memory_order_release );
        g_injecting.store( true , std::memory_order_release );
        g_status = "Injecting...";

        HANDLE thread = CreateThread( nullptr , 0 , inject_worker , context , 0 , nullptr );

        if ( !thread )
        {
            g_injecting.store( false , std::memory_order_release );
            g_status = "Failed to start worker thread";
            delete context;
            append_log( "Failed to start injection thread." );
            return;
        }

        CloseHandle( thread );
    }

    void poll_injection( )
    {
        if ( !g_inject_done.load( std::memory_order_acquire ) )
        {
            return;
        }

        g_inject_done.store( false , std::memory_order_release );
        g_injecting.store( false , std::memory_order_release );

        if ( g_inject_result.code == 0 )
        {
            g_status = "Injection succeeded";
            append_log( "Injection succeeded." );
        }
        else
        {
            g_status = "Injection failed";
            append_log(
                "Injection failed (0x" + std::to_string( g_inject_result.code ) + "): "
                + wide_to_utf8( inject_error_message_w( g_inject_result.code ) ) );
        }
    }

    bool export_log( )
    {
        std::string text;
        {
            std::lock_guard lock( g_log_mutex );
            text = g_log;
        }

        if ( text.empty( ) )
        {
            append_log( "Log is empty — nothing to export." );
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
            append_log( "Failed to write log file." );
            return false;
        }

        file.write( text.data( ) , static_cast< std::streamsize >( text.size( ) ) );
        append_log( "Log exported to " + wide_to_utf8( path ) );
        return true;
    }

    void apply_theme( bool light_mode )
    {
        if ( light_mode )
        {
            ImGui::StyleColorsLight( );
        }
        else
        {
            ImGui::StyleColorsDark( );
        }

        ImGuiStyle& style = ImGui::GetStyle( );
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.WindowPadding = ImVec2( 12.0f , 12.0f );
        style.ItemSpacing = ImVec2( 8.0f , 6.0f );
        style.FramePadding = ImVec2( 8.0f , 5.0f );

        if ( !light_mode )
        {
            ImVec4* colors = style.Colors;
            colors [ ImGuiCol_WindowBg ] = ImVec4( 0.10f , 0.10f , 0.11f , 1.00f );
            colors [ ImGuiCol_ChildBg ] = ImVec4( 0.12f , 0.12f , 0.13f , 1.00f );
            colors [ ImGuiCol_Header ] = ImVec4( 0.20f , 0.40f , 0.75f , 0.55f );
            colors [ ImGuiCol_HeaderHovered ] = ImVec4( 0.24f , 0.47f , 0.85f , 0.80f );
            colors [ ImGuiCol_HeaderActive ] = ImVec4( 0.24f , 0.47f , 0.85f , 1.00f );
            colors [ ImGuiCol_Button ] = ImVec4( 0.20f , 0.40f , 0.75f , 1.00f );
            colors [ ImGuiCol_ButtonHovered ] = ImVec4( 0.24f , 0.47f , 0.85f , 1.00f );
            colors [ ImGuiCol_ButtonActive ] = ImVec4( 0.16f , 0.34f , 0.65f , 1.00f );
        }
    }

    void get_clear_color( float out [ 4 ] )
    {
        if ( g_light_mode )
        {
            out [ 0 ] = 0.94f;
            out [ 1 ] = 0.94f;
            out [ 2 ] = 0.95f;
            out [ 3 ] = 1.0f;
        }
        else
        {
            out [ 0 ] = 0.08f;
            out [ 1 ] = 0.08f;
            out [ 2 ] = 0.09f;
            out [ 3 ] = 1.0f;
        }
    }

    void create_render_target( )
    {
        ID3D11Texture2D* back_buffer = nullptr;
        g_swap_chain->GetBuffer( 0 , IID_PPV_ARGS( &back_buffer ) );
        g_device->CreateRenderTargetView( back_buffer , nullptr , &g_render_target );
        back_buffer->Release( );
    }

    void cleanup_render_target( )
    {
        if ( g_render_target )
        {
            g_render_target->Release( );
            g_render_target = nullptr;
        }
    }
}

bool gui_app_create_device( void* hwnd )
{
    g_gui_hwnd = hwnd;

    DXGI_SWAP_CHAIN_DESC desc {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = static_cast< HWND >( hwnd );
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level {};
    const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_0 , D3D_FEATURE_LEVEL_10_0 };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr ,
        D3D_DRIVER_TYPE_HARDWARE ,
        nullptr ,
        0 ,
        levels ,
        2 ,
        D3D11_SDK_VERSION ,
        &desc ,
        &g_swap_chain ,
        &g_device ,
        &feature_level ,
        &g_context );

    if ( result == DXGI_ERROR_UNSUPPORTED )
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr ,
            D3D_DRIVER_TYPE_WARP ,
            nullptr ,
            0 ,
            levels ,
            2 ,
            D3D11_SDK_VERSION ,
            &desc ,
            &g_swap_chain ,
            &g_device ,
            &feature_level ,
            &g_context );
    }

    if ( result != S_OK )
    {
        return false;
    }

    create_render_target( );
    return true;
}

void gui_app_destroy_device( )
{
    cleanup_render_target( );

    if ( g_swap_chain )
    {
        g_swap_chain->Release( );
        g_swap_chain = nullptr;
    }

    if ( g_context )
    {
        g_context->Release( );
        g_context = nullptr;
    }

    if ( g_device )
    {
        g_device->Release( );
        g_device = nullptr;
    }
}

bool gui_app_init( )
{
    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO& io = ImGui::GetIO( );
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    apply_theme( g_light_mode );

    if ( const ImFont* font = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf" , 17.0f ) )
    {
        ( void )font;
    }
    else
    {
        io.Fonts->AddFontDefault( );
    }

    ImGui_ImplWin32_Init( g_gui_hwnd );
    ImGui_ImplDX11_Init( g_device , g_context );

    load_config( g_config );

    if ( !g_config.last_dll.empty( ) )
    {
        const auto utf8 = wide_to_utf8( g_config.last_dll );
        std::strncpy( g_dll_path , utf8.c_str( ) , sizeof( g_dll_path ) - 1 );
    }

    if ( !g_config.last_process.empty( ) )
    {
        const auto utf8 = wide_to_utf8( g_config.last_process );
        std::strncpy( g_process_name , utf8.c_str( ) , sizeof( g_process_name ) - 1 );
        std::strncpy( g_search , utf8.c_str( ) , sizeof( g_search ) - 1 );
    }

    refresh_processes( );
    append_log( "Manual Map Injector ready." );

    if ( !is_process_elevated( ) )
    {
        append_log( "Note: not running as administrator — some targets may fail." );
        g_status = "Ready (not elevated)";
    }

    return true;
}

void gui_app_shutdown( )
{
    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );
}

void gui_app_on_resize( unsigned int width , unsigned int height )
{
    g_resize_w = width;
    g_resize_h = height;
}

void gui_app_new_frame( )
{
    if ( g_resize_w != 0 && g_resize_h != 0 )
    {
        cleanup_render_target( );
        g_swap_chain->ResizeBuffers( 0 , g_resize_w , g_resize_h , DXGI_FORMAT_UNKNOWN , 0 );
        g_resize_w = g_resize_h = 0;
        create_render_target( );
    }

    if ( g_swap_occluded && g_swap_chain->Present( 0 , DXGI_PRESENT_TEST ) == DXGI_STATUS_OCCLUDED )
    {
        Sleep( 10 );
    }

    g_swap_occluded = false;

    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );

    flush_pending_log( );
    poll_injection( );
}

void gui_app_render( )
{
    ImGuiIO& io = ImGui::GetIO( );

    ImGui::SetNextWindowPos( ImVec2( 0.0f , 0.0f ) );
    ImGui::SetNextWindowSize( io.DisplaySize );
    ImGui::Begin(
        "ManualMapInjector" ,
        nullptr ,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus );

    ImGui::Text( "Status: %s" , g_status.c_str( ) );
    ImGui::SameLine( io.DisplaySize.x - 220.0f );
    if ( ImGui::Checkbox( "Light mode" , &g_light_mode ) )
    {
        apply_theme( g_light_mode );
    }
    ImGui::Separator( );

    const float left_width = io.DisplaySize.x * 0.42f;

    ImGui::BeginChild( "LeftPanel" , ImVec2( left_width , 0 ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( "Target Process" );
    ImGui::SetNextItemWidth( -120.0f );
    ImGui::InputTextWithHint( "##search" , "Filter by name or PID..." , g_search , sizeof( g_search ) );
    ImGui::SameLine( );
    if ( ImGui::Button( "Refresh" , ImVec2( -1.0f , 0.0f ) ) )
    {
        refresh_processes( );
    }

    if ( !g_config.recent_processes.empty( ) )
    {
        ImGui::SetNextItemWidth( -1.0f );

        if ( ImGui::BeginCombo( "##recent_process" , g_recent_process == 0 ? "Recent processes..." : wide_to_utf8( g_config.recent_processes [ g_recent_process - 1 ] ).c_str( ) ) )
        {
            for ( int idx = 0; idx < static_cast< int >( g_config.recent_processes.size( ) ); ++idx )
            {
                const auto label = wide_to_utf8( g_config.recent_processes [ idx ] );
                const bool selected = g_recent_process == idx + 1;

                if ( ImGui::Selectable( label.c_str( ) , selected ) )
                {
                    g_recent_process = idx + 1;
                    std::strncpy( g_process_name , label.c_str( ) , sizeof( g_process_name ) - 1 );
                    std::strncpy( g_search , label.c_str( ) , sizeof( g_search ) - 1 );
                }

                if ( selected )
                {
                    ImGui::SetItemDefaultFocus( );
                }
            }

            ImGui::EndCombo( );
        }
    }

    const auto visible = filtered_processes( );
    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if ( ImGui::BeginTable( "##processes" , 2 , table_flags , ImVec2( -1.0f , io.DisplaySize.y * 0.34f ) ) )
    {
        ImGui::TableSetupColumn( "PID" , ImGuiTableColumnFlags_WidthFixed , 72.0f );
        ImGui::TableSetupColumn( "Process Name" , ImGuiTableColumnFlags_WidthStretch );
        ImGui::TableSetupScrollFreeze( 0 , 1 );
        ImGui::TableHeadersRow( );

        for ( const auto& process : visible )
        {
            ImGui::TableNextRow( );
            ImGui::PushID( static_cast< int >( process.pid ) );

            const bool selected = g_selected_pid == static_cast< int >( process.pid );
            const std::string name_utf8 = wide_to_utf8( process.name );

            ImGui::TableSetColumnIndex( 0 );

            if ( ImGui::Selectable( "##row" , selected , ImGuiSelectableFlags_SpanAllColumns ) )
            {
                g_selected_pid = static_cast< int >( process.pid );
                std::strncpy( g_process_name , name_utf8.c_str( ) , sizeof( g_process_name ) - 1 );
            }

            ImGui::TableSetColumnIndex( 0 );
            ImGui::Text( "%u" , process.pid );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( name_utf8.c_str( ) );

            ImGui::PopID( );
        }

        ImGui::EndTable( );
    }

    ImGui::Spacing( );
    ImGui::TextUnformatted( "Inject by name" );
    ImGui::SetNextItemWidth( -1.0f );
    ImGui::InputTextWithHint( "##process_name" , "e.g. notepad.exe" , g_process_name , sizeof( g_process_name ) );

    ImGui::Spacing( );
    ImGui::Separator( );
    ImGui::TextUnformatted( "DLL Payload" );

    if ( !g_config.recent_dlls.empty( ) )
    {
        ImGui::SetNextItemWidth( -1.0f );

        if ( ImGui::BeginCombo( "##recent_dll" , g_recent_dll == 0 ? "Recent DLLs..." : wide_to_utf8( g_config.recent_dlls [ g_recent_dll - 1 ] ).c_str( ) ) )
        {
            for ( int idx = 0; idx < static_cast< int >( g_config.recent_dlls.size( ) ); ++idx )
            {
                const auto label = wide_to_utf8( g_config.recent_dlls [ idx ] );
                const bool selected = g_recent_dll == idx + 1;

                if ( ImGui::Selectable( label.c_str( ) , selected ) )
                {
                    g_recent_dll = idx + 1;
                    std::strncpy( g_dll_path , label.c_str( ) , sizeof( g_dll_path ) - 1 );
                }

                if ( selected )
                {
                    ImGui::SetItemDefaultFocus( );
                }
            }

            ImGui::EndCombo( );
        }
    }

    ImGui::SetNextItemWidth( -90.0f );
    ImGui::InputText( "##dll_path" , g_dll_path , sizeof( g_dll_path ) );
    ImGui::SameLine( );

    if ( ImGui::Button( "Browse" , ImVec2( -1.0f , 0.0f ) ) )
    {
        const auto selected = pick_dll_path( utf8_to_wide( g_dll_path ) );

        if ( !selected.empty( ) )
        {
            const auto utf8 = wide_to_utf8( selected );
            std::strncpy( g_dll_path , utf8.c_str( ) , sizeof( g_dll_path ) - 1 );
        }
    }

    ImGui::Checkbox( "Wait up to 30 seconds for process" , &g_wait_for_process );
    ImGui::EndChild( );

    ImGui::SameLine( );

    ImGui::BeginChild( "RightPanel" , ImVec2( 0.0f , 0.0f ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( "Output Log" );

    std::string log_copy;
    {
        std::lock_guard lock( g_log_mutex );
        log_copy = g_log;
    }

    ImGui::BeginChild( "LogScroll" , ImVec2( -1.0f , -48.0f ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( log_copy.c_str( ) );

    if ( ImGui::GetScrollY( ) >= ImGui::GetScrollMaxY( ) )
    {
        ImGui::SetScrollHereY( 1.0f );
    }

    ImGui::EndChild( );

    const bool busy = g_injecting.load( );

    if ( busy )
    {
        ImGui::BeginDisabled( );
    }

    if ( ImGui::Button( "Inject" , ImVec2( 120.0f , 36.0f ) ) )
    {
        start_injection( );
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Run as Admin" , ImVec2( 120.0f , 36.0f ) ) )
    {
        if ( relaunch_as_admin( nullptr ) )
        {
            PostMessageW( static_cast< HWND >( g_gui_hwnd ) , WM_CLOSE , 0 , 0 );
        }
        else
        {
            append_log( "Failed to relaunch as administrator." );
        }
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Clear Log" , ImVec2( 100.0f , 36.0f ) ) )
    {
        std::lock_guard lock( g_log_mutex );
        g_log.clear( );
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Export Log" , ImVec2( 100.0f , 36.0f ) ) )
    {
        export_log( );
    }

    if ( busy )
    {
        ImGui::EndDisabled( );
    }

    ImGui::EndChild( );
    ImGui::End( );

    ImGui::Render( );
    float clear [ 4 ] {};
    get_clear_color( clear );
    g_context->OMSetRenderTargets( 1 , &g_render_target , nullptr );
    g_context->ClearRenderTargetView( g_render_target , clear );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
}

void gui_app_present( )
{
    const HRESULT hr = g_swap_chain->Present( 1 , 0 );
    g_swap_occluded = ( hr == DXGI_STATUS_OCCLUDED );
}
