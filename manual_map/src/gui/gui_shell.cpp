#include "gui_shell.hpp"
#include "gui_app.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_tray.hpp"
#include "gui_widgets.hpp"

#include <app/inject_service.hpp>

#include <imgui.h>

#include <windows.h>
#include <windowsx.h>

#include <functional>

namespace
{
    constexpr float k_caption_button_w = 46.0f;
    constexpr int k_resize_border = 8;

    POINT g_drag_anchor {};
    RECT g_drag_window {};
    bool g_drag_active = false;

    ImU32 caption_glyph_color( bool light_mode )
    {
        return ImGui::GetColorU32( light_mode ? ImVec4( 0.12f , 0.12f , 0.14f , 1.0f ) : ImVec4( 0.92f , 0.92f , 0.94f , 1.0f ) );
    }

    void draw_minimize_glyph( ImDrawList* draw , const ImVec2& min , const ImVec2& max , ImU32 color )
    {
        const float cx = ( min.x + max.x ) * 0.5f;
        const float cy = ( min.y + max.y ) * 0.5f + 3.0f;
        draw->AddLine( ImVec2( cx - 5.0f , cy ) , ImVec2( cx + 5.0f , cy ) , color , 1.0f );
    }

    void draw_maximize_glyph( ImDrawList* draw , const ImVec2& min , const ImVec2& max , ImU32 color )
    {
        const float cx = ( min.x + max.x ) * 0.5f;
        const float cy = ( min.y + max.y ) * 0.5f;
        draw->AddRect( ImVec2( cx - 5.0f , cy - 4.0f ) , ImVec2( cx + 5.0f , cy + 6.0f ) , color , 0.0f , 0 , 1.0f );
    }

    void draw_restore_glyph( ImDrawList* draw , const ImVec2& min , const ImVec2& max , ImU32 color )
    {
        const float cx = ( min.x + max.x ) * 0.5f;
        const float cy = ( min.y + max.y ) * 0.5f;
        draw->AddRect( ImVec2( cx - 2.0f , cy - 6.0f ) , ImVec2( cx + 8.0f , cy + 4.0f ) , color , 0.0f , 0 , 1.0f );
        draw->AddRect( ImVec2( cx - 8.0f , cy - 2.0f ) , ImVec2( cx + 2.0f , cy + 8.0f ) , color , 0.0f , 0 , 1.0f );
    }

    void draw_close_glyph( ImDrawList* draw , const ImVec2& min , const ImVec2& max , ImU32 color )
    {
        const float cx = ( min.x + max.x ) * 0.5f;
        const float cy = ( min.y + max.y ) * 0.5f;
        draw->AddLine( ImVec2( cx - 5.0f , cy - 5.0f ) , ImVec2( cx + 5.0f , cy + 5.0f ) , color , 1.0f );
        draw->AddLine( ImVec2( cx + 5.0f , cy - 5.0f ) , ImVec2( cx - 5.0f , cy + 5.0f ) , color , 1.0f );
    }

    bool caption_button(
        const char* id ,
        const ImVec2& size ,
        bool close_button ,
        void ( *draw_glyph )( ImDrawList* , const ImVec2& , const ImVec2& , ImU32 ) ,
        ImU32 glyph_color )
    {
        ImGui::PushStyleColor( ImGuiCol_Button , ImVec4( 0 , 0 , 0 , 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive , ImVec4( 1 , 1 , 1 , close_button ? 0.0f : 0.12f ) );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered ,
            close_button ? ImVec4( 0.82f , 0.18f , 0.22f , 1.0f ) : ImVec4( 1 , 1 , 1 , 0.08f ) );

        const bool pressed = ImGui::Button( id , size );
        const ImVec2 min = ImGui::GetItemRectMin( );
        const ImVec2 max = ImGui::GetItemRectMax( );
        ImDrawList* draw = ImGui::GetWindowDrawList( );

        if ( close_button && ImGui::IsItemHovered( ) )
        {
            glyph_color = IM_COL32( 255 , 255 , 255 , 255 );
        }

        draw_glyph( draw , min , max , glyph_color );
        ImGui::PopStyleColor( 3 );
        return pressed;
    }

