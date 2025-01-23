//
// Created by delta on 1/23/2025.
//

#ifndef CHANNEL_H
#define CHANNEL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>
template<typename T,typename Del=void>
class Channel {
	std::queue<T> queue;              // 存储消息的队列
	std::mutex mtx;                   // 保护队列的互斥锁
	std::condition_variable cv_send;  // 用于通知发送操作
	std::condition_variable cv_recv;  // 用于通知接收操作
	bool closed = false;              // 标志 Channel 是否已关闭
	int max_size=50;

public:
	// 发送数据到 Channel，当队列满时阻塞
	void send(const T& value) {
		std::unique_lock<std::mutex> lock(mtx);

		if (closed) {
			throw std::runtime_error("Channel is closed");
		}

		cv_send.wait(lock, [this] { return queue.size()<max_size||closed; });

		queue.push(value);
		spdlog::debug("queue send: {}",queue.size());
		cv_recv.notify_all(); // 通知等待接收的线程
	}

	// 从 Channel 接收数据，当队列为空时阻塞
	// 返回一个 std::optional<T>，如果关闭且无数据，返回 std::nullopt
	std::optional<T> receive() {
		std::unique_lock lock(mtx);

		cv_recv.wait(lock, [this] { return !queue.empty() || closed; });

		if (!queue.empty()) {
			T value = queue.front();
			queue.pop();
			// spdlog::debug("queue receive: {}",queue.size());
			cv_send.notify_all();
			return value;
		}

		if (closed) {
			return std::nullopt;
		}

		return std::nullopt;
	}

	bool empty(){
		std::unique_lock lock{mtx};
		return queue.empty();
	}

	void clear(){
		std::unique_lock lock{mtx};
		while(!queue.empty()) {
			auto item=queue.front();
			queue.pop();
			// spdlog::debug("queue pop: {}",queue.size());

			Del{}(item);
		}
		spdlog::debug("clear: {}",queue.size());
		cv_send.notify_all();
		lock.unlock();

	}

	// 关闭 Channel，发送和接收操作会终止
	void close() {
		std::unique_lock lock(mtx);
		closed = true;
		cv_send.notify_all();
		cv_recv.notify_all(); // 通知所有等待接收的线程
	}

	// 检查 Channel 是否已关闭
	bool is_closed() const {
		std::unique_lock lock{mtx};
		return closed;
	}
};

#endif //CHANNEL_H
