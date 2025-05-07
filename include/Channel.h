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
	std::counting_semaphore<> semReady=std::counting_semaphore(0);
	std::atomic_bool closed = false;



public:
	void send(const T& value){
		if (closed) {
			spdlog::warn("Sending to a closed channel");
			return;
		}
		semEmpty.acquire();
		std::scoped_lock lk{mtx};
		if (closed) {
			spdlog::warn("Sending to a closed channel");
			return;
		}
		queue.push(value);
		semReady.release();
	}

	bool empty(){
		std::scoped_lock lk{mtx};
		return queue.empty();
	}

	std::optional<T> receive() {
		if (closed) {
			spdlog::warn("Receiving from a closed channel");
			return std::nullopt;
		}
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

	std::optional<T> peek(){
		std::scoped_lock lk{mtx};
		if (queue.empty()) {
			return std::nullopt;
		}
		if (closed) {
			spdlog::warn("Receiving from a closed channel");
			return std::nullopt;
		}
		return queue.front();
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
		// to wake up receive func
		semReady.release();
	}

	bool is_closed() const {
		return closed;
	}
};

#endif //CHANNEL_H
