#include "./v1_http_proxy.h"
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

struct SessionBody {
	using value_type = std::string;
	struct SessionReader {
		template<bool isRequest, class Headers>
		SessionReader(http::message<isRequest, SessionBody, Headers>& m){}
		void write(void const* data, std::size_t size, beast::error_code& ec) {
			std::cout << "Write  " << size << std::endl;
			// this will be called with each piece of the body, after chunk decoding
		}
	};
};


class Session : public std::enable_shared_from_this<Session> {
public:
	Session(std::shared_ptr<Serving> _serving)
		: serving(_serving)
		, resolver(net::make_strand(serving->server->iop))
		, stream(serving->server->iop)
		, serializer(response)
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
	SessionBody body;
	http::request<http::empty_body> request;
	http::response<http::buffer_body> response;
	http::response_serializer<http::buffer_body, http::fields> serializer;
	http::response_parser<http::buffer_body> remote_parser;
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

		for(auto it = got.begin(); it != got.end(); ++it){
			std::cout << it->name() << " : " << it->value() << std::endl;
		}

		std::cout << "got keepalive was " << got.keep_alive() << std::endl;
		
		response.result(http::status::ok);
		response.version(11);
		response.keep_alive(got.keep_alive());

		if(got.keep_alive()){
			response.set("Connection", "Keep-Alive");
		}
		
		response.set("X-Bare-Headers", "{}");
		
    	response.set(http::field::transfer_encoding, "chunked");
		
		response.body().data = nullptr;
		response.body().more = true;
		
		http::async_write_header(serving->socket, serializer, beast::bind_front_handler(&Session::on_client_write_headers, shared_from_this()));
		stream.async_read_some(boost::asio::buffer(read_buffer, sizeof(read_buffer)), beast::bind_front_handler(&Session::on_read_chunk, shared_from_this()));

	}
	void on_client_write_headers(beast::error_code ec, size_t bytes_transferred){
		std::cout << "wrote headers, reading some" << std::endl;
	}
	bool shutdown = false;
	void on_client_write_chunk(beast::error_code ec, size_t bytes_transferred){
		if (ec == http::error::need_buffer) {
			std::cout << "read more" << std::endl;
			stream.async_read_some(boost::asio::buffer(read_buffer, sizeof(read_buffer)), beast::bind_front_handler(&Session::on_read_chunk, shared_from_this()));
		}
		else{
			if (ec) {
				return fail(ec, "writing chunk");
			}
		
			std::cout << "wrote chunk" << std::endl;

			if(shutdown){
				serving->socket.shutdown(tcp::socket::shutdown_send, ec);
				serving->deadline.cancel();
			}
		}
	}
	void on_read_chunk(beast::error_code ec, size_t bytes_transferred){
		if (ec == boost::asio::error::eof) {
			std::cout << "eof, bytes transferred was " << bytes_transferred << std::endl;

			response.body().data = read_buffer;
            response.body().size = bytes_transferred;
			response.body().more = false;

			shutdown = true;
		}
		else {
			if (ec) {
				return fail(ec, "read");
			}

			std::cout << "will transfer " << bytes_transferred << " bytes" << std::endl;
			
			response.body().data = read_buffer;
            response.body().size = bytes_transferred;
            response.body().more = true;
		}
		
		http::async_write(serving->socket, serializer, beast::bind_front_handler(&Session::on_client_write_chunk, shared_from_this()));
	}
	void fail(beast::error_code ec, std::string message){
		std::cout << "Failed with message: " << message << ", " << ec.message() << ", " << ec.category().name() << ", " << ec.category().message(0) << std::endl;
	}
};

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	std::make_shared<Session>(serving)->process("x.com", "80", "/.htaccess", 11);
}