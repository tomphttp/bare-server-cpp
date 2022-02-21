#include "./process_memory.h"

#ifdef WIN32
#include <windows.h>
#include <psapi.h>

void process_memory_usage(double& vm_usage) {
	vm_usage = 0.0;

	//get the handle to this process
	HANDLE process = GetCurrentProcess();
	//to fill in the process' memory usage details
	PROCESS_MEMORY_COUNTERS pmc;
	
	if (GetProcessMemoryInfo(process, &pmc, sizeof(pmc))) {
		vm_usage = pmc.WorkingSetSize;
	}
}
#else
#include <fstream>
#include <unistd.h>

// https://stackoverflow.com/a/671389
void process_memory_usage(double& vm_usage) {
	vm_usage = 0.0;

	// 'file' stat seems to give the most reliable results
	std::ifstream stat_stream("/proc/self/stat",std::ios_base::in);

	// the two fields we want
	unsigned long vsize;
	long rss;

	{
		// dummy vars for leading entries in stat that we don't care about
		//
		std::string pid, comm, state, ppid, pgrp, session, tty_nr;
		std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
		std::string utime, stime, cutime, cstime, priority, nice;
		std::string O, itrealvalue, starttime;

		stat_stream
			>> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
			>> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
			>> utime >> stime >> cutime >> cstime >> priority >> nice
			>> O >> itrealvalue >> starttime >> vsize >> rss
		; // don't care about the rest

		stat_stream.close();
	}

	long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
	vm_usage = vsize;
}
#endif