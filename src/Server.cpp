#include "./Server.h"
#include "./Serving.h"

namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

Server::Server(std::string directory_, size_t threads)
	: directory(directory_)
	, iop(threads)
{}

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
