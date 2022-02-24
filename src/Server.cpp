#include "./Server.h"
#include "./Serving.h"
#include <boost/certify/https_verification.hpp>

namespace beast = boost::beast;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

Server::Server(std::string directory_, bool log_errors_, size_t threads)
	: directory(directory_)
	, iop(threads)
	, log_errors(log_errors)
	, ssl_ctx((ssl::context::tlsv12_client))
{
	ssl_ctx.set_verify_mode(ssl::verify_peer | ssl::context::verify_fail_if_no_peer_cert);
	ssl_ctx.set_default_verify_paths();
	
	boost::certify::enable_native_https_server_verification(ssl_ctx);
}

void Server::listen(std::string host, std::string port){
	tcp::resolver resolver(iop.get_executor());
	
	auto const resolved = resolver.resolve(host, port);

	tcp::acceptor acceptor(iop.get_executor(), {resolved->endpoint().address(),resolved->endpoint().port()});
	tcp::socket socket(iop.get_executor());
	
	http_server(acceptor, socket);

	iop.wait();
}

void Server::http_server(tcp::acceptor& acceptor, tcp::socket& socket) {
	std::shared_ptr<Server> self = shared_from_this();

	acceptor.async_accept(socket, [self, &acceptor, &socket](beast::error_code ec) {
		std::make_shared<Serving>(std::move(socket), self)->process();

		self->http_server(acceptor, socket);
	});
}
