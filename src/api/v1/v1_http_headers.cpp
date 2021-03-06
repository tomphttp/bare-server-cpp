#include <iostream>
#include "./v1_http_headers.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/writer.h>
#include "../../serialize_string.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

bool read_headers(
    std::string& host,
    std::string& port,
    std::string& protocol,
    std::shared_ptr<Serving> serving,
    http::request<http::buffer_body>& request) {
	std::string bare_headers;
	std::string bare_forward_headers;
	bool bare_protocol = false;
	bool bare_path = false;
	bool bare_port = false;
	bool bare_host = false;

	for (auto it = serving->request_parser.get().begin(); it != serving->request_parser.get().end(); ++it) {
		std::string name = it->name_string().to_string();
		std::string value = it->value().to_string();
		std::string lowercase = name;
		std::for_each(lowercase.begin(), lowercase.end(), [](char& c) { c = std::tolower(c); });

		if (lowercase == "x-bare-path") {
			bare_path = true;
			request.target(value);
		}

		if (lowercase == "x-bare-host") {
			bare_host = true;
			host = value;
		}

		if (lowercase == "x-bare-port") {
			bare_port = true;
			port = value;
		}

		if (lowercase == "x-bare-protocol") {
			bare_protocol = true;
			protocol = value;
		}

		if (lowercase == "x-bare-headers") {
			bare_headers = value;
		}

		if (lowercase == "x-bare-forward-headers") {
			bare_forward_headers = value;
		}
	}

	if ((!bare_headers.length() || !bare_forward_headers.length() || !bare_protocol || !bare_path || !bare_port || !bare_host) && serving->request_parser.get().method_string() == "OPTIONS") {
		auto response = std::make_shared<boost::beast::http::response<boost::beast::http::empty_body>>(serving->response_base());

		response->result(204);

		http::async_write(serving->socket, *response.get(), [serving, response](beast::error_code ec, std::size_t) {
			serving->on_sent(response->keep_alive());
		});

		return false;
	}

	if (!bare_headers.length()) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-headers","message":"Header was not specified."})");
		return false;
	}

	if (!bare_forward_headers.length()) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-forward-headers","message":"Header was not specified."})");
		return false;
	}

	if (!bare_protocol) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-protocol","message":"Header was not specified."})");
		return false;
	}

	if (!bare_path) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-path","message":"Header was not specified."})");
		return false;
	}

	if (!bare_host) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-host","message":"Header was not specified."})");
		return false;
	}

	if (!bare_port) {
		serving->json(400, R"({"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-port","message":"Header was not specified."})");
		return false;
	}

	// bare_headers
	{
		rapidjson::Document document;

		if (rapidjson::ParseResult result = document.Parse(bare_headers.c_str(), bare_headers.length()); result.IsError()) {
			serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-headers","message":)" + serialize_string(std::string("Header contained invalid JSON. (") + GetParseError_En(result.Code()) + ")") + R"(}})");
			return false;
		}

		if (!document.IsObject()) {
			serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-headers","message":"Headers object was not an object."})");
			return false;
		}

		for (auto it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
			std::string name(it->name.GetString(), it->name.GetStringLength());
			const rapidjson::Value& value = it->value;

			if (value.IsString()) {
				request.set(name, std::string(value.GetString(), value.GetStringLength()));
			} else if (value.IsArray()) {
				for (auto it = document.Begin(); it != document.End(); ++it) {
					const rapidjson::Value& value = *it;

					if (value.IsString()) {
						request.set(name, std::string(value.GetString(), value.GetStringLength()));
					} else {
						serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":)" + serialize_string("bare.headers." + name) + R"(,"message":"Header was not a String or Array."})");
						return false;
					}
				}
			} else {
				serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":)" + serialize_string("bare.headers." + name) + R"(,"message":"Header was not a String or Array."})");
				return false;
			}
		}
	}

	// forward_headers
	{
		rapidjson::Document document;

		if (rapidjson::ParseResult result = document.Parse(bare_forward_headers.c_str(), bare_forward_headers.length()); result.IsError()) {
			serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-forward-headers","message":)" + serialize_string(std::string("Header contained invalid JSON. (") + GetParseError_En(result.Code()) + ")") + R"(}})");
			return false;
		}

		if (!document.IsArray()) {
			serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-forward-headers","message":"Forwarded headers was not an array."})");
			return false;
		}

		std::vector<std::string> names = {
		    "content-length",
		    "transfer-encoding",
		};

		for (auto it = document.Begin(); it != document.End(); ++it) {
			if (!it->IsString()) {
				serving->json(400, R"({"code":"INVALID_BARE_HEADER","id":)" + serialize_string("request.headers.x-bare-forward-headers") + R"(,"message":"Header was not a String."})");
				return false;
			}

			std::string name(it->GetString(), it->GetStringLength());
			names.push_back(name);
		}

		for (std::string name : names) {
			std::string lowercase = name;
			std::for_each(lowercase.begin(), lowercase.end(), [](char& c) { c = std::tolower(c); });

			for (auto it = serving->request_parser.get().begin(); it != serving->request_parser.get().end(); ++it) {
				std::string same = it->name_string().to_string();
				std::string same_lowercase = same;
				std::for_each(same_lowercase.begin(), same_lowercase.end(), [](char& c) { c = std::tolower(c); });
				std::string value = it->value().to_string();

				if (same_lowercase == lowercase) {
					request.set(same, value);
				}
			}
		}
	}

	request.method_string(serving->request_parser.get().method_string());

	return true;
}

void write_headers(
    const http::response<http::buffer_body>& remote,
    http::response<http::buffer_body>& response) {
	rapidjson::Document headers;
	rapidjson::Document::AllocatorType& allocator = headers.GetAllocator();

	headers.SetObject();

	for (auto it = remote.begin(); it != remote.end(); ++it) {
		std::string name = it->name_string().to_string();
		std::string value = it->value().to_string();
		std::string lowercase = name;
		std::for_each(lowercase.begin(), lowercase.end(), [](char& c) { c = std::tolower(c); });

		if (lowercase == "content-encoding" || lowercase == "content-length" || lowercase == "transfer-encoding") {
			response.set(name, value);
		}

		bool appended = false;

		for (auto it = headers.MemberBegin(); it != headers.MemberEnd(); ++it) {
			std::string same_lowercase(it->name.GetString(), it->name.GetStringLength());
			std::for_each(same_lowercase.begin(), same_lowercase.end(), [](char& c) { c = std::tolower(c); });

			if (same_lowercase == lowercase) {
				// should become array
				if (it->value.IsString()) {
					rapidjson::Value saved_value(it->value, allocator);

					it->value.SetArray();
					it->value.PushBack(saved_value, allocator);
				}

				assert(it->value.IsArray());

				appended = true;
				it->value.PushBack(rapidjson::Value().SetString(value.data(), value.length(), allocator), allocator);

				break;
			}
		}

		if (appended) {
			continue;
		}

		headers.AddMember(rapidjson::Value().SetString(name.data(), name.length(), allocator), rapidjson::Value().SetString(value.data(), value.length(), allocator), allocator);
	}

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	headers.Accept(writer);
	std::string bare_headers(buffer.GetString(), buffer.GetSize());

	response.set("X-Bare-Headers", bare_headers);
	response.set("X-Bare-Status", std::to_string(remote.result_int()));
	response.set("X-Bare-Status-Text", remote.reason());
	response.set("X-Robots-Tag", "noindex");
}