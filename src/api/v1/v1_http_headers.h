#pragma once
#include <string>
#include <boost/beast.hpp>

// read x-bare headers and put result into output
// true if okay, false if error
bool read_headers(unsigned int& error_status, std::string& error, std::string& host, std::string& port, std::string& protocol, const boost::beast::http::request_header<boost::beast::http::fields>& server_request, boost::beast::http::request_header<boost::beast::http::fields>& request);

// serialize fields into a json string
void write_headers(const boost::beast::http::response_header<boost::beast::http::fields>& remote, boost::beast::http::response_header<boost::beast::http::fields>& response);