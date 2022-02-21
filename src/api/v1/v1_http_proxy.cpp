#include "./v1_http_proxy.h"
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
	Session(net::io_context& ioc) : resolver(net::make_strand(ioc)), stream(net::make_strand(ioc)) {}
	tcp::resolver resolver;
    beast::tcp_stream stream;
    beast::flat_buffer buffer; // (Must persist between reads)
    http::request<http::empty_body> request;
    http::response<http::string_body> response;
	void process(std::string host, std::string port, std::string path, int version){
		request.version(version);
        request.method(http::verb::get);
        request.target(path);
        request.set(http::field::host, host);
        request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

		resolver.async_resolve("sys32.dev", "80", beast::bind_front_handler(&Session::on_resolve, shared_from_this()));
	}
private:
	void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) {
            return fail(ec, "resolve");
		}
        // Set a timeout on the operation
        stream.expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        stream.async_connect(results, beast::bind_front_handler(&Session::on_connect, shared_from_this()));
    }

void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if(ec) {
            return fail(ec, "connect");
		}

        // Set a timeout on the operation
        stream.expires_after(std::chrono::seconds(30));

        // Send the HTTP request to the remote host
        http::async_write(stream, request,beast::bind_front_handler(&Session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if(ec) {
            return fail(ec, "write");
        }

        // Receive the HTTP response
        http::async_read(stream, buffer, response, beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if(ec) {
            return fail(ec, "read");
		}

        // Write the message to standard out
        std::cout << response << std::endl;

        // Gracefully close the socket
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes so don't bother reporting it.
        if(ec && ec != beast::errc::not_connected) {
            return fail(ec, "shutdown");
		}

        // If we get here then the connection is closed gracefully
	}
	void fail(beast::error_code, std::string message){
		std::cout << "Failed with message: " << message << std::endl;
	}
};


void v1_http_proxy(std::shared_ptr<Serving> serving) {
	boost::beast::ostream(serving->response.body()) << "Hello, World!";
	serving->response.content_length(serving->response.body().size());
	serving->write();


	net::io_context ioc{1};
	tcp::resolver resolver{ioc};
	beast::tcp_stream stream(ioc);

	std::make_shared<Session>(ioc)->process("sys32.dev", "80", "/", 11);

	ioc.run();
}