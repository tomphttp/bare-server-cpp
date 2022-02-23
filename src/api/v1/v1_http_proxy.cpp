#include "./v1_http_proxy.h"
#include "./v1_http_headers.h"
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

/*template<class Stream>
class PipeBody : public std::enable_shared_from_this<BaseSession<Stream>> {

};*/

template<class Stream>
class BaseSession : public std::enable_shared_from_this<BaseSession<Stream>> {
public:
	std::shared_ptr<Serving> serving;
	Stream& stream;
	http::request<http::buffer_body> request;
	http::response<http::buffer_body> response;
	http::request<http::buffer_body> outgoing_request;
	http::response_serializer<http::buffer_body, http::fields> serializer;
	http::response_parser<http::buffer_body> remote_parser;
	beast::flat_buffer buffer;
	tcp::resolver resolver;
	char read_buffer[8000];
	BaseSession(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_, Stream& stream_)
		: serving(serving_)
		, serializer(response)
		, resolver(net::make_strand(serving->server->iop))
		, stream(stream_)
		, request(serving->request)
		, outgoing_request(outgoing_request_)
	{}
	void process(std::string host, std::string port){
		// "sys32.dev", "443", "/", 11
		// std::string host, std::string port, std::string path, int version
		/*outgoing_request.version(request.version);
		outgoing_request.method(http::verb::get);
		outgoing_request.target(path);
		outgoing_request.set(http::field::host, host);
		outgoing_request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);*/

		resolver.async_resolve(host, port, beast::bind_front_handler(&BaseSession::on_resolve, this->shared_from_this()));
	}
	virtual void _connect(tcp::resolver::results_type results, std::function<void()> callback) = 0;
	virtual void _on_connect(std::function<void()> callback) = 0;
	void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
		if (ec) {
			return fail(ec, "resolve");
		}
		// Set a timeout on the operation
		// stream.expires_after(std::chrono::seconds(30));

		_connect(results, beast::bind_front_handler(&BaseSession::on_connect, this->shared_from_this()));
	}
	void on_connect() {
		auto self = this->shared_from_this();

		_on_connect([self](){
			// Set a timeout on the operation
			// self->stream.expires_after(std::chrono::seconds(30));

			// Send the HTTP request to the remote host
			http::async_write(self->stream, self->outgoing_request, beast::bind_front_handler(&BaseSession::on_write, self->shared_from_this()));
		});
	}
	void on_write(beast::error_code ec, size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);

		if(ec) {
			return fail(ec, "write");
		}
		
		http::async_read_header(stream, buffer, remote_parser, beast::bind_front_handler(&BaseSession::on_headers, this->shared_from_this()));
	}
	void on_headers(beast::error_code ec, size_t bytes_transferred){
		if(ec) {
			return fail(ec, "read");
		}
		
		response.result(http::status::ok);
		response.version(11);
		response.keep_alive(remote_parser.get().keep_alive());

		response.chunked(remote_parser.chunked());

		if(remote_parser.get().has_content_length()){
			response.content_length(remote_parser.content_length());
		}
	
		response.keep_alive(true);

		write_headers(remote_parser.get(), response);

		http::async_write_header(serving->socket, serializer, beast::bind_front_handler(&BaseSession::on_client_write_headers, this->shared_from_this()));
	}
	void on_client_write_headers(beast::error_code ec, size_t bytes_transferred){
		if (ec) {
			return fail(ec, "writing headers");
		}
		
		pipe_data();
	}
	// Write everything in the buffer (which might be empty)
	void write_response(){
		response.body().data = remote_parser.get().body().data;
		response.body().more = remote_parser.get().body().more;
		response.body().size = remote_parser.get().body().size;

		log_body();

		auto self = this->shared_from_this();

		http::async_write(serving->socket, serializer, [self](beast::error_code ec, size_t bytes){
			std::cout << "wrote body " << bytes << std::endl;
			std::cout << "remote parser done?: " << self->remote_parser.is_done() << std::endl;
			std::cout << "serializer done?: " << self->serializer.is_done() << std::endl;

			if(!self->remote_parser.is_done() && !self->serializer.is_done()){
				self->pipe_data();
			}
		});
	}
	void pipe_data(){
		std::cout << "wrote headers, reading some" << std::endl;

		// run loop once, check if serializer and remote are done before further running loop
		if (!remote_parser.is_done()) {
			auto self = this->shared_from_this();

			// Set up the body for writing into our small buffer
			remote_parser.get().body().data = read_buffer;
			remote_parser.get().body().size = sizeof(read_buffer);

			// Read as much as we can
			http::async_read(stream, buffer /*DYNAMIC*/, remote_parser, [self](beast::error_code ec, size_t bytes){
				std::cout << "read " << bytes << std::endl;
				
				// need_buffer is returned when buffer_body uses up the buffer
				if(ec && ec != http::error::need_buffer) {
					return self->fail(ec, "after read");
				}

				self->remote_parser.get().body().size = sizeof(self->read_buffer) - self->remote_parser.get().body().size;
				self->remote_parser.get().body().data = self->read_buffer;
				self->remote_parser.get().body().more = !self->remote_parser.is_done();		

				self->write_response();
			});
		}
		else {
			remote_parser.get().body().data = nullptr;
			remote_parser.get().body().size = 0;
			write_response();
		}
	}
	void log_body(){
		std::cout
			<< "Body contains " << response.body().size << "bytes" << std::endl
			<< std::string((char*)response.body().data, response.body().size) << std::endl
		;
	}
	void fail(beast::error_code ec, std::string message){
		std::cout << "Failed with message: " << message << ", " << ec.message() << ", " << ec.category().name() << ", " << ec.category().message(0) << std::endl;
	}
};

