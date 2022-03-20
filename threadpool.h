//
// Created by qwskyz on 2022/3/18.
//

#ifndef WEBSERVER_THREADPOOL_H
#define WEBSERVER_THREADPOOL_H

#include <pthread.h>
#include <list>
#include "lock.h"
#include <iostream>

template<typename T>
class threadPool {
public:
    explicit threadPool(int thead_number = 8, int max_requests = 10000);

    ~threadPool();

    bool append(T *request);

private:
    // the function of work thread, take out the request.txt from request.txt queue
    static void *worker(void *arg);

    void run();

private:
    // the current number of threads
    int m_thread_number;

    // the array of threads
    pthread_t *m_threads;

    // the max number of request.txt in request.txt queue
    int m_max_requests;

    // the request.txt queue
    std::list<T *> m_work_queue;

    // the locker of request.txt queue
    locker m_queue_locker;

    // request.txt to be handler
    sem m_queue_stat;

    // the flag of whether stop thread
    bool m_stop;
};

template<typename T>
threadPool<T>::threadPool(int thead_number, int max_requests):
        m_thread_number(thead_number),
        m_max_requests(max_requests),
        m_stop(false),
        m_threads(nullptr) {
    if (thead_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    // init thread array
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    for (int i = 0; i < m_thread_number; i++) {
        std::cout << "create the " << i << " thread" << std::endl;
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // core detach thread
        std::cout << "detach the " << i << " thread" << std::endl;
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadPool<T>::~threadPool<T>() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadPool<T>::append(T *request) {
    m_queue_locker.lock();
    if (m_work_queue.size() >= m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }

    m_work_queue.push_back(request);
    m_queue_locker.unlock();
    // core add the semaphore of request.txt to be handler
    m_queue_stat.post();
    return true;
}

template<typename T>
void *threadPool<T>::worker(void *arg) {
    threadPool<T>* pool = (threadPool<T>*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadPool<T>::run() {
    while (!m_stop) {
        // core reduce semaphore
        // s is similar to producer-consumer pattern
        m_queue_stat.wait();
        m_queue_locker.lock();
        if (m_work_queue.empty()) {
            m_queue_locker.unlock();
            continue;
        }
        T* request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queue_locker.unlock();
        if (!request) {
            continue;
        }
        request->process();
    }
}

#endif //WEBSERVER_THREADPOOL_H