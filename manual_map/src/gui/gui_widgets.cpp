#include "gui_widgets.hpp"
#include "gui_app.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "window_stealth.hpp"

#include "gui_tray.hpp"

#include <app/inject_service.hpp>

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

extern void* g_gui_hwnd;

namespace
{
    bool set_stealth_capture( gui_app_state& state , bool enabled );

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

namespace
{
    ImVec4 button_label_color( bool light_mode )
    {
        return light_mode ? ImVec4( 0.98f , 0.98f , 1.0f , 1.0f ) : ImVec4( 0.96f , 0.96f , 0.98f , 1.0f );
    }
}

bool gui_button( const char* label , const ImVec2& size )
{
    const gui_app_state* app_state = gui_app_state_ptr( );
    const bool style_label = app_state != nullptr;

    if ( style_label )
    {
        ImGui::PushStyleColor( ImGuiCol_Text , button_label_color( app_state->light_mode ) );
    }

    const bool pressed = ImGui::Button( label , size );

    if ( style_label )
    {
        ImGui::PopStyleColor( );
    }

    return pressed;
}

bool gui_small_button( const char* label )
{
    const gui_app_state* app_state = gui_app_state_ptr( );
    const bool style_label = app_state != nullptr;

    if ( style_label )
    {
        ImGui::PushStyleColor( ImGuiCol_Text , button_label_color( app_state->light_mode ) );
    }

    const bool pressed = ImGui::SmallButton( label );

    if ( style_label )
    {
        ImGui::PopStyleColor( );
    }

    return pressed;
}

bool gui_begin_section_card( const char* id , const char* title , bool default_open , bool* open_state , float padding_scale )
{
    const gui_app_state* app_state = gui_app_state_ptr( );
    const float base_pad = app_state ? gui_theme_tokens_for( *app_state ).card_inner_padding : 14.0f;
    const float inner_pad = base_pad * padding_scale;

    ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding , 6.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( inner_pad , inner_pad ) );
    ImGui::PushStyleColor( ImGuiCol_ChildBg , ImGui::GetStyleColorVec4( ImGuiCol_ChildBg ) );
    ImGui::BeginChild(
        id ,
        ImVec2( -1.0f , 0.0f ) ,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize ,
        gui_child_scroll_flags( ) );

    if ( open_state )
    {
        const bool open = ImGui::CollapsingHeader( title , open_state ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen );

        if ( !open )
        {
            ImGui::EndChild( );
            ImGui::PopStyleColor( );
            ImGui::PopStyleVar( 2 );
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
    ImGui::PopStyleVar( 2 );
    ImGui::Spacing( );
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
    const ImVec4 accent = gui_theme_accent( );
    const ImU32 fill = ImGui::GetColorU32( ImVec4( accent.x , accent.y , accent.z , 0.18f * alpha ) );
    draw->AddRectFilled( ImVec2( 0 , 0 ) , io.DisplaySize , fill );
    const char* text = "Drop DLL here";
    const ImVec2 text_size = ImGui::CalcTextSize( text );
    draw->AddText( ImVec2( ( io.DisplaySize.x - text_size.x ) * 0.5f , ( io.DisplaySize.y - text_size.y ) * 0.5f ) , IM_COL32( 255 , 255 , 255 , static_cast< int >( 220 * alpha ) ) , text );
}
