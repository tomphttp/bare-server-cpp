#include "./v1_http_headers.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/en.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

std::string serialize_string(std::string unserialized){
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	rapidjson::Value value;
	
	// no allocator
	value.SetString(unserialized.c_str(), unserialized.length());
	value.Accept(writer);
	
	return { buffer.GetString(), buffer.GetSize() };
}

bool read_headers(
	std::string& host,
	std::string& port,
	std::string& protocol,
	std::shared_ptr<Serving> serving,
	http::request<http::buffer_body>& request
){
	bool bare_headers = false;
	bool bare_method = false;

	for(auto it = serving->request_parser.get().begin(); it != serving->request_parser.get().end(); ++it){
		std::string name = it->name_string().to_string();
		std::string value = it->value().to_string();
		std::string lowercase = name;
		std::for_each(lowercase.begin(), lowercase.end(), [](char& c){ c = std::tolower(c); });
		
		if(lowercase == "x-bare-method"){
			bare_method = true;
			request.method_string(value);
		}

		if(lowercase == "x-bare-headers"){
			bare_headers = true;

			rapidjson::Document document;

			if(rapidjson::ParseResult result = document.Parse(value.c_str(), value.length())){
				serving->json(400, R"({"error":{"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-headers","message":)" + serialize_string(std::string("Header contained invalid JSON. (") + GetParseError_En(result.Code()) + ")") + R"(}})");
				return false;
			}

			if(!document.IsObject()){
				serving->json(400, R"({"error":{"code":"INVALID_BARE_HEADER","id":"request.headers.x-bare-headers","message":"Headers object was not an object."}})");
				return false;
			}

			for(auto it = document.MemberBegin(); it != document.MemberEnd(); ++it){
				std::string name(it->name.GetString(), it->name.GetStringLength());
				const rapidjson::Value& value = it->value;

				if(value.IsString()){
					request.set(name, std::string(value.GetString(), value.GetStringLength()));
				}else if(value.IsArray()){
					for(auto it = document.Begin(); it != document.End(); ++it){
						const rapidjson::Value& value = *it;

						if(value.IsString()){
							request.set(name, std::string(value.GetString(), value.GetStringLength()));
						}else{
							serving->json(400, R"({"error":{"code":"INVALID_BARE_HEADER","id":)" + serialize_string("bare.headers." + name) + R"(,"message":"Header was not a String or Array."}})");
							return false;
						}
					}
				}else{
					serving->json(400, R"({"error":{"code":"INVALID_BARE_HEADER","id":)" + serialize_string("bare.headers." + name) + R"(,"message":"Header was not a String or Array."}})");
					return false;
				}
			}
		}
	}

	if(!bare_method){
		serving->json(400, R"({"error":{"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-method","message":"Header was not specified."}})");
		return false;
	}
	
	if(!bare_headers){
		serving->json(400, R"({"error":{"code":"MISSING_BARE_HEADER","id":"request.headers.x-bare-headers","message":"Header was not specified."}})");
		return false;
	}
	
	return true;
}

void write_headers(
	const http::response<http::buffer_body>& remote,
	http::response<http::buffer_body>& response
){
	rapidjson::Document headers;
	rapidjson::Document::AllocatorType& allocator = headers.GetAllocator();

	headers.SetObject();

	for(auto it = remote.begin(); it != remote.end(); ++it){
		std::string name = it->name_string().to_string();
		std::string value = it->value().to_string();
		std::string lowercase = name;
		std::for_each(lowercase.begin(), lowercase.end(), [](char& c){ c = std::tolower(c); });
		
		bool appended = false;

		for(auto it = headers.MemberBegin(); it != headers.MemberEnd(); ++it){
			std::string same_lowercase(it->name.GetString(), it->name.GetStringLength());
			std::for_each(same_lowercase.begin(), same_lowercase.end(), [](char& c){ c = std::tolower(c); });
			
			if(same_lowercase == lowercase){
				// should become array
				if(it->value.IsString()){
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

		if(appended){
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
}