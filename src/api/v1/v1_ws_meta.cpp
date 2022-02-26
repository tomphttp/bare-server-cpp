#include "./v1_ws_meta.h"

void v1_ws_meta(std::shared_ptr<Serving> serving) {
	serving->json(200, R"(""Hello, World!"")");
}