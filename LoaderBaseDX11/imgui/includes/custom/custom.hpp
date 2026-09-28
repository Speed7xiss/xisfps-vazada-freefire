#pragma once

#include <includes/includes.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

namespace Custom {

	inline void CustomChild( const char* label , ImVec2 size , ImColor color , float rounding , bool border = true ) {
		ImGui::PushStyleColor( ImGuiCol_ChildBg , (ImVec4)color );
		ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding , rounding );
		ImGui::BeginChild( label , size , border );
	}

	inline void EndCustomChild( ImColor border_color , bool allowScroll = false ) {
		ImGui::EndChild( );
		ImGui::PopStyleVar( 1 );
		ImGui::PopStyleColor( 1 );
	}

	inline bool Tab( const char* Label , const char* Icon , bool Enabled , ImVec2 size ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems )
			return false;

		ImGui::PushFont( globals.intersemi );
		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID( Label );
		const ImVec2 label_size = ImGui::CalcTextSize( Label , NULL , true );

		const ImVec2 pos = window->DC.CursorPos;
		const ImRect total_bb( pos , pos + size );

		ImGui::PopFont( );
		ImGui::ItemSize( total_bb , style.FramePadding.y );
		if ( !ImGui::ItemAdd( total_bb , id ) )
			return false;

		bool hovered , held;
		bool pressed = ImGui::ButtonBehavior( total_bb , id , &hovered , &held );

		if ( Enabled ) {
			ImGui::PushFont( globals.font_awesome );
			window->DrawList->AddText( total_bb.Min + ImVec2( 15.f - ImGui::CalcTextSize( Icon ).x / 2.f , total_bb.GetHeight( ) / 2.f - ImGui::CalcTextSize( Icon ).y / 2.8f ) , IM_COL32( 80 , 80 , 80 , ( int ) ( ImGui::GetStyle( ).Alpha * 255 ) ) , Icon );
			ImGui::PopFont( );

			ImGui::PushFont( globals.intersemi );
			window->DrawList->AddText( total_bb.Min + ImVec2( 30.f , total_bb.GetHeight( ) / 2.f - ImGui::CalcTextSize( Label ).y / 1.9f ) , IM_COL32( 80 , 80 , 80 , ( int ) ( ImGui::GetStyle( ).Alpha * 255 ) ) , Label );
			ImGui::PopFont( );

			float indicatorWidth = 4.0f;
			float indicatorHeight = 21.0f;
			float indicatorOffset = -6.0f;
			float indicatorYPos = ( total_bb.Max.y - total_bb.Min.y - indicatorHeight ) * 0.6f;

			float cur_y = window->StateStorage.GetFloat( id + 10 , total_bb.Min.y + indicatorYPos );
			cur_y = ImLerp( cur_y , total_bb.Min.y + indicatorYPos , 0.1f );
			window->StateStorage.SetFloat( id + 10 , cur_y );

			ImVec2 indicatorMin = ImVec2( total_bb.Min.x + indicatorOffset , cur_y );
			ImVec2 indicatorMax = ImVec2( indicatorMin.x + indicatorWidth , indicatorMin.y + indicatorHeight );
			window->DrawList->AddRectFilled( indicatorMin , indicatorMax , IM_COL32( 80 , 80 , 80 , ( int ) ( ImGui::GetStyle( ).Alpha * 255 ) ) , 1.0f );
		}
		else {
			ImGui::PushFont( globals.font_awesome );
			window->DrawList->AddText( total_bb.Min + ImVec2( 15.f - ImGui::CalcTextSize( Icon ).x / 2.f , total_bb.GetHeight( ) / 2.f - ImGui::CalcTextSize( Icon ).y / 2.8f ) , IM_COL32( 50 , 50 , 50 , ( int ) ( ImGui::GetStyle( ).Alpha * 255 ) ) , Icon );
			ImGui::PopFont( );

			ImGui::PushFont( globals.intersemi );
			window->DrawList->AddText( total_bb.Min + ImVec2( 30.f , total_bb.GetHeight( ) / 2.f - ImGui::CalcTextSize( Label ).y / 1.9f ) , IM_COL32( 50 , 50 , 50 , ( int ) ( ImGui::GetStyle( ).Alpha * 255 ) ) , Label );
			ImGui::PopFont( );
		}

		return pressed;
	}

	inline bool CheckBox( const char* Label , bool* Checked ) {
		ImGuiWindow* Window = ImGui::GetCurrentWindow( );
		if ( Window->SkipItems )
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = Window->GetID( Label );

		float Width = ImGui::GetWindowContentRegionMax( ).x - ImGui::GetWindowContentRegionMin( ).x - ( Window->ScrollbarY ? 5.f : 0.f );
		ImVec2 TextSize = ImGui::CalcTextSize( Label );
		const ImVec2 CheckBoxSize( 22 , 22 );

		const ImVec2 Pos = Window->DC.CursorPos;
		const ImRect Rect( Pos , Pos + ImVec2( Width , CheckBoxSize.y - 4 ) );
		const ImRect Clickable( Pos , Pos + ImVec2( CheckBoxSize.x + TextSize.x + 8 , CheckBoxSize.y ) );

		ImGui::ItemSize( Rect , style.FramePadding.y );
		if ( !ImGui::ItemAdd( Rect , id , &Clickable ) )
			return false;

		bool Hovered , Held;
		bool Pressed = ImGui::ButtonBehavior( Clickable , id , &Hovered , &Held );
		if ( Pressed ) {
			*Checked = !( *Checked );
			ImGui::MarkItemEdited( id );
		}

		float anim_progress = Window->StateStorage.GetFloat( id , 0.0f );
		anim_progress = ImLerp( anim_progress , *Checked ? 1.0f : 0.0f , g.IO.DeltaTime * 10.0f );
		Window->StateStorage.SetFloat( id , anim_progress );

		ImU32 bg_color_anim = ImGui::ColorConvertFloat4ToU32( ImLerp( ImVec4( 0.086f , 0.086f , 0.090f , 1.0f ) , (ImVec4)g_Col.Base , anim_progress ) );
		ImU32 label_color_anim = ImGui::ColorConvertFloat4ToU32( ImLerp( ImVec4( 0.314f , 0.314f , 0.314f , 1.0f ) , ImVec4( 1.0f , 1.0f , 1.0f , 1.0f ) , anim_progress ) );
		
		ImVec2 bg_size_anim = ImLerp( ImVec2( 0 , 0 ) , ImVec2(CheckBoxSize.x / 2.0f, CheckBoxSize.y / 2.0f) , anim_progress );
		
		float check_alpha = ( anim_progress > 0.5f ) ? ( anim_progress - 0.5f ) * 2.0f : 0.0f;
		ImU32 check_color_anim = ImGui::ColorConvertFloat4ToU32( ImVec4( 0.086f , 0.086f , 0.090f , check_alpha ) );

		ImVec2 CheckBoxCenter( Rect.Min + ImVec2(CheckBoxSize.x / 2.0f, CheckBoxSize.y / 2.0f) );
		Window->DrawList->AddRectFilled( Rect.Min , Rect.Min + CheckBoxSize , IM_COL32( 22 , 22 , 23 , 255 ) , 4.f );
		Window->DrawList->AddRectFilled( CheckBoxCenter - bg_size_anim , CheckBoxCenter + bg_size_anim , bg_color_anim , 4.f );

		ImGui::RenderCheckMark( Window->DrawList , Rect.Min + ImVec2( CheckBoxSize.x / 4.0f , CheckBoxSize.y / 4.0f ) , check_color_anim , CheckBoxSize.x / 2.0f );

		ImGui::PushFont( globals.intersemi );
		Window->DrawList->AddText( ImVec2( Rect.Min.x + CheckBoxSize.x + 8.f , Rect.Min.y + Rect.GetHeight( ) / 2.0f - TextSize.y / 2.0f ) , label_color_anim , Label , ImGui::FindRenderedTextEnd( Label ) );
		ImGui::PopFont( );

		return Pressed;
	}

	const char* const KeyNames [ ] = { "None", "Left Button", "Right Button", "Cancel", "Mouse Button", "Mouse 5", "Mouse 6", "Unknown", "Backspace", "TAB", "Unknown", "Unknown", "Clear", "Enter", "Unknown", "Unknown", "Shift", "Control", "ALT", "Pause", "Capital", "Kana", "Unknown", "Junja", "Final", "Kanji", "Unknown", "Escape", "Convert", "Nonconvert", "Accpet", "Mode Changer", "Space", "Prior", "Next", "End", "Home", "Left", "Up", "Right", "Down", "Select", "Print", "Execute", "Snapshot", "Insert", "Delete", "Help", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Winndows", "Right Windows", "Apps", "Unknown", "Sleep", "Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Multiply", "Add", "Separator", "Subtract", "Decimal", "Divided", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "NumLock", "Scroll", "VK_OEM_NEC_EQUAL", "VK_OEM_FJ_MASSHOU", "VK_OEM_FJ_TOUROKU", "VK_OEM_FJ_LOYA", "VK_OEM_FJ_ROYA", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu" };

	inline bool KeyBindCustom( const char* label , int* Key , int* KeyState ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems )
			return false;

		ImGuiContext& g = *GImGui;
		ImGuiIO& io = g.IO;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID( label );

		ImGui::PushFont( globals.intersemi );
		
		float anim_a = window->StateStorage.GetFloat( id , 0.0f );
		anim_a = ImLerp( anim_a , ( ImGui::CalcTextSize( KeyNames [ *Key ] ).x + 25 + 20 ) , g.IO.DeltaTime * 10.f );
		window->StateStorage.SetFloat( id , anim_a );

		const ImVec2 label_size = ImGui::CalcTextSize( label , NULL , true );
		const float bind_x_pos = ( label_size.x > 0 && label [ 0 ] != '#' ) ? label_size.x + 10 : 0;
		const ImRect frame_bb( window->DC.CursorPos + ImVec2( bind_x_pos , 0 ) , window->DC.CursorPos + ImVec2( bind_x_pos + anim_a , 25 ) );
		const ImRect total_bb( window->DC.CursorPos , window->DC.CursorPos + ImVec2( window->Size.x - 20 , 25 ) );

		ImGui::PopFont( );
		ImGui::ItemSize( total_bb , style.FramePadding.y );
		if ( !ImGui::ItemAdd( total_bb , id ) )
			return false;

		const bool hovered = ImGui::ItemHoverable( frame_bb , id , ImGuiWindowFlags_None );
		if ( hovered ) {
			ImGui::SetHoveredID( id );
			g.MouseCursor = ImGuiMouseCursor_TextInput;
		}

		char buf_display [ 64 ] = "None";
		if ( *Key != 0 && g.ActiveId != id ) {
			strcpy( buf_display , KeyNames [ *Key ] );
		}
		else if ( g.ActiveId == id ) {
			strcpy( buf_display , "Press" );
		}

		char* output = strstr( buf_display , "None" );
		char* output2 = strstr( buf_display , "Press" );

		float anim_b_progress = window->StateStorage.GetFloat( id + 1 , 0.0f );
		anim_b_progress = ImLerp( anim_b_progress , ( output || output2 ) ? 0.0f : 1.0f , g.IO.DeltaTime * 10.f );
		window->StateStorage.SetFloat( id + 1 , anim_b_progress );
		ImU32 anim_b = ImGui::ColorConvertFloat4ToU32( ImLerp( ImVec4( 0.3f , 0.3f , 0.3f , 1.0f ) , ImVec4( 0.5f , 0.5f , 0.5f , 1.0f ) , anim_b_progress ) );

		ImGui::PushFont( globals.intersemi );
		window->DrawList->AddText( ImVec2( total_bb.Min.x , frame_bb.Min.y + frame_bb.GetHeight( ) / 2 - ImGui::CalcTextSize( label ).y / 2 ) , IM_COL32( 80 , 80 , 80 , 255 ) , label , ImGui::FindRenderedTextEnd( label ) );

		window->DrawList->AddRectFilled( frame_bb.Min , frame_bb.Max , IM_COL32( 22 , 22 , 23 , 255 ) , 5 );
		window->DrawList->AddText( ImVec2( frame_bb.Min.x + 7 , frame_bb.Min.y + frame_bb.GetHeight( ) / 2 - ImGui::CalcTextSize( label ).y / 2 ) , anim_b , buf_display );
		window->DrawList->AddRect( frame_bb.Min , frame_bb.Max , ImGui::GetColorU32( ImGuiCol_Border ) , 5 );
		ImGui::PopFont( );

		const bool user_clicked = hovered && io.MouseClicked [ 0 ];
		if ( user_clicked ) {
			if ( g.ActiveId != id ) {
				memset( io.MouseDown , 0 , sizeof( io.MouseDown ) );
				memset( io.KeysDown , 0 , sizeof( io.KeysDown ) );
				*Key = 0;
			}
			ImGui::SetActiveID( id , window );
			ImGui::FocusWindow( window );
		}
		else if ( io.MouseClicked [ 0 ] ) {
			if ( g.ActiveId == id )
				ImGui::ClearActiveID( );
		}

		bool value_changed = false;
		int key = *Key;

		if ( g.ActiveId == id ) {
			for ( auto i = 0; i < 5; i++ ) {
				if ( io.MouseDown [ i ] ) {
					switch ( i ) {
					case 0: key = VK_LBUTTON; break;
					case 1: key = VK_RBUTTON; break;
					case 2: key = VK_MBUTTON; break;
					case 3: key = VK_XBUTTON1; break;
					case 4: key = VK_XBUTTON2; break;
					}
					value_changed = true;
					ImGui::ClearActiveID( );
				}
			}
			if ( !value_changed ) {
				for ( auto i = VK_BACK; i <= VK_RMENU; i++ ) {
					if ( io.KeysDown [ i ] ) {
						key = i;
						value_changed = true;
						ImGui::ClearActiveID( );
					}
				}
			}

			if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) ) {
				*Key = 0;
				ImGui::ClearActiveID( );
			}
			else {
				*Key = key;
			}
		}
		return value_changed;
	}

	inline bool SliderHash( const char* label , ImGuiDataType data_type , void* p_data , const void* p_min , const void* p_max , const char* format , ImGuiSliderFlags flags ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems )
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID( label );

		const float w = ImGui::GetWindowSize( ).x - 20;
		const float frame_height = 17.0f;
		const float text_offset = 6.0f;

		const ImVec2 label_size = ImGui::CalcTextSize( label , NULL , true );
		const ImRect frame_bb( window->DC.CursorPos + ImVec2( 0 , label_size.y + text_offset ) , window->DC.CursorPos + ImVec2( w , label_size.y + text_offset + frame_height ) );
		const ImRect total_bb( window->DC.CursorPos , ImVec2( frame_bb.Max.x , frame_bb.Max.y + style.ItemInnerSpacing.y ) );

		ImGui::ItemSize( total_bb , style.FramePadding.y );
		if ( !ImGui::ItemAdd( total_bb , id , &frame_bb ) )
			return false;

		bool hovered = ImGui::ItemHoverable( frame_bb , id , ImGuiWindowFlags_None );
		if ( hovered && g.IO.MouseClicked [ 0 ] )
			ImGui::SetActiveID( id , window );

		ImRect grab_bb;
		bool value_changed = ImGui::SliderBehavior( frame_bb , id , data_type , p_data , p_min , p_max , format , flags , &grab_bb );

		if ( value_changed )
			ImGui::MarkItemEdited( id );

		float percent = 0.0f;
		bool is_zero = false;
		if ( data_type == ImGuiDataType_Float ) {
			float v = *( float* ) p_data; float min = *( float* ) p_min; float max = *( float* ) p_max;
			percent = ( max != min ) ? ( v - min ) / ( max - min ) : 0.0f;
			is_zero = ( v == 0.0f );
		}
		else if ( data_type == ImGuiDataType_S32 ) {
			int v = *( int* ) p_data; int min = *( int* ) p_min; int max = *( int* ) p_max;
			percent = ( max != min ) ? ( float ) ( v - min ) / ( float ) ( max - min ) : 0.0f;
			is_zero = ( v == 0 );
		}

		float grab_pos_anim = window->StateStorage.GetFloat( id , percent * frame_bb.GetWidth( ) );
		grab_pos_anim = ImLerp( grab_pos_anim , percent * frame_bb.GetWidth( ) , 0.15f );
		window->StateStorage.SetFloat( id , grab_pos_anim );

		float color_progress = window->StateStorage.GetFloat( id + 1 , is_zero ? 0.0f : 1.0f );
		color_progress = ImLerp( color_progress , is_zero ? 0.0f : 1.0f , 0.15f );
		window->StateStorage.SetFloat( id + 1 , color_progress );

		// Neutral Gray/White palette
		ImU32 bg_color = IM_COL32( 22 , 22 , 23 , 255 );
		ImU32 fill_color = IM_COL32( 80 , 80 , 80 , 255 );
		ImU32 grab_color = IM_COL32( 200 , 200 , 200 , 255 );
		ImU32 text_color_anim = ImGui::ColorConvertFloat4ToU32( ImLerp( ImVec4( 0.4f , 0.4f , 0.4f , 1.0f ) , ImVec4( 0.8f , 0.8f , 0.8f , 1.0f ) , color_progress ) );

		window->DrawList->AddRectFilled( frame_bb.Min , frame_bb.Max , bg_color , 4.0f );
		window->DrawList->AddRectFilled( frame_bb.Min + ImVec2( 1 , 0 ) , ImVec2( frame_bb.Min.x + grab_pos_anim + 1 , frame_bb.Max.y ) , fill_color , 4.0f , ImDrawFlags_RoundCornersLeft );
		window->DrawList->AddRect( frame_bb.Min , frame_bb.Max , ImGui::GetColorU32( ImGuiCol_Border ) , 4.0f );

		const float grab_width = 4.0f;
		window->DrawList->AddRectFilled( ImVec2( frame_bb.Min.x + grab_pos_anim - grab_width / 2.0f , frame_bb.Min.y ) , ImVec2( frame_bb.Min.x + grab_pos_anim + grab_width / 2.0f , frame_bb.Max.y ) , grab_color , 4.0f );

		char value_buf [ 64 ];
		ImGui::DataTypeFormatString( value_buf , IM_ARRAYSIZE( value_buf ) , data_type , p_data , format );
		window->DrawList->AddText( ImVec2( frame_bb.Max.x - ImGui::CalcTextSize( value_buf ).x , frame_bb.Min.y - label_size.y - 6.0f ) , text_color_anim , value_buf );

		if ( label_size.x > 0.0f )
			window->DrawList->AddText( ImVec2( frame_bb.Min.x , frame_bb.Min.y - label_size.y - 6.0f ) , text_color_anim , label , ImGui::FindRenderedTextEnd( label ) );

		return value_changed;
	}

	inline bool SliderCustom( const char* label , int* v , int v_min , int v_max , const char* format = "%d" , ImGuiSliderFlags flags = 0 ) {
		return SliderHash( label , ImGuiDataType_S32 , v , &v_min , &v_max , format , flags );
	}

	inline bool SliderFloatCustom( const char* label , float* v , float v_min , float v_max , const char* format = "%.1f" , ImGuiSliderFlags flags = 0 ) {
		return SliderHash( label , ImGuiDataType_Float , v , &v_min , &v_max , format , flags );
	}

	// Botão XISFPS — cópia 1:1 do widget button do cheat FreeFire / MultiLoader:
	//
	//   Fill  = widgets.background (22,22,23) — CONSTANTE, nunca muda.
	//   Stroke rest  = widgets.stroke (40,40,45) — hairline sutil.
	//   Stroke hover = g_Col.Base (95,0,0) a 60% opacity — só o contorno vira vermelho.
	//   Wash  hover  = g_Col.Base a 10% opacity DENTRO do botão (tint sutil).
	//   Text  rest  = widgets.text_inactive (80,80,80).
	//   Text  hover = widgets.text (235,235,238) — acende.
	//   Click flash = wash some (held → wash_alpha → 0).
	inline bool Button( const char* label , const ImVec2& size_arg = ImVec2( 0 , 0 ) ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems ) return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID( label );
		const ImVec2 label_size = ImGui::CalcTextSize( label , NULL , true );

		ImVec2 pos  = window->DC.CursorPos;
		ImVec2 size = ImGui::CalcItemSize( size_arg , label_size.x + style.FramePadding.x * 2.0f , label_size.y + style.FramePadding.y * 2.0f );

		const ImRect bb( pos , pos + size );
		ImGui::ItemSize( size , style.FramePadding.y );
		if ( !ImGui::ItemAdd( bb , id ) ) return false;

		bool hovered , held;
		bool pressed = ImGui::ButtonBehavior( bb , id , &hovered , &held );

		// hover_progress → anima STROKE (cinza → accent 60%).
		// wash_alpha     → anima WASH accent 10% DENTRO + texto (some no clique).
		float hover_progress = window->StateStorage.GetFloat( id     , 0.0f );
		hover_progress = ImLerp( hover_progress , hovered ? 1.0f : 0.0f , g.IO.DeltaTime * 14.0f );
		window->StateStorage.SetFloat( id , hover_progress );

		float wash_alpha = window->StateStorage.GetFloat( id + 1 , 0.0f );
		wash_alpha = ImLerp( wash_alpha , ( hovered && !held ) ? 1.0f : 0.0f , g.IO.DeltaTime * 8.0f );
		window->StateStorage.SetFloat( id + 1 , wash_alpha );

		// Paleta XISFPS (hardcoded — bate exato com colors.h do cheat).
		constexpr ImU32   BG           = IM_COL32(  22 ,  22 ,  23 , 255 );
		constexpr ImVec4  STROKE_REST  = ImVec4( 40.f/255 , 40.f/255 , 45.f/255 , 1.f );
		constexpr ImVec4  TEXT_REST    = ImVec4( 80.f/255 , 80.f/255 , 80.f/255 , 1.f );
		constexpr ImVec4  TEXT_HOVER   = ImVec4( 235.f/255 , 235.f/255 , 238.f/255 , 1.f );

		// Stroke — cinza rest → accent 60% hover.
		ImVec4 stroke_hover( g_Col.Base.x , g_Col.Base.y , g_Col.Base.z , 0.6f );
		ImVec4 stroke_c = ImLerp( STROKE_REST , stroke_hover , hover_progress );
		// Text acompanha o wash (some no clique).
		ImVec4 text_c   = ImLerp( TEXT_REST , TEXT_HOVER , wash_alpha );

		// Fill CONSTANTE.
		window->DrawList->AddRectFilled( bb.Min , bb.Max , BG , 5.0f );
		// Wash accent 10% DENTRO no hover.
		if ( wash_alpha > 0.01f ) {
			ImU32 wash = ImGui::ColorConvertFloat4ToU32(
				ImVec4( g_Col.Base.x , g_Col.Base.y , g_Col.Base.z , 0.10f * wash_alpha ) );
			window->DrawList->AddRectFilled( bb.Min , bb.Max , wash , 5.0f );
		}
		// Stroke — grey rest, accent 60% hover.
		window->DrawList->AddRect( bb.Min , bb.Max ,
			ImGui::ColorConvertFloat4ToU32( stroke_c ) , 5.0f , 0 , 1.0f );

		ImGui::PushFont( globals.intersemi );
		ImGui::PushStyleColor( ImGuiCol_Text , ImGui::ColorConvertFloat4ToU32( text_c ) );
		ImGui::RenderTextClipped( bb.Min , bb.Max , label , NULL , &label_size , style.ButtonTextAlign , &bb );
		ImGui::PopStyleColor( );
		ImGui::PopFont( );

		return pressed;
	}

	inline bool InputText( const char* label , const char* hint , char* buf , size_t buf_size , const ImVec2& size_arg = ImVec2( 0 , 0 ) , ImGuiInputTextFlags flags = 0 ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems ) return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		ImVec2 pos = window->DC.CursorPos;
		ImVec2 size = ImGui::CalcItemSize( size_arg , ImGui::GetContentRegionAvail( ).x , g.FontSize + style.FramePadding.y * 2.0f );
		const ImRect frame_bb( pos , pos + size );

		window->DrawList->AddRectFilled( frame_bb.Min , frame_bb.Max , IM_COL32( 22 , 22 , 23 , 255 ) , 4.0f );
		window->DrawList->AddRect( frame_bb.Min , frame_bb.Max , ImGui::GetColorU32( ImGuiCol_Border ) , 4.0f );

		window->DC.CursorPos = window->DC.CursorPos + ImVec2( 10 , 4 );
		ImGui::PushStyleColor( ImGuiCol_FrameBg , ImVec4( 0 , 0 , 0 , 0 ) );
		ImGui::PushStyleColor( ImGuiCol_Border , ImVec4( 0 , 0 , 0 , 0 ) );
		ImGui::PushStyleColor( ImGuiCol_TextSelectedBg , ImVec4( 0.3f , 0.3f , 0.3f , 0.6f ) );
		bool value_changed = ImGui::InputTextEx( label , hint , buf , ( int ) buf_size , size - ImVec2( 20 , 8 ) , flags );
		ImGui::PopStyleColor( 3 );

		window->DC.CursorPos = pos;
		ImGui::ItemSize( size , style.FramePadding.y );
		return value_changed;
	}

	inline bool Combo( const char* label , int* current_item , const char* const items [ ] , int items_count ) {
		ImGuiWindow* window = ImGui::GetCurrentWindow( );
		if ( window->SkipItems ) return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID( label );
		const float w = ImGui::GetContentRegionAvail( ).x - 15.0f;
		const float frame_height = 25.0f;
		const ImVec2 label_size = ImGui::CalcTextSize( label , NULL , true );
		const ImRect frame_bb( window->DC.CursorPos + ImVec2( 0 , label_size.y + 8.0f ) , window->DC.CursorPos + ImVec2( w , label_size.y + 8.0f + frame_height ) );
		const ImRect total_bb( window->DC.CursorPos , frame_bb.Max );

		ImGui::ItemSize( total_bb , style.FramePadding.y );
		if ( !ImGui::ItemAdd( total_bb , id , &frame_bb ) ) return false;

		bool hovered , held;
		bool pressed = ImGui::ButtonBehavior( frame_bb , id , &hovered , &held );

		if ( label_size.x > 0 ) {
			ImGui::PushFont( globals.intersemi );
			window->DrawList->AddText( total_bb.Min , ImGui::GetColorU32( ImGuiCol_Text ) , label , ImGui::FindRenderedTextEnd( label ) );
			ImGui::PopFont( );
		}

		window->DrawList->AddRectFilled( frame_bb.Min , frame_bb.Max , IM_COL32( 22 , 22 , 23 , 255 ) , 4.0f );
		window->DrawList->AddRect( frame_bb.Min , frame_bb.Max , ImGui::GetColorU32( ImGuiCol_Border ) , 4.0f );

		const char* preview_value = items [ *current_item ];
		ImGui::PushFont( globals.intersemi );
		ImGui::RenderTextClipped( frame_bb.Min + ImVec2( 10 , 0 ) , frame_bb.Max , preview_value , NULL , NULL , ImVec2( 0.0f , 0.5f ) , &frame_bb );
		ImGui::PopFont( );

		if ( pressed ) ImGui::OpenPopup( id );

		bool value_changed = false;
		ImGui::SetNextWindowPos( ImVec2( frame_bb.Min.x , frame_bb.Max.y + 2 ) );
		ImGui::SetNextWindowSizeConstraints( ImVec2( frame_bb.GetWidth( ) , 0 ) , ImVec2( frame_bb.GetWidth( ) , 300 ) );

		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding , ImVec2( 4 , 4 ) );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding , 4.0f );
		ImGui::PushStyleColor( ImGuiCol_PopupBg , IM_COL32( 22 , 22 , 23 , 255 ) );
		ImGui::PushStyleColor( ImGuiCol_Header , IM_COL32( 40 , 40 , 42 , 255 ) );
		ImGui::PushStyleColor( ImGuiCol_HeaderHovered , IM_COL32( 50 , 50 , 52 , 255 ) );
		ImGui::PushStyleColor( ImGuiCol_HeaderActive , IM_COL32( 60 , 60 , 62 , 255 ) );

		if ( ImGui::BeginPopupEx( id , ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar ) ) {
			for ( int i = 0; i < items_count; i++ ) {
				ImGui::PushFont( globals.intersemi );
				if ( ImGui::Selectable( items [ i ] , i == *current_item ) ) {
					*current_item = i;
					value_changed = true;
				}
				ImGui::PopFont( );
			}
			ImGui::EndPopup( );
		}
		ImGui::PopStyleColor( 4 );
		ImGui::PopStyleVar( 2 );

		return value_changed;
	}
}
