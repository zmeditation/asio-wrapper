
#ifndef FILE_CLIENT_H_
#define FILE_CLIENT_H_

#include <boost/atomic.hpp>

#include "../file_server/packer_unpacker.h"
#include "../include/st_asio_wrapper_tcp_client.h"
using namespace st_asio_wrapper;

extern boost::atomic_ushort completed_client_num;
extern int link_num;
extern __off64_t file_size;

class file_socket : public base_socket, public st_connector
{
public:
	file_socket(boost::asio::io_service& io_service_) : st_connector(io_service_), index(-1) {}
	virtual ~file_socket() {clear();}

	//reset all, be ensure that there's no any operations performed on this st_tcp_socket when invoke it
	virtual void reset() {clear(); st_connector::reset();}

	void set_index(int index_) {index = index_;}
	__off64_t get_rest_size() const
	{
		auto unpacker = boost::dynamic_pointer_cast<const data_unpacker>(inner_unpacker());
		return nullptr == unpacker ? 0 : unpacker->get_rest_size();
	}
	operator __off64_t() const {return get_rest_size();}

	bool get_file(const std::string& file_name)
	{
		if (TRANS_IDLE == state && !file_name.empty())
		{
			if (0 == id())
				file = fopen(file_name.data(), "w+b");
			else
				file = fopen(file_name.data(), "r+b");
			if (nullptr != file)
			{
				std::string order("\0", ORDER_LEN);
				order += file_name;

				state = TRANS_PREPARE;
				send_msg(order, true);

				return true;
			}
			else if (0 == index)
				printf("can't create file %s.\n", file_name.data());
		}

		return false;
	}

	void talk(const std::string& str)
	{
		if (TRANS_IDLE == state && !str.empty())
		{
			std::string order("\2", ORDER_LEN);
			order += str;
			send_msg(order, true);
		}
	}

protected:
	//msg handling
#ifndef FORCE_TO_USE_MSG_RECV_BUFFER
	//we can handle the msg very fast, so we don't use the recv buffer
	virtual bool on_msg(msg_type& msg) {handle_msg(msg); return true;}
#endif
	virtual bool on_msg_handle(msg_type& msg, bool link_down) {handle_msg(msg); return true;}
	//msg handling end

private:
	void clear()
	{
		state = TRANS_IDLE;
		if (nullptr != file)
		{
			fclose(file);
			file = nullptr;
		}

		inner_unpacker(boost::make_shared<DEFAULT_UNPACKER>());
	}
	void trans_end() {clear(); ++completed_client_num;}

	void handle_msg(msg_ctype& msg)
	{
		if (TRANS_BUSY == state)
		{
			assert(msg.empty());
			trans_end();
			return;
		}
		else if (msg.size() <= ORDER_LEN)
		{
			printf("wrong order length: " size_t_format ".\n", msg.size());
			return;
		}

		switch (*msg.data())
		{
		case 0:
			if (ORDER_LEN + DATA_LEN == msg.size() && nullptr != file && TRANS_PREPARE == state)
			{
				auto length = *(__off64_t*) std::next(msg.data(), ORDER_LEN);
				if (-1 == length)
				{
					if (0 == index)
						puts("get file failed!");
					trans_end();
				}
				else
				{
					if (0 == index)
						file_size = length;

					auto my_length = length / link_num;
					auto offset = my_length * index;

					if (link_num - 1 == index)
						my_length = length - offset;
					if (my_length > 0)
					{
						char buffer[ORDER_LEN + OFFSET_LEN + DATA_LEN];
						*buffer = 1; //head
						*(__off64_t*) std::next(buffer, ORDER_LEN) = offset;
						*(__off64_t*) std::next(buffer, ORDER_LEN + OFFSET_LEN) = my_length;

						state = TRANS_BUSY;
						send_msg(buffer, sizeof(buffer), true);

						fseeko64(file, offset, SEEK_SET);
						inner_unpacker(boost::make_shared<data_unpacker>(file, my_length));
					}
					else
						trans_end();
				}
			}
			break;
		case 2:
			if (0 == index)
				printf("server says: %s\n", std::next(msg.data(), ORDER_LEN));
			break;
		default:
			break;
		}
	}

private:
	int index;
};

class file_client : public st_tcp_client_base<file_socket>
{
public:
	file_client(st_service_pump& service_pump_) : st_tcp_client_base<file_socket>(service_pump_) {}

	__off64_t get_total_rest_size()
	{
		__off64_t total_rest_size = 0;
		do_something_to_all([&total_rest_size](object_ctype& item) {total_rest_size += *item;});
//		do_something_to_all([&total_rest_size](object_ctype& item) {total_rest_size += item->get_rest_size();});

		return total_rest_size;
	}
};

#endif //#ifndef FILE_CLIENT_H_
