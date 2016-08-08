/*
 * st_asio_wrapper_socket.h
 *
 *  Created on: 2013-8-4
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * this class used at both client and server endpoint, and in both TCP and UDP socket
 */

#ifndef ST_ASIO_WRAPPER_SOCKET_H_
#define ST_ASIO_WRAPPER_SOCKET_H_

#include <sstream>

#include <boost/date_time.hpp>
#include <boost/container/list.hpp>

#include "st_asio_wrapper_base.h"
#include "st_asio_wrapper_timer.h"

namespace st_asio_wrapper
{

template<typename Socket, typename Packer, typename Unpacker, typename InMsgType, typename OutMsgType>
class st_socket: public st_timer
{
public:
	struct statistic
	{
#ifdef ST_ASIO_FULL_STATISTIC
		static bool enabled() {return true;}
		typedef boost::posix_time::ptime stat_time;
		static stat_time local_time() {return boost::date_time::microsec_clock<boost::posix_time::ptime>::local_time();}
		typedef boost::posix_time::time_duration stat_duration;
#else
		struct dummy_duration {const dummy_duration& operator +=(const dummy_duration& other) {return *this;}}; //not a real duration, just satisfy compiler(d1 += d2)
		struct dummy_time {dummy_duration operator -(const dummy_time& other) {return dummy_duration();}}; //not a real time, just satisfy compiler(t1 - t2)

		static bool enabled() {return false;}
		typedef dummy_time stat_time;
		static stat_time local_time() {return stat_time();}
		typedef dummy_duration stat_duration;
#endif
		statistic() : send_msg_sum(0), send_byte_sum(0), recv_msg_sum(0), recv_byte_sum(0) {}
		void reset()
		{
			send_msg_sum = send_byte_sum = 0;
			send_delay_sum = send_time_sum = stat_duration();

			recv_msg_sum = recv_byte_sum = 0;
			dispatch_dealy_sum = recv_idle_sum = stat_duration();
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
			handle_time_1_sum = stat_duration();
#endif
			handle_time_2_sum = stat_duration();
		}

		statistic& operator +=(const struct statistic& other)
		{
			send_msg_sum += other.send_msg_sum;
			send_byte_sum += other.send_byte_sum;
			send_delay_sum += other.send_delay_sum;
			send_time_sum += other.send_time_sum;

			recv_msg_sum += other.recv_msg_sum;
			recv_byte_sum += other.recv_byte_sum;
			dispatch_dealy_sum += other.dispatch_dealy_sum;
			recv_idle_sum += other.recv_idle_sum;
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
			handle_time_1_sum += other.handle_time_1_sum;
#endif
			handle_time_2_sum += other.handle_time_2_sum;

			return *this;
		}

		std::string to_string() const
		{
			std::ostringstream s;
#ifdef ST_ASIO_FULL_STATISTIC
			BOOST_AUTO(tw, boost::posix_time::time_duration::num_fractional_digits());
			s << std::setfill('0') << "send corresponding statistic:\n"
				<< "message sum: " << send_msg_sum << std::endl
				<< "size in bytes: " << send_byte_sum << std::endl
				<< "send delay: " << send_delay_sum.total_seconds() << "." << std::setw(tw) << send_delay_sum.fractional_seconds() << std::setw(0) << std::endl
				<< "send duration: " << send_time_sum.total_seconds() << "." << std::setw(tw) << send_time_sum.fractional_seconds() << std::setw(0) << std::endl
				<< "\nrecv corresponding statistic:\n"
				<< "message sum: " << recv_msg_sum << std::endl
				<< "size in bytes: " << recv_byte_sum << std::endl
				<< "dispatch delay: " << dispatch_dealy_sum.total_seconds() << "." << std::setw(tw) << dispatch_dealy_sum.fractional_seconds() << std::setw(0) << std::endl
				<< "recv idle duration: " << recv_idle_sum.total_seconds() << "." << std::setw(tw) << recv_idle_sum.fractional_seconds() << std::setw(0) << std::endl
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
				<< "on_msg duration: " << handle_time_1_sum.total_seconds() << "." << std::setw(tw) << handle_time_1_sum.fractional_seconds() << std::setw(0) << std::endl
#endif
				<< "on_msg_handle duration: " << handle_time_2_sum.total_seconds() << "." << std::setw(tw) << handle_time_2_sum.fractional_seconds();
#else
			s << std::setfill('0') << "send corresponding statistic:\n"
				<< "message sum: " << send_msg_sum << std::endl
				<< "size in bytes: " << send_byte_sum << std::endl
				<< "\nrecv corresponding statistic:\n"
				<< "message sum: " << recv_msg_sum << std::endl
				<< "size in bytes: " << recv_byte_sum;
#endif
			return s.str();
		}

