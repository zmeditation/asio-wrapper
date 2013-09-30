/*
 * st_asio_wrapper_object_pool.h
 *
 *  Created on: 2013-8-7
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * this class used at both client and server endpoint, and in both tcp and udp socket
 * this class can only manage objects that inheirt from boost::asio::tcp::socket
 */

#ifndef ST_ASIO_WRAPPER_OBJECT_POOL_H_
#define ST_ASIO_WRAPPER_OBJECT_POOL_H_

#include "st_asio_wrapper_timer.h"
#include "st_asio_wrapper_service_pump.h"

#ifndef MAX_OBJECT_NUM
#define MAX_OBJECT_NUM				4096
#endif

//something like memory pool, if you open REUSE_OBJECT, all object in temp_object_can will never be freed,
//but waiting for reuse
//or, st_object_pool will free the objects in temp_object_can automatically and periodically,
//use SOCKET_FREE_INTERVAL to set the interval,
//see temp_object_can at the end of st_object_pool class for more details.
#ifndef REUSE_OBJECT
	#ifndef SOCKET_FREE_INTERVAL
	#define SOCKET_FREE_INTERVAL	10 //seconds, validate only REUSE_OBJECT not defined
	#endif
#endif

//define this to have st_object_pool invoke clear_all_closed_object() automatically and periodically
//this feature may serious influence server performance with huge number of objects
//so, re-write st_tcp_socket::on_recv_error and invoke st_object_pool::del_object() is recommended
//in long connection system
//in short connection system, you are recommended to open this feature, use CLEAR_CLOSED_SOCKET_INTERVAL
//to set the interval
#ifdef AUTO_CLEAR_CLOSED_SOCKET
	#ifndef CLEAR_CLOSED_SOCKET_INTERVAL
	#define CLEAR_CLOSED_SOCKET_INTERVAL	60 //seconds, validate only AUTO_CLEAR_CLOSED_SOCKET defined
	#endif
#endif

#ifndef CLOSED_SOCKET_MAX_DURATION
	#define CLOSED_SOCKET_MAX_DURATION	5 //seconds
	//after this duration, the corresponding object can be freed from the heap or reused again
#endif

namespace st_asio_wrapper
{

template<typename Socket>
class st_object_pool: public st_service_pump::i_service, public st_timer
{
protected:
	struct temp_object
	{
		const time_t closed_time;
		const boost::shared_ptr<Socket> object_ptr;

		temp_object(const boost::shared_ptr<Socket>& object_ptr_) :
			closed_time(time(NULL)), object_ptr(object_ptr_) {}

		bool is_timeout() const {return is_timeout(time(NULL));}
		bool is_timeout(time_t now) const {return closed_time <= now - CLOSED_SOCKET_MAX_DURATION;}
	};

protected:
	st_object_pool(st_service_pump& service_pump_) : i_service(service_pump_), st_timer(service_pump_) {}

	void start()
	{
#ifndef REUSE_OBJECT
		set_timer(0, 1000 * SOCKET_FREE_INTERVAL, NULL);
#endif
#ifdef AUTO_CLEAR_CLOSED_SOCKET
		set_timer(1, 1000 * CLEAR_CLOSED_SOCKET_INTERVAL, NULL);
#endif
	}

	void stop() {stop_all_timer();}

	bool add_object(const boost::shared_ptr<Socket>& object_ptr)
	{
		assert(object_ptr && &object_ptr->get_io_service() == &get_service_pump());
		mutex::scoped_lock lock(object_can_mutex);
		size_t object_num = object_can.size();
		if (object_num < MAX_OBJECT_NUM)
			object_can.push_back(object_ptr);
		lock.unlock();

		return object_num < MAX_OBJECT_NUM;
	}

	bool del_object(const boost::shared_ptr<Socket>& object_ptr)
	{
		bool found = false;

		mutex::scoped_lock lock(object_can_mutex);
		//object_can does not contain any duplicate items
		BOOST_AUTO(iter, std::find(object_can.begin(), object_can.end(), object_ptr));
		if (iter != object_can.end())
		{
			found = true;
			object_can.erase(iter);
		}
		lock.unlock();

		if (found)
		{
			mutex::scoped_lock lock(temp_object_can_mutex);
			temp_object_can.push_back(object_ptr);
		}

		return found;
	}

	boost::shared_ptr<Socket> reuse_object()
	{
#ifdef REUSE_OBJECT
		mutex::scoped_lock lock(temp_object_can_mutex);
		//objects are order by time, so we can use this feature to improve the performance
		for (BOOST_AUTO(iter, temp_object_can.begin()); iter->is_timeout() && iter != temp_object_can.end(); ++iter)
			if (!iter->object_ptr->started())
			{
				BOOST_AUTO(object_ptr, iter->object_ptr);
				temp_object_can.erase(iter);
				lock.unlock();

				object_ptr->reset();
				return object_ptr;
			}
#endif

		return boost::shared_ptr<Socket>();
	}

