#define NOMINMAX

#include "gui_state.hpp"

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
    unsigned int g_resize_w = 0;
    unsigned int g_resize_h = 0;

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
    }

    std::vector< process_entry > visible_processes( gui_app_state& state )
    {
        auto visible = filter_processes( state.all_processes , utf8_to_wide( state.search ).c_str( ) );
        sort_processes( visible , state.sort_mode );

        for ( auto& process : visible )
        {
            enrich_process_details( process );
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
    }

    int search_input_callback( ImGuiInputTextCallbackData* data )
    {
        if ( data->EventFlag == ImGuiInputTextFlags_CallbackEdit && g_state )
        {
            g_state->selected_pid = 0;
            g_state->list_focus = -1;
        }

        return 0;
    }

    struct inject_worker_context
    {
        gui_app_state* state = nullptr;
        inject_request request;
    };

    DWORD WINAPI inject_worker( void* parameter )
    {
        auto* context = static_cast< inject_worker_context* >( parameter );
        inject_result result = run_injection( context->request , context->state->config );
        delete context;

        if ( g_state )
        {
            g_state->inject_result = result;
            g_state->inject_done.store( true , std::memory_order_release );
        }

        return 0;
    }

    void launch_injection( gui_app_state& state )
    {
        if ( state.injecting.load( ) )
        {
            return;
        }

        if ( !state.dll_path [ 0 ] )
        {
            append_log( state , "Select a DLL first." );
            return;
        }

        auto* context = new inject_worker_context {};
        context->state = &state;
        context->request.dll_path = utf8_to_wide( state.dll_path );
        context->request.wait_for_ms = state.wait_for_process ? 30000u : 0u;
        context->request.delay_ms = static_cast< uint32_t >( state.inject_delay_sec ) * 1000u;
        context->request.inject_all = state.inject_all;
        context->request.log = [ &state ] ( const std::wstring& line )
        {
            append_log_w( state , line );
        };

        append_log( state , "[inject] Payload DLL: " + std::string( state.dll_path ) );

        if ( !state.config.cli_notes.empty( ) )
        {
            append_log( state , "[cli] " + wide_to_utf8( state.config.cli_notes ) );
        }

        if ( state.selected_pid > 0 )
        {
            context->request.process_id = static_cast< uint32_t >( state.selected_pid );
            append_log( state , "[inject] Target PID " + std::to_string( state.selected_pid ) );
        }
        else if ( state.search [ 0 ] )
        {
            context->request.process_name = utf8_to_wide( state.search );
            append_log( state , "[inject] Target process name: " + std::string( state.search ) );
        }
        else
        {
            delete context;
            append_log( state , "Search for a process and select it from the list, or type a process name." );
            return;
        }

        const std::wstring target_name = context->request.process_id
            ? [ & ] ( )
            {
                for ( const auto& process : state.all_processes )
                {
                    if ( static_cast< int >( process.pid ) == state.selected_pid )
                    {
                        return process.name;
                    }
                }

                return std::wstring {};
            }( )
            : context->request.process_name;

        if ( !target_name.empty( ) && !is_process_allowed( state.config , target_name ) )
        {
            delete context;
            append_log( state , "Process blocked by safety rules." );
            return;
        }

        state.inject_done.store( false , std::memory_order_release );
        state.injecting.store( true , std::memory_order_release );
        state.inject_progress = 0.15f;
        state.status = "Injecting...";

        HANDLE thread = CreateThread( nullptr , 0 , inject_worker , context , 0 , nullptr );

        if ( !thread )
        {
            state.injecting.store( false , std::memory_order_release );
            state.status = "Failed to start worker thread";
            delete context;
            append_log( state , "Failed to start injection thread." );
            return;
        }

        CloseHandle( thread );
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
        }
        else
        {
            state.status = "Injection failed (0x" + std::to_string( state.inject_result.code ) + ")";
            append_log(
                state ,
                "Injection failed (0x" + std::to_string( state.inject_result.code ) + "): "
                + wide_to_utf8( inject_error_message_w( state.inject_result.code ) ) );
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

    void draw_theme_toggle( gui_app_state& state )
    {
        ImGuiStyle& style = ImGui::GetStyle( );
        const float height = ImGui::GetFrameHeight( );
        const float width = height * 2.0f;
        const float radius = height * 0.5f;
        const ImVec2 pos = ImGui::GetCursorScreenPos( );

        ImGui::InvisibleButton( "##theme_toggle" , ImVec2( width , height ) );

        if ( ImGui::IsItemClicked( ) )
        {
            state.light_mode = !state.light_mode;
            apply_theme( state.light_mode );
        }

        const bool hovered = ImGui::IsItemHovered( );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImU32 track = ImGui::GetColorU32( state.light_mode ? ImVec4( 0.95f , 0.78f , 0.28f , 1.0f ) : ImVec4( 0.22f , 0.24f , 0.30f , 1.0f ) );
        draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , track , radius );

        if ( hovered )
        {
            draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , ImGui::GetColorU32( ImVec4( 1 , 1 , 1 , 0.08f ) ) , radius );
        }

        const float knob_x = pos.x + radius + ( state.light_mode ? ( width - radius * 2.0f ) : 0.0f );
        draw->AddCircleFilled( ImVec2( knob_x , pos.y + radius ) , radius - 3.0f , IM_COL32( 255 , 255 , 255 , 255 ) );

        ImGui::SameLine( 0.0f , style.ItemInnerSpacing.x );
        ImGui::AlignTextToFramePadding( );
        ImGui::TextUnformatted( state.light_mode ? "Light" : "Dark" );
    }

    void draw_process_table( gui_app_state& state , const std::vector< process_entry >& visible , float height )
    {
        ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoKeepColumnsVisible;

        if ( !ImGui::BeginTable( "##processes" , 7 , table_flags , ImVec2( -1.0f , height ) ) )
        {
            return;
        }

        ImGui::TableSetupColumn( " " , ImGuiTableColumnFlags_WidthFixed , 22.0f );
        ImGui::TableSetupColumn( "PID" , ImGuiTableColumnFlags_WidthFixed , 64.0f );
        ImGui::TableSetupColumn( "Process Name" , ImGuiTableColumnFlags_WidthStretch );
        ImGui::TableSetupColumn( "Arch" , ImGuiTableColumnFlags_WidthFixed , 42.0f );
        ImGui::TableSetupColumn( "Elev" , ImGuiTableColumnFlags_WidthFixed , 38.0f );
        ImGui::TableSetupColumn( "Sess" , ImGuiTableColumnFlags_WidthFixed , 42.0f );
        ImGui::TableSetupColumn( "PPID" , ImGuiTableColumnFlags_WidthFixed , 64.0f );
        ImGui::TableSetupScrollFreeze( 0 , 1 );
        ImGui::TableHeadersRow( );

        const ImGuiStyle& style = ImGui::GetStyle( );
        const float row_height = ImGui::GetTextLineHeight( ) + style.CellPadding.y * 2.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList( );

        for ( int row = 0; row < static_cast< int >( visible.size( ) ); ++row )
        {
            const auto& process = visible [ static_cast< size_t >( row ) ];
            ImGui::TableNextRow( ImGuiTableRowFlags_None , row_height );
            ImGui::TableSetColumnIndex( 0 );
            ImGui::PushID( process.pid );

            float row_width = 0.0f;

            for ( int column = 0; column < 7; ++column )
            {
                row_width += ImGui::GetColumnWidth( column );
            }

            const ImVec2 row_pos = ImGui::GetCursorScreenPos( );
            ImGui::InvisibleButton( "##row" , ImVec2( row_width , row_height ) );

            const bool hovered = ImGui::IsItemHovered( );
            const bool clicked = ImGui::IsItemClicked( );
            const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
            const ImVec2 min = ImGui::GetItemRectMin( );
            const ImVec2 max = ImGui::GetItemRectMax( );
            const bool selected = state.selected_pid == static_cast< int >( process.pid );
            const bool focused = state.list_focus == row;

            if ( selected )
            {
                draw->AddRectFilled( min , max , ImGui::GetColorU32( ImGuiCol_Header ) );
            }
            else if ( focused || hovered )
            {
                draw->AddRectFilled( min , max , ImGui::GetColorU32( ImGuiCol_HeaderHovered ) );
            }

            const float text_y = min.y + ( max.y - min.y - ImGui::GetTextLineHeight( ) ) * 0.5f;
            float column_x = min.x;

            const auto draw_cell = [ & ] ( const char* text , int column , bool right_align )
            {
                const float col_w = ImGui::GetColumnWidth( column );
                const ImVec2 size = ImGui::CalcTextSize( text );
                const float x = right_align
                    ? column_x + col_w - size.x - style.CellPadding.x
                    : column_x + style.CellPadding.x;
                draw->AddText( ImVec2( x , text_y ) , ImGui::GetColorU32( ImGuiCol_Text ) , text );
                column_x += col_w;
            };

            const std::string favorite = is_favorite( state , process.pid ) ? "*" : " ";
            draw_cell( favorite.c_str( ) , 0 , false );

            const std::string pid_text = std::to_string( process.pid );
            draw_cell( pid_text.c_str( ) , 1 , true );

            const std::string name_utf8 = wide_to_utf8( process.name );
            draw_cell( name_utf8.c_str( ) , 2 , false );

            const std::string arch = process.details_loaded ? ( process.is_wow64 ? "x86" : "x64" ) : "?";
            draw_cell( arch.c_str( ) , 3 , false );

            const std::string elev = process.details_loaded ? ( process.is_elevated ? "Y" : "N" ) : "?";
            draw_cell( elev.c_str( ) , 4 , false );

            const std::string sess = process.details_loaded ? std::to_string( process.session_id ) : "?";
            draw_cell( sess.c_str( ) , 5 , true );

            const std::string ppid = std::to_string( process.parent_pid );
            draw_cell( ppid.c_str( ) , 6 , true );

            if ( clicked )
            {
                state.list_focus = row;
                state.selected_pid = static_cast< int >( process.pid );
                std::strncpy( state.search , name_utf8.c_str( ) , sizeof( state.search ) - 1 );
            }

            if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
            {
                toggle_favorite( state , process.pid );
            }

            if ( double_clicked )
            {
                state.selected_pid = static_cast< int >( process.pid );
                std::strncpy( state.search , name_utf8.c_str( ) , sizeof( state.search ) - 1 );
                start_injection( state );
            }

            ImGui::PopID( );
        }

        ImGui::EndTable( );

        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::IsAnyItemActive( ) )
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
                std::strncpy( state.search , wide_to_utf8( focused_process.name ).c_str( ) , sizeof( state.search ) - 1 );

                if ( ImGui::IsKeyPressed( ImGuiKey_Enter ) )
                {
                    start_injection( state );
                }
            }
        }
    }

    void draw_recent_dll_cards( gui_app_state& state )
    {
        if ( state.config.recent_dlls.empty( ) )
        {
            return;
        }

        ImGui::TextUnformatted( "Recent Payloads" );
        ImGui::Spacing( );

        const ImVec4 dim_text = state.light_mode ? ImVec4( 0.40f , 0.42f , 0.46f , 1.0f ) : ImVec4( 0.55f , 0.58f , 0.64f , 1.0f );
        const std::wstring current_dll = utf8_to_wide( state.dll_path );

        for ( int idx = 0; idx < static_cast< int >( state.config.recent_dlls.size( ) ); ++idx )
        {
            const auto& dll_path = state.config.recent_dlls [ static_cast< size_t >( idx ) ];
            const std::filesystem::path path_obj( dll_path );
            const std::string filename = wide_to_utf8( path_obj.filename( ).wstring( ) );
            const std::string directory = wide_to_utf8( path_obj.parent_path( ).wstring( ) );
            const bool is_active = !current_dll.empty( ) && _wcsicmp( current_dll.c_str( ) , dll_path.c_str( ) ) == 0;

            ImGui::PushID( idx );
            const float card_width = ImGui::GetContentRegionAvail( ).x;
            const float card_height = 52.0f;
            const ImVec2 card_pos = ImGui::GetCursorScreenPos( );

            ImGui::InvisibleButton( "##dll_card" , ImVec2( card_width - 36.0f , card_height ) );

            if ( ImGui::IsItemClicked( ) )
            {
                std::strncpy( state.dll_path , wide_to_utf8( dll_path ).c_str( ) , sizeof( state.dll_path ) - 1 );
                refresh_dll_pe( state );
            }

            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImU32 bg = ImGui::GetColorU32( is_active
                ? ( state.light_mode ? ImVec4( 0.82f , 0.90f , 1.00f , 1.0f ) : ImVec4( 0.18f , 0.32f , 0.55f , 0.55f ) )
                : ( ImGui::IsItemHovered( )
                    ? ( state.light_mode ? ImVec4( 0.90f , 0.93f , 0.97f , 1.0f ) : ImVec4( 0.20f , 0.24f , 0.30f , 1.0f ) )
                    : ( state.light_mode ? ImVec4( 0.96f , 0.97f , 0.98f , 1.0f ) : ImVec4( 0.14f , 0.15f , 0.17f , 1.0f ) ) ) );

            draw->AddRectFilled( card_pos , ImVec2( card_pos.x + card_width - 36.0f , card_pos.y + card_height ) , bg , 0.0f );
            draw->AddText( ImVec2( card_pos.x + 10.0f , card_pos.y + 8.0f ) , ImGui::GetColorU32( ImGuiCol_Text ) , filename.c_str( ) );

            if ( !directory.empty( ) )
            {
                draw->AddText(
                    ImGui::GetFont( ) ,
                    ImGui::GetFontSize( ) * 0.85f ,
                    ImVec2( card_pos.x + 10.0f , card_pos.y + 28.0f ) ,
                    ImGui::GetColorU32( dim_text ) ,
                    directory.c_str( ) );
            }

            ImGui::SameLine( 0.0f , 0.0f );
            ImGui::SetCursorScreenPos( ImVec2( card_pos.x + card_width - 32.0f , card_pos.y + 10.0f ) );

            if ( ImGui::Button( "X" , ImVec2( 32.0f , ImGui::GetFrameHeight( ) ) ) )
            {
                remove_recent_dll( state.config , dll_path );
                save_config( state.config );
            }

            ImGui::Dummy( ImVec2( 0.0f , 4.0f ) );
            ImGui::PopID( );
        }

        ImGui::Spacing( );
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

        if ( !state.dll_pe.exports.empty( ) )
        {
            std::string export_line = "Exports: ";

            for ( size_t idx = 0; idx < state.dll_pe.exports.size( ); ++idx )
            {
                if ( idx )
                {
                    export_line += ", ";
                }

                export_line += wide_to_utf8( state.dll_pe.exports [ idx ] );
            }

            ImGui::TextWrapped( "%s" , export_line.c_str( ) );
        }
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

    void draw_settings_panel( gui_app_state& state )
    {
        if ( ImGui::CollapsingHeader( "Settings & Safety" ) )
        {
            ImGui::Checkbox( "Confirm before inject" , &state.confirm_inject );
            ImGui::Checkbox( "Log timestamps" , &state.log_timestamps );
            ImGui::Checkbox( "Allowlist mode (off = blocklist)" , &state.use_allowlist );
            state.config.confirm_inject = state.confirm_inject;
            state.config.log_timestamps = state.log_timestamps;
            state.config.use_allowlist = state.use_allowlist;

            ImGui::InputTextMultiline(
                "Process rules (one per line)" ,
                state.process_rules_text ,
                sizeof( state.process_rules_text ) ,
                ImVec2( -1.0f , 54.0f ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                sync_process_rules( state );
                save_config( state.config );
            }

            ImGui::InputText( "CLI notes (logged on inject)" , state.cli_notes , sizeof( state.cli_notes ) );

            if ( ImGui::IsItemDeactivatedAfterEdit( ) )
            {
                state.config.cli_notes = utf8_to_wide( state.cli_notes );
                save_config( state.config );
            }

            if ( ImGui::Button( "Export Settings" ) )
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

            if ( ImGui::Button( "Import Settings" ) )
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
                        state.confirm_inject = state.config.confirm_inject;
                        state.log_timestamps = state.config.log_timestamps;
                        state.use_allowlist = state.config.use_allowlist;
                        state.panel_split = state.config.panel_split;
                        std::strncpy( state.cli_notes , wide_to_utf8( state.config.cli_notes ).c_str( ) , sizeof( state.cli_notes ) - 1 );

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
                        save_config( state.config );
                        append_log( state , "Settings imported." );
                    }
                }
            }
        }
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
    std::strncpy( state.cli_notes , wide_to_utf8( state.config.cli_notes ).c_str( ) , sizeof( state.cli_notes ) - 1 );

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

    apply_theme( state.light_mode );
    refresh_processes( state );
    append_log( state , "Manual Map Injector ready." );

    if ( !is_process_elevated( ) )
    {
        append_log( state , "Note: not running as administrator — some targets may fail." );
        state.status = "Ready (not elevated)";
    }
}

