#include <boost/program_options.hpp>
#include <iostream>
#include <memory>
#include <string>
#include "./Serving.h"
#include "./api/routes.h"
#include <cassert>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace po = boost::program_options;
using tcp = boost::asio::ip::tcp;

Serving::Serving(tcp::socket socket_, std::shared_ptr<Server> server_)
	: socket(std::move(socket_))
	, server(server_)
	, deadline(socket.get_executor()
	, std::chrono::seconds(60))
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
	response.keep_alive(false);

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

int main(int argc, char* argv[]) {
	po::options_description desc("Allowed options");
    
	std::string host;
	std::string port;
	std::string directory;
	size_t threads = 0;

	desc.add_options ()
		("help", "Print help")
		("d,directory", po::value(&directory)->value_name("<string>")->default_value("/"), "Bare directory")
		("h,host", po::value(&host)->value_name("<string>")->default_value("localhost"), "Listening host")
		("p,port", po::value(&port)->value_name("<number>")->default_value("80"), "Listening port")
		("t,threads", po::value(&threads)->value_name("<number>")->default_value(4), "Amount of IO threads")
	;
    
    po::variables_map vm;
    po::store (po::command_line_parser (argc, argv).options (desc).run (), vm);
    po::notify (vm);
    
	if (vm.count ("help")) {
        std::cerr << desc << "\n";
        return 1;
	}

	try {
		std::shared_ptr<Server> server = std::make_shared<Server>(directory, threads);
		server->listen(host, port);
	}
	catch(std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}