		//send corresponding statistic
		boost::uint_fast64_t send_msg_sum; //not counted msgs in sending buffer
		boost::uint_fast64_t send_byte_sum; //not counted msgs in sending buffer
		stat_duration send_delay_sum; //from send_(native_)msg, post_(native_)msg(exclude msg packing) to asio::async_write
		stat_duration send_time_sum; //from asio::async_write to send_handler, this indicate your network's speed or load

		//recv corresponding statistic
		boost::uint_fast64_t recv_msg_sum; //include msgs in receiving buffer
		boost::uint_fast64_t recv_byte_sum; //include msgs in receiving buffer
		stat_duration dispatch_dealy_sum; //from parse_msg(exclude msg unpacking) to on_msg_handle
		stat_duration recv_idle_sum;
		//during this duration, st_socket suspended msg reception because of full receiving buffer, posting msgs or invoke on_msg
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
		stat_duration handle_time_1_sum; //on_msg consumed time, this indicate the efficiency of msg handling
#endif
		stat_duration handle_time_2_sum; //on_msg_handle consumed time, this indicate the efficiency of msg handling
	};

protected:
	struct in_msg : public InMsgType
	{
		in_msg() {restart();}
		void restart() {restart(statistic::local_time());}
		void restart(const typename statistic::stat_time& begin_time_) {begin_time = begin_time_;}

		typename statistic::stat_time begin_time;
	};

	struct out_msg : public OutMsgType
	{
		out_msg() {restart();}
		void restart() {restart(statistic::local_time());}
		void restart(const typename statistic::stat_time& begin_time_) {begin_time = begin_time_;}

		typename statistic::stat_time begin_time;
	};

	//keep size() constant time would better, because we invoke it frequently, so don't use std::list(gcc)
	typedef boost::container::list<in_msg> in_container_type;
	typedef boost::container::list<out_msg> out_container_type;

	static const unsigned char TIMER_BEGIN = st_timer::TIMER_END;
	static const unsigned char TIMER_DISPATCH_MSG = TIMER_BEGIN;
	static const unsigned char TIMER_SUSPEND_DISPATCH_MSG = TIMER_BEGIN + 1;
	static const unsigned char TIMER_HANDLE_POST_BUFFER = TIMER_BEGIN + 2;
	static const unsigned char TIMER_RE_DISPATCH_MSG = TIMER_BEGIN + 3;
	static const unsigned char TIMER_END = TIMER_BEGIN + 10;

	st_socket(boost::asio::io_service& io_service_) : st_timer(io_service_), _id(-1), next_layer_(io_service_), packer_(boost::make_shared<Packer>()), started_(false) {reset_state();}
	template<typename Arg>
	st_socket(boost::asio::io_service& io_service_, Arg& arg) : st_timer(io_service_), _id(-1), next_layer_(io_service_, arg), packer_(boost::make_shared<Packer>()), started_(false) {reset_state();}

	void reset()
	{
		close();
		reset_state();
		clear_buffer();
		stat.reset();

		st_timer::reset();
	}

	void reset_state()
	{
		packer_->reset_state();

		posting = false;
		sending = suspend_send_msg_ = false;
		dispatching = suspend_dispatch_msg_ = false;
//		started_ = false;
	}

