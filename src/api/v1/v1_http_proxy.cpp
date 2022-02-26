#include "./v1_http_proxy.h"
#include <iostream>
#include <sstream>
#include "../../serialize_string.h"
#include "./v1_http_headers.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

template <class Stream>
class BaseSession : public std::enable_shared_from_this<BaseSession<Stream>> {
   public:
	std::shared_ptr<Serving> serving;
	Stream& stream;
	http::response<http::buffer_body> response;
	http::request<http::buffer_body> outgoing_request;
	http::response_serializer<http::buffer_body> response_serializer;
	http::request_serializer<http::buffer_body> request_serializer;
	http::request_parser<http::buffer_body>& request_parser;
	http::response_parser<http::buffer_body> remote_parser;
	beast::flat_buffer buffer;
	tcp::resolver resolver;
	char read_buffer[8000];
	virtual void _connect(tcp::resolver::results_type results, std::function<void()> callback) = 0;
	BaseSession(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_, Stream& stream_)
	    : serving(serving_), resolver(net::make_strand(serving->server->iop)), stream(stream_), outgoing_request(outgoing_request_), request_serializer(outgoing_request), request_parser(serving->request_parser), response(serving->response_base()), response_serializer(response) {}
	void process(std::string host, std::string port) {
		resolver.async_resolve(host, port, beast::bind_front_handler(&BaseSession::on_resolve, this->shared_from_this()));
	}
	void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
		if (ec) {
			if (ec == boost::asio::error::host_not_found) {
				return void(serving->json(500, R"({"code":"HOST_NOT_FOUND","id":"request","message":"The specified host could not be resolved."})"));
			}

			return fail(ec, "Resolving host");
		}

		auto self = this->shared_from_this();

		_connect(results, [self]() {
			http::async_write_header(self->stream, self->request_serializer, beast::bind_front_handler(&BaseSession::on_write_headers, self->shared_from_this()));
		});
	}
	void on_write_headers(beast::error_code ec, size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);

		if (ec) {
			return fail(ec, "Sending headers to remote");
		}

		// std::cout << "Begin to pipe request" << std::endl;

		pipe_request();
	}
	// Write everything in the buffer (which might be empty)
	void write_request() {
		outgoing_request.body().data = request_parser.get().body().data;
		outgoing_request.body().more = request_parser.get().body().more;
		outgoing_request.body().size = request_parser.get().body().size;

		auto self = this->shared_from_this();

		http::async_write(serving->socket, request_serializer, [self](beast::error_code ec, size_t bytes) {
			if (!self->request_parser.is_done() && !self->request_serializer.is_done()) {
				self->pipe_request();
			} else {
				http::async_read_header(self->stream, self->buffer, self->remote_parser, beast::bind_front_handler(&BaseSession::on_headers, self->shared_from_this()));
			}
		});
	}
	void pipe_request() {
		// run loop once, check if serializer and remote are done before further running loop
		if (!request_parser.is_done()) {
			auto self = this->shared_from_this();

			// Set up the body for writing into our small buffer
			request_parser.get().body().data = read_buffer;
			request_parser.get().body().size = sizeof(read_buffer);

			// Read as much as we can
			http::async_read(serving->socket, serving->buffer /*DYNAMIC*/, request_parser, [self](beast::error_code ec, size_t bytes) {
				// need_buffer is returned when buffer_body uses up the buffer
				if (ec && ec != http::error::need_buffer) {
					return self->fail(ec, "Piping request, async_read");
				}

				self->request_parser.get().body().size = sizeof(self->read_buffer) - self->request_parser.get().body().size;
				self->request_parser.get().body().data = self->read_buffer;
				self->request_parser.get().body().more = !self->request_parser.is_done();

				self->write_request();
			});
		} else {
			request_parser.get().body().data = nullptr;
			request_parser.get().body().size = 0;
			write_request();
		}
	}
	void on_headers(beast::error_code ec, size_t bytes_transferred) {
		if (ec) {
			return fail(ec, "On remote headers");
		}

		response.chunked(remote_parser.chunked());

		if (remote_parser.get().has_content_length()) {
			response.content_length(remote_parser.content_length());
		}

		response.keep_alive(serving->request_parser.get().keep_alive());

		write_headers(remote_parser.get(), response);

		http::async_write_header(serving->socket, response_serializer, beast::bind_front_handler(&BaseSession::on_client_write_headers, this->shared_from_this()));
	}
	void on_client_write_headers(beast::error_code ec, size_t bytes_transferred) {
		if (ec) {
			return fail(ec, "After sending headers to client");
		}

		// std::cout << "Begin to pipe remote" << std::endl;

		pipe_remote();
	}
	// Write everything in the buffer (which might be empty)
	void write_remote_response() {
		response.body().data = remote_parser.get().body().data;
		response.body().more = remote_parser.get().body().more;
		response.body().size = remote_parser.get().body().size;

		auto self = this->shared_from_this();

		http::async_write(serving->socket, response_serializer, [self](beast::error_code ec, size_t bytes) {
			if (!self->remote_parser.is_done() && !self->response_serializer.is_done()) {
				self->pipe_remote();
			}
		});
	}
	void on_complete_response() {
		if (!response.keep_alive()) {
			beast::error_code ec;
			serving->socket.shutdown(tcp::socket::shutdown_send, ec);
		}
	}
	void pipe_remote() {
		// run loop once, check if serializer and remote are done before further running loop
		if (!remote_parser.is_done()) {
			auto self = this->shared_from_this();

			// Set up the body for writing into our small buffer
			remote_parser.get().body().data = read_buffer;
			remote_parser.get().body().size = sizeof(read_buffer);

			// Read as much as we can
			http::async_read(stream, buffer, remote_parser, [self](beast::error_code ec, size_t bytes) {
				// need_buffer is returned when buffer_body uses up the buffer
				if (ec && ec != http::error::need_buffer) {
					return self->fail(ec, "Piping remote, async_read");
				}

				self->remote_parser.get().body().size = sizeof(self->read_buffer) - self->remote_parser.get().body().size;
				self->remote_parser.get().body().data = self->read_buffer;
				self->remote_parser.get().body().more = !self->remote_parser.is_done();

				self->write_remote_response();
			});
		} else {
			remote_parser.get().body().data = nullptr;
			remote_parser.get().body().size = 0;
			write_remote_response();
			on_complete_response();
		}
	}
	void on_write_fail(beast::error_code ec, size_t bytes) {}
	void fail(beast::error_code ec, std::string where) {
		std::string error_message = ec.message();
		std::string error_category = ec.category().name();
		std::string error_category_message = ec.category().message(0);

		if (serving->server->log_errors) {
			std::cerr << "Failed at " << where << ", " << error_message << ", " << error_category << ", " << error_category_message << std::endl;
		}

		unsigned int status = 400;
		std::string json = R"({"code":"UNKNOWN","id":)" + serialize_string(error_category) + R"(,"message":)" + serialize_string(error_message + " (At" + where + ")") + R"(})";

		if (!response_serializer.is_header_done()) {
			serving->json(status, json);
		} else if (!response_serializer.is_done()) {
			// custom response body for the debugger
			std::string debug = R"({"statusCode":)" + std::to_string(status) + R"(,"error":)" + json + R"(})";

			response.body().data = debug.data();
			response.body().size = debug.size();
			response.body().more = false;

			http::async_write(serving->socket, response_serializer, beast::bind_front_handler(&BaseSession::on_write_fail, this->shared_from_this()));
		}
	}
};

