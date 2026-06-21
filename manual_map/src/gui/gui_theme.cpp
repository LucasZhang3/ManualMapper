#include "gui_theme.hpp"
#include "gui_state.hpp"

#include <imgui.h>

#include <algorithm>

namespace
{
    gui_theme_tokens g_tokens {};

    const ImVec4 g_accents [ 4 ] =
    {
        ImVec4( 0.55f , 0.35f , 0.95f , 1.0f ) ,
        ImVec4( 0.20f , 0.65f , 0.85f , 1.0f ) ,
        ImVec4( 0.90f , 0.45f , 0.35f , 1.0f ) ,
        ImVec4( 0.35f , 0.85f , 0.55f , 1.0f )
    };
}

ImVec4 gui_theme_accent( int accent_index )
{
    const int idx = accent_index < 0 ? 0 : ( accent_index >= 4 ? 3 : accent_index );
    return g_accents [ idx ];
}

ImVec4 gui_theme_accent_muted( int accent_index , bool light_mode )
{
    ImVec4 accent = gui_theme_accent( accent_index );

    if ( light_mode )
    {
        return ImVec4( accent.x * 0.85f + 0.15f , accent.y * 0.85f + 0.15f , accent.z * 0.85f + 0.15f , 0.35f );
    }

    return ImVec4( accent.x , accent.y , accent.z , 0.45f );
}

const gui_theme_tokens& gui_theme_tokens_for( const gui_app_state& state )
{
    g_tokens.body_size = state.config.compact_mode ? 15.0f : 17.0f;
    g_tokens.label_size = state.config.compact_mode ? 13.0f : 15.0f;
    g_tokens.mono_size = state.config.compact_mode ? 14.0f : 16.0f;
    g_tokens.title_bar_height = state.config.compact_mode ? 34.0f : 38.0f;
    g_tokens.tab_bar_height = state.config.compact_mode ? 34.0f : 38.0f;
    g_tokens.status_bar_height = state.config.compact_mode ? 26.0f : 28.0f;
    g_tokens.row_height = state.config.compact_mode ? 40.0f : 44.0f;
    g_tokens.shell_padding = state.config.compact_mode ? 12.0f : 16.0f;
    g_tokens.card_padding = state.config.compact_mode ? 10.0f : 12.0f;
    g_tokens.card_inner_padding = state.config.compact_mode ? 12.0f : 14.0f;
    g_tokens.section_spacing = state.config.compact_mode ? 8.0f : 10.0f;
    return g_tokens;
}

void gui_theme_apply_accent_colors( gui_app_state& state )
{
    const ImVec4 accent = gui_theme_accent( state.config.accent_index );
    ImVec4* style_colors = ImGui::GetStyle( ).Colors;

    style_colors [ ImGuiCol_Header ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.22f : 0.35f );
    style_colors [ ImGuiCol_HeaderHovered ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.32f : 0.50f );
    style_colors [ ImGuiCol_HeaderActive ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.42f : 0.65f );
    style_colors [ ImGuiCol_Button ] = ImVec4( accent.x , accent.y , accent.z , 1.0f );
    style_colors [ ImGuiCol_ButtonHovered ] = ImVec4(
        ( std::min )( accent.x * 1.08f , 1.0f ) ,
        ( std::min )( accent.y * 1.08f , 1.0f ) ,
        ( std::min )( accent.z * 1.08f , 1.0f ) ,
        1.0f );
    style_colors [ ImGuiCol_ButtonActive ] = ImVec4( accent.x * 0.88f , accent.y * 0.88f , accent.z * 0.88f , 1.0f );
    style_colors [ ImGuiCol_CheckMark ] = accent;
    style_colors [ ImGuiCol_SliderGrab ] = accent;
    style_colors [ ImGuiCol_SliderGrabActive ] = ImVec4( accent.x * 0.9f , accent.y * 0.9f , accent.z * 0.9f , 1.0f );
    style_colors [ ImGuiCol_ScrollbarGrab ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.55f : 0.65f );
    style_colors [ ImGuiCol_ScrollbarGrabHovered ] = ImVec4( accent.x , accent.y , accent.z , 0.85f );
    style_colors [ ImGuiCol_ScrollbarGrabActive ] = accent;
    style_colors [ ImGuiCol_TextSelectedBg ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.28f : 0.40f );
    style_colors [ ImGuiCol_NavHighlight ] = ImVec4( accent.x , accent.y , accent.z , 0.70f );
    style_colors [ ImGuiCol_SeparatorHovered ] = ImVec4( accent.x , accent.y , accent.z , 0.55f );
    style_colors [ ImGuiCol_SeparatorActive ] = accent;
    style_colors [ ImGuiCol_Tab ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.10f : 0.14f );
    style_colors [ ImGuiCol_TabHovered ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.20f : 0.28f );
    style_colors [ ImGuiCol_TabSelected ] = gui_theme_accent_muted( state.config.accent_index , state.light_mode );
    style_colors [ ImGuiCol_TabSelectedOverline ] = accent;
    style_colors [ ImGuiCol_FrameBgHovered ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.16f : 0.28f );
    style_colors [ ImGuiCol_FrameBgActive ] = ImVec4( accent.x , accent.y , accent.z , state.light_mode ? 0.24f : 0.38f );
    style_colors [ ImGuiCol_PlotHistogram ] = accent;
    style_colors [ ImGuiCol_PlotHistogramHovered ] = ImVec4( accent.x * 1.05f , accent.y * 1.05f , accent.z * 1.05f , 1.0f );
}

