#pragma once
#include "./Server.h"

class Serving : public std::enable_shared_from_this<Serving> {
public:
	std::shared_ptr<Server> server;
	boost::asio::ip::tcp::socket socket;
	boost::beast::http::request<boost::beast::http::dynamic_body> request;
	boost::beast::http::response<boost::beast::http::dynamic_body> response;
	Serving(boost::asio::ip::tcp::socket socket, std::shared_ptr<Server> server);
	void process();
	void write();
private:
	boost::beast::flat_buffer buffer;
	boost::asio::steady_timer deadline;
	void init_deadline();
	void read();
	void respond();
};