class SessionHTTP : public BaseSession<beast::tcp_stream> {
public:
	beast::tcp_stream stream_;
	void _connect(tcp::resolver::results_type results, std::function<void()> callback){
		auto self = this->shared_from_this();

		stream.async_connect(results, [self,callback](beast::error_code ec, tcp::resolver::results_type::endpoint_type){
			if(ec) {
				return self->fail(ec, "connect");
			}

			callback();

		});
	}
	void _on_connect(std::function<void()> callback){
		callback();
	}
	SessionHTTP(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_)
		: stream_(serving_->server->iop)
		, BaseSession(serving_, outgoing_request_, stream_)
	{}
};

class SessionHTTPS : public BaseSession<beast::ssl_stream<beast::tcp_stream>> {
public:
	beast::ssl_stream<beast::tcp_stream> stream_;
	void _connect(tcp::resolver::results_type results, std::function<void()> callback){
		auto self = this->shared_from_this();

		std::string host = results->host_name();
		
		if(!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str())) {
			beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
			return fail(ec, "Setting SSL hostname");
		}
		
		beast::get_lowest_layer(stream_).async_connect(results, [self,callback](beast::error_code ec, tcp::resolver::results_type::endpoint_type){
			if(ec) {
				return self->fail(ec, "connect");
			}

			callback();

		});
	}
	void _on_connect(std::function<void()> callback){
		auto self = this->shared_from_this();
		
		stream_.async_handshake(ssl::stream_base::client, [self,callback](const boost::system::error_code& ec){
			if(ec){
				return self->fail(ec, "SSL handshake");
			}

			callback();
		});
	}
	SessionHTTPS(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_)
		: stream_(serving_->server->iop, serving->server->ssl_ctx)
		, BaseSession(serving_, outgoing_request_, stream_)
	{}
};

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	/*http::request<http::buffer_body> outgoing_request;

	std::string error, host, port, protocol;
	unsigned int status = 200;

	// outgoing_request.body() = serving->request.body();
	
	if(!read_headers(status, error, host, port, protocol, serving->request, outgoing_request)){
		beast::ostream(serving->response.body()) << error;
		serving->response.set(http::field::content_type, "application/json");
		serving->response.result(status);
		serving->write();
		return;
	}

	if(protocol == "http:"){
		std::make_shared<SessionHTTPS>(serving, outgoing_request)->process(host, port);
	}else if(protocol == "https:"){
		std::make_shared<SessionHTTP>(serving, outgoing_request)->process(host, port);
	}*/
	
	http::request<http::buffer_body> outgoing_request;
	outgoing_request.version(11);
	outgoing_request.method(http::verb::get);
	outgoing_request.target("/");
	outgoing_request.set(http::field::host, "sys32.dev");
	outgoing_request.body().data = nullptr;
	outgoing_request.body().more = false;

	std::make_shared<SessionHTTPS>(serving, outgoing_request)->process("sys32.dev", "443");
}