void gui_theme_init( gui_app_state& state )
{
    const auto& tokens = gui_theme_tokens_for( state );
    state.title_bar_height = tokens.title_bar_height;
    state.tab_bar_height = tokens.tab_bar_height;
    gui_theme_apply( state );
}

void gui_theme_apply( gui_app_state& state )
{
    const auto& tokens = gui_theme_tokens_for( state );

    if ( state.light_mode )
    {
        ImGui::StyleColorsLight( );
    }
    else
    {
        ImGui::StyleColorsDark( );
    }

    ImGuiStyle& style = ImGui::GetStyle( );
    const float rounding = state.config.compact_mode ? 5.0f : 6.0f;
    style.WindowRounding = rounding;
    style.ChildRounding = rounding;
    style.FrameRounding = rounding;
    style.PopupRounding = rounding;
    style.ScrollbarRounding = rounding;
    style.GrabRounding = rounding;
    style.TabRounding = rounding;
    style.WindowPadding = ImVec2( tokens.card_padding , tokens.card_padding );
    style.ItemSpacing = ImVec2( 8.0f , state.config.compact_mode ? 5.0f : 6.0f );
    style.FramePadding = ImVec2( 10.0f , state.config.compact_mode ? 5.0f : 6.0f );
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.ScrollbarSize = 0.0f;
    style.ScrollbarRounding = 0.0f;

    ImVec4* style_colors = style.Colors;

    if ( state.light_mode )
    {
        style_colors [ ImGuiCol_Text ] = ImVec4( 0.12f , 0.12f , 0.14f , 1.0f );
        style_colors [ ImGuiCol_WindowBg ] = ImVec4( 0.94f , 0.94f , 0.95f , 1.0f );
        style_colors [ ImGuiCol_ChildBg ] = ImVec4( 0.98f , 0.98f , 0.99f , 1.0f );
        style_colors [ ImGuiCol_PopupBg ] = ImVec4( 0.98f , 0.98f , 0.99f , 0.98f );
        style_colors [ ImGuiCol_Border ] = ImVec4( 0.86f , 0.87f , 0.90f , 0.55f );
        style_colors [ ImGuiCol_TextDisabled ] = ImVec4( 0.45f , 0.47f , 0.52f , 1.0f );
        style_colors [ ImGuiCol_TableRowBgAlt ] = ImVec4( 0.96f , 0.97f , 0.98f , 1.0f );
    }
    else
    {
        style_colors [ ImGuiCol_WindowBg ] = ImVec4( 0.10f , 0.10f , 0.11f , 1.00f );
        style_colors [ ImGuiCol_ChildBg ] = ImVec4( 0.13f , 0.13f , 0.15f , 1.00f );
        style_colors [ ImGuiCol_PopupBg ] = ImVec4( 0.12f , 0.12f , 0.14f , 0.98f );
        style_colors [ ImGuiCol_Border ] = ImVec4( 0.22f , 0.23f , 0.28f , 0.45f );
        style_colors [ ImGuiCol_TextDisabled ] = ImVec4( 0.55f , 0.58f , 0.64f , 1.0f );
        style_colors [ ImGuiCol_TableRowBgAlt ] = ImVec4( 0.11f , 0.11f , 0.12f , 1.0f );
    }

    style_colors [ ImGuiCol_Separator ] = style_colors [ ImGuiCol_Border ];
    style_colors [ ImGuiCol_FrameBg ] = state.light_mode
        ? ImVec4( 0.90f , 0.91f , 0.93f , 1.0f )
        : ImVec4( 0.16f , 0.17f , 0.20f , 1.0f );

    gui_theme_apply_accent_colors( state );

    if ( state.font_body )
    {
        ImGui::GetIO( ).FontDefault = state.font_body;
    }
}
