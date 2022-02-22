#include "./v1_http_proxy.h"
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

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
		, serializer(response.get())
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
	http::response_parser<http::buffer_body> response;
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
		
		response.get().result(http::status::ok);
		response.get().version(11);
		response.get().keep_alive(remote_parser.get().keep_alive());

		response.get().chunked(remote_parser.chunked());

		if(remote_parser.get().has_content_length()){
			response.get().content_length(remote_parser.content_length());
		}
	
		response.get().keep_alive(true);


		rapidjson::Document headers;
		rapidjson::Document::AllocatorType& allocator = headers.GetAllocator();
		
		headers.SetObject();

		for(auto it = remote_parser.get().begin(); it != remote_parser.get().end(); ++it){
			std::string name = it->name_string().to_string();
			std::string value = it->value().to_string();
			std::string lowercase = name;
			std::for_each(lowercase.begin(), lowercase.end(), [](char& c){ c = std::tolower(c); });
			
			bool appended = false;

			for(auto it = headers.MemberBegin(); it != headers.MemberEnd(); ++it){
				std::string same_lowercase(it->name.GetString(), it->name.GetStringLength());
				std::for_each(same_lowercase.begin(), same_lowercase.end(), [](char& c){ c = std::tolower(c); });
				
				if(same_lowercase == lowercase){
					// should become array
					if(it->value.IsString()){
						rapidjson::Value saved_value(it->value, allocator);

						it->value.SetArray();
						it->value.PushBack(saved_value, allocator);
					}

					assert(it->value.IsArray());

					appended = true;
					it->value.PushBack(rapidjson::Value().SetString(value.data(), value.length(), allocator), allocator);

					break;
				}
			}

			if(appended){
				continue;
			}

			headers.AddMember(rapidjson::Value().SetString(name.data(), name.length(), allocator), rapidjson::Value().SetString(value.data(), value.length(), allocator), allocator);

			std::cout << lowercase << ": " << value << std::endl;
		}

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		headers.Accept(writer);
	
		response.get().set("X-Bare-Headers", buffer.GetString());
		
		response.get().set("X-Bare-Status", std::to_string(remote_parser.get().result_int()));
		response.get().set("X-Bare-Status-Text", remote_parser.get().reason());
		
		http::async_write_header(serving->socket, serializer, beast::bind_front_handler(&Session::on_client_write_headers, shared_from_this()));
	}
	void on_client_write_headers(beast::error_code ec, size_t bytes_transferred){
		if (ec) {
			return fail(ec, "writing headers");
		}
		
		pipe_data();
	}
	// Write everything in the buffer (which might be empty)
	void write_response(){
		response.get().body().data = remote_parser.get().body().data;
		response.get().body().more = remote_parser.get().body().more;
		response.get().body().size = remote_parser.get().body().size;

		log_body();

		auto self = shared_from_this();

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
			auto self = shared_from_this();

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
			<< "Body contains " << response.get().body().size << "bytes" << std::endl
			<< std::string((char*)response.get().body().data, response.get().body().size) << std::endl
		;
	}
	void fail(beast::error_code ec, std::string message){
		std::cout << "Failed with message: " << message << ", " << ec.message() << ", " << ec.category().name() << ", " << ec.category().message(0) << std::endl;
	}
};

void v1_http_proxy(std::shared_ptr<Serving> serving) {
	std::make_shared<Session>(serving)->process("x.com", "80", "/.htaccess", 11);
}