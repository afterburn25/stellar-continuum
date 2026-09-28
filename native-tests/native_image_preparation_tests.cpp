#include <stellar/engine/native_image_preparation.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using stellar::native_map::ImagePreparationQueue;
using stellar::native_map::RgbaImage;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const RgbaImage> image(){return RgbaImage::create(1,1,{10,20,30,255});}
struct Gate final {
  struct State final {std::mutex mutex;std::condition_variable started_cv,released_cv;bool started{},released{};};
  ~Gate(){release();}
  [[nodiscard]]std::shared_ptr<State> state()const{return state_;}
  static void block(const std::shared_ptr<State>&state){std::unique_lock lock(state->mutex);state->started=true;state->started_cv.notify_all();state->released_cv.wait(lock,[&]{return state->released;});}
  void wait_started()const{std::unique_lock lock(state_->mutex);require(state_->started_cv.wait_until(lock,std::chrono::steady_clock::now()+std::chrono::seconds(15),[this]{return state_->started;}),"image preparation worker did not reach its gate");}
  void release()const{{std::lock_guard lock(state_->mutex);state_->released=true;}state_->released_cv.notify_all();}
 private:std::shared_ptr<State> state_{std::make_shared<State>()};
};
void wait_ready(const ImagePreparationQueue::Ticket &ticket){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!ticket.ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();require(ticket.ready(),"image preparation ticket did not become ready");}
void wait_drained(const ImagePreparationQueue &queue){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(queue.outstanding_jobs()!=0&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();require(queue.outstanding_jobs()==0,"image preparation worker did not drain");}

void cancellation_and_capacity(){
  ImagePreparationQueue queue(1,4);Gate gate;auto ticket=*queue.submit(4,[gate_state=gate.state()]{Gate::block(gate_state);return image();});gate.wait_started();
  require(!ticket.ready()&&queue.outstanding_jobs()==1&&queue.reserved_bytes()==4,"running preparation was not retained as outstanding");
  require(!queue.submit(4,[]{return image();}),"running preparation bypassed job capacity");ticket.cancel();
  require(!queue.submit(4,[]{return image();}),"canceled running preparation released capacity before the worker finished");
  gate.release();wait_drained(queue);require(queue.reserved_bytes()==0,"canceled running preparation retained reservation after completion");
  auto retry=*queue.submit(4,[]{return image();});wait_ready(retry);(void)retry.take();
}
void queued_cancellation_skips_factory(){
  ImagePreparationQueue queue(2,8);Gate gate;std::atomic<int> stale_runs{};
  auto running=*queue.submit(4,[gate_state=gate.state()]{Gate::block(gate_state);return image();});gate.wait_started();
  auto stale=*queue.submit(4,[&]{++stale_runs;return image();});stale.cancel();gate.release();wait_ready(running);(void)running.take();wait_drained(queue);
  require(stale_runs.load()==0,"canceled queued preparation executed its factory");
}
void ready_and_failure_release_budgets(){
  ImagePreparationQueue queue(1,4);auto ready=*queue.submit(4,[]{return image();});wait_ready(ready);
  require(!queue.submit(4,[]{return image();}),"ready unconsumed preparation released its reservation");auto result=ready.take();require(result&&queue.outstanding_jobs()==0,"taking ready preparation did not release capacity");
  std::weak_ptr<const RgbaImage> weak=result;result.reset();require(weak.expired(),"taken image remained retained by the queue");
  std::weak_ptr<const RgbaImage> canceled_weak;auto canceled=*queue.submit(4,[&]{auto result=image();canceled_weak=result;return result;});wait_ready(canceled);canceled.cancel();require(canceled_weak.expired()&&queue.outstanding_jobs()==0,"canceling a finished preparation retained its image or reservation");
  auto failed=*queue.submit(4,[]()->std::shared_ptr<const RgbaImage>{throw std::runtime_error("prepared failure");});wait_ready(failed);bool propagated{};try{(void)failed.take();}catch(const std::runtime_error &error){propagated=std::string(error.what())=="prepared failure";}require(propagated&&queue.outstanding_jobs()==0,"worker exception did not propagate and release capacity");
  std::weak_ptr<const RgbaImage> oversized_weak;auto oversized=*queue.submit(4,[&]{auto result=RgbaImage::create(2,1,{1,2,3,4,5,6,7,8});oversized_weak=result;return result;});wait_ready(oversized);require(oversized_weak.expired(),"oversized prepared image was retained while its error was pending");bool budget_rejected{};try{(void)oversized.take();}catch(const std::length_error &){budget_rejected=true;}require(budget_rejected&&queue.reserved_bytes()==0,"oversized prepared image bypassed its reservation");
}
void owner_restrictions_and_ticket_lifetime(){
  auto queue=std::make_unique<ImagePreparationQueue>(1,4);
  auto ticket=*queue->submit(4,[]{return image();});wait_ready(ticket);
  bool submit_rejected{},take_rejected{},query_rejected{};
  std::jthread other([&]{
    try{(void)queue->submit(4,[]{return image();});}catch(const std::logic_error&){submit_rejected=true;}
    try{(void)ticket.take();}catch(const std::logic_error&){take_rejected=true;}
    try{(void)queue->outstanding_jobs();}catch(const std::logic_error&){query_rejected=true;}
  });
  other.join();
  require(submit_rejected&&take_rejected&&query_rejected,"non-owner accessed image preparation admission or collection");
  require(ticket.ready()&&queue->outstanding_jobs()==1,"rejected non-owner collection consumed the result");
  queue.reset();
  require(static_cast<bool>(ticket.take()),"ticket could not collect an image after explicit queue shutdown");
}
void request_validation_and_draining_destructor(){
  ImagePreparationQueue queue(1,4);bool empty{},zero{},oversize{};try{(void)queue.submit(4,{});}catch(const std::invalid_argument&){empty=true;}try{(void)queue.submit(0,[]{return image();});}catch(const std::length_error&){zero=true;}try{(void)queue.submit(5,[]{return image();});}catch(const std::length_error&){oversize=true;}require(empty&&zero&&oversize,"invalid preparation requests were accepted");
  std::atomic<bool> completed{};auto draining=std::make_unique<ImagePreparationQueue>(1,4);Gate gate;auto ticket=*draining->submit(4,[gate_state=gate.state(),&completed]{Gate::block(gate_state);completed=true;return image();});gate.wait_started();ticket.cancel();gate.release();draining.reset();require(completed.load(),"queue destructor did not drain the existing worker");
}
}

int main()try{cancellation_and_capacity();queued_cancellation_skips_factory();ready_and_failure_release_budgets();owner_restrictions_and_ticket_lifetime();request_validation_and_draining_destructor();std::cout<<"native image preparation tests passed\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
