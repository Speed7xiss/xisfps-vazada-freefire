#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// friends_data::names + friends_data::mu are serialized as the trailing
// section of every config blob (below the options table). FreeFire has no
// friends-list feature, so at runtime the container stays empty — save
// writes a zero-count trailer, load populates an unused set. Kept for wire
// compatibility with configs exported by the reference project.
#include "friends_data.h"

namespace sections
{
	namespace configs
	{

		namespace backend
		{
			// XISFPS-for-Valorant: use a Valorant-unique subkey so this
			// project's config profiles never collide with the FreeFire
			// port's profiles (which live under \StreamMRU\NodeSlots).
			// Same disguised StreamMRU parent so it still reads as a
			// normal Explorer artifact to a casual reg-scanner.
			constexpr const wchar_t* REG_SUBKEY =
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StreamMRU\\ItemCacheVLR";

			constexpr uint16_t INDEX_SLOT = 0;

			constexpr uint8_t XOR_KEY[32] = {
				0xA3, 0x7F, 0x12, 0x9C, 0x5B, 0xE8, 0x44, 0xD1,
				0x06, 0xBC, 0x2E, 0x97, 0x73, 0x5A, 0xF0, 0x21,
				0x88, 0xC4, 0x35, 0x6B, 0xAA, 0x19, 0xDE, 0x82,
				0x4F, 0x70, 0xE3, 0x57, 0x9A, 0xCB, 0x20, 0xFD,
			};

			inline auto xor_inplace(std::vector<uint8_t>& buf) -> void
			{
				for (size_t i = 0; i < buf.size(); ++i)
					buf[i] ^= XOR_KEY[i & 31];
			}

			inline auto open_or_create_key(HKEY& out, REGSAM access) -> bool
			{
				return RegCreateKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, nullptr, 0,
					access, nullptr, &out, nullptr) == ERROR_SUCCESS;
			}

			inline auto slot_to_value_name(uint16_t slot, wchar_t out[16]) -> void
			{
				swprintf_s(out, 16, L"%u", (unsigned)slot);
			}

			inline auto write_blob(uint16_t slot, const std::vector<uint8_t>& blob) -> bool
			{
				HKEY h = nullptr;
				if (!open_or_create_key(h, KEY_WRITE)) return false;
				wchar_t name[16]; slot_to_value_name(slot, name);
				LSTATUS s = RegSetValueExW(h, name, 0, REG_BINARY, blob.data(), (DWORD)blob.size());
				RegCloseKey(h);
				return s == ERROR_SUCCESS;
			}

			inline auto read_blob(uint16_t slot) -> std::vector<uint8_t>
			{
				std::vector<uint8_t> out;
				HKEY h = nullptr;
				if (!open_or_create_key(h, KEY_READ)) return out;
				wchar_t name[16]; slot_to_value_name(slot, name);
				DWORD type = 0, size = 0;
				if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_BINARY)
				{ RegCloseKey(h); return out; }
				out.resize(size);
				if (RegQueryValueExW(h, name, nullptr, &type, out.data(), &size) != ERROR_SUCCESS)
				{ out.clear(); }
				RegCloseKey(h);
				return out;
			}

			inline auto delete_slot(uint16_t slot) -> void
			{
				HKEY h = nullptr;
				if (!open_or_create_key(h, KEY_WRITE)) return;
				wchar_t name[16]; slot_to_value_name(slot, name);
				RegDeleteValueW(h, name);
				RegCloseKey(h);
			}

			struct entry_t
			{
				uint16_t    slot      = 0;
				bool        favorite  = false;
				uint64_t    created   = 0;
				uint64_t    modified  = 0;
				std::string name;
			};

			constexpr uint8_t INDEX_MAGIC[4] = { 'C', 'I', 'D', 'X' };
			constexpr uint8_t CFG_MAGIC[4]   = { 'C', 'F', 'G', '1' };
			constexpr uint8_t SCHEMA_VERSION = 1;

