#include "./instance_info.h"
#include "../process_memory.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

// 1024 squared
constexpr unsigned long long MEGABYTE = 1048576;

void instance_information(std::shared_ptr<Serving> serving) {
	rapidjson::Document document;
	
	document.SetObject();
	
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

	rapidjson::Value versions;

	versions.SetArray();

	versions.PushBack(rapidjson::Value().SetString("v1", allocator), allocator);

	document.AddMember(rapidjson::Value().SetString("versions", allocator), versions, allocator);

	document.AddMember(rapidjson::Value().SetString("language", allocator), rapidjson::Value().SetString("C++", allocator), allocator);

	double vm = 0.0;
	process_memory_usage(vm);

	document.AddMember(rapidjson::Value().SetString("memoryUsage", allocator), rapidjson::Value().SetDouble(vm / MEGABYTE), allocator);

	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

	document.Accept(writer);

	std::string body = buffer.GetString();

	serving->json(200, { buffer.GetString(), buffer.GetSize() });
}