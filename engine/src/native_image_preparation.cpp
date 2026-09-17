#include <stellar/engine/native_image_preparation.hpp>

#include <stellar/engine/foundation.hpp>

#include <cassert>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace stellar::native_map {
class EngineJobs final {
 public:
  EngineJobs():jobs_(1){}
  stellar::engine::JobSystem jobs_;
};

struct ImagePreparationQueue::SharedState final {
  std::mutex mutex;
  std::size_t jobs{}, bytes{}, maximum_jobs{}, maximum_bytes{};
};

struct ImagePreparationQueue::Ticket::Cell final {
  std::mutex mutex;
  std::shared_ptr<SharedState> state;
  std::function<std::shared_ptr<const RgbaImage>()> factory;
  std::size_t reservation{};
  bool canceled{}, started{}, finished{}, reservation_released{};
  std::shared_ptr<const RgbaImage> image;
  std::exception_ptr error;

  void release_reservation() noexcept {
    bool release{};
    { std::lock_guard lock(mutex); if(!reservation_released){reservation_released=true;release=true;} }
    if(release){std::lock_guard lock(state->mutex);assert(state->jobs>0&&state->bytes>=reservation);--state->jobs;state->bytes-=reservation;}
  }
  void run() noexcept {
    std::function<std::shared_ptr<const RgbaImage>()> work;
    { std::lock_guard lock(mutex); if(canceled){finished=true;factory={};work={};}else{started=true;work=std::move(factory);} }
    if(!work){release_reservation();return;}
    std::shared_ptr<const RgbaImage> result;std::exception_ptr failure;
    try{result=work();if(!result)throw std::runtime_error("Image preparation factory returned no image.");if(result->byte_size()>reservation)throw std::length_error("Image preparation factory exceeded its reserved output budget.");}
    catch(...){result.reset();failure=std::current_exception();}
    bool drop{};
    { std::lock_guard lock(mutex); finished=true;drop=canceled;if(!drop){image=std::move(result);error=std::move(failure);} }
    if(drop){result.reset();failure={};work={};release_reservation();}
  }
};

ImagePreparationQueue::Ticket::Ticket(std::shared_ptr<Cell> cell,std::thread::id owner):cell_(std::move(cell)),owner_(owner){}
ImagePreparationQueue::Ticket::~Ticket(){cancel();}
ImagePreparationQueue::Ticket::Ticket(Ticket &&other)noexcept:cell_(std::move(other.cell_)),owner_(other.owner_){}
ImagePreparationQueue::Ticket &ImagePreparationQueue::Ticket::operator=(Ticket &&other)noexcept{if(this!=&other){cancel();cell_=std::move(other.cell_);owner_=other.owner_;}return *this;}
bool ImagePreparationQueue::Ticket::ready()const noexcept{if(!cell_)return false;std::lock_guard lock(cell_->mutex);return cell_->finished&&!cell_->canceled;}
void ImagePreparationQueue::Ticket::cancel()noexcept{if(!cell_)return;auto cell=std::move(cell_);bool release{};{std::lock_guard lock(cell->mutex);cell->canceled=true;if(!cell->started)cell->factory={};if(cell->finished){cell->image.reset();cell->error={};cell->factory={};release=true;}}if(release)cell->release_reservation();}
std::shared_ptr<const RgbaImage> ImagePreparationQueue::Ticket::take(){
  if(std::this_thread::get_id()!=owner_)throw std::logic_error("Image preparation tickets may only be taken by their submitting thread.");
  if(!cell_)throw std::logic_error("Image preparation ticket is no longer available.");
  auto cell=cell_;std::shared_ptr<const RgbaImage> image;std::exception_ptr error;
  {std::lock_guard lock(cell->mutex);if(!cell->finished||cell->canceled)throw std::logic_error("Image preparation ticket is not ready.");image=std::move(cell->image);error=cell->error;}
  cell_.reset();
  cell->release_reservation();if(error)std::rethrow_exception(error);return image;
}

ImagePreparationQueue::ImagePreparationQueue(std::size_t maximum_jobs,std::size_t maximum_bytes):owner_(std::this_thread::get_id()),state_(std::make_shared<SharedState>()),jobs_(std::make_unique<EngineJobs>()){
  if(!maximum_jobs||!maximum_bytes)throw std::invalid_argument("Image preparation limits must be nonzero.");state_->maximum_jobs=maximum_jobs;state_->maximum_bytes=maximum_bytes;
}
ImagePreparationQueue::~ImagePreparationQueue()=default;
void ImagePreparationQueue::require_owner()const{if(std::this_thread::get_id()!=owner_)throw std::logic_error("Image preparation queue may only be used by its creating thread.");}
std::optional<ImagePreparationQueue::Ticket> ImagePreparationQueue::submit(std::size_t reservation,std::function<std::shared_ptr<const RgbaImage>()> factory){
  require_owner();if(!factory)throw std::invalid_argument("Image preparation requires a factory.");if(!reservation||reservation>state_->maximum_bytes)throw std::length_error("Image preparation reservation exceeds the configured output budget.");
  auto cell=std::make_shared<Ticket::Cell>();cell->state=state_;cell->factory=std::move(factory);cell->reservation=reservation;
  bool admitted{};
  try{
    {std::lock_guard lock(state_->mutex);if(state_->jobs>=state_->maximum_jobs||state_->bytes>state_->maximum_bytes-reservation)return std::nullopt;++state_->jobs;state_->bytes+=reservation;admitted=true;}
    (void)jobs_->jobs_.submit([cell]{cell->run();});
  }catch(...){if(admitted)cell->release_reservation();throw;}
  return Ticket{std::move(cell),owner_};
}
std::size_t ImagePreparationQueue::outstanding_jobs()const{require_owner();std::lock_guard lock(state_->mutex);return state_->jobs;}
std::size_t ImagePreparationQueue::reserved_bytes()const{require_owner();std::lock_guard lock(state_->mutex);return state_->bytes;}
} // namespace stellar::native_map