	void clear_buffer()
	{
		post_msg_buffer.clear();
		send_msg_buffer.clear();
		recv_msg_buffer.clear();
		temp_msg_buffer.clear();

		last_send_msg.clear();
		last_dispatch_msg.clear();
	}

public:
	//please do not change id at runtime via the following function, except this st_socket is not managed by st_object_pool,
	//it should only be used by st_object_pool when this st_socket being reused or creating new st_socket.
	void id(boost::uint_fast64_t id) {assert(!started_); if (started_) unified_out::error_out("id is unchangeable!"); else _id = id;}
	boost::uint_fast64_t id() const {return _id;}

	Socket& next_layer() {return next_layer_;}
	const Socket& next_layer() const {return next_layer_;}
	typename Socket::lowest_layer_type& lowest_layer() {return next_layer().lowest_layer();}
	const typename Socket::lowest_layer_type& lowest_layer() const {return next_layer().lowest_layer();}

	virtual bool obsoleted()
	{
		if (started() || ST_THIS is_async_calling())
			return false;

		boost::unique_lock<boost::shared_mutex> lock(recv_msg_buffer_mutex, boost::try_to_lock);
		return lock.owns_lock() && recv_msg_buffer.empty(); //if successfully locked, means this st_socket is idle
	}

	bool started() const {return started_;}
	void start()
	{
		boost::unique_lock<boost::shared_mutex> lock(start_mutex);
		if (!started_)
			started_ = do_start();
	}

	//return false not means failure, but means already closed.
	bool close()
	{
		if (!lowest_layer().is_open())
			return false;

		boost::system::error_code ec;
		lowest_layer().close(ec);
		return true;
	}

	bool send_msg() //return false if send buffer is empty or sending not allowed or io_service stopped
	{
		boost::unique_lock<boost::shared_mutex> lock(send_msg_buffer_mutex);
		return do_send_msg();
	}

	void suspend_send_msg(bool suspend) {if (!(suspend_send_msg_ = suspend)) send_msg();}
	bool suspend_send_msg() const {return suspend_send_msg_;}

	void suspend_dispatch_msg(bool suspend)
	{
		suspend_dispatch_msg_ = suspend;
		stop_timer(TIMER_SUSPEND_DISPATCH_MSG);
		do_dispatch_msg(true);
	}
	bool suspend_dispatch_msg() const {return suspend_dispatch_msg_;}

	const struct statistic& get_statistic() const {return stat;}

	//get or change the packer at runtime
	//changing packer at runtime is not thread-safe, please pay special attention
	//we can resolve this defect via mutex, but i think it's not worth, because this feature is not frequently used
	boost::shared_ptr<i_packer<typename Packer::msg_type> > inner_packer() {return packer_;}
	boost::shared_ptr<const i_packer<typename Packer::msg_type> > inner_packer() const {return packer_;}
	void inner_packer(const boost::shared_ptr<i_packer<typename Packer::msg_type> >& _packer_) {packer_ = _packer_;}

	//if you use can_overflow = true to invoke send_msg or send_native_msg, it will always succeed no matter whether the send buffer is available or not,
	//this can exhaust all virtual memory, please pay special attentions.
	bool is_send_buffer_available()
	{
		boost::shared_lock<boost::shared_mutex> lock(send_msg_buffer_mutex);
		return send_msg_buffer.size() < ST_ASIO_MAX_MSG_NUM;
	}

	//don't use the packer but insert into send buffer directly
	bool direct_send_msg(const InMsgType& msg, bool can_overflow = false) {InMsgType tmp_msg(msg); return direct_send_msg(tmp_msg, can_overflow);}
	//after this call, msg becomes empty, please note.
	bool direct_send_msg(InMsgType& msg, bool can_overflow = false)
	{
		boost::unique_lock<boost::shared_mutex> lock(send_msg_buffer_mutex);
		return can_overflow || send_msg_buffer.size() < ST_ASIO_MAX_MSG_NUM ? do_direct_send_msg(msg) : false;
	}

