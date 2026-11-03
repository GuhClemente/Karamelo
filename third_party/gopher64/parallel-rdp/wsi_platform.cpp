#include "wsi_platform.hpp"
#include <SDL3/SDL_vulkan.h>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif

VkSurfaceKHR SDL_WSIPlatform::create_surface(VkInstance instance,
                                             VkPhysicalDevice gpu) {
#if defined(__APPLE__)
  __block VkSurfaceKHR surface = nullptr;
  void (^create_block)(void) = ^{
    bool result = SDL_Vulkan_CreateSurface(window, instance, NULL, &surface);
    if (!result) {
      printf("[GOPHER64] Error creating surface: %s\n", SDL_GetError());
    }
  };
  if (!pthread_main_np()) {
    dispatch_sync(dispatch_get_main_queue(), create_block);
  } else {
    create_block();
  }
  return surface;
#else
  VkSurfaceKHR surface = nullptr;
  bool result = SDL_Vulkan_CreateSurface(window, instance, NULL, &surface);
  if (result != true) {
    printf("Error creating surface\n");
  }
  return surface;
#endif
}

void SDL_WSIPlatform::destroy_surface(VkInstance instance,
                                      VkSurfaceKHR surface) {
  SDL_Vulkan_DestroySurface(instance, surface, NULL);
}

std::vector<const char *> SDL_WSIPlatform::get_instance_extensions() {
#if defined(__APPLE__)
  __block unsigned int extensionCount = 0;
  __block char const *const *extensions = nullptr;
  void (^ext_block)(void) = ^{
    extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
  };
  if (!pthread_main_np()) {
    dispatch_sync(dispatch_get_main_queue(), ext_block);
  } else {
    ext_block();
  }
#else
  unsigned int extensionCount = 0;
  char const *const *extensions =
      SDL_Vulkan_GetInstanceExtensions(&extensionCount);
#endif
  if (extensions == NULL) {
    printf("Error getting instance extensions\n");
  }

  std::vector<const char *> extensionNames;
  for (unsigned int i = 0; i < extensionCount; ++i) {
    extensionNames.push_back(extensions[i]);
  }
  return extensionNames;
}

uint32_t SDL_WSIPlatform::get_surface_width() {
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  return w;
}

uint32_t SDL_WSIPlatform::get_surface_height() {
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  return h;
}

bool SDL_WSIPlatform::alive(Vulkan::WSI &wsi) { return true; }

void SDL_WSIPlatform::poll_input() {
#if defined(__APPLE__)
  if (pthread_main_np()) {
    SDL_PumpEvents();
  }
#else
  SDL_PumpEvents();
#endif
}

void SDL_WSIPlatform::poll_input_async(Granite::InputTrackerHandler *handler) {}

void SDL_WSIPlatform::set_window(SDL_Window *_window) { window = _window; }

void SDL_WSIPlatform::do_resize() { resize = true; }
