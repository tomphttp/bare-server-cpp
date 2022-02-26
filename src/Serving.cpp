#include "./Serving.h"

#include <cassert>
#include <chrono>
#include <iostream>

#include "./api/routes.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = boost::asio::ip::tcp;

Serving::Serving(tcp::socket socket_, std::shared_ptr<Server> server_)
    : socket(std::move(socket_))
    , server(server_)
    , buffer(8192) {
}

void Serving::process() {  // headers and body
	std::shared_ptr<Serving> self = shared_from_this();

	http::async_read_header(socket, buffer, request_parser, [self](beast::error_code ec, std::size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);
		
		if (!ec) {
			self->respond();
		}else{
			std::cerr << "error code " << ec.category().message(1) << ", " << ec.value() << std::endl;
		} });
}

const boost::beast::http::response<boost::beast::http::empty_body>&
Serving::response_base() {
	return response;
}

void Serving::respond() {
	response.result(http::status::ok);
	response.version(11);
	response.version(request_parser.get().version());

	if (request_parser.get().keep_alive()) {
		response.keep_alive(true);
		response.set(http::field::connection, "keep-alive");
		response.set(http::field::keep_alive, "timeout=5");
	}

	response.set("Access-Control-Allow-Headers", "*");
	response.set("Access-Control-Allow-Origin", "*");
	response.set("Access-Control-Expose-Headers", "*");

	std::string target = request_parser.get().target().to_string();

	size_t directory_index = target.find(server->directory);

	if (directory_index == std::string::npos) {
		return json(404, R"({"message":"Unknown directory"})");
	}

	target = "/" + target.substr(directory_index + server->directory.length());

	bool route_exists = routes.contains(target);

	if (!route_exists) {
		return void(json(404, R"({"message":"Not found."})"));
	}

	Route* route = routes.at(target);

	assert(route != NULL);

	route(shared_from_this());
}

void Serving::on_sent(bool keep_alive) {
	if (keep_alive) {
		std::make_shared<Serving>(std::move(socket), server)->process();
	} else {
		beast::error_code ec;
		socket.shutdown(tcp::socket::shutdown_send, ec);
	}
}

void Serving::json

    (unsigned int status,

     std::string serialized) {
	std::shared_ptr<Serving> self = shared_from_this();
	auto response = std::make_shared<http::response<http::dynamic_body>>(response_base());

	beast::ostream(response->body()) << serialized;
	response->set(http::field::content_type, "application/json");
	response->content_length(response->body().size());
	response->result(status);

	http::async_write(socket, *response.get(), [self, response](beast::error_code ec, std::size_t) { self->on_sent(response->keep_alive()); });
}