void gui_state_shutdown( gui_app_state& state )
{
    save_config( state.config );
    g_state = nullptr;
}

void gui_state_on_resize( gui_app_state& state , unsigned int width , unsigned int height )
{
    ( void )state;
    g_resize_w = width;
    g_resize_h = height;
}

void gui_state_new_frame( gui_app_state& state )
{
    flush_pending_log( state );
    poll_injection( state );
    poll_auto_inject( state );
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

    ImGui::SetNextWindowPos( ImVec2( 0.0f , 0.0f ) );
    ImGui::SetNextWindowSize( io.DisplaySize );
    ImGui::Begin(
        "ManualMapInjector" ,
        nullptr ,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus );

    const bool elevated = is_process_elevated( );
    ImGui::Text( "Status: %s" , state.status.c_str( ) );

    if ( state.last_error_code )
    {
        ImGui::SameLine( );
        ImGui::TextDisabled( "(?)" );
        ImGui::SetItemTooltip( "%s" , wide_to_utf8( inject_error_message_w( state.last_error_code ) ).c_str( ) );
    }

    ImGui::SameLine( );
    ImGui::TextDisabled( elevated ? "| Admin" : "| Standard user" );

    if ( !elevated && ImGui::IsItemClicked( ) )
    {
        if ( relaunch_as_admin( L"--gui" ) )
        {
            PostMessageW( static_cast< HWND >( g_gui_hwnd ) , WM_CLOSE , 0 , 0 );
        }
    }

    ImGui::SameLine( io.DisplaySize.x - 130.0f );
    draw_theme_toggle( state );

    if ( state.injecting.load( ) )
    {
        ImGui::ProgressBar( state.inject_progress , ImVec2( -1.0f , 0.0f ) );
    }

    ImGui::Separator( );

    const float left_width = io.DisplaySize.x * state.panel_split;

    ImGui::BeginChild( "LeftPanel" , ImVec2( left_width , 0 ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( "Target Process" );
    ImGui::SetNextItemWidth( -240.0f );
    ImGui::InputTextWithHint(
        "##search" ,
        "Search by name or PID..." ,
        state.search ,
        sizeof( state.search ) ,
        ImGuiInputTextFlags_CallbackEdit ,
        search_input_callback );

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

    const auto visible = visible_processes( state );
    draw_process_table( state , visible , io.DisplaySize.y * 0.30f );

    if ( state.selected_pid > 0 )
    {
        ImGui::TextDisabled( "Selected PID: %d (right-click row for favorites)" , state.selected_pid );
    }
    else if ( state.search [ 0 ] )
    {
        ImGui::TextDisabled( "Inject by name: %s" , state.search );
    }

    ImGui::Checkbox( "Wait up to 30 seconds for process" , &state.wait_for_process );
    ImGui::SameLine( );
    ImGui::Checkbox( "Inject all instances" , &state.inject_all );
    ImGui::Checkbox( "Auto-inject when process appears" , &state.auto_inject );
    ImGui::SameLine( );
    ImGui::SetNextItemWidth( 80.0f );
    ImGui::InputInt( "Delay (s)" , &state.inject_delay_sec );
    state.inject_delay_sec = ( std::max )( 0 , state.inject_delay_sec );

    ImGui::Spacing( );
    ImGui::Separator( );
    ImGui::TextUnformatted( "DLL Payload" );
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

    draw_pe_preview( state );
    draw_settings_panel( state );
    ImGui::EndChild( );

    ImGui::SameLine( );
    ImGui::InvisibleButton( "##splitter" , ImVec2( 4.0f , -1.0f ) );

    if ( ImGui::IsItemActive( ) )
    {
        state.panel_split = ( std::clamp )( state.panel_split + io.MouseDelta.x / io.DisplaySize.x , 0.28f , 0.62f );
        state.config.panel_split = state.panel_split;
    }

    if ( ImGui::IsItemHovered( ) || ImGui::IsItemActive( ) )
    {
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
    }

    ImGui::SameLine( );
    ImGui::BeginChild( "RightPanel" , ImVec2( 0.0f , 0.0f ) , ImGuiChildFlags_Border );
    ImGui::TextUnformatted( "Output Log" );
    ImGui::SetNextItemWidth( 160.0f );
    ImGui::InputTextWithHint( "##logfilter" , "Filter tags..." , state.log_filter , sizeof( state.log_filter ) );
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

    ImGui::BeginChild( "LogScroll" , ImVec2( -1.0f , -48.0f ) , ImGuiChildFlags_Border );
    std::istringstream stream( log_copy );
    std::string line;

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

    if ( ImGui::GetScrollY( ) >= ImGui::GetScrollMaxY( ) )
    {
        ImGui::SetScrollHereY( 1.0f );
    }

    ImGui::EndChild( );

    const bool busy = state.injecting.load( );

    if ( busy )
    {
        ImGui::BeginDisabled( );
    }

    if ( ImGui::Button( "Inject" , ImVec2( 120.0f , 36.0f ) ) )
    {
        start_injection( state );
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Run as Admin" , ImVec2( 120.0f , 36.0f ) ) )
    {
        if ( relaunch_as_admin( L"--gui" ) )
        {
            PostMessageW( static_cast< HWND >( g_gui_hwnd ) , WM_CLOSE , 0 , 0 );
        }
        else
        {
            append_log( state , "Failed to relaunch as administrator." );
        }
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Clear Log" , ImVec2( 100.0f , 36.0f ) ) )
    {
        std::lock_guard lock( state.log_mutex );
        state.log.clear( );
    }

    ImGui::SameLine( );

    if ( ImGui::Button( "Export Log" , ImVec2( 100.0f , 36.0f ) ) )
    {
        export_log( state );
    }

    if ( busy )
    {
        ImGui::EndDisabled( );
    }

    ImGui::EndChild( );
    draw_confirm_popup( state );
    ImGui::End( );
}