	bool direct_post_msg(const InMsgType& msg, bool can_overflow = false) {InMsgType tmp_msg(msg); return direct_post_msg(tmp_msg, can_overflow);}
	//after this call, msg becomes empty, please note.
	bool direct_post_msg(InMsgType& msg, bool can_overflow = false)
	{
		if (direct_send_msg(msg, can_overflow))
			return true;

		boost::unique_lock<boost::shared_mutex> lock(post_msg_buffer_mutex);
		return do_direct_post_msg(msg);
	}

	//how many msgs waiting for sending or dispatching
	GET_PENDING_MSG_NUM(get_pending_post_msg_num, post_msg_buffer, post_msg_buffer_mutex)
	GET_PENDING_MSG_NUM(get_pending_send_msg_num, send_msg_buffer, send_msg_buffer_mutex)
	GET_PENDING_MSG_NUM(get_pending_recv_msg_num, recv_msg_buffer, recv_msg_buffer_mutex)

	PEEK_FIRST_PENDING_MSG(peek_first_pending_post_msg, post_msg_buffer, post_msg_buffer_mutex, InMsgType)
	PEEK_FIRST_PENDING_MSG(peek_first_pending_send_msg, send_msg_buffer, send_msg_buffer_mutex, InMsgType)
	PEEK_FIRST_PENDING_MSG(peek_first_pending_recv_msg, recv_msg_buffer, recv_msg_buffer_mutex, OutMsgType)

	POP_FIRST_PENDING_MSG(pop_first_pending_post_msg, post_msg_buffer, post_msg_buffer_mutex, InMsgType)
	POP_FIRST_PENDING_MSG(pop_first_pending_send_msg, send_msg_buffer, send_msg_buffer_mutex, InMsgType)
	POP_FIRST_PENDING_MSG(pop_first_pending_recv_msg, recv_msg_buffer, recv_msg_buffer_mutex, OutMsgType)

	//clear all pending msgs
	POP_ALL_PENDING_MSG(pop_all_pending_post_msg, post_msg_buffer, post_msg_buffer_mutex, in_container_type)
	POP_ALL_PENDING_MSG(pop_all_pending_send_msg, send_msg_buffer, send_msg_buffer_mutex, in_container_type)
	POP_ALL_PENDING_MSG(pop_all_pending_recv_msg, recv_msg_buffer, recv_msg_buffer_mutex, out_container_type)

protected:
	virtual bool do_start() = 0;
	virtual bool do_send_msg() = 0; //must mutex send_msg_buffer before invoke this function
	virtual void do_recv_msg() = 0;

	virtual bool is_send_allowed() const {return !suspend_send_msg_;} //can send msg or not(just put into send buffer)

	//generally, you don't have to rewrite this to maintain the status of connections(TCP)
	virtual void on_send_error(const boost::system::error_code& ec) {unified_out::error_out("send msg error (%d %s)", ec.value(), ec.message().data());}
	//receiving error or peer endpoint quit(false ec means ok)
	virtual void on_recv_error(const boost::system::error_code& ec) = 0;

#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
	//if you want to use your own receive buffer, you can move the msg to your own receive buffer, then handle them as your own strategy(may be you'll need a msg dispatch thread),
	//or you can handle the msg at here, but this will reduce efficiency because this msg handling will block the next msg receiving on the same st_socket,
	//but if you can handle the msg very fast, you are recommended to handle them at here, which will inversely more efficient,
	//because msg receive buffer and msg dispatching are not needed any more.
	//
	//return true means msg been handled, st_socket will not maintain it anymore, return false means msg cannot be handled right now, you must handle it in on_msg_handle()
	//notice: on_msg_handle() will not be invoked from within this function
	//
	//notice: the msg is unpacked, using inconstant is for the convenience of swapping
	virtual bool on_msg(OutMsgType& msg) = 0;
#endif

