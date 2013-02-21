
//configuration
#define SERVER_PORT		5051
#define AUTO_CLEAR_CLOSED_SOCKET //auto clear closed clients
#define ENHANCED_STABILITY
//configuration

#include "file_server.h"

#define QUIT_COMMAND	"quit"
#define RESTART_COMMAND	"restart"
#define LIST_ALL_CLIENT	"list_all_client"

int main()
{
	std::string str;
	st_service_pump service_pump;
	file_server file_server_(service_pump);

	puts("\nthis is a file server.");
	puts("type quit to end this server.");

	service_pump.start_service();
	while(service_pump.is_running())
	{
		std::getline(std::cin, str);
		if (str == QUIT_COMMAND)
			service_pump.stop_service();
		else if (str == RESTART_COMMAND)
		{
			service_pump.stop_service();
			service_pump.start_service();
		}
		else if (str == LIST_ALL_CLIENT)
			file_server_.list_all_client();
		else
			file_server_.talk(str);
	}

	return 0;
}

//restore configuration
#undef SERVER_PORT
#undef AUTO_CLEAR_CLOSED_SOCKET //auto clear closed clients
#undef ENHANCED_STABILITY
//restore configuration
