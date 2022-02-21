#include "./v1_http_proxy.h"

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	boost::beast::ostream(serving->response.body()) << "Hello, World!";
	serving->response.content_length(serving->response.body().size());
	serving->write();
}