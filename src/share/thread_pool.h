#ifndef RYHME_SHARE_THREAD_POOL_H
#define RYHME_SHARE_THREAD_POOL_H

#include <functional>
#include <queue>
#include <pthread.h>
#include <semaphore.h>
#include <mutex>
#include <chrono>
#include "src/common/errorno.h"

namespace ryhme {
namespace share {

  class Functor {
    public:
      Functor() { func_ = []()->int{ return 0; }; }
      Functor(std::function<int()> func) : func_(func) {}
      int operator()() { return func_(); }
    private:
      std::function<int()> func_;
  };

  class Task {
    public:
    Task(void * arg) { arg_ = arg; }
    virtual ~Task() {}
    virtual int run() = 0;   
    void *arg_;
  };

  class IOTask : public Task {
    public:
      IOTask() : Task(nullptr) {}
      IOTask(void *arg) : Task(arg) {}
      virtual ~IOTask() {}
      virtual int run() override;
      void set_work(std::function<void()> func) { func_ = func; }
    private:
      std::function<void()> func_;
  };

  enum PirorityLevel {
    LEVEL_ONE = 1,
    LEVEL_TWO = 2,
    LEVEL_THREE = 3,
    LEVEL_FOUR = 4,
    LEVEL_FIVE = 5,
  };

  class TaskMgr {
    public:
      TaskMgr(int capacity) : capacity_(capacity) { 
        pthread_cond_init(&read_condition_, nullptr); 
        pthread_cond_init(&write_condition_, nullptr); 
      }
      ~TaskMgr();
      int add_task(Task *task);
      int rm_task(Task *task);
      int pop_task(Task *&task);
      void debug();
    private:
      pthread_mutex_t mutex_;
      pthread_cond_t read_condition_;
      pthread_cond_t write_condition_;
      std::queue<Task *> tasks_; //priotity
      int capacity_;
  };

  enum ThreadStatus {
    INITIAL = 0,
    RUNNING = 1,
    SLEEPING = 2,
    FINISHED = 3,
    ERROR = 4,
  };

  class ThreadPool;
  class IRunWrapper;
  class TimerRunWrapper;
  class Thread {
    public:
      Thread(ThreadPool *thread_pool, Functor func, int idx);
      virtual ~Thread() {}
      virtual void stop() { is_stop_ = true; }
      virtual int start() { return 0; }
      virtual int join() { return 0; }
      virtual int try_join() { return 0; }
      virtual int destroy() { return 0; }
      virtual int get_tid() { return 0; }
      inline bool is_stopped() { return is_stop_; }
      ThreadPool *get_thread_pool() { return thread_pool_; }
      inline IRunWrapper* get_run_wrapper() { return run_wrapper_; }
      inline void set_run_wrapper(IRunWrapper *run_wrapper) { run_wrapper_ = run_wrapper; }
      inline int run() { return func_(); }
    protected:
    protected:
      ThreadPool *thread_pool_;
      Functor func_;
      int idx_;
      bool is_stop_;
      ThreadStatus status_;
      IRunWrapper *run_wrapper_;
  };

  class PThread : public Thread {
    public:
      PThread(ThreadPool *thread_pool, Functor func, int idx) : 
            Thread(thread_pool, func, idx), thread_(0) {}
      ~PThread();
      pthread_t get_pthread() { return thread_; }
      virtual int start() override;
      virtual int join() override;
      virtual int try_join() override;
      virtual int destroy() override;
      virtual int get_tid() override { return thread_; }
    private:
      static void *th_run_(void *arg);
      pthread_t thread_;
         
  };

  class ThreadPool {
    public:
      ThreadPool(int thread_capacity) : 
        threads_(nullptr), threads_capacity_(thread_capacity), threads_size_(0), is_audit_thread_(true) {
        };
      virtual ~ThreadPool();
      virtual int start_all(Functor func);
      virtual int stop_all();
      virtual int start_th(Functor func, const int idx);
      virtual int init();
      virtual int destroy();
      virtual int join_all();
      virtual int create_thread(Thread *&thread, Functor func, const int idx, const bool start = true) { return common::R_SUCCESS; }
      virtual int destroy_thread(Thread *thread);
      virtual int handle() { return common::R_SUCCESS; }
      virtual int resize_thread_pool(const int size);
      inline int *get_thread_size() { return &threads_size_; }
      inline bool get_is_audit_thread() { return is_audit_thread_; }
      inline void set_is_audit_thread(const bool audit) { is_audit_thread_ = audit; }
      virtual double get_cpu_ratio() { return 0; }
      int get_thread_idx() const { return thread_idx_; }
      virtual int run(const int idx) { thread_idx_ = idx; return 0; }
    protected:
      void set_thread_idx(int idx) { thread_idx_ = idx; }
      
    protected:
      static thread_local int thread_idx_;
      Thread **threads_;
      int threads_capacity_;
      int threads_size_;
      bool is_stop_;
      bool is_audit_thread_;
  };

  class PThreadPool : public ThreadPool{
    friend class PThread;
    public:
      PThreadPool(int thread_capacity);
      virtual ~PThreadPool() {}
      int acquire_task(Task *&task);
      virtual int create_thread(Thread *&thread, Functor func, const int idx, const bool start = true) override;
      //inline void set_task_queue(TaskMgr *task_mgr) { task_mgr_ = task_mgr; }
      virtual double get_cpu_ratio() override;
      
    protected:
      virtual int run(const int idx) override;
      //virtual int handle_task();
      //TaskMgr *task_mgr_;
      
  };


  class IRunWrapper {
    public:
      IRunWrapper() {}
      ~IRunWrapper() {}
      virtual int pre_run() { return common::R_SUCCESS; }
      virtual int after_run() { return common::R_SUCCESS; }
      virtual void set_arg(void *arg) {}
      virtual void* get_arg() {return nullptr; }
  };

  class TimerRunWrapper : public IRunWrapper{
    public:
      struct TimerArg {
        TimerArg () : real_begin_t_(),
                      cpu_begin_t_(),
                      wait_time_(0),
                      calc_time_(0) {}
        struct timespec real_begin_t_;
        struct timespec cpu_begin_t_;
        double wait_time_;
        double calc_time_;
      };
      TimerRunWrapper (TimerArg *arg) : arg_(arg) {}
      TimerRunWrapper () : arg_(nullptr) {}
      virtual ~TimerRunWrapper () {}
      int pre_run() override;
      int after_run() override;
      //int alter_arg(void *arg) override;
      inline void set_arg(void *arg) override { arg_ = reinterpret_cast<TimerArg *>(arg); }
      virtual void* get_arg() override {return arg_; }
    private:
      TimerArg *arg_;
  };
} //end share
} //end ryhme

#endif