    void handle_title_drag( HWND window )
    {
        if ( ImGui::IsItemActivated( ) )
        {
            GetCursorPos( &g_drag_anchor );
            GetWindowRect( window , &g_drag_window );
            g_drag_active = true;
        }

        if ( !ImGui::IsItemActive( ) )
        {
            g_drag_active = false;
            return;
        }

        if ( !g_drag_active || !ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            return;
        }

        POINT cursor {};
        GetCursorPos( &cursor );
        SetWindowPos(
            window ,
            nullptr ,
            g_drag_window.left + ( cursor.x - g_drag_anchor.x ) ,
            g_drag_window.top + ( cursor.y - g_drag_anchor.y ) ,
            0 ,
            0 ,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    }

    void draw_title_bar( gui_app_state& state , HWND window , float width , float height , float side_padding )
    {
        const ImU32 glyph = caption_glyph_color( state.light_mode );
        const float buttons_w = k_caption_button_w * 3.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        ImDrawList* draw = ImGui::GetWindowDrawList( );

        float label_x = origin.x + side_padding;
        const float icon_size = height - 10.0f;

        if ( state.app_icon_texture )
        {
            draw->AddImage(
                state.app_icon_texture ,
                ImVec2( label_x , origin.y + 5.0f ) ,
                ImVec2( label_x + icon_size , origin.y + 5.0f + icon_size ) );
            label_x += icon_size + 8.0f;
        }

        if ( state.font_header )
        {
            ImGui::PushFont( state.font_header );
        }

        draw->AddText(
            ImVec2( label_x , origin.y + ( height - ImGui::GetTextLineHeight( ) ) * 0.5f ) ,
            ImGui::GetColorU32( ImGuiCol_Text ) ,
            "Manual Map Injector" );

        if ( state.font_header )
        {
            ImGui::PopFont( );
        }

        ImGui::SetCursorScreenPos( origin );
        ImGui::InvisibleButton( "##shell_drag" , ImVec2( width - buttons_w , height ) );
        handle_title_drag( window );

        ImGui::SetCursorScreenPos( ImVec2( origin.x + width - buttons_w , origin.y ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing , ImVec2( 0.0f , 0.0f ) );

        if ( caption_button( "##shell_min" , ImVec2( k_caption_button_w , height ) , false , draw_minimize_glyph , glyph ) )
        {
            if ( state.config.min_to_tray )
            {
                gui_tray_minimize_to_tray( window );
            }
            else
            {
                ShowWindow( window , SW_MINIMIZE );
            }
        }

        ImGui::SameLine( 0.0f , 0.0f );

        const bool maximized = IsZoomed( window ) != FALSE;
        state.window_maximized = maximized;

        if ( caption_button(
                "##shell_max" ,
                ImVec2( k_caption_button_w , height ) ,
                false ,
                maximized ? draw_restore_glyph : draw_maximize_glyph ,
                glyph ) )
        {
            ShowWindow( window , maximized ? SW_RESTORE : SW_MAXIMIZE );
        }

        ImGui::SameLine( 0.0f , 0.0f );

        if ( caption_button( "##shell_close" , ImVec2( k_caption_button_w , height ) , true , draw_close_glyph , glyph ) )
        {
            PostMessageW( window , WM_CLOSE , 0 , 0 );
        }

        ImGui::PopStyleVar( );
    }

    void draw_tab_bar( gui_app_state& state )
    {
        const auto& tokens = gui_theme_tokens_for( state );
        struct tab_item { gui_page page; const char* label; };
        static const tab_item items [ ] =
        {
            { gui_page::injection , "Injection" } ,
            { gui_page::history , "History" } ,
            { gui_page::settings , "Settings" }
        };

        const ImVec4 accent = gui_theme_accent( );
        const float tab_h = tokens.tab_bar_height - 8.0f;
        const float text_pad = ( tab_h - ImGui::GetTextLineHeight( ) ) * 0.5f;
        const float frame_pad_y = text_pad > 0.0f ? text_pad : 0.0f;

        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding , ImVec2( 16.0f , frame_pad_y ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing , ImVec2( 6.0f , 0.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding , ImGui::GetStyle( ).TabRounding );

        for ( size_t index = 0; index < IM_ARRAYSIZE( items ); ++index )
        {
            const auto& item = items [ index ];

            if ( index > 0 )
            {
                ImGui::SameLine( );
            }

            const bool selected = state.current_page == item.page;

            if ( selected )
            {
                const ImVec4 selected_bg = gui_theme_accent_muted( state.light_mode );
                ImGui::PushStyleColor( ImGuiCol_Button , selected_bg );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered , selected_bg );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive , selected_bg );
            }
            else
            {
                ImGui::PushStyleColor( ImGuiCol_Button , ImGui::GetStyleColorVec4( ImGuiCol_FrameBg ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered , ImGui::GetStyleColorVec4( ImGuiCol_FrameBgHovered ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive , ImGui::GetStyleColorVec4( ImGuiCol_FrameBgActive ) );
            }

            if ( ImGui::Button( item.label , ImVec2( 0.0f , tab_h ) ) )
            {
                state.current_page = item.page;
            }

            ImGui::PopStyleColor( 3 );
        }

        ImGui::PopStyleVar( 3 );
    }

    void draw_shell_background( const ImVec2& display , float rounding )
    {
        const ImVec2 origin = ImGui::GetWindowPos( );
        ImGui::GetWindowDrawList( )->AddRectFilled(
            origin ,
            ImVec2( origin.x + display.x , origin.y + display.y ) ,
            ImGui::GetColorU32( ImGuiCol_WindowBg ) ,
            rounding );
    }

    void draw_status_bar_content( gui_app_state& state )
    {
        std::string target = "No target";

        if ( state.selected_pid > 0 )
        {
            for ( const auto& process : state.all_processes )
            {
                if ( static_cast< int >( process.pid ) == state.selected_pid )
                {
                    target = std::to_string( process.pid ) + " " + [ ] ( const std::wstring& name )
                    {
                        if ( name.empty( ) )
                        {
                            return std::string {};
                        }

                        const int length = WideCharToMultiByte( CP_UTF8 , 0 , name.c_str( ) , -1 , nullptr , 0 , nullptr , nullptr );

                        if ( length <= 0 )
                        {
                            return std::string {};
                        }

                        std::string utf8( static_cast< size_t >( length - 1 ) , '\0' );
                        WideCharToMultiByte( CP_UTF8 , 0 , name.c_str( ) , -1 , utf8.data( ) , length , nullptr , nullptr );
                        return utf8;
                    }( process.name );
                    break;
                }
            }
        }
        else if ( state.search [ 0 ] )
        {
            target = state.search;
        }

        const bool elevated = is_process_elevated( );
        const auto& tokens = gui_theme_tokens_for( state );
        const float content_width = ImGui::GetContentRegionAvail( ).x;
        const float right_gutter = tokens.shell_padding * 0.5f;
        const float item_gap = 24.0f;
        ImGui::AlignTextToFramePadding( );
        ImGui::TextUnformatted( state.status.c_str( ) );
        ImGui::SameLine( 0.0f , item_gap );
        ImGui::TextDisabled( "%s" , target.c_str( ) );

        const char* privilege_label = elevated ? "Admin" : "Standard";
        const ImVec2 privilege_size = ImGui::CalcTextSize( privilege_label );
        ImGui::SameLine( content_width - privilege_size.x - right_gutter );
        ImGui::TextColored(
            elevated ? gui_theme_accent( ) : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) ,
            "%s" ,
            privilege_label );
    }

    void begin_shell_child( const char* id , const ImVec2& pos , const ImVec2& size , const ImVec4& bg )
    {
        ImGui::SetCursorPos( pos );
        ImGui::PushStyleColor( ImGuiCol_ChildBg , bg );
        ImGui::BeginChild( id , size , ImGuiChildFlags_None , gui_child_scroll_flags( ) );
    }

    void end_shell_child( )
    {
        ImGui::EndChild( );
        ImGui::PopStyleColor( );
    }
}

LRESULT gui_shell_hit_test( HWND hwnd , LPARAM lparam )
{
    float title_h = 36.0f;

    if ( gui_app_state* state = gui_app_state_ptr( ) )
    {
        title_h = gui_theme_tokens_for( *state ).title_bar_height;
    }

    POINT point { GET_X_LPARAM( lparam ) , GET_Y_LPARAM( lparam ) };
    ScreenToClient( hwnd , &point );

    RECT client {};
    GetClientRect( hwnd , &client );

    if ( point.y < static_cast< LONG >( title_h ) )
    {
        return HTCLIENT;
    }

    const bool on_left = point.x < k_resize_border;
    const bool on_right = point.x >= client.right - k_resize_border;
    const bool on_bottom = point.y >= client.bottom - k_resize_border;

    if ( on_bottom && on_left )
    {
        return HTBOTTOMLEFT;
    }

    if ( on_bottom && on_right )
    {
        return HTBOTTOMRIGHT;
    }

    if ( on_bottom )
    {
        return HTBOTTOM;
    }

    if ( on_left )
    {
        return HTLEFT;
    }

    if ( on_right )
    {
        return HTRIGHT;
    }

    return HTCLIENT;
}

void gui_shell_render(
    gui_app_state& state ,
    void* hwnd ,
    ImGuiIO& io ,
    const std::function< void( ) >& render_page ,
    const std::function< void( ) >& render_overlays )
{
    const auto& tokens = gui_theme_tokens_for( state );
    const HWND window = static_cast< HWND >( hwnd );
    const ImVec2 display = io.DisplaySize;

    const float title_h = tokens.title_bar_height;
    const float tab_h = tokens.tab_bar_height;
    const float status_h = tokens.status_bar_height;
    const float side_pad = tokens.shell_padding;
    const float content_y = title_h + tab_h;
    const float content_h = display.y - content_y - status_h;
    const float inner_w = display.x - side_pad * 2.0f;
    const float rounding = state.window_maximized ? 0.0f : ImGui::GetStyle( ).WindowRounding;
    const ImVec2 content_padding( side_pad , tokens.card_padding );

    const ImVec4 title_bg = state.light_mode ? ImVec4( 0.98f , 0.98f , 0.99f , 1.0f ) : ImVec4( 0.08f , 0.08f , 0.09f , 1.0f );
    const ImVec4 tab_bg = state.light_mode ? ImVec4( 0.96f , 0.96f , 0.97f , 1.0f ) : ImVec4( 0.10f , 0.10f , 0.11f , 1.0f );
    const ImVec4 status_bg = state.light_mode ? ImVec4( 0.92f , 0.93f , 0.95f , 1.0f ) : ImVec4( 0.07f , 0.07f , 0.08f , 1.0f );

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( 0.0f , 0.0f ) );
    ImGui::SetNextWindowPos( ImVec2( 0.0f , 0.0f ) );
    ImGui::SetNextWindowSize( display );
    ImGui::Begin(
        "ManualMapInjector" ,
        nullptr ,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar );

    draw_shell_background( display , rounding );

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( 0.0f , 0.0f ) );
    begin_shell_child( "##shell_title" , ImVec2( 0.0f , 0.0f ) , ImVec2( display.x , title_h ) , title_bg );
    draw_title_bar( state , window , display.x , title_h , side_pad );
    end_shell_child( );
    ImGui::PopStyleVar( );

    begin_shell_child( "##shell_tabs" , ImVec2( side_pad , title_h ) , ImVec2( inner_w , tab_h ) , tab_bg );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( 0.0f , 4.0f ) );
    draw_tab_bar( state );
    ImGui::PopStyleVar( );
    end_shell_child( );

    begin_shell_child( "##shell_main" , ImVec2( side_pad , content_y ) , ImVec2( inner_w , content_h ) , ImGui::GetStyleColorVec4( ImGuiCol_WindowBg ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , content_padding );

    if ( render_page )
    {
        render_page( );
    }

    ImGui::PopStyleVar( );
    end_shell_child( );

    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( side_pad + 32.0f , 6.0f ) );
    begin_shell_child( "##shell_status" , ImVec2( 0.0f , display.y - status_h ) , ImVec2( display.x , status_h ) , status_bg );
    draw_status_bar_content( state );
    end_shell_child( );
    ImGui::PopStyleVar( );

    if ( render_overlays )
    {
        render_overlays( );
    }

    ImGui::End( );
    ImGui::PopStyleVar( );
}