class SessionHTTP : public BaseSession<beast::tcp_stream> {
   public:
	beast::tcp_stream stream_;
	void _connect(tcp::resolver::results_type results, std::function<void()> callback) {
		auto self = this->shared_from_this();

		stream.expires_after(std::chrono::seconds(30));

		stream.async_connect(results, [self, callback](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
			if (ec) {
				return self->fail(ec, "connect");
			}

			callback();
		});
	}
	SessionHTTP(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_)
	    : stream_(serving_->server->iop), BaseSession(serving_, outgoing_request_, stream_) {}
};

class SessionHTTPS : public BaseSession<beast::ssl_stream<beast::tcp_stream>> {
   public:
	beast::ssl_stream<beast::tcp_stream> stream_;
	void _connect(tcp::resolver::results_type results, std::function<void()> callback) {
		auto self = std::shared_ptr<SessionHTTPS>(this);

		std::string host = results->host_name();

		if (!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str())) {
			beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
			return fail(ec, "Setting SSL hostname");
		}

		beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

		beast::get_lowest_layer(stream_).async_connect(results, [self, callback](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
			if (ec) {
				if (ec == beast::error::timeout) {
					return void(self->serving->json(500, R"({"code":"CONNECTION_TIMEOUT","id":"request","message":"The specified host didn't connect in time."})"));
				}

				return self->fail(ec, "connect");
			}

			self->stream_.async_handshake(ssl::stream_base::client, [self, callback](const boost::system::error_code& ec) {
				if (ec) {
					return self->fail(ec, "SSL handshake");
				}

				callback();
			});
		});
	}
	SessionHTTPS(std::shared_ptr<Serving> serving_, const http::request<http::buffer_body>& outgoing_request_)
	    : stream_(serving_->server->iop, serving->server->ssl_ctx), BaseSession(serving_, outgoing_request_, stream_) {}
};

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	http::request<http::buffer_body> outgoing_request;
	outgoing_request.version(11);

	std::string host, port, protocol;

	if (!read_headers(host, port, protocol, serving, outgoing_request)) {
		return;
	}

	if (protocol == "http:") {
		std::make_shared<SessionHTTP>(serving, outgoing_request)->process(host, port);
	} else if (protocol == "https:") {
		std::make_shared<SessionHTTPS>(serving, outgoing_request)->process(host, port);
	}
}