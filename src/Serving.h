#pragma once
#include "./Server.h"

class Serving : public std::enable_shared_from_this<Serving> {
public:
	std::shared_ptr<Server> server;
	boost::asio::ip::tcp::socket socket;
	boost::beast::http::request_parser<boost::beast::http::buffer_body> request_parser;
	boost::beast::http::response<boost::beast::http::dynamic_body> response;
	boost::beast::flat_buffer buffer;
	Serving(boost::asio::ip::tcp::socket socket, std::shared_ptr<Server> server);
	void process();
	void write();
	void json(unsigned int status, std::string serialized);
private:
	void respond();
};
