
#include <iostream>

//configuration
#define ST_ASIO_SERVER_PORT		9528
#define ST_ASIO_REUSE_OBJECT //use objects pool
//#define ST_ASIO_FREE_OBJECT_INTERVAL 60 //it's useless if ST_ASIO_REUSE_OBJECT macro been defined
//#define ST_ASIO_SYNC_DISPATCH //do not open this feature, see below for more details
#define ST_ASIO_DISPATCH_BATCH_MSG
#define ST_ASIO_ENHANCED_STABILITY
#define ST_ASIO_FULL_STATISTIC //full statistic will slightly impact efficiency
#define ST_ASIO_USE_STEADY_TIMER
//#define ST_ASIO_USE_SYSTEM_TIMER
#define ST_ASIO_ALIGNED_TIMER
#define ST_ASIO_AVOID_AUTO_STOP_SERVICE
#define ST_ASIO_DECREASE_THREAD_AT_RUNTIME
//#define ST_ASIO_MAX_MSG_NUM		16
//if there's a huge number of links, please reduce messge buffer via ST_ASIO_MAX_MSG_NUM macro.
//please think about if we have 512 links, how much memory we can accupy at most with default ST_ASIO_MAX_MSG_NUM?
//it's 2 * 1024 * 1024 * 512 = 1G

//use the following macro to control the type of packer and unpacker
#define PACKER_UNPACKER_TYPE	0
//0-default packer and unpacker, head(length) + body
//1-replaceable packer and unpacker, head(length) + body
//2-fixed length packer and unpacker
//3-prefix and/or suffix packer and unpacker

#if 1 == PACKER_UNPACKER_TYPE
#define ST_ASIO_DEFAULT_PACKER replaceable_packer<>
#define ST_ASIO_DEFAULT_UNPACKER replaceable_unpacker<>
#elif 2 == PACKER_UNPACKER_TYPE
#undef ST_ASIO_HEARTBEAT_INTERVAL
#define ST_ASIO_HEARTBEAT_INTERVAL	0 //not support heartbeat
#define ST_ASIO_DEFAULT_PACKER fixed_length_packer
#define ST_ASIO_DEFAULT_UNPACKER fixed_length_unpacker
#elif 3 == PACKER_UNPACKER_TYPE
#undef ST_ASIO_HEARTBEAT_INTERVAL
#define ST_ASIO_HEARTBEAT_INTERVAL	0 //not support heartbeat
#define ST_ASIO_DEFAULT_PACKER prefix_suffix_packer
#define ST_ASIO_DEFAULT_UNPACKER prefix_suffix_unpacker
#endif
//configuration

#include "../include/ext/tcp.h"
using namespace st_asio_wrapper;
using namespace st_asio_wrapper::tcp;
using namespace st_asio_wrapper::ext;
using namespace st_asio_wrapper::ext::tcp;

#define QUIT_COMMAND	"quit"
#define RESTART_COMMAND	"restart"
#define STATUS			"status"
#define STATISTIC		"statistic"
#define LIST_ALL_CLIENT	"list all client"
#define INCREASE_THREAD	"increase thread"
#define DECREASE_THREAD	"decrease thread"

//demonstrate how to use custom packer
//under the default behavior, each tcp::socket has their own packer, and cause memory waste
//at here, we make each echo_socket use the same global packer for memory saving
//notice: do not do this for unpacker, because unpacker has member variables and can't share each other
BOOST_AUTO(global_packer, boost::make_shared<ST_ASIO_DEFAULT_PACKER>());

//demonstrate how to control the type of tcp::server_socket_base::server from template parameter
class i_echo_server : public i_server
{
public:
	virtual void test() = 0;
};

