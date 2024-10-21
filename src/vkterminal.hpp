#ifndef VK_TERMINAL_H
#define VK_TERMINAL_H

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.hpp>

#include <chrono>
#include <ranges>
#include <fstream>

class VkTerminal {
public:
	VkTerminal();
	~VkTerminal();
	void run();

private:
	struct PushConstants {
		glm::mat4 model;
		glm::mat4 cam;
		vk::DeviceAddress vertexBuffer;
	};

	struct RenderTarget {
		vk::Image image;
		vk::ImageView view;
		vk::DeviceMemory memory;
	};

	struct HostVisibleBuffer {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		char* hostPtr;
	};

	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
	};

	void pollInputs();
	bool updateSurfaceSize();
	void render();

	glm::ivec2 getConsoleSize(HANDLE console);
	std::vector<uint32_t> getShaderSource(const char* path);
	uint32_t getMemoryIndex(vk::MemoryPropertyFlags flags, uint32_t mask);
	RenderTarget createRenderTarget(glm::ivec2 dimensions, vk::Format format, vk::ImageUsageFlags usage, uint32_t queueFamily);
	void destroyRenderTarget(RenderTarget target);
	HostVisibleBuffer createHostVisibleBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, uint32_t queueFamily, bool deviceLocal = false);
	void destroyHostVisibleBuffer(HostVisibleBuffer buffer);
	
	
	
	HANDLE m_console;
	uint32_t m_queueFamily;
	glm::ivec2 m_windowSize;
	vk::Instance m_instance;
	vk::PhysicalDeviceMemoryProperties m_memProps;
	vk::Device m_device;
	vk::Queue m_queue;
	vk::CommandPool m_commandPool;
	vk::CommandBuffer m_commandBuffer;
	vk::Fence m_fence;
	vk::PipelineLayout m_graphicsLayout;
	vk::Pipeline m_graphicsPipeline;
	RenderTarget m_colorTarget;
	RenderTarget m_depthTarget;
	HostVisibleBuffer m_readbackBuffer;
	HostVisibleBuffer m_vertexBuffer;
	vk::DeviceAddress m_vertexBufferDeviceAddress;
	size_t m_numVertices;

	glm::vec3 m_pos;
	glm::vec3 m_rot;
	std::chrono::steady_clock::time_point m_lastFrame;
	float m_dt = 0.0f;

	static constexpr float m_speed = 1.0f;
	static constexpr float m_rotSpeed = 30.0f;


	std::vector<const char*> m_layers{
#ifdef _DEBUG
		"VK_LAYER_KHRONOS_validation"
#endif
	};
};

#endif