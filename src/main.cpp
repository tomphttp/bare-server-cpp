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

	desc.add_options ()
		("help", "Print help")
		("directory,d", po::value(&directory)->value_name("<string>")->default_value("/"), "Bare directory")
		("host,h", po::value(&host)->value_name("<string>")->default_value("localhost"), "Listening host")
		("port,p", po::value(&port)->value_name("<number>")->default_value("80"), "Listening port")
		("errors,e", "Error logging")
		("threads,t", po::value(&threads)->value_name("<number>")->default_value(4), "Amount of IO threads")
	;
    
    po::variables_map vm;
    po::store (po::command_line_parser (argc, argv).options (desc).run (), vm);
    po::notify (vm);
    
	if(vm.count ("help")){
		std::cerr << desc << "\n";
		return 1;
	}

	try {
		std::shared_ptr<Server> server = std::make_shared<Server>(directory, vm.count("errors"), threads);
		std::cout << "Created Bare Server on directory: " << directory << std::endl;
		std::cout << "HTTP server listening. View live at http://" << host << ":" << port << directory << std::endl;
		server->listen(host, port);
	}
	catch(std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}