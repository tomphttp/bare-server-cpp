#include "./v1_http_headers.h"

bool read_headers(unsigned int& error_status, std::string& error, std::string& host, std::string& port, std::string& protocol, const boost::beast::http::request_header<boost::beast::http::fields>& input, boost::beast::http::request_header<boost::beast::http::fields>& output) {
	error = "test";

	// for(input)
	
	return false;
}