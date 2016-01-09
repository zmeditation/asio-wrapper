/*
 * st_asio_wrapper_server.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * this class only used at server endpoint
 */

#ifndef ST_ASIO_WRAPPER_SERVER_H_
#define ST_ASIO_WRAPPER_SERVER_H_

#include "st_asio_wrapper_server_socket.h"
#include "st_asio_wrapper_object_pool.h"

#ifndef SERVER_PORT
#define SERVER_PORT					5051
#endif

#ifndef ASYNC_ACCEPT_NUM
#define ASYNC_ACCEPT_NUM			1 //how many async_accept delivery concurrently
#endif

//in set_server_addr, if the IP is empty, TCP_DEFAULT_IP_VERSION will define the IP version, or the IP version will be deduced by the IP address.
//boost::asio::ip::tcp::v4() means ipv4 and boost::asio::ip::tcp::v6() means ipv6.
#ifndef TCP_DEFAULT_IP_VERSION
#define TCP_DEFAULT_IP_VERSION boost::asio::ip::tcp::v4()
#endif

namespace st_asio_wrapper
{

template<typename Socket = st_server_socket, typename Pool = st_object_pool<Socket>, typename Server = i_server>
class st_server_base : public Server, public Pool
{
public:
	st_server_base(st_service_pump& service_pump_) : Pool(service_pump_), acceptor(service_pump_) {set_server_addr(SERVER_PORT);}
	template<typename Arg>
	st_server_base(st_service_pump& service_pump_, Arg arg) : Pool(service_pump_, arg), acceptor(service_pump_) {set_server_addr(SERVER_PORT);}

	bool set_server_addr(unsigned short port, const std::string& ip = std::string())
	{
		if (ip.empty())
			server_addr = boost::asio::ip::tcp::endpoint(TCP_DEFAULT_IP_VERSION, port);
		else
		{
			boost::system::error_code ec;
			BOOST_AUTO(addr, boost::asio::ip::address::from_string(ip, ec));
			if (ec)
				return false;

			server_addr = boost::asio::ip::tcp::endpoint(addr, port);
		}

		return true;
	}
	const boost::asio::ip::tcp::endpoint& get_server_addr() const {return server_addr;}

	void stop_listen() {boost::system::error_code ec; acceptor.cancel(ec); acceptor.close(ec);}
	bool is_listening() const {return acceptor.is_open();}

	//implement i_server's pure virtual functions
	virtual st_service_pump& get_service_pump() {return Pool::get_service_pump();}
	virtual const st_service_pump& get_service_pump() const {return Pool::get_service_pump();}
	virtual bool del_client(const boost::shared_ptr<st_timer>& client_ptr)
	{
		BOOST_AUTO(raw_client_ptr, boost::dynamic_pointer_cast<Socket>(client_ptr));
		return raw_client_ptr && ST_THIS del_object(raw_client_ptr) ? raw_client_ptr->force_close(), true : false;
	}

	//do not use graceful_close() as client does, because in this function, object_can_mutex has been locked, graceful_close will wait until on_recv_error() been invoked,
	//but in on_recv_error(), we need to lock object_can_mutex too(in del_object()), this will cause dead lock
	void close_all_client() {ST_THIS do_something_to_all(boost::bind(&Socket::force_close, _1));}

	///////////////////////////////////////////////////
	//msg sending interface
	TCP_BROADCAST_MSG(broadcast_msg, send_msg)
	TCP_BROADCAST_MSG(broadcast_native_msg, send_native_msg)
	//guarantee send msg successfully even if can_overflow equal to false, success at here just means putting the msg into send buffer successfully
	TCP_BROADCAST_MSG(safe_broadcast_msg, safe_send_msg)
	TCP_BROADCAST_MSG(safe_broadcast_native_msg, safe_send_native_msg)
	//msg sending interface
	///////////////////////////////////////////////////

	void disconnect(typename Pool::object_ctype& client_ptr) {ST_THIS del_object(client_ptr); client_ptr->disconnect();}
	void force_close(typename Pool::object_ctype& client_ptr) {ST_THIS del_object(client_ptr); client_ptr->force_close();}
	void graceful_close(typename Pool::object_ctype& client_ptr, bool sync = true) {ST_THIS del_object(client_ptr); client_ptr->graceful_close(sync);}

protected:
	virtual bool init()
	{
		boost::system::error_code ec;
		acceptor.open(server_addr.protocol(), ec); assert(!ec);
#ifndef NOT_REUSE_ADDRESS
		acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec); assert(!ec);
#endif
		acceptor.bind(server_addr, ec); assert(!ec);
		if (ec) {get_service_pump().stop(); unified_out::error_out("bind failed."); return false;}
		acceptor.listen(boost::asio::ip::tcp::acceptor::max_connections, ec); assert(!ec);
		if (ec) {get_service_pump().stop(); unified_out::error_out("listen failed."); return false;}

		ST_THIS start();

		for (int i = 0; i < ASYNC_ACCEPT_NUM; ++i)
			start_next_accept();

		return true;
	}
	virtual void uninit() {ST_THIS stop(); stop_listen(); close_all_client();}
	virtual bool on_accept(typename Pool::object_ctype& client_ptr) {return true;}

	virtual void start_next_accept()
	{
		typename Pool::object_type client_ptr = ST_THIS create_object(boost::ref(*this));
		acceptor.async_accept(client_ptr->lowest_layer(), boost::bind(&st_server_base::accept_handler, this, boost::asio::placeholders::error, client_ptr));
	}

protected:
	bool add_client(typename Pool::object_ctype& client_ptr)
	{
		if (ST_THIS add_object(client_ptr))
		{
			client_ptr->show_info("client:", "arrive.");
			return true;
		}

		return false;
	}

	void accept_handler(const boost::system::error_code& ec, typename Pool::object_ctype& client_ptr)
	{
		if (!ec)
		{
			if (on_accept(client_ptr))
			{
				if (add_client(client_ptr))
					client_ptr->start();
				else
				{
					client_ptr->show_info("client:", "been refused cause of too many clients.");
					client_ptr->force_close();
				}
			}

			start_next_accept();
		}
		else
			stop_listen();
	}

protected:
	boost::asio::ip::tcp::endpoint server_addr;
	boost::asio::ip::tcp::acceptor acceptor;
};
typedef st_server_base<> st_server;

} //namespace

#endif /* ST_ASIO_WRAPPER_SERVER_H_ */
