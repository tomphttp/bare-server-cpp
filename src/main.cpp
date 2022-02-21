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

Serving::Serving(tcp::socket s)
	: socket(std::move(s))
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

class Server : public std::enable_shared_from_this<Server> {
public:
	std::string directory;
	Server(std::string directory_)
		: directory(directory_)
	{}
	void listen(std::string host, std::string port){
		net::io_context ioc{1};
		tcp::resolver resolver{ioc};
		auto const iter = resolver.resolve(host, port);
		auto const endpoint = iter->endpoint();
		tcp::acceptor acceptor{ioc, {endpoint.address(),endpoint.port()}};
		tcp::socket socket{ioc};
		http_server(acceptor, socket);
		ioc.run();
	}
	void http_server(tcp::acceptor& acceptor, tcp::socket& socket) {
		acceptor.async_accept(socket, [&](beast::error_code ec) {
			std::make_shared<Serving>(std::move(socket))->process();

			http_server(acceptor, socket);
		});
	}
};

int main(int argc, char* argv[]) {
	po::options_description desc ("Allowed options");
    
	std::string host;
	std::string port;
	std::string directory;

	desc.add_options ()
		("help", "Print help")
		("d,directory", po::value(&directory)->value_name("<string>")->default_value("/"), "Bare directory")
		("h,host", po::value(&host)->value_name("<string>")->default_value("localhost"), "Listening host")
		("p,port", po::value(&port)->value_name("<number>")->default_value("80"), "Listening port")
	;
    
    po::variables_map vm;
    po::store (po::command_line_parser (argc, argv).options (desc).run (), vm);
    po::notify (vm);
    
	if (vm.count ("help")) {
        std::cerr << desc << "\n";
        return 1;
	}

	try {
		std::shared_ptr<Server> server = std::make_shared<Server>(directory);
		server->listen(host, port);
	}
	catch(std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}