	virtual bool on_timer(unsigned char id, const void* user_data)
	{
		switch(id)
		{
#ifndef REUSE_OBJECT
		case 0:
			free_object();
			return true;
			break;
#endif
#ifdef AUTO_CLEAR_CLOSED_SOCKET
		case 1:
			{
				BOOST_AUTO(objects, object_can);
				clear_all_closed_object(objects);
				if (!objects.empty())
				{
					mutex::scoped_lock lock(temp_object_can_mutex);
					temp_object_can.insert(temp_object_can.end(), objects.begin(), objects.end());
				}
				return true;
			}
			break;
#endif
		case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: //reserved
			break;
		default:
			return st_timer::on_timer(id, user_data);
			break;
		}

		return false;
	}

public:
	size_t size()
	{
		mutex::scoped_lock lock(object_can_mutex);
		return object_can.size();
	}

	size_t closed_object_size()
	{
		mutex::scoped_lock lock(temp_object_can_mutex);
		return temp_object_can.size();
	}

	boost::shared_ptr<Socket> at(size_t index)
	{
		mutex::scoped_lock lock(object_can_mutex);
		assert(index < object_can.size());
		return index < object_can.size() ?
			*(boost::next(object_can.begin(), index)) : boost::shared_ptr<Socket>();
	}

	void list_all_object() {do_something_to_all(boost::bind(&Socket::show_info, _1, "", ""));}

	//Empty ip means don't care, any ip will match
	//Zero port means don't care, any port will match
	//this function only used with tcp socket, because for udp socket, remote endpoint means nothing.
	void find_object(const std::string& ip, unsigned short port, container::list<boost::shared_ptr<Socket> >& objects)
	{
		mutex::scoped_lock lock(object_can_mutex);
		if (ip.empty() && 0 == port)
			objects.insert(objects.end(), object_can.begin(), object_can.end());
		else
			for (BOOST_AUTO(iter, object_can.begin()); iter != object_can.end(); ++iter)
				if ((*iter)->is_open())
				{
					tcp::endpoint ep = (*iter)->remote_endpoint();
					if ((0 == port || port == ep.port()) && (ip.empty() || ip == ep.address().to_string()))
						objects.push_back((*iter));
				}
	}

	//Clear all closed objects from the list
	//Consider the following conditions:
	//1.You don't invoke del_object in on_recv_error and on_send_error, or close the socket in on_unpack_error
	//2.For some reason(I haven't met yet), on_recv_error, on_send_error and on_unpack_error
	// not been invoked
	//st_object_pool will automatically invoke this function if AUTO_CLEAR_CLOSED_SOCKET been defined
	void clear_all_closed_object(container::list<boost::shared_ptr<Socket> >& objects)
	{
		mutex::scoped_lock lock(object_can_mutex);
		for (BOOST_AUTO(iter, object_can.begin()); iter != object_can.end();)
			if (!(*iter)->is_open())
			{
				objects.resize(objects.size() + 1);
				objects.back().swap(*iter);
				iter = object_can.erase(iter);
			}
			else
				++iter;
	}

	//free a specific number of objects
	//if you use object pool(define REUSE_OBJECT), you may need to free some objects
	//when the object pool(get_closed_object_size()) goes big enough for memory saving(because
	//the objects in temp_object_can are waiting for reuse and will never be freed)
	//if you don't use object pool, st_object_pool will invoke this automatically and periodically
	//so, you don't need invoke this exactly
	void free_object(size_t num = -1)
	{
		if (0 == num)
			return;

		mutex::scoped_lock lock(temp_object_can_mutex);
		//objects are order by time, so we can use this feature to improve the performance
		for (BOOST_AUTO(iter, temp_object_can.begin()); num > 0 && iter->is_timeout() && iter != temp_object_can.end();)
			if (!iter->object_ptr->started())
			{
				iter = temp_object_can.erase(iter);
				--num;
			}
			else
				++iter;
	}

	DO_SOMETHING_TO_ALL_MUTEX(object_can, object_can_mutex)
	DO_SOMETHING_TO_ONE_MUTEX(object_can, object_can_mutex)

protected:
	//keep size() constant time would better, because we invoke it frequently, so don't use std::list(gcc)
	container::list<boost::shared_ptr<Socket> > object_can;
	mutex object_can_mutex;

	//because all objects are dynamic created and stored in object_can, maybe when the recv error occur
	//(at this point, your standard practice is deleting the object from object_can), some other
	//asynchronous calls are still queued in boost::asio::io_service, and will be dequeued in the future,
	//we must guarantee these objects not be freed from the heap, so, we move these objects from
	//object_can to temp_object_can, and free them from the heap in the near future(controlled by the
	//0(id) timer)
	//if AUTO_CLEAR_CLOSED_SOCKET been defined, clear_all_closed_object() will be invoked automatically
	//and periodically to move all closed objects to temp_object_can.
	container::list<temp_object> temp_object_can;
	mutex temp_object_can_mutex;
};

} //namespace

#endif /* ST_ASIO_WRAPPER_OBJECT_POOL_H_ */