	//handling msg in om_msg_handle() will not block msg receiving on the same st_socket
	//return true means msg been handled, false means msg cannot be handled right now, and st_socket will re-dispatch it asynchronously
	//if link_down is true, no matter return true or false, st_socket will not maintain this msg anymore, and continue dispatch the next msg continuously
	//
	//notice: the msg is unpacked, using inconstant is for the convenience of swapping
	virtual bool on_msg_handle(OutMsgType& msg, bool link_down) = 0;

#ifdef ST_ASIO_WANT_MSG_SEND_NOTIFY
	//one msg has sent to the kernel buffer, msg is the right msg
	//notice: the msg is packed, using inconstant is for the convenience of swapping
	virtual void on_msg_send(InMsgType& msg) {}
#endif
#ifdef ST_ASIO_WANT_ALL_MSG_SEND_NOTIFY
	//send buffer goes empty
	//notice: the msg is packed, using inconstant is for the convenience of swapping
	virtual void on_all_msg_send(InMsgType& msg) {}
#endif

	//call this in recv_handler (in subclasses) only
	void dispatch_msg()
	{
		if (!temp_msg_buffer.empty())
		{
#ifndef ST_ASIO_FORCE_TO_USE_MSG_RECV_BUFFER
			out_container_type temp_2_msg_buffer;
			BOOST_AUTO(begin_time, statistic::local_time());
			for (BOOST_AUTO(iter, temp_msg_buffer.begin()); !suspend_dispatch_msg_ && !posting && iter != temp_msg_buffer.end();)
				if (on_msg(*iter))
					temp_msg_buffer.erase(iter++);
				else
					temp_2_msg_buffer.splice(temp_2_msg_buffer.end(), temp_msg_buffer, iter++);
			BOOST_AUTO(time_duration, statistic::local_time() - begin_time);
			stat.handle_time_1_sum += time_duration;
			stat.recv_idle_sum += time_duration;

			if (!temp_2_msg_buffer.empty())
			{
				boost::unique_lock<boost::shared_mutex> lock(recv_msg_buffer_mutex);
				if (splice_helper(recv_msg_buffer, temp_2_msg_buffer))
					do_dispatch_msg(false);
			}
			temp_msg_buffer.splice(temp_msg_buffer.begin(), temp_2_msg_buffer);
#else
			boost::unique_lock<boost::shared_mutex> lock(recv_msg_buffer_mutex);
			if (splice_helper(recv_msg_buffer, temp_msg_buffer))
				do_dispatch_msg(false);
#endif
		}

		if (!overflow)
			do_recv_msg(); //receive msg sequentially, which means second receiving only after first receiving success
		else
		{
			recv_idle_begin_time = statistic::local_time();
			set_timer(TIMER_DISPATCH_MSG, 50, boost::bind(&st_socket::timer_handler, this, _1));
		}
	}

	void do_dispatch_msg(bool need_lock)
	{
		boost::unique_lock<boost::shared_mutex> lock(recv_msg_buffer_mutex, boost::defer_lock);
		if (need_lock) lock.lock();

		if (suspend_dispatch_msg_)
		{
			if (!dispatching && !recv_msg_buffer.empty())
				set_timer(TIMER_SUSPEND_DISPATCH_MSG, 24 * 60 * 60 * 1000, boost::bind(&st_socket::timer_handler, this, _1)); //one day
		}
		else if (!posting)
		{
			bool dispatch_all = false;
			if (stopped())
				dispatch_all = !(dispatching = false);
			else if (!dispatching)
			{
				if (!started())
					dispatch_all = true;
				else if (!recv_msg_buffer.empty())
				{
					dispatching = true;
					if (last_dispatch_msg.empty())
					{
						last_dispatch_msg.restart(recv_msg_buffer.front().begin_time);
						last_dispatch_msg.swap(recv_msg_buffer.front());
						recv_msg_buffer.pop_front();
					}

					post(boost::bind(&st_socket::msg_handler, this));
				}
			}

			if (dispatch_all)
			{
#ifndef ST_ASIO_DISCARD_MSG_WHEN_LINK_DOWN
				st_asio_wrapper::do_something_to_all(recv_msg_buffer, boost::bind(&st_socket::on_msg_handle, this, _1, true));
#endif
				recv_msg_buffer.clear();
			}
		}
	}

	//must mutex send_msg_buffer before invoke this function
	bool do_direct_send_msg(InMsgType& msg)
	{
		if (!msg.empty())
		{
			send_msg_buffer.resize(send_msg_buffer.size() + 1);
			send_msg_buffer.back().swap(msg);
			do_send_msg();
		}

		return true;
	}

