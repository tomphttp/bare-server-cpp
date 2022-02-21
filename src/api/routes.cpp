#include "./routes.h"
#include "./instance_info.h"
#include "./v1/v1_http_proxy.h"
#include "./instance_info.h"

std::map<std::string, Route> routes = {
	{"/", &instance_information},
	{"/v1/", &v1_http_proxy},
};