#pragma once

// XISFPS-for-FreeFire port of `friends_data` from the reference project.
//
// The Configs subsystem (sections/configs.h) serializes this container as a
// trailing section of every saved config blob so imported configs travel with
// their friends whitelist. FreeFire has no friends-list feature (nothing
// feeds `names`), so at runtime the container stays empty — save writes a
// zero-count trailer, load populates an unused set. Kept 1:1 with the source
// so blobs exported from FiveM stay wire-compatible if a user ever imports
// one via the "Import Clipboard" button.

#include <string>
#include <unordered_set>
#include <mutex>

namespace friends_data
{
	inline std::unordered_set<std::string> names;
	inline std::mutex                      mu;

	inline auto add(const std::string& n) -> void
	{
		std::lock_guard<std::mutex> lk(mu);
		names.insert(n);
	}

	inline auto remove(const std::string& n) -> void
	{
		std::lock_guard<std::mutex> lk(mu);
		names.erase(n);
	}

	inline auto toggle(const std::string& n) -> bool
	{
		std::lock_guard<std::mutex> lk(mu);
		auto it = names.find(n);
		if (it != names.end()) { names.erase(it); return false; }
		names.insert(n);
		return true;
	}

	inline auto is_friend(const std::string& n) -> bool
	{
		if (n.empty()) return false;
		std::lock_guard<std::mutex> lk(mu);
		return names.find(n) != names.end();
	}
}
