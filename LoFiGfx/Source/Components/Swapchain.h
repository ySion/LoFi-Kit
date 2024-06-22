#pragma once

#include "../Helper.h"
#include "Texture.h"

namespace LoFi::Component {

      class Swapchain {
      public:

            NO_COPY_MOVE_CONS(Swapchain);

            //Swapchain(Swapchain&& other) noexcept;

           // Swapchain& operator=(Swapchain&& other) noexcept;

            Swapchain(entt::entity id);

            ~Swapchain();

            [[nodiscard]] VkSurfaceKHR GetSurface() const { return _surface; }

            [[nodiscard]] VkSurfaceKHR* GetSurfacePtr() { return &_surface; }

            [[nodiscard]] VkSwapchainKHR GetSwapchain() const { return _swapchain; }

            [[nodiscard]] VkSwapchainKHR* GetSwapchainPtr() { return &_swapchain; }

            [[nodiscard]] VkSemaphore GetCurrentSemaphore() const { return _imageAvailableSemaphores[_currentFrameIndex]; }

            [[nodiscard]] Texture* GetCurrentRenderTarget() const { return _images.at(_currentImageIndex).get(); }

            [[nodiscard]] uint32_t GetCurrentRenderTargetIndex() const { return _currentImageIndex; }

            VkImageMemoryBarrier2 GenerateCurrentRenderTargetBarrier() const;

            void AcquireNextImage();


      private:

            void CreateOrRecreateSwapChain();

            friend class Window;

      private:

            entt::entity _id{};

            VkSurfaceKHR _surface{};

            VkSwapchainKHR _swapchain{};

            VkSemaphore _imageAvailableSemaphores[3];

            uint8_t _currentFrameIndex{};

            uint32_t _currentImageIndex{};

            std::vector<std::unique_ptr<Texture>> _images{};
      };
}

