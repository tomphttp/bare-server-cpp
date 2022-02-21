#include <map>
#include <string>
#include "../Serving.h"

using Route = void(std::shared_ptr<Serving>);

extern const std::map<std::string, Route*> routes;