#include "./Serving.h"
#include "./api/routes.h"
#include <iostream>
#include <chrono>
#include <cassert>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = boost::asio::ip::tcp;

Serving::Serving(tcp::socket socket_, std::shared_ptr<Server> server_)
	: socket(std::move(socket_))
	, server(server_)
	, buffer(8192)
{}

void Serving::process(){ // headers and body
	std::shared_ptr<Serving> serving = shared_from_this();

	http::async_read_header(socket, buffer, request_parser, [serving](beast::error_code ec, std::size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);
		
		if (!ec) {
			serving->respond();
		}else{
			std::cerr << "error code " << ec.category().message(1) << ", " << ec.value() << std::endl;
		}
	});
}

void Serving::write(){
	std::shared_ptr<Serving> serving = shared_from_this();

	http::async_write(socket, response, [serving](beast::error_code ec, std::size_t) {
		// serving->socket.shutdown(tcp::socket::shutdown_send, ec);
	});
}

void Serving::respond(){
	response.version(request_parser.get().version());
	response.keep_alive(true);

	std::string target = request_parser.get().target().to_string();
	
	size_t directory_index = target.find(server->directory);

	target = "/" + std::string(target.begin() + directory_index + server->directory.length(), target.end());

	std::cout << target << std::endl;

	bool route_exists = routes.contains(target);

	if(!route_exists){
		json(404, R"({"message":"Not found."})");
		return;
	}

	Route* route = routes.at(target);
	
	assert(route != NULL);
	
	route(shared_from_this());
}


void Serving::json(unsigned int status, std::string serialized){
	beast::ostream(response.body()) << serialized;
	response.set(http::field::content_type, "application/json");
	response.content_length(response.body().size());
	response.result(status);
	write();
}
