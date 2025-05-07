//
// Created by delta on 1/23/2025.
//

#ifndef CHANNEL_H
#define CHANNEL_H

#include <mutex>
#include <queue>
#include <optional>
#include <semaphore>
template<typename T,size_t Size,typename Del=void>
class Channel {
	std::queue<T> queue;
	std::mutex mtx;
	std::counting_semaphore<> semEmpty=std::counting_semaphore(Size);
	std::counting_semaphore<> semReady;
	std::atomic_bool closed = false;



public:
	void send(const T& value){
		semEmpty.acquire();
		std::scoped_lock lk{mtx};
		if (closed) {
			spdlog::warn("Sending to a closed channel");
			return;
		}
		queue.push(value);
		semReady.release();
	}

	std::optional<T> receive() {
		semReady.acquire();
		std::scoped_lock lk{mtx};
		if (closed) {
			spdlog::warn("Receiving from a closed channel");
			return std::nullopt;
		}
		// asserts the queue is not empty
		assert(queue.size()!=0);
		std::optional<T> ret(std::move(queue.front()));
		queue.pop();
		semEmpty.release();
		return ret;
	}

	void clear(){
		std::scoped_lock lk{mtx};
		while (!queue.empty()) {
			semReady.acquire();
			auto item=queue.front();
			Del{}(item);
			queue.pop();
			semEmpty.release();
		}
	}
	void close() {
		closed=true;
		clear();
	}

	bool is_closed() const {
		return closed;
	}
};

#endif //CHANNEL_H
