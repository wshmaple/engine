/**
 * @file
 */

#include "ThreadPool.h"
#include "String.h"
#include "core/Trace.h"

namespace core {

ThreadPool::ThreadPool(size_t threads, const char *name) :
		_stop(false) {
	_workers.reserve(threads);
	if (name == nullptr) {
		name = "ThreadPool";
	}
	for (size_t i = 0; i < threads; ++i) {
		_workers.emplace_back([this, name, i] {
			const std::string n = core::string::format("%s-%i", name, (int)i);
			core_trace_thread(n.c_str());
			for (;;) {
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(this->_queueMutex);
					this->_condition.wait(lock, [this] {
						return this->_stop || !this->_tasks.empty();
					});
					if (this->_stop && this->_tasks.empty()) {
						return;
					}
					task = std::move(this->_tasks.front());
					this->_tasks.pop();
				}

				core_trace_scoped(ThreadPoolWorker);
				task();
			}
		});
	}
}

ThreadPool::~ThreadPool() {
	shutdown();
}

void ThreadPool::shutdown(bool wait) {
	_stop = true;
	if (!wait) {
		std::unique_lock<std::mutex> lock(_queueMutex);
		while (!_tasks.empty()) {
			_tasks.pop();
		}
		_condition.notify_all();
	}
	for (std::thread &worker : _workers) {
		worker.join();
	}
	_workers.clear();
}

}
