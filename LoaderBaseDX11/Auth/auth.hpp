#include <string>
#include <vector>

namespace Auth {
	class api {
	public:
		void init();
		void login();

		bool intialized = false;
		bool reauth_mode = false;
		std::string version, applicationId, product, subproduct;
		api(std::string applicationId, std::string version, std::string product, std::string subproduct) : applicationId(applicationId), version(version), product(product), subproduct(subproduct) {}

		std::string get_hwid();
		std::string get_hwid_components();

		class user_data {
		public:
			std::string discordUsername;
			std::string hwid;
			std::string expiry;
			std::string product;
			std::string subproduct;
			std::string role;
			std::string createdById;
			std::string createdByUsername;
		};
		user_data user;

		class response_data {
		public:
			std::string message;
			std::string key;
			std::string version;
			bool success = false;
		};
		response_data response;

		bool is_authorized() const noexcept {
			if (!response.success)    return false;
			if (user.hwid.empty())    return false;
			if (user.expiry.empty())  return false;
			if (user.product.empty()) return false;
			return true;
		}

	private:
		void update_response(bool success, std::string message) {
			api::response.success = success;
			api::response.message = message;
		};
	};
}
