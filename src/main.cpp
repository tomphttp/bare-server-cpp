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

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace po = boost::program_options;
using tcp = boost::asio::ip::tcp;

class http_connection : public std::enable_shared_from_this<http_connection> {
public:
	http_connection(tcp::socket socket) : socket_(std::move(socket)) {}
	// Initiate the asynchronous operations associated with the connection.
	void start() {
		read_request();
		check_deadline();
	}
private:
	// The socket for the currently connected client.
	tcp::socket socket_;

	// The buffer for performing reads.
	beast::flat_buffer buffer_{8192};

	// The request message.
	http::request<http::dynamic_body> request_;

	// The response message.
	http::response<http::dynamic_body> response_;

	// The timer for putting a deadline on connection processing.
	net::steady_timer deadline_{ socket_.get_executor(), std::chrono::seconds(60) };

	// Asynchronously receive a complete request message.
	void read_request() {
		auto self = shared_from_this();

		http::async_read(socket_, buffer_, request_, [self](beast::error_code ec, std::size_t bytes_transferred) {
			boost::ignore_unused(bytes_transferred);
			if (!ec) {
				self->process_request();
			}
		});
	}

	// Determine what needs to be done with the request message.
	void process_request() {
		response_.version(request_.version());
		response_.keep_alive(false);

		const std::string target = request_.target().to_string();
		
		if(target == "/v1/"){
			
		}
		else if(target == "/v1/ws-meta"){

		}
		else if(target == "/"){
			rapidjson::Document document;
			document.SetObject();
			
			rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

			rapidjson::Value versions;
			versions.SetArray();
			versions.PushBack(rapidjson::Value().SetString("v1", allocator), allocator);
			document.AddMember(rapidjson::Value().SetString("versions", allocator), versions, allocator);

			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

			document.Accept(writer);

			std::string serialized = buffer.GetString();

			response_.set("content-type", "application/json");
			beast::ostream(response_.body()) << serialized;

			write_response();

			return;
		}
		
		response_.result(http::status::not_found);
		response_.set(http::field::content_type, "text/plain");
		beast::ostream(response_.body()) << "File not found\r\n";
	}

	// Construct a response message based on the program state.
	void create_response() {
		if(request_.target() == "/count") {
			response_.set(http::field::content_type, "text/html");
			beast::ostream(response_.body()) << "<html>\n" <<  "<head><title>Request count</title></head>\n" <<  "<body>\n" <<  "<h1>Request count</h1>\n" <<  "<p>There have been undefined requests so far.</p>\n" <<  "</body>\n" <<  "</html>\n";
		}
		else if(request_.target() == "/time") {
			response_.set(http::field::content_type, "text/html");
			beast::ostream(response_.body())
				<<  "<html>\n"
				<<  "<head><title>Current time</title></head>\n"
				<<  "<body>\n"
				<<  "<h1>Current time</h1>\n"
				<<  "<p>The current time is NaN"
				<<  " seconds since the epoch.</p>\n"
				<<  "</body>\n"
				<<  "</html>\n";
		}
		else {
			response_.result(http::status::not_found);
			response_.set(http::field::content_type, "text/plain");
			beast::ostream(response_.body()) << "File not found\r\n";
		}
	}

	// Asynchronously transmit the response message.
	void write_response() {
		auto self = shared_from_this();

		response_.content_length(response_.body().size());

		http::async_write(socket_, response_, [self](beast::error_code ec, std::size_t) {
			self->socket_.shutdown(tcp::socket::shutdown_send, ec);
			self->deadline_.cancel();
		});
	}

	// Check whether we have spent enough time on this connection.
	void check_deadline() {
		auto self = shared_from_this();

		deadline_.async_wait([self](beast::error_code ec) {
			if(!ec) {
				// Close socket to cancel any outstanding operation.
				self->socket_.close(ec);
			}
		});
	}
};

// "Loop" forever accepting new connections.
void http_server(tcp::acceptor& acceptor, tcp::socket& socket) {
	acceptor.async_accept(socket, [&](beast::error_code ec) {
		if(!ec) {
			std::make_shared<http_connection>(std::move(socket))->start();
		}

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