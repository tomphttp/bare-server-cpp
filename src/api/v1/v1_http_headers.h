#pragma once
#include <string>
#include <boost/beast.hpp>
#include "../../Serving.h"

// read x-bare headers and put result into output
// true if okay, false if error
bool read_headers(
	std::string& host,
	std::string& port,
	std::string& protocol,
	std::shared_ptr<Serving> serving,
	boost::beast::http::request<boost::beast::http::buffer_body>& request
);

// serialize fields into a json string
void write_headers(
	const boost::beast::http::response<boost::beast::http::buffer_body>& remote,
	boost::beast::http::response<boost::beast::http::buffer_body>& response
);