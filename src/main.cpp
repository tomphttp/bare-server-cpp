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
		("d,directory", po::value(&directory)->value_name("<string>")->default_value("/"), "Bare directory")
		("h,host", po::value(&host)->value_name("<string>")->default_value("localhost"), "Listening host")
		("p,port", po::value(&port)->value_name("<number>")->default_value("80"), "Listening port")
		("t,threads", po::value(&threads)->value_name("<number>")->default_value(4), "Amount of IO threads")
	;
    
    po::variables_map vm;
    po::store (po::command_line_parser (argc, argv).options (desc).run (), vm);
    po::notify (vm);
    
	if (vm.count ("help")) {
        std::cerr << desc << "\n";
        return 1;
	}

	try {
		std::shared_ptr<Server> server = std::make_shared<Server>(directory, threads);
		server->listen(host, port);
	}
	catch(std::exception const& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}