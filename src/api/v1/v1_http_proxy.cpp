#include "./v1_http_proxy.h"
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
	Session(std::shared_ptr<Serving> _serving)
		: serving(_serving)
		, resolver(net::make_strand(serving->server->iop))
		, stream(serving->server->iop)
		, serializer(response)
		, remote_serializer(remote_parser.get())
	{}
	std::shared_ptr<Serving> serving;
	tcp::resolver resolver;
	beast::tcp_stream stream;
	beast::flat_buffer buffer; // (Must persist between reads)
	char read_buffer[8000];
	void process(std::string host, std::string port, std::string path, int version){
		request.version(version);
		request.method(http::verb::get);
		request.target(path);
		request.set(http::field::host, host);
		request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

		resolver.async_resolve(host, port, beast::bind_front_handler(&Session::on_resolve, shared_from_this()));
	}
private:
	http::request<http::empty_body> request;
	http::response<http::buffer_body> response;
	http::response_serializer<http::buffer_body, http::fields> serializer;
	http::response_parser<http::buffer_body> remote_parser;
	http::response_serializer<http::buffer_body, http::fields> remote_serializer;
	void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
		if (ec) {
			return fail(ec, "resolve");
		}
		// Set a timeout on the operation
		stream.expires_after(std::chrono::seconds(30));

		// Make the connection on the IP address we get from a lookup
		stream.async_connect(results, beast::bind_front_handler(&Session::on_connect, shared_from_this()));
	}

	void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
		if(ec) {
			return fail(ec, "connect");
		}

		// Set a timeout on the operation
		stream.expires_after(std::chrono::seconds(30));

		// Send the HTTP request to the remote host
		http::async_write(stream, request, beast::bind_front_handler(&Session::on_write, shared_from_this()));
	}
	void on_write(beast::error_code ec, size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);

		if(ec) {
			return fail(ec, "write");
		}
		
		http::async_read_header(stream, buffer, remote_parser, beast::bind_front_handler(&Session::on_headers, shared_from_this()));
	}
	void on_headers(beast::error_code ec, size_t bytes_transferred){
		if(ec) {
			return fail(ec, "read");
		}
		
		auto got = remote_parser.get();

		response.result(http::status::ok);
		response.version(11);
		response.keep_alive(got.keep_alive());

		for(auto it = got.begin(); it != got.end(); ++it){
			std::string name = it->name_string().to_string();
			std::string value = it->value().to_string();
			std::string lowercase = name;
			std::for_each(lowercase.begin(), lowercase.end(), [](char& c){ c = std::tolower(c); });

			std::cout << lowercase << ": " << value << std::endl;

		}

		std::cout << remote_parser.chunked() << std::endl;

		response.chunked(remote_parser.chunked());

		if(got.has_content_length()){
			response.content_length(remote_parser.content_length());
		}

		std::cout << "got keepalive was " << got.keep_alive() << std::endl;
		
		if(got.keep_alive()){
			response.set("Connection", "Keep-Alive");
		}
		
		response.set("X-Bare-Headers", "{}");
		
		http::async_write_header(serving->socket, serializer, beast::bind_front_handler(&Session::on_client_write_headers, shared_from_this()));
	}
	void on_client_write_headers(beast::error_code ec, size_t bytes_transferred){
		if (ec) {
			return fail(ec, "writing headers");
		}
		
		std::cout << "wrote headers, reading some" << std::endl;

		do {
			if (!remote_parser.is_done()) {
				// Set up the body for writing into our small buffer
				remote_parser.get().body().data = read_buffer;
				remote_parser.get().body().size = sizeof(read_buffer);

				// Read as much as we can
				http::read(stream, buffer /*DYNAMIC*/, remote_parser, ec);

				// This error is returned when buffer_body uses up the buffer
				if(ec == http::error::need_buffer) {
					ec = {};
				}

				if (ec) {
					return fail(ec, "after read");
				}

				// Set up the body for reading.
				// This is how much was parsed:
				remote_parser.get().body().size = sizeof(read_buffer) - remote_parser.get().body().size;
				remote_parser.get().body().data = read_buffer;
				remote_parser.get().body().more = !remote_parser.is_done();
			}
			else {
				remote_parser.get().body().data = nullptr;
				remote_parser.get().body().size = 0;
			}

			std::cout
				<< "Body contains " << remote_parser.get().body().size << "bytes" << std::endl
				<< std::string((char*)remote_parser.get().body().data, remote_parser.get().body().size) << std::endl
			;
			// Write everything in the buffer (which might be empty)
			http::write(serving->socket, remote_serializer, ec);
			
			// This error is returned when buffer_body uses up the buffer
			if(ec == http::error::need_buffer) {
				ec = {};
			}
			else if(ec) {
				return fail(ec, "writing");
			}
		}
		while (!remote_parser.is_done() && !remote_serializer.is_done());
	}
	void fail(beast::error_code ec, std::string message){
		std::cout << "Failed with message: " << message << ", " << ec.message() << ", " << ec.category().name() << ", " << ec.category().message(0) << std::endl;
	}
};

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	std::make_shared<Session>(serving)->process("x.com", "80", "/.htaccess", 11);
}