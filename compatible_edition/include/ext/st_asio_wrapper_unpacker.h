/*
 * st_asio_wrapper_unpacker.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * unpacker base class
 */

#ifndef ST_ASIO_WRAPPER_UNPACKER_H_
#define ST_ASIO_WRAPPER_UNPACKER_H_

#include "st_asio_wrapper_ext.h"

#ifdef ST_ASIO_HUGE_MSG
#define ST_ASIO_HEAD_TYPE	boost::uint32_t
#define ST_ASIO_HEAD_N2H	ntohl
#else
#define ST_ASIO_HEAD_TYPE	boost::uint16_t
#define ST_ASIO_HEAD_N2H	ntohs
#endif
#define ST_ASIO_HEAD_LEN	(sizeof(ST_ASIO_HEAD_TYPE))

namespace st_asio_wrapper { namespace ext {

//protocol: length + body
class unpacker : public i_unpacker<std::string>
{
public:
	unpacker() {reset_state();}
	size_t current_msg_length() const {return cur_msg_len;} //current msg's total length, -1 means not available

	bool parse_msg(size_t bytes_transferred, boost::container::list<std::pair<const char*, size_t> >& msg_can)
	{
		//length + msg
		remain_len += bytes_transferred;
		assert(remain_len <= ST_ASIO_MSG_BUFFER_SIZE);

		const char* pnext = raw_buff.begin();
		bool unpack_ok = true;
		while (unpack_ok) //considering stick package problem, we need a loop
			if ((size_t) -1 != cur_msg_len)
			{
				if (cur_msg_len > ST_ASIO_MSG_BUFFER_SIZE || cur_msg_len <= ST_ASIO_HEAD_LEN)
					unpack_ok = false;
				else if (remain_len >= cur_msg_len) //one msg received
				{
					msg_can.push_back(std::make_pair(boost::next(pnext, ST_ASIO_HEAD_LEN), cur_msg_len - ST_ASIO_HEAD_LEN));
					remain_len -= cur_msg_len;
					std::advance(pnext, cur_msg_len);
					cur_msg_len = -1;
				}
				else
					break;
			}
			else if (remain_len >= ST_ASIO_HEAD_LEN) //the msg's head been received, stick package found
			{
				ST_ASIO_HEAD_TYPE head;
				memcpy(&head, pnext, ST_ASIO_HEAD_LEN);
				cur_msg_len = ST_ASIO_HEAD_N2H(head);
			}
			else
				break;

		if (pnext == raw_buff.begin()) //we should have at least got one msg.
			unpack_ok = false;

		return unpack_ok;
	}

public:
	virtual void reset_state() {cur_msg_len = -1; remain_len = 0;}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		boost::container::list<std::pair<const char*, size_t> > msg_pos_can;
		bool unpack_ok = parse_msg(bytes_transferred, msg_pos_can);
		for (BOOST_AUTO(iter, msg_pos_can.begin()); iter != msg_pos_can.end(); ++iter)
		{
			msg_can.resize(msg_can.size() + 1);
			msg_can.back().assign(iter->first, iter->second);
		}

		if (unpack_ok && remain_len > 0)
		{
			const char* pnext = boost::next(msg_pos_can.back().first, msg_pos_can.back().second);
			memcpy(raw_buff.begin(), pnext, remain_len); //left behind unparsed data
		}

		//if unpacking failed, successfully parsed msgs will still returned via msg_can(stick package), please note.
		return unpack_ok;
	}

	//a return value of 0 indicates that the read operation is complete. a non-zero value indicates the maximum number
	//of bytes to be read on the next call to the stream's async_read_some function. ---boost::asio::async_read
	//read as many as possible to reduce asynchronous call-back, and don't forget to handle stick package carefully in parse_msg function.
	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (ec)
			return 0;

		size_t data_len = remain_len + bytes_transferred;
		assert(data_len <= ST_ASIO_MSG_BUFFER_SIZE);

		if ((size_t) -1 == cur_msg_len && data_len >= ST_ASIO_HEAD_LEN) //the msg's head been received
		{
			ST_ASIO_HEAD_TYPE head;
			memcpy(&head, raw_buff.begin(), ST_ASIO_HEAD_LEN);
			cur_msg_len = ST_ASIO_HEAD_N2H(head);
			if (cur_msg_len > ST_ASIO_MSG_BUFFER_SIZE || cur_msg_len <= ST_ASIO_HEAD_LEN) //invalid msg, stop reading
				return 0;
		}

