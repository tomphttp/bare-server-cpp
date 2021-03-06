#include <boost/program_options.hpp>
#include <iostream>
#include <memory>
#include <string>
#include "./Server.h"

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
	po::options_description desc("Allowed options");

	std::string host;
	std::string port;
	std::string directory;
	size_t threads = 0;

	const char* port_env = getenv("PORT");

	std::string default_port;

	if (port_env == NULL) {
		default_port = "80";
	} else {
		default_port = port_env;
	}

	// clang-format off
	desc.add_options ()
		("help", "Print help")
		("directory,d", po::value(&directory)->value_name("<string>")->default_value("/"), "Bare directory")
		("host,h", po::value(&host)->value_name("<string>")->default_value("localhost"), "Listening host")
		("port,p", po::value(&port)->value_name("<number>")->default_value(default_port), "Listening port")
		("errors,e", "Error logging")
		("threads,t", po::value(&threads)->value_name("<number>")->default_value(4), "Amount of IO threads")
	;
	// clang-format on

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cerr << desc << "\n";
		return 1;
	}

	bool log_errors = vm.count("errors");

	try {
		std::shared_ptr<Server> server = std::make_shared<Server>(directory, log_errors, threads);
		std::cout << "Created Bare Server on directory: " << directory << std::endl;
		std::cout << "Error logging is " << (log_errors ? "enabled." : "disabled." ) << std::endl;
		std::cout << "HTTP server listening. View live at http://" << host << ":" << port << directory << std::endl;
		server->listen(host, port);
	} catch (std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}