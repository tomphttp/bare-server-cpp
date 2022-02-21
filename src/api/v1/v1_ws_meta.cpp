#include "./v1_ws_meta.h"

void v1_ws_meta(std::shared_ptr<Serving> serving){
	boost::beast::ostream(serving->response.body()) << "Hello, World!";
	serving->response.content_length(serving->response.body().size());
	serving->write();
}