		return data_len >= cur_msg_len ? 0 : boost::asio::detail::default_max_transfer_size;
		//read as many as possible except that we have already got an entire msg
	}

	virtual boost::asio::mutable_buffers_1 prepare_next_recv()
	{
		assert(remain_len < ST_ASIO_MSG_BUFFER_SIZE);
		return boost::asio::buffer(boost::asio::buffer(raw_buff) + remain_len);
	}

protected:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
	size_t cur_msg_len; //-1 means head not received, so msg length is not available.
	size_t remain_len; //half-baked msg
};

//protocol: UDP has message boundary, so we don't need a specific protocol to unpack it.
class udp_unpacker : public i_udp_unpacker<std::string>
{
public:
	virtual void parse_msg(msg_type& msg, size_t bytes_transferred) {assert(bytes_transferred <= ST_ASIO_MSG_BUFFER_SIZE); msg.assign(raw_buff.data(), bytes_transferred);}
	virtual boost::asio::mutable_buffers_1 prepare_next_recv() {return boost::asio::buffer(raw_buff);}

protected:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
};

//protocol: length + body
class replaceable_unpacker : public i_unpacker<replaceable_buffer>
{
public:
	virtual void reset_state() {unpacker_.reset_state();}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		unpacker::container_type tmp_can;
		bool unpack_ok = unpacker_.parse_msg(bytes_transferred, tmp_can);
		for (BOOST_AUTO(iter, tmp_can.begin()); iter != tmp_can.end(); ++iter)
		{
			BOOST_AUTO(raw_buffer, new string_buffer());
			raw_buffer->swap(*iter);
			msg_can.resize(msg_can.size() + 1);
			msg_can.back().raw_buffer(raw_buffer);
		}

		//if unpacking failed, successfully parsed msgs will still returned via msg_can(stick package), please note.
		return unpack_ok;
	}

	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred) {return unpacker_.completion_condition(ec, bytes_transferred);}
	virtual boost::asio::mutable_buffers_1 prepare_next_recv() {return unpacker_.prepare_next_recv();}

protected:
	unpacker unpacker_;
};

//protocol: UDP has message boundary, so we don't need a specific protocol to unpack it.
class replaceable_udp_unpacker : public i_udp_unpacker<replaceable_buffer>
{
public:
	virtual void parse_msg(msg_type& msg, size_t bytes_transferred)
	{
		assert(bytes_transferred <= ST_ASIO_MSG_BUFFER_SIZE);

		BOOST_AUTO(raw_msg, new string_buffer());
		raw_msg->assign(raw_buff.data(), bytes_transferred);
		msg.raw_buffer(raw_msg);
	}
	virtual boost::asio::mutable_buffers_1 prepare_next_recv() {return boost::asio::buffer(raw_buff);}

protected:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
};

//protocol: length + body
//this unpacker demonstrate how to forbid memory copying while parsing msgs (let asio write msg directly).
class non_copy_unpacker : public i_unpacker<basic_buffer>
{
public:
	non_copy_unpacker() {reset_state();}
	size_t current_msg_length() const {return raw_buff.size();} //current msg's total length(not include the head), 0 means not available

public:
	virtual void reset_state() {raw_buff.clear(); step = 0;}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		if (0 == step) //the head been received
		{
			assert(raw_buff.empty() && ST_ASIO_HEAD_LEN == bytes_transferred);

			size_t cur_msg_len = ST_ASIO_HEAD_N2H(head) - ST_ASIO_HEAD_LEN;
			if (cur_msg_len > ST_ASIO_MSG_BUFFER_SIZE - ST_ASIO_HEAD_LEN) //invalid msg, stop reading
				return false;

			raw_buff.attach(new char[cur_msg_len], cur_msg_len); assert(!raw_buff.empty());
			step = 1;
		}
		else if (1 == step) //the body been received
		{
			assert(!raw_buff.empty());
			if (bytes_transferred != raw_buff.size())
				return false;

			msg_can.resize(msg_can.size() + 1);
			msg_can.back().swap(raw_buff);
			step = 0;
		}