typedef server_socket_base<ST_ASIO_DEFAULT_PACKER, ST_ASIO_DEFAULT_UNPACKER, i_echo_server> echo_socket_base;
class echo_socket : public echo_socket_base
{
public:
	echo_socket(i_echo_server& server_) : echo_socket_base(server_)
	{
		packer(global_packer);

#if 2 == PACKER_UNPACKER_TYPE
		boost::dynamic_pointer_cast<ST_ASIO_DEFAULT_UNPACKER>(unpacker())->fixed_length(1024);
#elif 3 == PACKER_UNPACKER_TYPE
		boost::dynamic_pointer_cast<ST_ASIO_DEFAULT_UNPACKER>(unpacker())->prefix_suffix("begin", "end");
#endif
	}

public:
	//because we use objects pool(REUSE_OBJECT been defined), so, strictly speaking, this virtual
	//function must be rewrote, but we don't have member variables to initialize but invoke father's
	//reset() directly, so, it can be omitted, but we keep it for possibly future using
	virtual void reset() {echo_socket_base::reset();}

protected:
	virtual void on_recv_error(const boost::system::error_code& ec)
	{
		//the type of tcp::server_socket_base::server now can be controlled by derived class(echo_socket),
		//which is actually i_echo_server, so, we can invoke i_echo_server::test virtual function.
		get_server().test();
		echo_socket_base::on_recv_error(ec);
	}

	//msg handling: send the original msg back(echo server)
/*
#ifdef ST_ASIO_SYNC_DISPATCH //do not open this feature
	//do not hold msg_can for further using, return from on_msg as quickly as possible
	virtual size_t on_msg(boost::container::list<out_msg_type>& msg_can)
	{
		if (!is_send_buffer_available())
			return 0; //congestion control
		//here if we cannot handle all messages in msg_can, do not use sync message dispatching except we can bear message disordering,
		//this is because on_msg_handle can be invoked concurrently with the next on_msg (new messages arrived) and then disorder messages.
		//and do not try to handle all messages here (just for echo_server's business logic) because:
		//1. we can not use safe_send_msg as i said many times, we should not block service threads.
		//2. if we use true can_overflow to call send_msg, then buffer usage will be out of control, we should not take this risk.

		st_asio_wrapper::do_something_to_all(msg_can, boost::bind((bool (echo_socket::*)(out_msg_ctype&, bool)) &echo_socket::send_msg, this, _1, true));
		BOOST_AUTO(re, msg_can.size());
		msg_can.clear();

		return re;
	}
#endif
*/
#ifdef ST_ASIO_DISPATCH_BATCH_MSG
	//do not hold msg_can for further using, access msg_can and return from on_msg_handle as quickly as possible
	virtual size_t on_msg_handle(out_queue_type& msg_can)
	{
		if (!is_send_buffer_available())
			return 0;

		out_container_type tmp_can;
		//this manner requires the container used by the message queue can be spliced (such as std::list, but not std::vector,
		// st_asio_wrapper doesn't require this characteristic).
		//these code can be compiled because we used list as the container of the message queue, see macro ST_ASIO_OUTPUT_CONTAINER for more details
		//to consume all messages in msg_can, see echo_client
		msg_can.lock();
		BOOST_AUTO(begin_iter, msg_can.begin());
		//don't be too greedy, here is in a service thread, we should not block this thread for a long time
		BOOST_AUTO(end_iter, msg_can.size() > 10 ? boost::next(begin_iter, 10) : msg_can.end());
		tmp_can.splice(tmp_can.end(), msg_can, begin_iter, end_iter); //the rest messages will be dispatched via the next on_msg_handle
		msg_can.unlock();

		st_asio_wrapper::do_something_to_all(tmp_can, boost::bind((bool (echo_socket::*)(out_msg_ctype&, bool)) &echo_socket::send_msg, this, _1, true));
		return tmp_can.size();
	}
#else
	virtual bool on_msg_handle(out_msg_type& msg) {return send_msg(msg);}
#endif
	//msg handling end
};

typedef server_base<echo_socket, object_pool<echo_socket>, i_echo_server> echo_server_base;
class echo_server : public echo_server_base
{
public:
	echo_server(service_pump& service_pump_) : echo_server_base(service_pump_) {}

	//from i_echo_server, pure virtual function, we must implement it.
	virtual void test() {/*puts("in echo_server::test()");*/}
};

#if ST_ASIO_HEARTBEAT_INTERVAL > 0
typedef server_socket_base<packer, unpacker> normal_socket;
#else
//demonstrate how to open heartbeat function without defining macro ST_ASIO_HEARTBEAT_INTERVAL
typedef server_socket_base<packer, unpacker> normal_socket_base;
class normal_socket : public normal_socket_base
{
public:
	normal_socket(i_server& server_) : normal_socket_base(server_) {}

protected:
	//demo client needs heartbeat (macro ST_ASIO_HEARTBEAT_INTERVAL been defined), pleae note that the interval (here is 5) must be equal to
	//macro ST_ASIO_HEARTBEAT_INTERVAL defined in demo client, and macro ST_ASIO_HEARTBEAT_MAX_ABSENCE must has the same value as demo client's.
	virtual void on_connect() {start_heartbeat(5);}
};
#endif