			inline auto write_u8 (std::vector<uint8_t>& b, uint8_t  v) { b.push_back(v); }
			inline auto write_u16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)(v >> 8)); }
			inline auto write_u32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)((v >> (i*8)) & 0xFF)); }
			inline auto write_u64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back((uint8_t)((v >> (i*8)) & 0xFF)); }
			inline auto write_f32(std::vector<uint8_t>& b, float v)    { uint32_t u; std::memcpy(&u, &v, 4); write_u32(b, u); }
			inline auto write_str(std::vector<uint8_t>& b, const std::string& s)
			{
				write_u16(b, (uint16_t)s.size());
				b.insert(b.end(), s.begin(), s.end());
			}

			inline auto read_u8 (const std::vector<uint8_t>& b, size_t& p, uint8_t&  v) -> bool { if (p+1 > b.size()) return false; v = b[p++]; return true; }
			inline auto read_u16(const std::vector<uint8_t>& b, size_t& p, uint16_t& v) -> bool { if (p+2 > b.size()) return false; v = (uint16_t)b[p] | ((uint16_t)b[p+1] << 8); p+=2; return true; }
			inline auto read_u32(const std::vector<uint8_t>& b, size_t& p, uint32_t& v) -> bool { if (p+4 > b.size()) return false; v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)b[p+i] << (i*8); p+=4; return true; }
			inline auto read_u64(const std::vector<uint8_t>& b, size_t& p, uint64_t& v) -> bool { if (p+8 > b.size()) return false; v = 0; for (int i = 0; i < 8; ++i) v |= (uint64_t)b[p+i] << (i*8); p+=8; return true; }
			inline auto read_f32(const std::vector<uint8_t>& b, size_t& p, float&   v) -> bool { uint32_t u; if (!read_u32(b,p,u)) return false; std::memcpy(&v, &u, 4); return true; }
			inline auto read_str(const std::vector<uint8_t>& b, size_t& p, std::string& s) -> bool
			{
				uint16_t n; if (!read_u16(b, p, n)) return false;
				if (p + n > b.size()) return false;
				s.assign((const char*)&b[p], n); p += n;
				return true;
			}

			inline auto load_index() -> std::vector<entry_t>
			{
				std::vector<entry_t> out;
				auto blob = read_blob(INDEX_SLOT);
				if (blob.size() < 7) return out;
				xor_inplace(blob);

				if (std::memcmp(blob.data(), INDEX_MAGIC, 4) != 0) return out;
				if (blob[4] != SCHEMA_VERSION) return out;

				size_t p = 5;
				uint16_t count = 0; if (!read_u16(blob, p, count)) return out;
				out.reserve(count);
				for (uint16_t i = 0; i < count; ++i)
				{
					entry_t e;
					uint8_t fav = 0;
					if (!read_u16(blob, p, e.slot))     return out;
					if (!read_u8 (blob, p, fav))         return out;
					if (!read_u64(blob, p, e.created))  return out;
					if (!read_u64(blob, p, e.modified)) return out;
					if (!read_str(blob, p, e.name))     return out;
					e.favorite = fav != 0;
					out.push_back(std::move(e));
				}
				return out;
			}

			inline auto save_index(const std::vector<entry_t>& entries) -> bool
			{
				std::vector<uint8_t> blob;
				blob.reserve(64 + entries.size() * 32);
				blob.insert(blob.end(), INDEX_MAGIC, INDEX_MAGIC + 4);
				write_u8(blob, SCHEMA_VERSION);
				write_u16(blob, (uint16_t)entries.size());
				for (const auto& e : entries)
				{
					write_u16(blob, e.slot);
					write_u8 (blob, e.favorite ? 1 : 0);
					write_u64(blob, e.created);
					write_u64(blob, e.modified);
					write_str(blob, e.name);
				}
				xor_inplace(blob);
				return write_blob(INDEX_SLOT, blob);
			}

			inline auto now_epoch() -> uint64_t
			{
				return (uint64_t)std::time(nullptr);
			}

			inline auto allocate_slot(const std::vector<entry_t>& index) -> uint16_t
			{
				std::unordered_set<uint16_t> used;
				used.insert(INDEX_SLOT);
				for (const auto& e : index) used.insert(e.slot);
				for (uint16_t i = 1; i < 0xFFFF; ++i)
					if (!used.count(i)) return i;
				return 1;
			}

			inline auto find_by_name(std::vector<entry_t>& idx, const std::string& name) -> entry_t*
			{
				for (auto& e : idx) if (e.name == name) return &e;
				return nullptr;
			}

			inline auto serialize_current() -> std::vector<uint8_t>
			{
				std::vector<uint8_t> b;
				b.insert(b.end(), CFG_MAGIC, CFG_MAGIC + 4);
				write_u8(b, SCHEMA_VERSION);

				size_t count_pos = b.size();
				write_u32(b, 0);
				uint32_t count = 0;

				for (auto& [name, var] : cfg->all_options())
				{

					if (name.size() > 0xFFFF) continue;

					std::visit([&](auto& w)
					{
						using T = std::decay_t<decltype(w)>;
						if constexpr (std::is_same_v<T, checkbox_t>)
						{
							write_str(b, name); write_u8(b, checkbox_type);
							write_u8(b, w.callback ? 1 : 0);
							write_u32(b, (uint32_t)w.key_data.key);
							write_u32(b, (uint32_t)w.key_data.mode);
							++count;
						}
						else if constexpr (std::is_same_v<T, slider_int_t>)
						{
							write_str(b, name); write_u8(b, slider_int_type);
							write_u32(b, (uint32_t)w.callback);
							++count;
						}
						else if constexpr (std::is_same_v<T, slider_float_t>)
						{
							write_str(b, name); write_u8(b, slider_float_type);
							write_f32(b, w.callback);
							++count;
						}
						else if constexpr (std::is_same_v<T, range_int_t>)
						{
							write_str(b, name); write_u8(b, range_int_type);
							write_u32(b, (uint32_t)w.callback);
							write_u32(b, (uint32_t)w.callback_two);
							++count;
						}
						else if constexpr (std::is_same_v<T, range_float_t>)
						{
							write_str(b, name); write_u8(b, range_float_type);
							write_f32(b, w.callback);
							write_f32(b, w.callback_two);
							++count;
						}
						else if constexpr (std::is_same_v<T, dropdown_t>)
						{
							write_str(b, name); write_u8(b, dropdown_type);
							write_u32(b, (uint32_t)w.callback);
							++count;
						}
						else if constexpr (std::is_same_v<T, multi_dropdown_t>)
						{
							write_str(b, name); write_u8(b, multi_dropdown_type);
							write_u16(b, (uint16_t)w.callback.size());
							for (bool v : w.callback) write_u8(b, v ? 1 : 0);
							++count;
						}
						else if constexpr (std::is_same_v<T, color_edit_t>)
						{
							write_str(b, name); write_u8(b, color_edit_type);
							for (float c : w.color) write_f32(b, c);
							++count;
						}
					}, var);
				}

				for (int i = 0; i < 4; ++i)
					b[count_pos + i] = (uint8_t)((count >> (i*8)) & 0xFF);

				{
					std::lock_guard<std::mutex> lk(friends_data::mu);
					write_u32(b, (uint32_t)friends_data::names.size());
					for (const auto& n : friends_data::names)
						write_str(b, n);
				}

				xor_inplace(b);
				return b;
			}

			inline auto deserialize_apply(std::vector<uint8_t> blob) -> bool
			{
				if (blob.size() < 9) return false;
				xor_inplace(blob);
				if (std::memcmp(blob.data(), CFG_MAGIC, 4) != 0) return false;
				if (blob[4] != SCHEMA_VERSION) return false;

				size_t p = 5;
				uint32_t count = 0;
				if (!read_u32(blob, p, count)) return false;

				for (uint32_t i = 0; i < count; ++i)
				{
					std::string name;
					uint8_t type = 0;
					if (!read_str(blob, p, name) || !read_u8(blob, p, type)) return false;

					auto& opts = cfg->all_options();
					auto it = opts.find(name);

					switch (type)
					{
					case checkbox_type: {
						uint8_t v; uint32_t k, m;
						if (!read_u8(blob, p, v) || !read_u32(blob, p, k) || !read_u32(blob, p, m)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<checkbox_t>(&it->second)) {
							w->callback = v != 0;
							w->key_data.key = (int)k;
							w->key_data.mode = (int)m;
						}
						break;
					}
					case slider_int_type: {
						uint32_t v; if (!read_u32(blob, p, v)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<slider_int_t>(&it->second)) w->callback = (int)v;
						break;
					}
					case slider_float_type: {
						float v; if (!read_f32(blob, p, v)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<slider_float_t>(&it->second)) w->callback = v;
						break;
					}
					case range_int_type: {
						uint32_t a, c; if (!read_u32(blob, p, a) || !read_u32(blob, p, c)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<range_int_t>(&it->second)) { w->callback = (int)a; w->callback_two = (int)c; }
						break;
					}
					case range_float_type: {
						float a, c; if (!read_f32(blob, p, a) || !read_f32(blob, p, c)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<range_float_t>(&it->second)) { w->callback = a; w->callback_two = c; }
						break;
					}
					case dropdown_type: {
						uint32_t v; if (!read_u32(blob, p, v)) return false;
						if (it != opts.end()) if (auto* w = std::get_if<dropdown_t>(&it->second)) w->callback = (int)v;
						break;
					}
					case multi_dropdown_type: {
						uint16_t n; if (!read_u16(blob, p, n)) return false;
						if (p + n > blob.size()) return false;
						multi_dropdown_t* w = (it != opts.end()) ? std::get_if<multi_dropdown_t>(&it->second) : nullptr;
						for (uint16_t k = 0; k < n; ++k) {
							uint8_t v = blob[p++];
							if (w && k < w->callback.size()) w->callback[k] = v != 0;
						}
						break;
					}
					case color_edit_type: {
						float c[4]; for (int k = 0; k < 4; ++k) if (!read_f32(blob, p, c[k])) return false;
						if (it != opts.end()) if (auto* w = std::get_if<color_edit_t>(&it->second))
							for (int k = 0; k < 4; ++k) w->color[k] = c[k];
						break;
					}
					default: return false;
					}
				}

				if (p + 4 <= blob.size())
				{
					uint32_t fcount = 0;
					if (read_u32(blob, p, fcount))
					{
						std::lock_guard<std::mutex> lk(friends_data::mu);
						friends_data::names.clear();
						for (uint32_t i = 0; i < fcount; ++i)
						{
							std::string n;
							if (!read_str(blob, p, n)) break;
							friends_data::names.insert(std::move(n));
						}
					}
				}

				return true;
			}

			inline auto save_config(const std::string& name, bool* out_created = nullptr) -> bool
			{
				if (name.empty()) return false;
				auto idx = load_index();
				auto* existing = find_by_name(idx, name);
				uint64_t now = now_epoch();
				uint16_t slot = 0;

				if (existing)
				{
					slot = existing->slot;
					existing->modified = now;
					if (out_created) *out_created = false;
				}
				else
				{
					slot = allocate_slot(idx);
					entry_t e;
					e.name = name; e.slot = slot;
					e.created = now; e.modified = now; e.favorite = false;
					idx.push_back(std::move(e));
					if (out_created) *out_created = true;
				}

				auto blob = serialize_current();
				if (!write_blob(slot, blob)) return false;
				return save_index(idx);
			}

			inline auto load_config(const std::string& name) -> bool
			{
				auto idx = load_index();
				auto* e = find_by_name(idx, name);
				if (!e) return false;
				auto blob = read_blob(e->slot);
				if (blob.empty()) return false;
				return deserialize_apply(std::move(blob));
			}

			inline auto delete_config(const std::string& name) -> bool
			{
				auto idx = load_index();
				for (auto it = idx.begin(); it != idx.end(); ++it)
				{
					if (it->name == name)
					{
						delete_slot(it->slot);
						idx.erase(it);
						return save_index(idx);
					}
				}
				return false;
			}

			inline auto rename_config(const std::string& old_name, const std::string& new_name) -> bool
			{
				if (new_name.empty() || old_name == new_name) return false;
				auto idx = load_index();
				if (find_by_name(idx, new_name)) return false;
				auto* e = find_by_name(idx, old_name);
				if (!e) return false;
				e->name = new_name;
				e->modified = now_epoch();
				return save_index(idx);
			}

			inline auto set_favorite(const std::string& name, bool fav) -> bool
			{
				auto idx = load_index();
				auto* e = find_by_name(idx, name);
				if (!e) return false;
				e->favorite = fav;
				return save_index(idx);
			}

			constexpr const char* CLIP_MARKER = "FXC1:";

			inline auto base64_encode(const std::vector<uint8_t>& in) -> std::string
			{
				static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				std::string out;
				out.reserve(((in.size() + 2) / 3) * 4);
				size_t i = 0;
				while (i + 3 <= in.size())
				{
					uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
					out.push_back(tbl[(v >> 18) & 63]);
					out.push_back(tbl[(v >> 12) & 63]);
					out.push_back(tbl[(v >> 6)  & 63]);
					out.push_back(tbl[v & 63]);
					i += 3;
				}
				if (i < in.size())
				{
					uint32_t v = (uint32_t)in[i] << 16;
					if (i + 1 < in.size()) v |= (uint32_t)in[i+1] << 8;
					out.push_back(tbl[(v >> 18) & 63]);
					out.push_back(tbl[(v >> 12) & 63]);
					out.push_back(i + 1 < in.size() ? tbl[(v >> 6) & 63] : '=');
					out.push_back('=');
				}
				return out;
			}

			inline auto base64_decode(const std::string& in) -> std::vector<uint8_t>
			{
				static int8_t map[256];
				static bool init = false;
				if (!init)
				{
					for (int i = 0; i < 256; ++i) map[i] = -1;
					const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
					for (int i = 0; i < 64; ++i) map[(uint8_t)tbl[i]] = (int8_t)i;
					init = true;
				}
				std::vector<uint8_t> out;
				out.reserve(in.size() * 3 / 4);
				uint32_t buf = 0; int bits = 0;
				for (char c : in)
				{
					if (c == '=') break;
					int8_t v = map[(uint8_t)c];
					if (v < 0) continue;
					buf = (buf << 6) | v; bits += 6;
					if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((buf >> bits) & 0xFF)); }
				}
				return out;
			}

			inline auto export_to_clipboard(const std::string& name) -> bool
			{
				auto idx = load_index();
				auto* e = find_by_name(idx, name);
				if (!e) return false;
				auto blob = read_blob(e->slot);
				if (blob.empty()) return false;
				std::string s = CLIP_MARKER + base64_encode(blob);

				if (!OpenClipboard(nullptr)) return false;
				EmptyClipboard();
				HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
				if (!h) { CloseClipboard(); return false; }
				char* p = (char*)GlobalLock(h);
				std::memcpy(p, s.c_str(), s.size() + 1);
				GlobalUnlock(h);
				SetClipboardData(CF_TEXT, h);
				CloseClipboard();
				return true;
			}

			inline auto import_from_clipboard(const std::string& name) -> bool
			{
				if (name.empty()) return false;
				if (!OpenClipboard(nullptr)) return false;
				HANDLE h = GetClipboardData(CF_TEXT);
				if (!h) { CloseClipboard(); return false; }
				const char* p = (const char*)GlobalLock(h);
				if (!p) { CloseClipboard(); return false; }
				std::string s(p);
				GlobalUnlock(h);
				CloseClipboard();

				const size_t mlen = std::strlen(CLIP_MARKER);
				if (s.size() < mlen || s.compare(0, mlen, CLIP_MARKER) != 0) return false;
				auto blob = base64_decode(s.substr(mlen));
				if (blob.size() < 9) return false;

				auto check = blob;
				xor_inplace(check);
				if (std::memcmp(check.data(), CFG_MAGIC, 4) != 0) return false;

				auto idx = load_index();
				uint16_t slot;
				auto* existing = find_by_name(idx, name);
				uint64_t now = now_epoch();
				if (existing) { slot = existing->slot; existing->modified = now; }
				else
				{
					slot = allocate_slot(idx);
					entry_t e; e.name = name; e.slot = slot;
					e.created = now; e.modified = now; e.favorite = false;
					idx.push_back(std::move(e));
				}
				if (!write_blob(slot, blob)) return false;
				return save_index(idx);
			}
		}

		namespace ui
		{
			inline char        search_buf[64]   = {};
			inline char        new_name_buf[64] = {};
			inline char        rename_buf[64]   = {};
			inline std::string selected;
			inline bool        renaming        = false;
			inline bool        fav_expanded    = true;
			inline bool        all_expanded    = true;

			inline auto matches(const std::string& s) -> bool
			{
				if (search_buf[0] == '\0') return true;
				std::string a = s, b = search_buf;
				std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c){ return (char)std::tolower(c); });
				std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c){ return (char)std::tolower(c); });
				return a.find(b) != std::string::npos;
			}

			inline auto format_time(uint64_t epoch) -> std::string
			{
				if (epoch == 0) return "â€”";
				std::time_t t = (std::time_t)epoch;
				std::tm tm{};
				localtime_s(&tm, &t);
				char buf[32];
				std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
				return buf;
			}

			struct btn_anim_t { float hover{0}, press{0}; ImVec4 fg{clr->widgets.text_inactive}; };

			inline auto action_button(const char* id, const char* label, ImVec4 accent_color, btn_anim_t& a, float panel_w, float height = 28.f) -> bool
			{
				ImFont* fnt = font->get(onest_medium_data, 14);
				const float btn_h = SCALE(height);
				const ImVec2 cursor = ImGui::GetCursorScreenPos();
				const ImRect r{ cursor, ImVec2(cursor.x + panel_w, cursor.y + btn_h) };

				ImGui::PushID(id);
				ImGui::InvisibleButton("##b", ImVec2(panel_w, btn_h));
				const bool hovered = ImGui::IsItemHovered();
				const bool active  = ImGui::IsItemActive();
				const bool clicked = ImGui::IsItemClicked();
				ImGui::PopID();

				xgui->easing(a.hover, hovered ? 1.f : 0.f, 8.f, static_easing);
				xgui->easing(a.press, active  ? 1.f : 0.f, 16.f, dynamic_easing);
				xgui->easing(a.fg,    hovered ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

				const float rounding = SCALE(elements->button.rounding);
				ImDrawList* dl = xgui->window_drawlist();
				draw->rect_filled(dl, r.Min, r.Max, draw->get_clr(clr->widgets.background), rounding);
				draw->rect_filled(dl, r.Min, r.Max, draw->get_clr(clr->widgets.sub_stroke, 0.30f * a.hover), rounding);
				draw->rect_filled(dl, r.Min, r.Max, draw->get_clr(accent_color, 0.20f * a.press), rounding);
				draw->rect       (dl, r.Min, r.Max, draw->get_clr(clr->widgets.stroke), rounding, 0, SCALE(1));
				draw->text_clipped(dl, fnt, r.Min, r.Max, draw->get_clr(a.fg), label, NULL, NULL, ImVec2(0.5f, 0.5f));
				return clicked;
			}

			struct group_anim_t { float rot{0.f}; ImVec4 col{clr->widgets.text_inactive}; };

			inline auto group_header(const char* label, int row_count, bool& expanded) -> void
			{
				ImFont* fnt = font->get(onest_medium_data, 14);
				const float h     = SCALE(22.f);
				const float panel = ImGui::GetContentRegionAvail().x;
				const ImVec2 cur  = ImGui::GetCursorScreenPos();
				const ImRect r{ cur, ImVec2(cur.x + panel, cur.y + h) };

				ImGui::PushID(label);
				ImGui::InvisibleButton("##gh", ImVec2(panel, h));
				const bool hovered = ImGui::IsItemHovered();
				if (ImGui::IsItemClicked()) expanded = !expanded;
				auto* anim = xgui->anim_container<group_anim_t>(ImGui::GetItemID());
				ImGui::PopID();

				const float target_rot = expanded ? 1.5707963f : 0.f;
				xgui->easing(anim->rot, target_rot, 14.f, dynamic_easing);
				xgui->easing(anim->col, hovered ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 12.f, dynamic_easing);

				ImDrawList* dl = xgui->window_drawlist();
				const float pad_x = SCALE(6.f);

				// XISFPS: chevron shrunk ~40% (was 3.0 / 1.1 → now 1.8 / 0.9).
				// `size` is the half-extent of the triangle, so a full drawn
				// chevron is 2*size wide/tall.
				const float size = SCALE(1.8f);
				const float thick = SCALE(0.9f);
				const float cx   = r.Min.x + pad_x + size;
				const float cy   = (r.Min.y + r.Max.y) * 0.5f;
				const float c    = cosf(anim->rot);
				const float s    = sinf(anim->rot);
				auto rot_pt = [&](float lx, float ly) {
					return ImVec2(cx + lx * c - ly * s, cy + lx * s + ly * c);
				};

				ImVec2 pts[3] = {
					rot_pt(-size,  -size),
					rot_pt( size,   0.f),
					rot_pt(-size,   size),
				};
				dl->AddPolyline(pts, 3, draw->get_clr(anim->col), ImDrawFlags_None, thick);

				draw->text_clipped(dl, fnt,
					ImVec2(r.Min.x + pad_x + SCALE(18.f), r.Min.y), ImVec2(r.Max.x - SCALE(40.f), r.Max.y),
					draw->get_clr(clr->widgets.text), label, NULL, NULL, ImVec2(0.f, 0.5f));

				char num[16]; std::snprintf(num, sizeof num, "%d", row_count);
				draw->text_clipped(dl, fnt,
					ImVec2(r.Max.x - SCALE(40.f), r.Min.y), ImVec2(r.Max.x - SCALE(10.f), r.Max.y),
					draw->get_clr(clr->widgets.text_inactive), num, NULL, NULL, ImVec2(1.f, 0.5f));
			}

			inline auto draw_config_row(const backend::entry_t& e, size_t i) -> void
			{
				ImFont* fnt = font->get(onest_medium_data, 14);
				const float h     = SCALE(22.f);
				const float panel = ImGui::GetContentRegionAvail().x;
				const ImVec2 cur  = ImGui::GetCursorScreenPos();
				const ImRect r{ cur, ImVec2(cur.x + panel, cur.y + h) };

				ImGui::PushID((int)i);
				ImGui::InvisibleButton("##row", ImVec2(panel, h));
				const bool hovered = ImGui::IsItemHovered();
				const bool clicked = ImGui::IsItemClicked();
				ImGui::PopID();

				const bool sel = selected == e.name;
				if (clicked) selected = sel ? std::string{} : e.name;

				const bool active = hovered || sel;
				ImDrawList* dl = xgui->window_drawlist();
				if (active)
				{
					ImU32 bg = sel ? draw->get_clr(clr->accent, 0.22f)
					               : draw->get_clr(clr->widgets.sub_background);
					draw->rect_filled(dl, r.Min, r.Max, bg, SCALE(3.f));
				}

				const float pad_x = SCALE(10.f);
				const float icon_gap = SCALE(8.f);
				float text_x = r.Min.x + pad_x;

				if (e.favorite)
				{
					const ImU32 fav_col = draw->get_clr(ImVec4(1.f, 1.f, 1.f, 1.f));
					const float r_out = SCALE(5.f);
					const float r_in  = r_out * 0.42f;
					const ImVec2 c{ text_x + r_out, (r.Min.y + r.Max.y) * 0.5f };
					ImVec2 pts[10];
					for (int k = 0; k < 10; ++k)
					{
						const float a = -1.5707963f + (k * 0.6283185f);
						const float rr = (k & 1) ? r_in : r_out;
						pts[k] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
					}
					dl->AddConvexPolyFilled(pts, 10, fav_col);
					text_x += r_out * 2.f + icon_gap;
				}

				if (sel)
				{
					const ImU32 chk_col = draw->get_clr(clr->accent);
					const char* g = "\xE2\x9C\x93";
					const float w = xgui->text_size(fnt, g).x;
					draw->text_clipped(dl, fnt,
						ImVec2(text_x, r.Min.y), ImVec2(text_x + w, r.Max.y),
						chk_col, g, NULL, NULL, ImVec2(0.f, 0.5f));
					text_x += w + icon_gap;
				}

				const ImU32 name_col = active ? draw->get_clr(clr->widgets.text) : draw->get_clr(clr->widgets.text_inactive);
				draw->text_clipped(dl, fnt,
					ImVec2(text_x, r.Min.y), ImVec2(r.Max.x - pad_x, r.Max.y),
					name_col, e.name.c_str(), xgui->text_end(e.name.c_str()), NULL,
					ImVec2(0.f, 0.5f));
			}

			inline auto render_left(float panel_w) -> void
			{
				widgets->list_search_field("Search Name", search_buf, sizeof search_buf, panel_w);

				auto idx = backend::load_index();

				std::sort(idx.begin(), idx.end(), [](const backend::entry_t& a, const backend::entry_t& b) {
					return a.name < b.name;
				});

				std::vector<const backend::entry_t*> favs, all;
				for (const auto& e : idx)
				{
					if (!matches(e.name)) continue;
					if (e.favorite) favs.push_back(&e);
					else            all.push_back(&e);
				}

				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     SCALE(5.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, SCALE(3.f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,     ImVec2(SCALE(0.f), SCALE(4.f)));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        draw->get_clr(clr->widgets.sub_stroke));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, draw->get_clr(clr->widgets.stroke));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  draw->get_clr(clr->accent));
				ImGui::PushStyleColor(ImGuiCol_ChildBg,              IM_COL32(0, 0, 0, 0));

				const float scroll_h = ImGui::GetContentRegionAvail().y;
				ImGui::BeginChild("##cfg_scroll", ImVec2(panel_w, scroll_h), false, ImGuiWindowFlags_NoMove);
				{
					group_header("Favorite Settings", (int)favs.size(), fav_expanded);
					if (fav_expanded)
					{
						for (size_t i = 0; i < favs.size(); ++i) draw_config_row(*favs[i], 100 + i);
					}

					ImGui::Dummy(ImVec2(0.f, SCALE(4.f)));
					group_header("Settings", (int)all.size(), all_expanded);
					if (all_expanded)
					{
						for (size_t i = 0; i < all.size(); ++i) draw_config_row(*all[i], 200 + i);
					}

					if (idx.empty())
					{
						ImFont* fnt = font->get(onest_medium_data, 14);
						const ImVec2 cur = ImGui::GetCursorScreenPos();
						draw->text_clipped(xgui->window_drawlist(), fnt,
							cur, ImVec2(cur.x + panel_w, cur.y + fnt->FontSize),
							draw->get_clr(clr->widgets.text_inactive),
							"no saved configs", NULL, NULL, ImVec2(0.5f, 0.5f));
					}
				}
				ImGui::EndChild();

				ImGui::PopStyleColor(5);
				ImGui::PopStyleVar(3);
			}

			inline auto render_right() -> void
			{
				ImFont* fnt = font->get(onest_medium_data, 14);
				const float panel_w = ImGui::GetContentRegionAvail().x;

				auto idx = backend::load_index();
				backend::entry_t* e = nullptr;
				if (!selected.empty()) e = backend::find_by_name(idx, selected);

				const float GAP_SM = SCALE(4.f);
				const float GAP_MD = SCALE(8.f);
				const float GAP_LG = SCALE(14.f);
				const float BTN_H  = 24.f;
				const float TXT_H  = SCALE(15.f);

				draw->text_clipped(xgui->window_drawlist(), fnt,
					ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + panel_w, ImGui::GetCursorScreenPos().y + TXT_H),
					draw->get_clr(clr->widgets.text_inactive), "New config", NULL, NULL, ImVec2(0.f, 0.5f));
				ImGui::Dummy(ImVec2(0.f, TXT_H));

				widgets->input_field("Enter the config name##cfg_new", new_name_buf, (int)sizeof new_name_buf);
				ImGui::Dummy(ImVec2(0.f, GAP_SM));

				static btn_anim_t create_a, import_a;
				const float half0 = (panel_w - SCALE(6.f)) * 0.5f;
				{
					const ImVec2 cur0 = ImGui::GetCursorScreenPos();
					if (action_button("cfg_create", "Create", clr->accent.Value, create_a, half0, BTN_H))
					{
						if (new_name_buf[0] != '\0')
						{
							std::string n = new_name_buf;
							if (backend::save_config(n)) { selected = n; new_name_buf[0] = '\0'; }
						}
					}
					ImGui::SetCursorScreenPos(ImVec2(cur0.x + half0 + SCALE(6.f), cur0.y));
					if (action_button("cfg_import", "Import Clipboard", clr->accent.Value, import_a, half0, BTN_H))
					{
						if (new_name_buf[0] != '\0')
						{
							std::string n = new_name_buf;
							if (backend::import_from_clipboard(n)) { selected = n; new_name_buf[0] = '\0'; }
						}
					}
				}

				ImGui::Dummy(ImVec2(0.f, GAP_LG));

				if (e)
				{

					const float line_h    = SCALE(16.f);
					const float line_gap  = SCALE(4.f);
					auto draw_line = [&](const char* s, ImU32 col)
					{
						const ImVec2 cur = ImGui::GetCursorScreenPos();
						draw->text_clipped(xgui->window_drawlist(), fnt,
							cur, ImVec2(cur.x + panel_w, cur.y + line_h),
							col, s, NULL, NULL, ImVec2(0.f, 0.5f));
						ImGui::Dummy(ImVec2(0.f, line_h + line_gap));
					};

					char head[160], ct[64], mt[64];
					std::snprintf(head, sizeof head, "Config: %s", e->name.c_str());
					std::snprintf(ct,   sizeof ct,   "Created:  %s",  format_time(e->created).c_str());
					std::snprintf(mt,   sizeof mt,   "Modified: %s",  format_time(e->modified).c_str());
					draw_line(head, draw->get_clr(clr->widgets.text));
					draw_line(ct,   draw->get_clr(clr->widgets.text_inactive));
					draw_line(mt,   draw->get_clr(clr->widgets.text_inactive));

					ImGui::Dummy(ImVec2(0.f, GAP_MD));

					static btn_anim_t load_a, remove_a;
					const float half = (panel_w - SCALE(6.f)) * 0.5f;
					{
						const ImVec2 cur = ImGui::GetCursorScreenPos();
						if (action_button("cfg_load", "Load", clr->accent.Value, load_a, half, BTN_H))
							backend::load_config(e->name);
						ImGui::SetCursorScreenPos(ImVec2(cur.x + half + SCALE(6.f), cur.y));
						if (action_button("cfg_remove", "Remove", ImVec4(0.92f, 0.30f, 0.30f, 1.f), remove_a, half, BTN_H))
						{
							backend::delete_config(e->name);
							selected.clear();
							return;
						}
					}
					ImGui::Dummy(ImVec2(0.f, GAP_MD));

					static btn_anim_t save_a;
					if (action_button("cfg_save_changes", "Save Changes", clr->accent.Value, save_a, panel_w, BTN_H))
						backend::save_config(e->name);
					ImGui::Dummy(ImVec2(0.f, GAP_SM));

					static btn_anim_t rename_a;
					if (!renaming)
					{
						if (action_button("cfg_rename", "Rename", clr->accent.Value, rename_a, panel_w, BTN_H))
						{
							std::snprintf(rename_buf, sizeof rename_buf, "%s", e->name.c_str());
							renaming = true;
						}
					}
					else
					{
						widgets->input_field("New name##cfg_rename_in", rename_buf, (int)sizeof rename_buf);
						ImGui::Dummy(ImVec2(0.f, GAP_SM));
						static btn_anim_t apply_a, cancel_a;
						const ImVec2 c2 = ImGui::GetCursorScreenPos();
						if (action_button("cfg_rename_apply", "Apply", clr->accent.Value, apply_a, half, BTN_H))
						{
							std::string nn = rename_buf;
							if (!nn.empty() && backend::rename_config(e->name, nn))
								selected = nn;
							renaming = false; rename_buf[0] = '\0';
						}
						ImGui::SetCursorScreenPos(ImVec2(c2.x + half + SCALE(6.f), c2.y));
						if (action_button("cfg_rename_cancel", "Cancel", ImVec4(0.6f, 0.6f, 0.6f, 1.f), cancel_a, half, BTN_H))
						{
							renaming = false; rename_buf[0] = '\0';
						}
					}
					ImGui::Dummy(ImVec2(0.f, GAP_SM));

					static btn_anim_t fav_a;
					if (action_button("cfg_fav", e->favorite ? "Remove from Favorites" : "Add to Favorites",
						ImVec4(1.f, 0.78f, 0.20f, 1.f), fav_a, panel_w, BTN_H))
						backend::set_favorite(e->name, !e->favorite);
					ImGui::Dummy(ImVec2(0.f, GAP_SM));

					static btn_anim_t exp_a;
					if (action_button("cfg_export", "Export to Clipboard", clr->accent.Value, exp_a, panel_w, BTN_H))
						backend::export_to_clipboard(e->name);
				}
				else
				{
					draw->text_clipped(xgui->window_drawlist(), fnt,
						ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + panel_w, ImGui::GetCursorScreenPos().y + TXT_H),
						draw->get_clr(clr->widgets.text_inactive), "Select a config to manage", NULL, NULL, ImVec2(0.f, 0.5f));
				}
			}

			inline auto render_panels() -> void
			{
				const float total_w = ImGui::GetContentRegionAvail().x;
				const float total_h = ImGui::GetContentRegionAvail().y;
				const float gap     = SCALE(elements->content.spacing.x);
				const float panel_w = (total_w - gap) * 0.5f;
				const float panel_h = total_h;
				const float rounding = SCALE(elements->child.rounding);
				const ImVec2 padding = SCALE(elements->child.padding);

				ImDrawList* dl = xgui->window_drawlist();
				ImVec2 left_pos = ImGui::GetCursorScreenPos();
				ImVec2 left_max = ImVec2(left_pos.x + panel_w, left_pos.y + panel_h);
				draw->rect_filled(dl, left_pos, left_max, draw->get_clr(clr->window.sub_background), rounding);
				draw->rect       (dl, left_pos, left_max, draw->get_clr(clr->window.stroke), rounding, 0, SCALE(1));

				ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
				ImGui::BeginChild("##cfg_left_panel", ImVec2(panel_w, panel_h), false,
					ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				render_left(ImGui::GetContentRegionAvail().x);
				ImGui::EndChild();
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();

				ImGui::SameLine(0.f, gap);

				ImVec2 right_pos = ImGui::GetCursorScreenPos();
				ImVec2 right_max = ImVec2(right_pos.x + panel_w, right_pos.y + panel_h);
				draw->rect_filled(dl, right_pos, right_max, draw->get_clr(clr->window.sub_background), rounding);
				draw->rect       (dl, right_pos, right_max, draw->get_clr(clr->window.stroke), rounding, 0, SCALE(1));

				ImGui::PushStyleColor(ImGuiCol_ChildBg,             IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,         IM_COL32(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,       draw->get_clr(clr->widgets.sub_stroke));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,draw->get_clr(clr->widgets.stroke));
				ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, draw->get_clr(clr->accent));
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     SCALE(5.f));
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, SCALE(3.f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,     padding);
				ImGui::BeginChild("##cfg_right_panel", ImVec2(panel_w, panel_h), false,
					ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove);
				render_right();
				ImGui::EndChild();
				ImGui::PopStyleVar(3);
				ImGui::PopStyleColor(5);
			}
		}

		inline auto render_panels() -> void { ui::render_panels(); }
	}
}
