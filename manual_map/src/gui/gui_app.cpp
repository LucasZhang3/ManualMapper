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
#include <filesystem>
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

    bool g_light_mode = false;

    int search_input_callback( ImGuiInputTextCallbackData* data )
    {
        if ( data->EventFlag == ImGuiInputTextFlags_CallbackEdit )
        {
            g_selected_pid = 0;
        }

        return 0;
    }

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
        else if ( g_search [ 0 ] )
        {
            context->request.process_name = utf8_to_wide( g_search );
            append_log( "[inject] Target process name: " + std::string( g_search ) );
        }
        else
        {
            delete context;
            append_log( "Search for a process and select it from the list, or type a process name." );
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

    bool draw_theme_toggle( )
    {
        ImGuiStyle& style = ImGui::GetStyle( );
        const float height = ImGui::GetFrameHeight( );
        const float width = height * 2.0f;
        const float radius = height * 0.5f;
        const ImVec2 pos = ImGui::GetCursorScreenPos( );

        ImGui::InvisibleButton( "##theme_toggle" , ImVec2( width , height ) );
        const bool clicked = ImGui::IsItemClicked( );
        const bool hovered = ImGui::IsItemHovered( );

        if ( clicked )
        {
            g_light_mode = !g_light_mode;
            apply_theme( g_light_mode );
        }

        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImU32 track_off = ImGui::GetColorU32( ImVec4( 0.22f , 0.24f , 0.30f , 1.0f ) );
        const ImU32 track_on = ImGui::GetColorU32( ImVec4( 0.95f , 0.78f , 0.28f , 1.0f ) );
        const ImU32 track = g_light_mode ? track_on : track_off;
        const ImU32 track_hover = ImGui::GetColorU32( hovered ? ImVec4( 0.32f , 0.34f , 0.40f , 1.0f ) : ImVec4( 0.0f , 0.0f , 0.0f , 0.0f ) );

        draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , track , radius );

        if ( hovered )
        {
            draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , track_hover , radius );
        }

        const float knob_travel = width - radius * 2.0f;
        const float knob_x = pos.x + radius + ( g_light_mode ? knob_travel : 0.0f );
        const ImU32 knob_color = ImGui::GetColorU32( ImVec4( 1.0f , 1.0f , 1.0f , 1.0f ) );
        draw->AddCircleFilled( ImVec2( knob_x , pos.y + radius ) , radius - 3.0f , knob_color );

        const ImU32 icon_color = ImGui::GetColorU32( ImVec4( 1.0f , 1.0f , 1.0f , g_light_mode ? 0.35f : 0.85f ) );
        draw->AddCircleFilled( ImVec2( pos.x + radius , pos.y + radius ) , 3.5f , icon_color );

        const ImU32 sun_color = ImGui::GetColorU32( ImVec4( 1.0f , 1.0f , 1.0f , g_light_mode ? 0.90f : 0.35f ) );
        draw->AddCircleFilled( ImVec2( pos.x + width - radius , pos.y + radius ) , 3.5f , sun_color );

        ImGui::SameLine( 0.0f , style.ItemInnerSpacing.x );
        ImGui::AlignTextToFramePadding( );
        ImGui::TextUnformatted( g_light_mode ? "Light" : "Dark" );

        return clicked;
    }

    void draw_recent_dll_cards( )
    {
        if ( g_config.recent_dlls.empty( ) )
        {
            return;
        }

        ImGui::TextUnformatted( "Recent Payloads" );
        ImGui::Spacing( );

        const ImVec4 dim_text = g_light_mode ? ImVec4( 0.40f , 0.42f , 0.46f , 1.0f ) : ImVec4( 0.55f , 0.58f , 0.64f , 1.0f );
        const ImVec4 active_bg = g_light_mode ? ImVec4( 0.82f , 0.90f , 1.00f , 1.0f ) : ImVec4( 0.18f , 0.32f , 0.55f , 0.55f );
        const ImVec4 hover_bg = g_light_mode ? ImVec4( 0.90f , 0.93f , 0.97f , 1.0f ) : ImVec4( 0.20f , 0.24f , 0.30f , 1.0f );
        const ImVec4 idle_bg = g_light_mode ? ImVec4( 0.96f , 0.97f , 0.98f , 1.0f ) : ImVec4( 0.14f , 0.15f , 0.17f , 1.0f );

        const std::wstring current_dll = utf8_to_wide( g_dll_path );

        for ( int idx = 0; idx < static_cast< int >( g_config.recent_dlls.size( ) ); ++idx )
        {
            const auto& dll_path = g_config.recent_dlls [ static_cast< size_t >( idx ) ];
            const std::filesystem::path path_obj( dll_path );
            const std::string filename = wide_to_utf8( path_obj.filename( ).wstring( ) );
            const std::string directory = wide_to_utf8( path_obj.parent_path( ).wstring( ) );
            const bool is_active = !current_dll.empty( ) && _wcsicmp( current_dll.c_str( ) , dll_path.c_str( ) ) == 0;

            ImGui::PushID( idx );

            const float card_width = ImGui::GetContentRegionAvail( ).x;
            const float card_height = 52.0f;
            const ImVec2 card_pos = ImGui::GetCursorScreenPos( );

            ImGui::InvisibleButton( "##dll_card" , ImVec2( card_width - 36.0f , card_height ) );
            const bool card_hovered = ImGui::IsItemHovered( );
            const bool card_clicked = ImGui::IsItemClicked( );

            ImDrawList* draw = ImGui::GetWindowDrawList( );
            ImVec4 bg = is_active ? active_bg : ( card_hovered ? hover_bg : idle_bg );
            draw->AddRectFilled(
                card_pos ,
                ImVec2( card_pos.x + card_width - 36.0f , card_pos.y + card_height ) ,
                ImGui::GetColorU32( bg ) ,
                0.0f );

            if ( is_active )
            {
                draw->AddRect(
                    card_pos ,
                    ImVec2( card_pos.x + card_width - 36.0f , card_pos.y + card_height ) ,
                    ImGui::GetColorU32( ImVec4( 0.24f , 0.47f , 0.85f , 1.0f ) ) ,
                    0.0f ,
                    0 ,
                    1.5f );
            }

            const ImVec2 text_pad( 10.0f , 8.0f );
            draw->AddText(
                ImVec2( card_pos.x + text_pad.x , card_pos.y + text_pad.y ) ,
                ImGui::GetColorU32( ImGuiCol_Text ) ,
                filename.c_str( ) );

            if ( !directory.empty( ) )
            {
                draw->AddText(
                    ImGui::GetFont( ) ,
                    ImGui::GetFontSize( ) * 0.85f ,
                    ImVec2( card_pos.x + text_pad.x , card_pos.y + text_pad.y + ImGui::GetTextLineHeight( ) + 2.0f ) ,
                    ImGui::GetColorU32( dim_text ) ,
                    directory.c_str( ) );
            }

            if ( card_clicked )
            {
                const auto utf8 = wide_to_utf8( dll_path );
                std::strncpy( g_dll_path , utf8.c_str( ) , sizeof( g_dll_path ) - 1 );
            }

            ImGui::SameLine( 0.0f , 0.0f );
            ImGui::SetCursorScreenPos( ImVec2( card_pos.x + card_width - 32.0f , card_pos.y + ( card_height - ImGui::GetFrameHeight( ) ) * 0.5f ) );

            if ( ImGui::Button( "X" , ImVec2( 32.0f , ImGui::GetFrameHeight( ) ) ) )
            {
                remove_recent_dll( g_config , dll_path );
                save_config( g_config );
            }

            ImGui::Dummy( ImVec2( 0.0f , 4.0f ) );
            ImGui::PopID( );
        }

        ImGui::Spacing( );
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
    ImGui::SameLine( io.DisplaySize.x - 130.0f );
    draw_theme_toggle( );
    ImGui::Separator( );

    const float left_width = io.DisplaySize.x * 0.42f;

    ImGui::BeginChild( "LeftPanel" , ImVec2( left_width , 0 ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( "Target Process" );
    ImGui::SetNextItemWidth( -120.0f );
    ImGui::InputTextWithHint(
        "##search" ,
        "Search by name or PID..." ,
        g_search ,
        sizeof( g_search ) ,
        ImGuiInputTextFlags_CallbackEdit ,
        search_input_callback );
    ImGui::SameLine( );
    if ( ImGui::Button( "Refresh" , ImVec2( -1.0f , 0.0f ) ) )
    {
        refresh_processes( );
    }

    const auto visible = filtered_processes( );
    ImGuiTableFlags table_flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if ( ImGui::BeginTable( "##processes" , 2 , table_flags , ImVec2( -1.0f , io.DisplaySize.y * 0.38f ) ) )
    {
        ImGui::TableSetupColumn( "PID" , ImGuiTableColumnFlags_WidthFixed , 80.0f );
        ImGui::TableSetupColumn( "Process Name" , ImGuiTableColumnFlags_WidthStretch );
        ImGui::TableSetupScrollFreeze( 0 , 1 );
        ImGui::TableHeadersRow( );

        const ImGuiSelectableFlags row_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;

        for ( const auto& process : visible )
        {
            ImGui::TableNextRow( );
            ImGui::PushID( static_cast< int >( process.pid ) );

            const bool selected = g_selected_pid == static_cast< int >( process.pid );
            const std::string name_utf8 = wide_to_utf8( process.name );
            const std::string pid_utf8 = std::to_string( process.pid );

            ImGui::TableSetColumnIndex( 0 );

            if ( ImGui::Selectable( "##row" , selected , row_flags ) )
            {
                g_selected_pid = static_cast< int >( process.pid );
                std::strncpy( g_search , name_utf8.c_str( ) , sizeof( g_search ) - 1 );
            }

            ImGui::TableSetColumnIndex( 0 );
            ImGui::AlignTextToFramePadding( );
            const float pid_col_width = ImGui::GetColumnWidth( );
            const float pid_text_width = ImGui::CalcTextSize( pid_utf8.c_str( ) ).x;
            ImGui::SetCursorPosX( ImGui::GetCursorPosX( ) + pid_col_width - pid_text_width - ImGui::GetStyle( ).CellPadding.x );
            ImGui::TextUnformatted( pid_utf8.c_str( ) );

            ImGui::TableSetColumnIndex( 1 );
            ImGui::AlignTextToFramePadding( );
            ImGui::TextUnformatted( name_utf8.c_str( ) );

            ImGui::PopID( );
        }

        ImGui::EndTable( );
    }

    if ( g_selected_pid > 0 )
    {
        ImGui::TextDisabled( "Selected PID: %d" , g_selected_pid );
    }
    else if ( g_search [ 0 ] )
    {
        ImGui::TextDisabled( "Inject by name: %s" , g_search );
    }

    ImGui::Spacing( );
    ImGui::Separator( );
    ImGui::TextUnformatted( "DLL Payload" );

    draw_recent_dll_cards( );

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
