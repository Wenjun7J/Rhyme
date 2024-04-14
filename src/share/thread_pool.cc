#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <unistd.h>
#include <thread>
#include "sys/sysinfo.h"
#include "thread_pool.h"
#include "src/share/thread_pool.h"

using namespace ryhme::common;
namespace ryhme {

namespace share {
  
  thread_local int ThreadPool::thread_idx_ = 0;

  int IOTask::run() {
    int ret = R_SUCCESS;
    func_();
    return ret;
  }

  Thread::Thread(ThreadPool *thread_pool, Functor func, int idx) : 
    thread_pool_(thread_pool), func_(func), idx_(idx), is_stop_(true), status_(ThreadStatus::INITIAL), run_wrapper_(nullptr) {
  } 

  PThread::~PThread() {
    destroy();
  }

  int PThread::destroy() {
    int ret = R_SUCCESS;
    if (get_tid() != 0) {
      if (R_EAGAIN == try_join()) {
        ret = join();
      }
    } else {
      //do nothing
    }
    return ret;
  }

  int PThread::start() {
    int ret = R_SUCCESS;
    const int count = __sync_fetch_and_add(thread_pool_->get_thread_size(), 1);
    const int max_thread_count = 64;
    bool need_destroy = false;
    if (count >= max_thread_count) {
      __sync_fetch_and_add(thread_pool_->get_thread_size(), -1);
      //log error
    } else {
      pthread_attr_t attr;
      int pret = pthread_attr_init(&attr);
      if (pret == 0) {
        is_stop_ = false;
        pret = pthread_create(&thread_, &attr, th_run_, this);
        if (pret != 0) {
          thread_ = 0;
          ret = R_ERROR;
        } else {
          while (__atomic_load_n((int*)&status_,  __ATOMIC_SEQ_CST) == (int)ThreadStatus::INITIAL) {
            sched_yield();
          }
          if (status_ == ThreadStatus::ERROR) {
            ret = R_ERROR;
            std::cout<< "create thread failed" << status_ << std::endl;
          }
        }
      } else {
        need_destroy = true;
        ret = R_ERROR;
        std::cout<< "init pthread attr failed" << std::endl;
      }
      if (need_destroy) {
        pthread_attr_destroy(&attr);
      }
      if (0 != pret) {
        stop();
        destroy();
        __sync_fetch_and_add(thread_pool_->get_thread_size(), -1);
        ret = R_ERROR;
      } else {
      }
    }
    std::cout<<"start thread:"<< idx_ <<"ret:" << ret <<" need_destroy:" << need_destroy<< " status_"<<status_ << " is_stop:" <<is_stop_<<std::endl;
    return ret ;
  }

  int PThread::try_join() {
    int ret = R_SUCCESS;
    if (thread_ != 0) {
      int pret = pthread_tryjoin_np(thread_, nullptr);
      if (0 != pret) {
        ret = R_EAGAIN;
      }
    }
    return ret;
  }

  int PThread::join() {
    int ret = R_SUCCESS;
    if (thread_ != 0) {
      int pret = pthread_join(thread_, nullptr);   
      if (pret != 0) {
        std::cout<< "pthread join failed:" << pret << std::endl;
      }
    }
    return ret;
  }

  void *PThread::th_run_(void *arg) {
    int ret = R_SUCCESS;
    PThread *th = reinterpret_cast<PThread *>(arg);
    th->status_ = ThreadStatus::RUNNING;
    th->get_thread_pool()->run(th->idx_);
    th->status_ = ThreadStatus::FINISHED;
    __sync_fetch_and_add(th->thread_pool_->get_thread_size(), -1);
    return nullptr;
  }
  
  ThreadPool::~ThreadPool() {
    //stop();
    join_all();
    destroy();
  }

  int ThreadPool::destroy_thread(Thread *thread) {
    int ret = R_SUCCESS;
    assert(thread->get_thread_pool() == this);
    thread->stop();
    ret = thread->destroy();
    delete thread;
    __sync_fetch_and_add(&threads_size_, -1);
    return ret;
  }

  int ThreadPool::resize_thread_pool(const int size) {
    //TODO: LOCK
    int ret = R_SUCCESS;
    if (size <= 0) {
      std::cout<< "size should be positive, size:" << size << std::endl;
    } else if (is_stop_) {
      threads_capacity_ = size;
    } else {
      if (size == threads_size_) {
        //skip
      } else if (size < threads_size_) {
        for (int i = 0; E_SUCC(ret) && size < threads_size_ && i < threads_capacity_; i++) {
          if (threads_[i] == nullptr) {
            //skip
          } else {
            threads_[i]->stop();
            ret = threads_[i]->destroy();
            delete threads_[i];
            threads_[i] = nullptr;
            __sync_fetch_and_add(&threads_size_, -1);
          }
        }
      } else if (size > threads_size_) {
        Thread **new_threads = (Thread**)std::malloc(size * sizeof(Thread*));
        // for (int i = 0; E_SUCC(ret) && size > threads_size_ && i < threads_capacity_; i++) {
        //   if (new_threads[i] == nullptr) {
        //     ret = create_thread(new_threads[i], i);
        //   }
        // }

        // if (size > threads_capacity_) {
        //   memcpy(new_threads, threads_, sizeof(Thread*) * threads_capacity_);
        //   free (threads_);
        //   threads_ = new_threads;
        //   for (int i = threads_capacity_; E_SUCC(ret) && i < size; i++) {
        //     ret = create_thread(new_threads[i], i);
        //   }
        //   threads_capacity_ = size;
        // }
      }
    }
    return ret;
  }

