#pragma once
#include <string>
#include <boost/beast.hpp>

// read x-bare headers and put result into output
// true if okay, false if error
bool read_headers(unsigned int& error_status, std::string& error, std::string& host, std::string& port, std::string& protocol, const boost::beast::http::request_header<boost::beast::http::fields>& input, boost::beast::http::request_header<boost::beast::http::fields>& output);

// serialize fields into a json string
std::string write_headers(const boost::beast::http::fields& input, boost::beast::http::fields& output);