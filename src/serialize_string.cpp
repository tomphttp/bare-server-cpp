#include "./serialize_string.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

std::string serialize_string(std::string unserialized) {
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	rapidjson::Value value;

	// no allocator
	value.SetString(unserialized.c_str(), unserialized.length());
	value.Accept(writer);

	return {buffer.GetString(), buffer.GetSize()};
}