  PThreadPool::PThreadPool(int thread_capacity) : ThreadPool(thread_capacity) {
  }

  int ThreadPool::start_all(Functor func) {
    int ret = R_SUCCESS;
    assert(threads_ != nullptr);
    if (threads_ != nullptr) {
      for (int i = 0; i < threads_capacity_; i++) {
        ret = start_th(func, i);
      }
    }
    return ret;
  }

  int ThreadPool::start_th(Functor func, const int idx) {
    int ret = common::R_SUCCESS;
    assert(!is_stop_);
    assert(threads_ != nullptr);
    assert(idx < threads_capacity_);
    create_thread(threads_[idx], func, idx);
    assert(threads_[idx] != nullptr);
    threads_[idx]->start();
    return ret;
  }

  int ThreadPool::init() {
    int ret = R_SUCCESS;
    threads_ = (Thread**)std::malloc(threads_capacity_ * sizeof(Thread *));
    if (threads_ != nullptr) {
      memset(threads_, 0, sizeof(Thread*) * threads_capacity_);
      is_stop_ = false;
      // for (int  i = 0; i < threads_capacity_; i++) {
      //   create_thread(threads_[i], i);
      // }

      //if start fail:
      // stop; join; destory;
    } else {
      ret = R_ALLOC_FAIL;
    }
    std::cout<< "start thread pool with size:" << threads_capacity_  << std::endl;
    return ret;
  }

  int ThreadPool::stop_all() {
    int ret = R_SUCCESS;
    //TODO: LOCK?
    is_stop_ = true;
    if (threads_ != NULL) {
      for (int i = 0; i < threads_capacity_; i++) {
        if (threads_[i] != nullptr) {
          threads_[i]->stop();
        }
      }
    }
    return ret;
  }

  int ThreadPool::destroy() {
    int ret = R_SUCCESS;
    if (threads_ != nullptr) {
      for (int i = 0; i < threads_capacity_; i++) {
        if (threads_[i] != nullptr) {
          destroy_thread(threads_[i]);
        }
      }
      free(threads_);
      threads_ = nullptr;
    }
    return ret;
  }

  int ThreadPool::join_all() {
    int ret = R_SUCCESS;
    if (threads_ != nullptr) {
      for (int i = 0; i < threads_capacity_; i++) {
        if (threads_[i] != nullptr) {
          ret = threads_[i]->join();
        }
      }
    }
    return ret;
  }

  int PThreadPool::run(const int idx) {
    int ret = R_SUCCESS;
    thread_idx_ = idx;
    Thread *th = threads_[thread_idx_];
    if (is_audit_thread_) {
      while (!th->is_stopped()) {
        th->get_run_wrapper()->pre_run();
        ret = th->run();
        th->get_run_wrapper()->after_run();
      }
    } else {
      while (!th->is_stopped()) {
        ret = th->run();
      }
    }
    return ret;
  }

  // int PThreadPool::handle_task() {
  //   int ret = R_SUCCESS;
  //   Task *task = nullptr;
  //   ret = acquire_task(task);
  //   if (task != NULL) {
  //     std::cout <<"thread " << thread_idx_ << " begin do: ";
  //     task->run();
  //     delete task; //may use allocator to manager;
  //   } else {
  //     int ret = R_ERROR;
  //     std::cout<< "run task is null" << std::endl;
  //   }
  //   return ret;
  // }

  double PThreadPool::get_cpu_ratio() {
    double ratio = 0;
    if (is_audit_thread_) {
      double total_wait_time = 0;
      double total_calc_time = 0;
      for (int i = 0; i < threads_capacity_; i++) {
        TimerRunWrapper::TimerArg* arg = reinterpret_cast<TimerRunWrapper::TimerArg*>(threads_[i]->get_run_wrapper()->get_arg());
        total_wait_time += arg->wait_time_;
        total_calc_time += arg->calc_time_;
      }
      // printf("system cpu num is %d\n", get_nprocs_conf());
      // printf("system enable num is %d\n", get_nprocs());
      printf("thread pool total calc time %lf\n", total_calc_time);
      printf("thread pool total wait time %lf\n", total_wait_time);
      if (total_wait_time + total_calc_time == 0) {
        int ret = R_ERROR;
      } else {
        ratio = 1.0 * threads_size_ * (total_calc_time / (total_wait_time + total_calc_time)) / get_nprocs_conf();
        
        printf("thread pool cost cpu ratio %lf\n", ratio);
      }
    }

    return ratio;
  }

