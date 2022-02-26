#pragma once
#include "./Server.h"

class Serving : public std::enable_shared_from_this<Serving> {
   private:
	boost::beast::http::response<boost::beast::http::empty_body> response;

   public:
	std::shared_ptr<Server> server;
	boost::asio::ip::tcp::socket socket;
	boost::beast::http::request_parser<boost::beast::http::buffer_body> request_parser;
	boost::beast::flat_buffer buffer;
	Serving(boost::asio::ip::tcp::socket socket, std::shared_ptr<Server> server);
	const boost::beast::http::response<boost::beast::http::empty_body>& response_base();
	void process();
	void on_sent(bool keep_alive);
	void json(unsigned int status, std::string serialized);

   private:
	void respond();
};