		return true;
	}

	//a return value of 0 indicates that the read operation is complete. a non-zero value indicates the maximum number
	//of bytes to be read on the next call to the stream's async_read_some function. ---boost::asio::async_read
	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (ec)
			return 0;

		if (0 == step) //want the head
		{
			assert(raw_buff.empty());

			if (bytes_transferred < ST_ASIO_HEAD_LEN)
				return boost::asio::detail::default_max_transfer_size;

			assert(ST_ASIO_HEAD_LEN == bytes_transferred);
			size_t cur_msg_len = ST_ASIO_HEAD_N2H(head) - ST_ASIO_HEAD_LEN;
			if (cur_msg_len > ST_ASIO_MSG_BUFFER_SIZE - ST_ASIO_HEAD_LEN) //invalid msg, stop reading
				step = -1;
			else
				raw_buff.assign(cur_msg_len);
		}
		else if (1 == step) //want the body
		{
			assert(!raw_buff.empty());
			return boost::asio::detail::default_max_transfer_size;
		}
		else
			assert(false);

		return 0;
	}

	virtual boost::asio::mutable_buffers_1 prepare_next_recv() {return raw_buff.empty() ? boost::asio::buffer((char*) &head, ST_ASIO_HEAD_LEN) : boost::asio::buffer(raw_buff.data(), raw_buff.size());}

private:
	ST_ASIO_HEAD_TYPE head;
	//please note that we don't have a fixed size array with maximum size any more(like the default unpacker).
	//this is very useful if you have very few but very large msgs, fox example:
	//you have a very large msg(1M size), but all others are very small, if you use a fixed size array to hold msgs in the unpackers,
	//all the unpackers must have an array with at least 1M size, each st_socket will have a unpacker, this will cause your application occupy very large memory but with very low utilization ratio.
	//this unbuffered_unpacker will resolve above problem, and with another benefit: no memory replication needed any more.
	msg_type raw_buff;
	int step; //-1-error format, 0-want the head, 1-want the body
};

//protocol: fixed lenght
class fixed_length_unpacker : public i_unpacker<std::string>
{
public:
	fixed_length_unpacker() {reset_state();}

	void fixed_length(size_t fixed_length) {assert(0 < fixed_length && fixed_length <= ST_ASIO_MSG_BUFFER_SIZE); _fixed_length = fixed_length;}
	size_t fixed_length() const {return _fixed_length;}

public:
	virtual void reset_state() {remain_len = 0;}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		//length + msg
		remain_len += bytes_transferred;
		assert(remain_len <= ST_ASIO_MSG_BUFFER_SIZE);

		const char* pnext = raw_buff.begin();
		while (remain_len >= _fixed_length) //considering stick package problem, we need a loop
		{
			msg_can.resize(msg_can.size() + 1);
			msg_can.back().assign(pnext, _fixed_length);
			remain_len -= _fixed_length;
			std::advance(pnext, _fixed_length);
		}

		if (pnext == raw_buff.begin()) //we should have at least got one msg.
			return false;
		else if (remain_len > 0) //left behind unparsed msg
			memcpy(raw_buff.begin(), pnext, remain_len);

		return true;
	}

	//a return value of 0 indicates that the read operation is complete. a non-zero value indicates the maximum number
	//of bytes to be read on the next call to the stream's async_read_some function. ---boost::asio::async_read
	//read as many as possible to reduce asynchronous call-back(st_tcp_socket_base::recv_handler), and don't forget to handle
	//stick package carefully in parse_msg function.
	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (ec)
			return 0;

		size_t data_len = remain_len + bytes_transferred;
		assert(data_len <= ST_ASIO_MSG_BUFFER_SIZE);

		return data_len >= _fixed_length ? 0 : boost::asio::detail::default_max_transfer_size;
		//read as many as possible except that we have already got an entire msg
	}

	virtual boost::asio::mutable_buffers_1 prepare_next_recv()
	{
		assert(remain_len < ST_ASIO_MSG_BUFFER_SIZE);
		return boost::asio::buffer(boost::asio::buffer(raw_buff) + remain_len);
	}

private:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
	size_t _fixed_length;
	size_t remain_len; //half-baked msg
};

//protocol: [prefix] + body + suffix
class prefix_suffix_unpacker : public i_unpacker<std::string>
{
public:
	prefix_suffix_unpacker() {reset_state();}

	void prefix_suffix(const std::string& prefix, const std::string& suffix) {assert(!suffix.empty() && prefix.size() + suffix.size() < ST_ASIO_MSG_BUFFER_SIZE); _prefix = prefix; _suffix = suffix;}
	const std::string& prefix() const {return _prefix;}
	const std::string& suffix() const {return _suffix;}

