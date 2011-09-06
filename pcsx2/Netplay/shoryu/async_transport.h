#pragma once
#include <cstdint>
#include <algorithm>

#include <boost/unordered_map.hpp>
#include <boost/shared_array.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/thread/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#define foreach         BOOST_FOREACH
#define reverse_foreach BOOST_REVERSE_FOREACH

#include "boost_extensions.h"
#include "tools.h"
#include "archive.h"
#include "peer.h"

/*IMPROVE:
 * exception handling
 * architecture
 * lock mutexes only when changing data!
 */

namespace shoryu
{
	enum OperationType
	{
		Send,
		Recv
	};

	template<OperationType Operation, int BufferSize>
		class transaction_data
		{
		public:
			endpoint ep;
			boost::array<char, BufferSize> buffer;
			size_t buffer_length;
		};
		template<OperationType Operation, int BufferSize, int BufferQueueSize>
		class transaction_buffer
		{
		public:
			transaction_buffer() : _next_buffer(0) {}
			boost::array<transaction_data<Operation,BufferSize>, BufferQueueSize> buffer;
			transaction_data<Operation,BufferSize>& next()
			{
				boost::unique_lock<boost::mutex> lock(_mutex);
				uint32_t i = _next_buffer;
				_next_buffer = ++_next_buffer % BufferQueueSize;
				return buffer[i];
			}
		private:
			volatile uint32_t _next_buffer;
			boost::mutex _mutex;
		};

	template<class DataType, int BufferQueueSize = 256, int BufferSize = 512>
	class async_transport
	{
	public:
		typedef typename peer<DataType> peer_type;
		typedef typename peer_data<DataType> peer_data_type;
		typedef boost::unordered_map<endpoint, boost::shared_ptr<peer_type>> peer_map_type;
		typedef std::list<const peer_data_type > peer_list_type;
		typedef std::function<void(const error_code&)> error_handler_type;
		typedef std::function<void(const endpoint&, DataType&)> receive_handler_type;
		typedef boost::mutex mutex_type;
		typedef boost::unique_lock<mutex_type> lock_type;
		
		async_transport() : _socket(_io_service), _is_running(false)
		{
		}
		virtual ~async_transport()
		{
			stop();
		}
		//Not thread-safe. Avoid concurrent calls with other methods
		void start(unsigned short port, int thread_num = 3)
		{
			_is_running = true;
			_socket.open(boost::asio::ip::udp::v4());
			_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::any(), port));
			receive_loop();
			for(int i=0; i < thread_num; i++)
				_thread_group.create_thread(boost::bind(&boost::asio::io_service::run, &_io_service));
		}
		//Not thread-safe. Avoid concurrent calls with other methods
		void stop()
		{
			_is_running = false;
			_socket.close();
			_io_service.stop();
			_thread_group.join_all();
			_io_service.reset();
			_peers.clear();
		}

		error_handler_type& error_handler()
		{
			return _err_handler;
		}
		void error_handler(const error_handler_type& f)
		{
			_err_handler = f;
		}
		receive_handler_type& receive_handler()
		{
			return _recv_handler;
		}
		void receive_handler(const receive_handler_type& f)
		{
			_recv_handler = f;
		}
		void queue(const boost::asio::ip::udp::endpoint& ep, const DataType& data)
		{
			//lock_type lock(_mutex);
			if(_is_running)
				queue_impl(ep, data);
		}
		int send(const boost::asio::ip::udp::endpoint& ep) {
			//lock_type lock(_mutex);
			if(_is_running)
				return send_impl(ep);
			return -1;
		}
		inline const peer_list_type peers()
		{
			//lock_type lock(_mutex);
			peer_list_type list;
			foreach(peer_map_type::value_type& kv, _peers)
				list.push_back(kv.second->data);
			return list;
		}
		inline const peer_data_type& peer(const endpoint& ep)
		{
			//lock_type lock(_mutex); 
			return _peers[ep]->data;
		}
		inline int port()
		{
			//lock_type lock(_mutex); 
			return _socket.local_endpoint().port();
		}
	private:
		inline int send_impl(const boost::asio::ip::udp::endpoint& ep)
		{
			transaction_data<Send,BufferSize>& t = _send_buffer.next();
			t.ep = ep;
			oarchive oa(t.buffer.begin(), t.buffer.end());
			int send_n = find_peer(ep).serialize_datagram(oa);

			t.buffer_length = oa.pos();
			_socket.async_send_to(boost::asio::buffer(t.buffer, t.buffer_length), boost::ref(t.ep),
				boost::bind(&async_transport::send_handler, this, boost::ref(t), 
				boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error));
			
			return send_n;
		}

		inline uint64_t queue_impl(const boost::asio::ip::udp::endpoint& ep, const DataType& data)
		{
			return find_peer(ep).queue_msg(data);
		}
		
		inline peer_type& find_peer(const endpoint& ep)
		{
			peer_map_type::iterator end;
			if(_peers.find(ep) == end)
			{
				_peers[ep].reset(new peer_type());
				_peers[ep]->data.ep = ep;
			}
			return *_peers[ep];
		}

		void receive_loop()
		{
			transaction_data<Recv,BufferSize>& t = _recv_buffer.next();
			_socket.async_receive_from(boost::asio::buffer(t.buffer, BufferSize), 
				boost::ref(t.ep), boost::bind(&async_transport::receive_handler, this, 
				boost::ref(t), boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error));
		}
		void receive_handler( transaction_data<Recv,BufferSize>& t, size_t bytes_recvd, const boost::system::error_code& e)
		{
			//lock_type lock(_mutex);
			if(_is_running)
			{
				receive_loop();
				if (!e)
				{
					t.buffer_length = bytes_recvd;
					finalize(t);
				}
				else
				{
					if(_err_handler)
						_err_handler(e);
				}
			}
		}
		void send_handler(const transaction_data<Send,BufferSize>& t, size_t bytes_sent, const boost::system::error_code& e)
		{
			//lock_type lock(_mutex);
			if(_is_running)
			{
				if(!e)
					finalize(t);
				else
				{
					if(_err_handler)
						_err_handler(e);
				}
			}
		}
		// This method is not thread-safe by itself. It should be mutex-guarded.
		void finalize(const transaction_data<Send,BufferSize>& transaction)
		{
			//Outgoing transaction finalization is omitted for better performance
		}
		void finalize(const transaction_data<Recv,BufferSize>& transaction)
		{
			iarchive ia(transaction.buffer.begin(), transaction.buffer.begin()+transaction.buffer_length);
			peer_type& peer = find_peer(transaction.ep);
			try
			{
				peer.deserialize_datagram(ia, [&](DataType& data)
				{
					if(_recv_handler)
						_recv_handler(transaction.ep, data);
				});
			}
			//TODO: Exception handling code
			catch(std::exception&)
			{
				if(_err_handler)
					_err_handler(error_code());
			}
		}

		error_handler_type _err_handler;
		receive_handler_type _recv_handler;

		volatile bool _is_running;

		boost::asio::io_service _io_service;
		boost::asio::ip::udp::socket _socket;
		boost::thread_group _thread_group;

		peer_map_type _peers;

		transaction_buffer<Send,BufferSize, BufferQueueSize> _send_buffer;
		transaction_buffer<Recv,BufferSize, BufferQueueSize> _recv_buffer;

		//mutex_type _mutex;
	};
}