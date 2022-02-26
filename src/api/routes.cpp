#include "./routes.h"
#include "./instance_info.h"
#include "./v1/v1_http_proxy.h"
#include "./v1/v1_ws_meta.h"

const std::map<std::string, Route*> routes = {
    {"/", &instance_information},
    {"/v1/", &v1_http_proxy},
    {"/v1/ws-meta", &v1_ws_meta},
};