	//must mutex post_msg_buffer before invoke this function
	bool do_direct_post_msg(InMsgType& msg)
	{
		if (!msg.empty())
		{
			post_msg_buffer.resize(post_msg_buffer.size() + 1);
			post_msg_buffer.back().swap(msg);
			if (!posting)
			{
				posting = true;
				set_timer(TIMER_HANDLE_POST_BUFFER, 50, boost::bind(&st_socket::timer_handler, this, _1));
			}
		}

		return true;
	}

private:
	bool timer_handler(unsigned char id)
	{
		switch (id)
		{
		case TIMER_DISPATCH_MSG: //delay putting msgs into receive buffer cause of receive buffer overflow
			stat.recv_idle_sum += statistic::local_time() - recv_idle_begin_time;
			dispatch_msg();
			break;
		case TIMER_SUSPEND_DISPATCH_MSG: //suspend dispatching msgs
			do_dispatch_msg(true);
			break;
		case TIMER_HANDLE_POST_BUFFER:
			{
				boost::unique_lock<boost::shared_mutex> lock(post_msg_buffer_mutex);
				{
					boost::unique_lock<boost::shared_mutex> lock(send_msg_buffer_mutex);
					if (splice_helper(send_msg_buffer, post_msg_buffer))
						do_send_msg();
				}

				bool empty = post_msg_buffer.empty();
				posting = !empty;
				lock.unlock();

				if (empty)
					do_dispatch_msg(true);

				return !empty; //continue the timer if some msgs still left behind
			}
			break;
		case TIMER_RE_DISPATCH_MSG: //re-dispatch
			do_dispatch_msg(true);
			break;
		default:
			assert(false);
			break;
		}

		return false;
	}

	void msg_handler()
	{
		BOOST_AUTO(begin_time, statistic::local_time());
		stat.dispatch_dealy_sum += begin_time - last_dispatch_msg.begin_time;
		bool re = on_msg_handle(last_dispatch_msg, false); //must before next msg dispatching to keep sequence
		BOOST_AUTO(end_time, statistic::local_time());
		stat.handle_time_2_sum += end_time - begin_time;
		boost::unique_lock<boost::shared_mutex> lock(recv_msg_buffer_mutex);
		dispatching = false;
		if (!re) //dispatch failed, re-dispatch
		{
			last_dispatch_msg.restart(end_time);
			set_timer(TIMER_RE_DISPATCH_MSG, 50, boost::bind(&st_socket::timer_handler, this, _1));
		}
		else //dispatch msg sequentially, which means second dispatching only after first dispatching success
		{
			last_dispatch_msg.clear();
			do_dispatch_msg(false);
		}
	}

protected:
	boost::uint_fast64_t _id;
	Socket next_layer_;

	in_msg last_send_msg;
	out_msg last_dispatch_msg;
	boost::shared_ptr<i_packer<typename Packer::msg_type> > packer_;

	in_container_type post_msg_buffer, send_msg_buffer;
	out_container_type recv_msg_buffer, temp_msg_buffer;
	//st_socket will invoke dispatch_msg() when got some msgs. if these msgs can't be pushed into recv_msg_buffer because of:
	// 1. msg dispatching suspended;
	// 2. post_msg_buffer not empty.
	//st_socket will delay 50 milliseconds(non-blocking) to invoke dispatch_msg() again, and now, as you known, temp_msg_buffer is used to hold these msgs temporarily.
	boost::shared_mutex post_msg_buffer_mutex, send_msg_buffer_mutex;
	boost::shared_mutex recv_msg_buffer_mutex;

	bool posting;
	bool sending, suspend_send_msg_;
	bool dispatching, suspend_dispatch_msg_;

	bool started_; //has started or not
	boost::shared_mutex start_mutex;

	struct statistic stat;
	typename statistic::stat_time recv_idle_begin_time;
};

} //namespace

#endif /* ST_ASIO_WRAPPER_SOCKET_H_ */
