#pragma once
#include <chrono>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>

class Serving : public std::enable_shared_from_this<Serving> {
public:
	boost::asio::ip::tcp::socket socket;
	boost::beast::http::request<boost::beast::http::dynamic_body> request;
	boost::beast::http::response<boost::beast::http::dynamic_body> response;
	Serving(boost::asio::ip::tcp::socket socket);
	void process();
	void write();
private:
	boost::beast::flat_buffer buffer{8192};
	boost::asio::steady_timer deadline{ socket.get_executor(), std::chrono::seconds(60) };
	void init_deadline();
	void read();
	void respond();
};