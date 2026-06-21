#include "gui_widgets.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "window_stealth.hpp"

#include "gui_tray.hpp"

#include <app/inject_service.hpp>

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>

extern void* g_gui_hwnd;

namespace
{
    bool set_stealth_capture( gui_app_state& state , bool enabled );

    std::string wide_to_utf8_local( const std::wstring& text )
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

    bool set_stealth_capture( gui_app_state& state , bool enabled )
    {
        std::wstring error;
        const bool applied = apply_capture_stealth( g_gui_hwnd , enabled , &error );

        if ( !applied )
        {
            gui_push_toast( state , "Stealth capture failed" , toast_type::error_type );
            return false;
        }

        state.stealth_capture = enabled;
        state.config.stealth_capture = enabled;
        save_config( state.config );
        return true;
    }

    void title_bar_button( const char* id , const char* label , float width , float height )
    {
        ImGui::PushStyleColor( ImGuiCol_Button , ImVec4( 0 , 0 , 0 , 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered , ImVec4( 1 , 1 , 1 , 0.08f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive , ImVec4( 1 , 1 , 1 , 0.12f ) );
        ImGui::Button( id , ImVec2( width , height ) );
        const ImVec2 min = ImGui::GetItemRectMin( );
        const ImVec2 max = ImGui::GetItemRectMax( );
        ImGui::PopStyleColor( 3 );
        ImGui::GetWindowDrawList( )->AddText(
            ImVec2( min.x + ( max.x - min.x - ImGui::CalcTextSize( label ).x ) * 0.5f , min.y + ( max.y - min.y - ImGui::GetTextLineHeight( ) ) * 0.5f ) ,
            ImGui::GetColorU32( ImGuiCol_Text ) ,
            label );
    }
}

void gui_push_toast( gui_app_state& state , const char* message , toast_type type )
{
    gui_toast toast {};
    toast.message = message ? message : "";
    toast.type = type;
    toast.remaining = 3.0f;
    state.toasts.push_back( std::move( toast ) );
}

void gui_draw_toasts( gui_app_state& state )
{
    const ImGuiIO& io = ImGui::GetIO( );
    float y = io.DisplaySize.y - 16.0f;

    for ( int idx = static_cast< int >( state.toasts.size( ) ) - 1; idx >= 0; --idx )
    {
        gui_toast& toast = state.toasts [ static_cast< size_t >( idx ) ];
        toast.remaining -= io.DeltaTime;

        if ( toast.remaining <= 0.0f )
        {
            state.toasts.erase( state.toasts.begin( ) + idx );
            continue;
        }

        const float alpha = ( std::min )( 1.0f , toast.remaining );
        ImVec4 bg = ImVec4( 0.18f , 0.19f , 0.22f , 0.92f * alpha );

        if ( toast.type == toast_type::success )
        {
            bg = ImVec4( 0.15f , 0.45f , 0.28f , 0.92f * alpha );
        }
        else if ( toast.type == toast_type::error_type )
        {
            bg = ImVec4( 0.55f , 0.18f , 0.18f , 0.92f * alpha );
        }

        const ImVec2 text_size = ImGui::CalcTextSize( toast.message.c_str( ) );
        const ImVec2 pad( 14.0f , 10.0f );
        const ImVec2 size( text_size.x + pad.x * 2.0f , text_size.y + pad.y * 2.0f );
        const ImVec2 pos( io.DisplaySize.x - size.x - 16.0f , y - size.y );
        ImDrawList* draw = ImGui::GetForegroundDrawList( );
        draw->AddRectFilled( pos , ImVec2( pos.x + size.x , pos.y + size.y ) , ImGui::GetColorU32( bg ) , 6.0f );
        draw->AddText( ImVec2( pos.x + pad.x , pos.y + pad.y ) , IM_COL32( 255 , 255 , 255 , static_cast< int >( 255 * alpha ) ) , toast.message.c_str( ) );
        y -= size.y + 8.0f;
    }
}

void gui_draw_help_marker( const char* description )
{
    ImGui::SameLine( );
    ImGui::TextDisabled( "(?)" );

    if ( ImGui::BeginItemTooltip( ) )
    {
        ImGui::PushTextWrapPos( ImGui::GetFontSize( ) * 32.0f );
        ImGui::TextUnformatted( description );
        ImGui::PopTextWrapPos( );
        ImGui::EndTooltip( );
    }
}

bool gui_draw_pill_toggle( gui_app_state& state , const char* id , bool* value , const char* label_on , const char* label_off , const char* tooltip )
{
    ImGuiStyle& style = ImGui::GetStyle( );
    const float height = ImGui::GetFrameHeight( );
    const float width = height * 2.0f;
    const float radius = height * 0.5f;
    const ImVec2 pos = ImGui::GetCursorScreenPos( );
    const ImGuiID widget_id = ImGui::GetID( id );

    ImGui::InvisibleButton( id , ImVec2( width , height ) );

    if ( ImGui::IsItemClicked( ) )
    {
        *value = !*value;
    }

    float& anim = state.pill_anim [ widget_id ];

    if ( anim < 0.0f )
    {
        anim = *value ? 1.0f : 0.0f;
    }

    const float target = *value ? 1.0f : 0.0f;
    anim += ( target - anim ) * ( std::min )( 1.0f , ImGui::GetIO( ).DeltaTime * 12.0f );

    const ImVec4 accent = gui_theme_accent( state.config.accent_index );
    const ImVec4 track_mix(
        0.22f + ( accent.x - 0.22f ) * anim ,
        0.24f + ( accent.y - 0.24f ) * anim ,
        0.30f + ( accent.z - 0.30f ) * anim ,
        1.0f );
    const ImU32 track_mix_color = ImGui::GetColorU32( track_mix );
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , track_mix_color , radius );

    if ( ImGui::IsItemHovered( ) )
    {
        draw->AddRectFilled( pos , ImVec2( pos.x + width , pos.y + height ) , ImGui::GetColorU32( ImVec4( 1 , 1 , 1 , 0.06f ) ) , radius );
    }

    const float knob_x = pos.x + radius + anim * ( width - radius * 2.0f );
    draw->AddCircleFilled( ImVec2( knob_x , pos.y + radius ) , radius - 3.0f , IM_COL32( 255 , 255 , 255 , 255 ) );

    ImGui::SameLine( 0.0f , style.ItemInnerSpacing.x );
    ImGui::AlignTextToFramePadding( );
    ImGui::TextUnformatted( *value ? label_on : label_off );

    if ( tooltip )
    {
        gui_draw_help_marker( tooltip );
    }

    return ImGui::IsItemClicked( );
}

bool gui_begin_section_card( const char* id , const char* title , bool default_open , bool* open_state )
{
    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding , 6.0f );
    ImGui::PushStyleColor( ImGuiCol_ChildBg , ImGui::GetStyleColorVec4( ImGuiCol_ChildBg ) );
    ImGui::BeginChild( id , ImVec2( -1.0f , 0.0f ) , ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize );

