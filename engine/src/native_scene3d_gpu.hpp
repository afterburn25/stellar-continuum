#pragma once
#include <stellar/engine/native_scene3d.hpp>
struct SDL_GPUDevice;
struct SDL_Renderer;
namespace stellar::native_map {
// Window-thread only. Prepares distinct offscreen targets before SDL's 2D
// command buffer, then composites them in the original command order.
class Scene3DRenderer final {
 public:
  Scene3DRenderer(SDL_GPUDevice*,SDL_Renderer*);
  ~Scene3DRenderer();
  void prepare(const DrawList&);
  void composite(const Scene3DView&);
  // Retunes the TextureStreamer byte budget; takes effect next prepare().
  void set_texture_budget(std::uint64_t bytes);
  [[nodiscard]] Scene3DStatistics statistics()const noexcept;
 private:
  struct Storage;std::unique_ptr<Storage> storage_;
};
}
