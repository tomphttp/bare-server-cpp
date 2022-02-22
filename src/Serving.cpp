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
	, deadline(socket.get_executor(), std::chrono::seconds(60))
	, buffer(8192)
{}

void Serving::process(){
	read();
	init_deadline();
}

void Serving::init_deadline(){
	std::shared_ptr<Serving> serving = shared_from_this();

	deadline.async_wait([serving](beast::error_code ec) {
		if(!ec) {
			// Close socket to cancel any outstanding operation.
			serving->socket.close(ec);
		}
	});
}

void Serving::read(){ // headers and body
	std::shared_ptr<Serving> serving = shared_from_this();

	http::async_read(socket, buffer, request, [serving](beast::error_code ec, std::size_t bytes_transferred) {
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
		serving->socket.shutdown(tcp::socket::shutdown_send, ec);
		serving->deadline.cancel();
	});
}

void Serving::respond(){
	response.version(request.version());
	response.keep_alive(true);

	std::string target = request.target().to_string();
	
	bool route_exists = routes.contains(target);

	if(!route_exists){
		response.result(http::status::not_found);
		response.set(http::field::content_type, "text/plain");
		beast::ostream(response.body()) << "File not found";
		response.content_length(response.body().size());

		return void(write());
	}

	Route* route = routes.at(target);
	
	assert(route != NULL);
	
	route(shared_from_this());
}