	size_t peek_msg(size_t data_len, const char* buff)
	{
		assert(NULL != buff);

		if ((size_t) -1 == first_msg_len)
		{
			if (data_len >= _prefix.size())
			{
				if (0 != memcmp(_prefix.data(), buff, _prefix.size()))
					return 0; //invalid msg, stop reading
				else
					first_msg_len = 0; //prefix been checked.
			}
		}
		else if (0 != first_msg_len)
			return 0;

		size_t min_len = _prefix.size() + _suffix.size();
		if (data_len > min_len)
		{
			const char* end = (const char*) memmem(boost::next(buff, _prefix.size()), data_len - _prefix.size(), _suffix.data(), _suffix.size());
			if (NULL != end)
			{
				first_msg_len = end - buff + _suffix.size(); //got a msg
				return 0;
			}
			else if (data_len >= ST_ASIO_MSG_BUFFER_SIZE)
				return 0; //invalid msg, stop reading
		}

		return boost::asio::detail::default_max_transfer_size; //read as many as possible
	}

	//like strstr, except support \0 in the middle of mem and sub_mem
	static const void* memmem(const void* mem, size_t len, const void* sub_mem, size_t sub_len)
	{
		if (NULL != mem && NULL != sub_mem && sub_len <= len)
		{
			size_t valid_len = len - sub_len;
			for (size_t i = 0; i <= valid_len; ++i, mem = (const char*) mem + 1)
				if (0 == memcmp(mem, sub_mem, sub_len))
					return mem;
		}

		return NULL;
	}

public:
	virtual void reset_state() {first_msg_len = -1; remain_len = 0;}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		//length + msg
		remain_len += bytes_transferred;
		assert(remain_len <= ST_ASIO_MSG_BUFFER_SIZE);

		const char* pnext = raw_buff.begin();
		size_t min_len = _prefix.size() + _suffix.size();
		while (0 == peek_msg(remain_len, pnext) && (size_t) -1 != first_msg_len && 0 != first_msg_len)
		{
			assert(first_msg_len > min_len);
			size_t msg_len = first_msg_len - min_len;

			msg_can.resize(msg_can.size() + 1);
			msg_can.back().assign(boost::next(pnext, _prefix.size()), msg_len);
			remain_len -= first_msg_len;
			std::advance(pnext, first_msg_len);
			first_msg_len = -1;
		}

		if (pnext == raw_buff.begin()) //we should have at least got one msg.
			return false;
		else if (remain_len > 0)
			memcpy(raw_buff.begin(), pnext, remain_len); //left behind unparsed msg

		return true;
	}

	//a return value of 0 indicates that the read operation is complete. a non-zero value indicates the maximum number
	//of bytes to be read on the next call to the stream's async_read_some function. ---boost::asio::async_read
	//read as many as possible to reduce asynchronous call-back(st_tcp_socket_base::recv_handler), and don't forget to handle
	//stick package carefully in parse_msg function.
	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred)
	{
		if (ec)
			return 0;

		size_t data_len = remain_len + bytes_transferred;
		assert(data_len <= ST_ASIO_MSG_BUFFER_SIZE);

		return peek_msg(data_len, raw_buff.begin());
	}

	virtual boost::asio::mutable_buffers_1 prepare_next_recv()
	{
		assert(remain_len < ST_ASIO_MSG_BUFFER_SIZE);
		return boost::asio::buffer(boost::asio::buffer(raw_buff) + remain_len);
	}

private:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
	std::string _prefix, _suffix;
	size_t first_msg_len;
	size_t remain_len; //half-baked msg
};

//protocol: stream (non-protocol)
class stream_unpacker : public i_unpacker<std::string>
{
public:
	virtual void reset_state() {}
	virtual bool parse_msg(size_t bytes_transferred, container_type& msg_can)
	{
		if (0 == bytes_transferred)
			return false;

		assert(bytes_transferred <= ST_ASIO_MSG_BUFFER_SIZE);

		msg_can.resize(msg_can.size() + 1);
		msg_can.back().assign(raw_buff.data(), bytes_transferred);
		return true;
	}

	virtual size_t completion_condition(const boost::system::error_code& ec, size_t bytes_transferred) {return ec || bytes_transferred > 0 ? 0 : boost::asio::detail::default_max_transfer_size;}
	virtual boost::asio::mutable_buffers_1 prepare_next_recv() {return boost::asio::buffer(raw_buff);}

protected:
	boost::array<char, ST_ASIO_MSG_BUFFER_SIZE> raw_buff;
};

}} //namespace

#endif /* ST_ASIO_WRAPPER_UNPACKER_H_ */