    if ( open_state )
    {
        ImGui::PushStyleColor( ImGuiCol_Header , ImVec4( 0 , 0 , 0 , 0 ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderHovered , ImVec4( 1 , 1 , 1 , 0.04f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderActive , ImVec4( 1 , 1 , 1 , 0.06f ) );
        const bool open = ImGui::CollapsingHeader( title , open_state ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen );
        ImGui::PopStyleColor( 3 );

        if ( !open )
        {
            ImGui::EndChild( );
            ImGui::PopStyleColor( );
            ImGui::PopStyleVar( );
            return false;
        }
    }
    else
    {
        ImGui::TextUnformatted( title );
        ImGui::Separator( );
    }

    ImGui::Spacing( );
    return true;
}

void gui_end_section_card( )
{
    ImGui::EndChild( );
    ImGui::PopStyleColor( );
    ImGui::PopStyleVar( );
    ImGui::Spacing( );
}

void gui_draw_title_bar( gui_app_state& state , void* hwnd )
{
    const auto& tokens = gui_theme_tokens_for( state );
    const ImGuiIO& io = ImGui::GetIO( );
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 bar_min = ImGui::GetCursorScreenPos( );
    const ImVec2 bar_max( bar_min.x + io.DisplaySize.x , bar_min.y + tokens.title_bar_height );
    const ImU32 bar_bg = ImGui::GetColorU32( state.light_mode ? ImVec4( 0.98f , 0.98f , 0.99f , 1.0f ) : ImVec4( 0.08f , 0.08f , 0.09f , 1.0f ) );
    draw->AddRectFilled( bar_min , bar_max , bar_bg );

    float x = bar_min.x + 12.0f;
    const float icon_size = tokens.title_bar_height - 12.0f;

    if ( state.app_icon_texture )
    {
        draw->AddImage( state.app_icon_texture , ImVec2( x , bar_min.y + 6.0f ) , ImVec2( x + icon_size , bar_min.y + 6.0f + icon_size ) );
        x += icon_size + 8.0f;
    }

    if ( state.font_header )
    {
        ImGui::PushFont( state.font_header );
    }

    draw->AddText( ImVec2( x , bar_min.y + ( tokens.title_bar_height - ImGui::GetTextLineHeight( ) ) * 0.5f ) , ImGui::GetColorU32( ImGuiCol_Text ) , "Manual Map Injector" );

    if ( state.font_header )
    {
        ImGui::PopFont( );
    }

    ImGui::SetCursorScreenPos( ImVec2( bar_min.x , bar_min.y ) );
    ImGui::InvisibleButton( "##title_drag" , ImVec2( io.DisplaySize.x - 120.0f , tokens.title_bar_height ) );

    if ( ImGui::IsItemActive( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        ReleaseCapture( );
        SendMessageW( static_cast< HWND >( hwnd ) , WM_NCLBUTTONDOWN , HTCAPTION , 0 );
    }

    const float btn_w = 40.0f;
    ImGui::SetCursorScreenPos( ImVec2( bar_max.x - btn_w * 3.0f , bar_min.y + 2.0f ) );
    title_bar_button( "##min" , "_" , btn_w , tokens.title_bar_height - 4.0f );

    if ( ImGui::IsItemClicked( ) )
    {
        if ( state.config.min_to_tray )
        {
            gui_tray_minimize_to_tray( hwnd );
        }
        else
        {
            ShowWindow( static_cast< HWND >( hwnd ) , SW_MINIMIZE );
        }
    }

    ImGui::SetCursorScreenPos( ImVec2( bar_max.x - btn_w * 2.0f , bar_min.y + 2.0f ) );
    title_bar_button( "##max" , state.window_maximized ? "R" : "[]" , btn_w , tokens.title_bar_height - 4.0f );

    if ( ImGui::IsItemClicked( ) )
    {
        if ( state.window_maximized )
        {
            ShowWindow( static_cast< HWND >( hwnd ) , SW_RESTORE );
            state.window_maximized = false;
        }
        else
        {
            ShowWindow( static_cast< HWND >( hwnd ) , SW_MAXIMIZE );
            state.window_maximized = true;
        }
    }

    ImGui::SetCursorScreenPos( ImVec2( bar_max.x - btn_w , bar_min.y + 2.0f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered , ImVec4( 0.85f , 0.20f , 0.20f , 0.85f ) );
    title_bar_button( "##close" , "X" , btn_w , tokens.title_bar_height - 4.0f );
    ImGui::PopStyleColor( );

    if ( ImGui::IsItemClicked( ) )
    {
        PostMessageW( static_cast< HWND >( hwnd ) , WM_CLOSE , 0 , 0 );
    }

    ImGui::SetCursorScreenPos( ImVec2( bar_min.x , bar_max.y ) );
    ImGui::Dummy( ImVec2( 0.0f , 0.0f ) );
}

void gui_draw_status_bar( gui_app_state& state )
{
    const auto& tokens = gui_theme_tokens_for( state );
    const ImGuiIO& io = ImGui::GetIO( );
    const ImVec2 bar_min( 0.0f , io.DisplaySize.y - tokens.status_bar_height );
    const ImVec2 bar_max( io.DisplaySize.x , io.DisplaySize.y );
    ImDrawList* draw = ImGui::GetForegroundDrawList( );
    draw->AddRectFilled( bar_min , bar_max , ImGui::GetColorU32( state.light_mode ? ImVec4( 0.92f , 0.93f , 0.95f , 1.0f ) : ImVec4( 0.07f , 0.07f , 0.08f , 1.0f ) ) );

    std::string target = "No target";

    if ( state.selected_pid > 0 )
    {
        for ( const auto& process : state.all_processes )
        {
            if ( static_cast< int >( process.pid ) == state.selected_pid )
            {
                target = std::to_string( process.pid ) + " " + wide_to_utf8_local( process.name );
                break;
            }
        }
    }
    else if ( state.search [ 0 ] )
    {
        target = state.search;
    }

    const bool elevated = is_process_elevated( );
    const float y = bar_min.y + ( tokens.status_bar_height - ImGui::GetTextLineHeight( ) ) * 0.5f;
    draw->AddText( ImVec2( 12.0f , y ) , ImGui::GetColorU32( ImGuiCol_Text ) , state.status.c_str( ) );
    draw->AddText( ImVec2( io.DisplaySize.x * 0.35f , y ) , ImGui::GetColorU32( ImGuiCol_TextDisabled ) , target.c_str( ) );
    draw->AddText( ImVec2( io.DisplaySize.x - 120.0f , y ) , ImGui::GetColorU32( elevated ? gui_theme_accent( state.config.accent_index ) : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) ) , elevated ? "Admin" : "Standard" );
}

void gui_draw_sidebar( gui_app_state& state )
{
    const auto& tokens = gui_theme_tokens_for( state );
    ImGui::BeginChild( "Sidebar" , ImVec2( tokens.sidebar_width , -tokens.status_bar_height ) , ImGuiChildFlags_None );

    struct nav_item { gui_page page; const char* label; };
    static const nav_item items [ ] =
    {
        { gui_page::injection , "Injection" } ,
        { gui_page::history , "History" } ,
        { gui_page::settings , "Settings" }
    };

    ImGui::Spacing( );

    for ( const auto& item : items )
    {
        const bool selected = state.current_page == item.page;
        ImGui::PushStyleColor( ImGuiCol_Header , selected ? gui_theme_accent_muted( state.config.accent_index , state.light_mode ) : ImVec4( 0 , 0 , 0 , 0 ) );

        if ( ImGui::Selectable( item.label , selected , 0 , ImVec2( -1.0f , tokens.row_height * 0.75f ) ) )
        {
            state.current_page = item.page;
        }

        ImGui::PopStyleColor( );
    }

    ImGui::EndChild( );
    ImGui::SameLine( );
}

void gui_draw_command_palette( gui_app_state& state )
{
    if ( !state.command_palette_open )
    {
        return;
    }

    ImGui::OpenPopup( "CommandPalette" );
    ImGui::SetNextWindowSize( ImVec2( 420.0f , 280.0f ) , ImGuiCond_Appearing );
    ImGui::SetNextWindowPos( ImGui::GetMainViewport( )->GetCenter( ) , ImGuiCond_Appearing , ImVec2( 0.5f , 0.35f ) );

    if ( ImGui::BeginPopupModal( "CommandPalette" , &state.command_palette_open , ImGuiWindowFlags_NoResize ) )
    {
        ImGui::TextDisabled( "Ctrl+K — quick actions" );
        ImGui::Separator( );

        if ( ImGui::Selectable( "Refresh process list" ) )
        {
            state.pending_palette_refresh = true;
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::Selectable( "Inject now" ) )
        {
            state.pending_palette_inject = true;
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::Selectable( "Toggle light/dark theme" ) )
        {
            state.light_mode = !state.light_mode;
            state.config.light_mode = state.light_mode;
            gui_theme_apply( state );
            save_config( state.config );
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::Selectable( "Toggle stealth capture" ) )
        {
            set_stealth_capture( state , !state.stealth_capture );
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::Selectable( "Go to Settings" ) )
        {
            state.current_page = gui_page::settings;
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::Selectable( "Go to History" ) )
        {
            state.current_page = gui_page::history;
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            state.command_palette_open = false;
            ImGui::CloseCurrentPopup( );
        }

        ImGui::EndPopup( );
    }
}

void gui_draw_first_run_wizard( gui_app_state& state )
{
    if ( !state.show_wizard )
    {
        return;
    }

    ImGui::OpenPopup( "FirstRunWizard" );
    ImGui::SetNextWindowSize( ImVec2( 480.0f , 260.0f ) , ImGuiCond_Appearing );
    ImGui::SetNextWindowPos( ImGui::GetMainViewport( )->GetCenter( ) , ImGuiCond_Appearing , ImVec2( 0.5f , 0.5f ) );

    if ( ImGui::BeginPopupModal( "FirstRunWizard" , nullptr , ImGuiWindowFlags_NoResize ) )
    {
        if ( state.wizard_step == 0 )
        {
            ImGui::TextUnformatted( "Step 1: Select your DLL payload" );
            ImGui::TextWrapped( "Use Browse or drag-and-drop a .dll file onto the window." );
        }
        else if ( state.wizard_step == 1 )
        {
            ImGui::TextUnformatted( "Step 2: Choose a target process" );
            ImGui::TextWrapped( "Search the process list and click a row to select. Right-click to favorite." );
        }
        else
        {
            ImGui::TextUnformatted( "Step 3: Ready to inject" );
            ImGui::TextWrapped( "Press Inject or Enter when a target and DLL are set. Use F5 to refresh processes." );
        }

        ImGui::Spacing( );
        ImGui::Separator( );
        ImGui::Spacing( );

        if ( state.wizard_step > 0 && ImGui::Button( "Back" ) )
        {
            --state.wizard_step;
        }

        ImGui::SameLine( );

        if ( state.wizard_step < 2 && ImGui::Button( "Next" ) )
        {
            ++state.wizard_step;
        }

        ImGui::SameLine( );

        if ( ImGui::Button( state.wizard_step < 2 ? "Skip" : "Finish" ) )
        {
            state.show_wizard = false;
            state.config.first_run_complete = true;
            save_config( state.config );
            ImGui::CloseCurrentPopup( );
        }

        ImGui::EndPopup( );
    }
}

void gui_draw_drag_overlay( gui_app_state& state )
{
    if ( state.drag_highlight_timer <= 0.0f )
    {
        return;
    }

    state.drag_highlight_timer -= ImGui::GetIO( ).DeltaTime;
    const ImGuiIO& io = ImGui::GetIO( );
    ImDrawList* draw = ImGui::GetForegroundDrawList( );
    const float alpha = ( std::min )( 1.0f , state.drag_highlight_timer );
    const ImU32 fill = ImGui::GetColorU32( ImVec4( gui_theme_accent( state.config.accent_index ).x , gui_theme_accent( state.config.accent_index ).y , gui_theme_accent( state.config.accent_index ).z , 0.18f * alpha ) );
    draw->AddRectFilled( ImVec2( 0 , 0 ) , io.DisplaySize , fill );
    const char* text = "Drop DLL here";
    const ImVec2 text_size = ImGui::CalcTextSize( text );
    draw->AddText( ImVec2( ( io.DisplaySize.x - text_size.x ) * 0.5f , ( io.DisplaySize.y - text_size.y ) * 0.5f ) , IM_COL32( 255 , 255 , 255 , static_cast< int >( 220 * alpha ) ) , text );
}