  int PThreadPool::create_thread(Thread *&thread, Functor func, const int idx, const bool start) {
    int ret = R_SUCCESS;
    if (thread != nullptr) {
      destroy_thread(thread);
    } 
    thread = new PThread(this, func, idx);
    threads_[idx] = thread;
    if (is_audit_thread_) {
      TimerRunWrapper::TimerArg *arg = new TimerRunWrapper::TimerArg();
      TimerRunWrapper *run_wrapper_ = new TimerRunWrapper(arg);
      thread->set_run_wrapper(run_wrapper_);
    }
    return ret;
  }

  // int PThreadPool::acquire_task(Task *&task) {
  //   int ret = R_SUCCESS;
  //   task = nullptr;
  //   ret = task_mgr_->pop_task(task);
  //   return ret;
  // }

  int TaskMgr::pop_task(Task *&task) {
    int ret = R_SUCCESS;
    task = nullptr;
    
    pthread_mutex_lock(&mutex_);
    while (tasks_.empty()) {
      pthread_cond_wait(&read_condition_, &mutex_);
    }
    task = tasks_.front();
    tasks_.pop();
    pthread_mutex_unlock(&mutex_);
    pthread_cond_signal(&write_condition_);
    return ret;
  }

  int TaskMgr::add_task(Task *task) {
    int ret = R_SUCCESS;
    pthread_mutex_lock(&mutex_);
    if (tasks_.size() < capacity_) {
      tasks_.push(task);
    } else {
      while (tasks_.size() >= capacity_) {
        pthread_cond_wait(&write_condition_, &mutex_);
      }
    }
    pthread_mutex_unlock(&mutex_);
    pthread_cond_signal(&read_condition_);
    return ret;
  }

  TaskMgr::~TaskMgr() {
    while (!tasks_.empty()) {
      Task *task = tasks_.front();
      tasks_.pop();
      delete task;
    }
  }


  void TaskMgr::debug() {
    std::cout<<"task size: "<<tasks_.size()<<std::endl;
  }


  int TimerRunWrapper::pre_run() {
    clock_gettime(CLOCK_REALTIME, &arg_->real_begin_t_);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &arg_->cpu_begin_t_);
    return R_SUCCESS;
  }

  int TimerRunWrapper::after_run() {  
    struct timespec real_end;
    struct timespec cpu_end;
    clock_gettime(CLOCK_REALTIME, &real_end);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_end);
    if (arg_->wait_time_ > 1e9 || arg_->calc_time_ > 1e9) {
      arg_->wait_time_ = 0;
      arg_->calc_time_ = 0;
    }
    double calc_time = 1.0 * (cpu_end.tv_sec + cpu_end.tv_nsec/ 1000.0 / 1000.0 / 1000.0) - 1.0 *(arg_->cpu_begin_t_.tv_sec + arg_->cpu_begin_t_.tv_nsec/ 1000.0 / 1000.0 / 1000.0);
    double real_time = 1.0 * (real_end.tv_sec + real_end.tv_nsec/ 1000.0 / 1000.0 / 1000.0) - 1.0 *(arg_->real_begin_t_.tv_sec + arg_->real_begin_t_.tv_nsec/ 1000.0 / 1000.0 / 1000.0);
    double wait_time = real_time - calc_time;
    arg_->wait_time_ += wait_time;
    arg_->calc_time_ += calc_time;

    return R_SUCCESS;
  }


} //end share
} //end ryhme


using namespace ryhme::share;
static int val = 0;
int main() {
  TaskMgr *task_mgr = new TaskMgr(1024);
  PThreadPool *thread_pool = new PThreadPool(16);
  thread_pool->init();
  thread_pool->start_all(Functor([&task_mgr, &thread_pool]()->int {
    int ret = 0;
    Task *task = nullptr;
    std::cout <<"thread " << thread_pool->get_thread_idx()<<"running\n";
    task_mgr->pop_task(task);
    if (task != nullptr) {
      std::cout <<"thread " << thread_pool->get_thread_idx() << " begin do: ";
      task->run();
      delete task; //may use allocator to manager;
    } else {
      int ret = R_ERROR;
      std::cout<< "run task is null" << std::endl;
    }
    return ret;
  }));
  int val = 0;
  std::mutex mut;
  while(1) {
    IOTask *task = new IOTask();
    task->set_work([&val, &mut]() {
      for (int i = 0; i < 1 ;i++) {val++;} 
      
      int x= 0;
      for (int i = 0; i < 100000000 ;i++) { x ++; } 
      //std::this_thread::sleep_for(std::chrono::milliseconds(200));
      mut.lock();
      std::cout<< "work: "<< val <<std::endl;
      mut.unlock();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    task_mgr->add_task(task);
    task_mgr->debug();

    thread_pool->get_cpu_ratio();
  }
  return  0;
}