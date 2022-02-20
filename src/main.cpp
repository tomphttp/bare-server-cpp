#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include "./memory.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace po = boost::program_options;
using tcp = boost::asio::ip::tcp;

// 1024 squared
constexpr unsigned long long MEGABYTE = 1048576;

class Serving : public std::enable_shared_from_this<Serving> {
public:
	Serving(tcp::socket s) : socket(std::move(s)) {}
	void process(){
		read();
	}
private:
	tcp::socket socket;
	beast::flat_buffer buffer{8192};
	http::request<http::dynamic_body> request;
	http::response<http::dynamic_body> response;
	// net::steady_timer deadline{ socket.get_executor(), std::chrono::seconds(60) };
	// The timer for putting a deadline on connection processing.
	/*deadline.async_wait([&socket](beast::error_code ec) {
		if(!ec) {
			// Close socket to cancel any outstanding operation.
			// socket.close(ec);
		}
	});*/
	void read(){ // headers and body
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
	void respond(){
		std::cout << request.target().to_string() << std::endl;

		response.result(http::status::not_found);
		response.set(http::field::content_type, "text/plain");
		beast::ostream(response.body()) << "File not found";
		response.content_length(response.body().size());

		write();
	}
	void write(){
		std::shared_ptr<Serving> serving = shared_from_this();

		http::async_write(socket, response, [serving](beast::error_code ec, std::size_t) {
			serving->socket.shutdown(tcp::socket::shutdown_send, ec);
			// deadline.cancel();
		});
	}
};

// "Loop" forever accepting new connections.
void http_server(tcp::acceptor& acceptor, tcp::socket& socket) {
	acceptor.async_accept(socket, [&](beast::error_code ec) {
		std::make_shared<Serving>(std::move(socket))->process();

		http_server(acceptor, socket);
	});
}

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
		unsigned short listen_port = static_cast<unsigned short>(std::atoi(port.c_str()));

		net::io_context ioc{1};

		tcp::resolver resolver{ioc};
		auto const iter = resolver.resolve(host, port);
		auto const endpoint = iter->endpoint();
		tcp::acceptor acceptor{ioc, {endpoint.address(),endpoint.port()}};
		tcp::socket socket{ioc};
		http_server(acceptor, socket);

		ioc.run();
	}
	catch(std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}