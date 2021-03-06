#pragma once
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

class Server : public std::enable_shared_from_this<Server> {
   public:
	bool log_errors;
	std::string directory;
	boost::asio::thread_pool iop;
	boost::asio::ssl::context ssl_ctx;
	Server(std::string directory, bool log_errors, size_t threads);
	void listen(std::string host, std::string port);
	void http_server(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::ip::tcp::socket& socket);
};