int main(int argc, const char* argv[])
{
	printf("usage: %s [<service thread number=1> [<port=%d> [ip=0.0.0.0]]]\n", argv[0], ST_ASIO_SERVER_PORT);
	puts("normal server's port will be 100 larger.");
	if (argc >= 2 && (0 == strcmp(argv[1], "--help") || 0 == strcmp(argv[1], "-h")))
		return 0;
	else
		puts("type " QUIT_COMMAND " to end.");

	service_pump sp;
	//only need a simple server? you can directly use server or tcp::server_base, because of normal_socket,
	//this server cannot support fixed_length_packer/fixed_length_unpacker and prefix_suffix_packer/prefix_suffix_unpacker,
	//the reason is these packer and unpacker need additional initializations that normal_socket not implemented,
	//see echo_socket's constructor for more details.
	server_base<normal_socket> server_(sp);
	echo_server echo_server_(sp); //echo server

	if (argc > 3)
	{
		server_.set_server_addr(atoi(argv[2]) + 100, argv[3]);
		echo_server_.set_server_addr(atoi(argv[2]), argv[3]);
	}
	else if (argc > 2)
	{
		server_.set_server_addr(atoi(argv[2]) + 100);
		echo_server_.set_server_addr(atoi(argv[2]));
	}
	else
		server_.set_server_addr(ST_ASIO_SERVER_PORT + 100);

	int thread_num = 1;
	if (argc > 1)
		thread_num = std::min(16, std::max(thread_num, atoi(argv[1])));

#if 3 == PACKER_UNPACKER_TYPE
	global_packer->prefix_suffix("begin", "end");
#endif

	sp.start_service(thread_num);
	while(sp.is_running())
	{
		std::string str;
		std::getline(std::cin, str);
		if (str.empty())
			;
		else if (QUIT_COMMAND == str)
			sp.stop_service();
		else if (RESTART_COMMAND == str)
		{
			sp.stop_service();
			sp.start_service(thread_num);
		}
		else if (STATISTIC == str)
		{
			printf("normal server, link #: " ST_ASIO_SF ", invalid links: " ST_ASIO_SF "\n", server_.size(), server_.invalid_object_size());
			printf("echo server, link #: " ST_ASIO_SF ", invalid links: " ST_ASIO_SF "\n", echo_server_.size(), echo_server_.invalid_object_size());
			puts("");
			puts(echo_server_.get_statistic().to_string().data());
		}
		else if (STATUS == str)
		{
			server_.list_all_status();
			echo_server_.list_all_status();
		}
		else if (LIST_ALL_CLIENT == str)
		{
			puts("clients from normal server:");
			server_.list_all_object();
			puts("clients from echo server:");
			echo_server_.list_all_object();
		}
		else if (INCREASE_THREAD == str)
			sp.add_service_thread(1);
		else if (DECREASE_THREAD == str)
			sp.del_service_thread(1);
		else
		{
//			/*
			//broadcast series functions call pack_msg for each client respectively, because clients may used different protocols(so different type of packers, of course)
			server_.broadcast_msg(str.data(), str.size() + 1, false);
			//send \0 character too, because demo client used basic_buffer as its msg type, it will not append \0 character automatically as std::string does,
			//so need \0 character when printing it.
//			*/
			/*
			//if all clients used the same protocol, we can pack msg one time, and send it repeatedly like this:
			packer p;
			packer::msg_type msg;
			//send \0 character too, because demo client used basic_buffer as its msg type, it will not append \0 character automatically as std::string does,
			//so need \0 character when printing it.
			if (p.pack_msg(msg, str.data(), str.size() + 1))
				server_.do_something_to_all(boost::bind((bool (normal_socket::*)(packer::msg_ctype&, bool)) &normal_socket::direct_send_msg, _1, boost::cref(msg), false));
			*/
			/*
			//if demo client is using stream_unpacker
			server_.do_something_to_all(boost::bind((bool (normal_socket::*)(packer::msg_ctype&, bool)) &normal_socket::direct_send_msg, _1, boost::cref(str), false));
			*/
		}
	}

	return 0;
}
