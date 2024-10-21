#include "vkterminal.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

VkTerminal::VkTerminal() {
	m_console = GetStdHandle(STD_OUTPUT_HANDLE);
	m_windowSize = getConsoleSize(m_console);
	vk::ApplicationInfo appInfo({}, {}, {}, {}, vk::ApiVersion13);
	m_instance = vk::createInstance({ {}, &appInfo, m_layers });

	vk::PhysicalDevice physicalDevice;
	for (auto device : m_instance.enumeratePhysicalDevices()) {
		auto featuresChain = device.getFeatures2<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan12Features,
			vk::PhysicalDeviceVulkan13Features
		>();

		auto features = featuresChain.get<vk::PhysicalDeviceFeatures2>();
		auto features12 = featuresChain.get<vk::PhysicalDeviceVulkan12Features>();
		auto features13 = featuresChain.get<vk::PhysicalDeviceVulkan13Features>();

		if (features12.bufferDeviceAddress && features12.scalarBlockLayout && features13.dynamicRendering) {
			physicalDevice = device;
			break;
		}
	}

	m_memProps = physicalDevice.getMemoryProperties();

	for (auto [idx, queueFamily] : std::views::enumerate(physicalDevice.getQueueFamilyProperties())) {
		if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
			m_queueFamily = idx;
			break;
		}
	}
	float priority = 1.0f;
	vk::DeviceQueueCreateInfo queueInfo({}, m_queueFamily, 1, &priority);

	vk::StructureChain<
		vk::DeviceCreateInfo,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features
	> featuresChain({ {}, queueInfo }, {}, {});
	featuresChain.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress = true;
	featuresChain.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout = true;
	featuresChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = true;

	m_device = physicalDevice.createDevice(featuresChain.get<vk::DeviceCreateInfo>());
	m_queue = m_device.getQueue(m_queueFamily, 0);
	m_commandPool = m_device.createCommandPool({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_queueFamily });
	m_commandBuffer = m_device.allocateCommandBuffers({ m_commandPool, vk::CommandBufferLevel::ePrimary, 1 }).front();
	m_fence = m_device.createFence({});

	vk::PushConstantRange constantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants));
	m_graphicsLayout = m_device.createPipelineLayout({ {}, {}, constantRange });

	auto vsCode = getShaderSource("shaders/basic.vert.spv");
	auto fsCode = getShaderSource("shaders/basic.frag.spv");
	vk::ShaderModule vertexShader = m_device.createShaderModule({ {}, vsCode });
	vk::ShaderModule fragmentShader = m_device.createShaderModule({ {}, fsCode });
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{
		vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader, "main"),
		vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader, "main")
	};

	vk::Format colorAttachmentFormat = vk::Format::eR8Uint;
	std::vector<vk::DynamicState> dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };

	vk::PipelineVertexInputStateCreateInfo vertexInfo;
	vk::PipelineInputAssemblyStateCreateInfo assemblyInfo({}, vk::PrimitiveTopology::eTriangleList);
	vk::PipelineViewportStateCreateInfo viewportInfo({}, 1, {}, 1);
	vk::PipelineRasterizationStateCreateInfo rasterizerInfo({}, {}, {}, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, {}, {}, {}, {}, 1.0f);
	vk::PipelineMultisampleStateCreateInfo multisampleInfo({}, vk::SampleCountFlagBits::e1);
	vk::PipelineDepthStencilStateCreateInfo depthInfo({}, true, true, vk::CompareOp::eLess, false, false, {}, {}, 0.0f, 1.0f);
	vk::PipelineColorBlendAttachmentState attachmentState({}, {}, {}, {}, {}, {}, {}, vk::ColorComponentFlagBits::eR);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo({}, {}, {}, attachmentState);
	vk::PipelineDynamicStateCreateInfo dynamicInfo({}, dynamicStates);
	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineInfo{
		{ {}, shaderStages, &vertexInfo, &assemblyInfo, {}, &viewportInfo, &rasterizerInfo, &multisampleInfo, &depthInfo, &colorBlendInfo, &dynamicInfo, m_graphicsLayout },
		{ {}, colorAttachmentFormat, vk::Format::eD32Sfloat }
	};
	auto [_, pipeline] = m_device.createGraphicsPipeline({}, pipelineInfo.get<vk::GraphicsPipelineCreateInfo>());
	m_graphicsPipeline = pipeline;

	m_device.destroyShaderModule(vertexShader);
	m_device.destroyShaderModule(fragmentShader);

	std::vector<Vertex> vertices;

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "models/teapot.obj");
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			vertices.push_back({
				{ attrib.vertices[3 * index.vertex_index + 0], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2] },
				{ attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1], attrib.normals[3 * index.normal_index + 2] }
			});
		}
	}

	m_vertexBuffer = createHostVisibleBuffer(vertices.size() * sizeof(Vertex), vk::BufferUsageFlagBits::eShaderDeviceAddress, m_queueFamily, true);
	m_vertexBufferDeviceAddress = m_device.getBufferAddress({ m_vertexBuffer.buffer });
	memcpy(m_vertexBuffer.hostPtr, vertices.data(), vertices.size() * sizeof(Vertex));
	
	m_numVertices = vertices.size();

	m_colorTarget = createRenderTarget(m_windowSize, vk::Format::eR8Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, m_queueFamily);
	m_depthTarget = createRenderTarget(m_windowSize, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment, m_queueFamily);
	m_readbackBuffer = createHostVisibleBuffer(m_windowSize.x * m_windowSize.y, vk::BufferUsageFlagBits::eTransferDst, m_queueFamily);

	m_pos = glm::vec3(-1.5f, -1.5f, -3.0f);
	m_rot = glm::vec3(0.5f, 0.5f, 1.0f);
	m_dt = 0.0f;
}

VkTerminal::~VkTerminal() {
	destroyHostVisibleBuffer(m_readbackBuffer);
	destroyRenderTarget(m_depthTarget);
	destroyRenderTarget(m_colorTarget);

	m_device.destroyPipeline(m_graphicsPipeline);
	m_device.destroyPipelineLayout(m_graphicsLayout);
	m_device.destroyFence(m_fence);
	m_device.destroyCommandPool(m_commandPool);
	m_device.destroy();
	m_instance.destroy();
}

void VkTerminal::run() {
	while (true) {
		auto now = std::chrono::steady_clock::now();
		m_dt = std::chrono::duration<float>(now - m_lastFrame).count();
		m_lastFrame = now;

		pollInputs();
		if (updateSurfaceSize()) {
			render();
		}
	}
}

void VkTerminal::pollInputs() {
	

	if (GetAsyncKeyState('W')) {
		m_pos += m_rot * m_speed * m_dt;
	}
	if (GetAsyncKeyState('S')) {
		m_pos -= m_rot * m_speed * m_dt;
	}
	if (GetAsyncKeyState('D')) {
		m_pos += glm::normalize(glm::cross(m_rot, glm::vec3(0.0f, 1.0f, 0.0f))) * m_speed * m_dt;
	}
	if (GetAsyncKeyState('A')) {
		m_pos -= glm::normalize(glm::cross(m_rot, glm::vec3(0.0f, 1.0f, 0.0f))) * m_speed * m_dt;
	}

	if (GetAsyncKeyState(VK_CONTROL)) {
		m_pos += glm::vec3(0.0f, 1.0f, 0.0f) * m_speed * m_dt;
	}
	if (GetAsyncKeyState(VK_SPACE)) {
		m_pos -= glm::vec3(0.0f, 1.0f, 0.0f) * m_speed * m_dt;
	}

	if (GetAsyncKeyState('I')) {
		m_rot = glm::rotate(m_rot, glm::radians(-m_rotSpeed) * m_dt, glm::normalize(glm::cross(m_rot, glm::vec3(0.0f, 1.0f, 0.0f))));
	}
	if (GetAsyncKeyState('K')) {
		m_rot = glm::rotate(m_rot, glm::radians(m_rotSpeed) * m_dt, glm::normalize(glm::cross(m_rot, glm::vec3(0.0f, 1.0f, 0.0f))));
	}
	if (GetAsyncKeyState('L')) {
		m_rot = glm::rotate(m_rot, glm::radians(-m_rotSpeed) * m_dt, glm::vec3(0.0f, 1.0f, 0.0f));
	}
	if (GetAsyncKeyState('J')) {
		m_rot = glm::rotate(m_rot, glm::radians(m_rotSpeed) * m_dt, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

bool VkTerminal::updateSurfaceSize() {
	glm::ivec2 currentSize = getConsoleSize(m_console);
	if (currentSize.x == 0 || currentSize.y == 0) {
		return false;
	}
	else if (currentSize != m_windowSize) {
		m_windowSize = currentSize;

		destroyHostVisibleBuffer(m_readbackBuffer);
		destroyRenderTarget(m_depthTarget);
		destroyRenderTarget(m_colorTarget);

		m_colorTarget = createRenderTarget(m_windowSize, vk::Format::eR8Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, m_queueFamily);
		m_depthTarget = createRenderTarget(m_windowSize, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment, m_queueFamily);
		m_readbackBuffer = createHostVisibleBuffer(m_windowSize.x * m_windowSize.y, vk::BufferUsageFlagBits::eTransferDst, m_queueFamily);
	}
	return true;
}


void VkTerminal::render() {
	static float degrees = 0.0f;
	degrees += 100.0f * m_dt;

	while(degrees >= 360.0f) {
		degrees -= 360.0f;
	}

	int x = 0;
	x++;

	m_commandBuffer.reset();

	m_commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	m_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);
	m_commandBuffer.setViewport(0, vk::Viewport(0, 0, m_windowSize.x, m_windowSize.y, 0, 1));
	m_commandBuffer.setScissor(0, vk::Rect2D({ 0, 0 }, { static_cast<uint32_t>(m_windowSize.x), static_cast<uint32_t>(m_windowSize.y) }));

	glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::scale(model, glm::vec3(0.1f));

	glm::mat4 cam =
		glm::perspective(glm::radians(45.0f), static_cast<float>(m_windowSize.x) / static_cast<float>(m_windowSize.y), 0.1f, 100.0f)
		* glm::lookAt(m_pos, m_pos + m_rot, glm::vec3(0.0f, 1.0f, 0.0f));
	vk::ArrayProxy<const PushConstants> pcs{ { model, cam, m_vertexBufferDeviceAddress } };
	m_commandBuffer.pushConstants(m_graphicsLayout, vk::ShaderStageFlagBits::eVertex, 0, pcs);

	m_commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		vk::ImageMemoryBarrier(
			vk::AccessFlagBits::eMemoryRead,
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			{}, {},
			m_colorTarget.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
		)
	);
	m_commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe,
		vk::PipelineStageFlagBits::eEarlyFragmentTests,
		{}, {}, {},
		vk::ImageMemoryBarrier(
			vk::AccessFlagBits::eMemoryRead,
			vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			{}, {},
			m_depthTarget.image,
			{ vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
		)
	);

	vk::RenderingAttachmentInfo colorAttachment(
		m_colorTarget.view,
		vk::ImageLayout::eColorAttachmentOptimal,
		{}, {}, {},
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		{ { 32u, 0u, 0u, 0u } }
	);
	vk::RenderingAttachmentInfo depthAttachment(
		m_depthTarget.view,
		vk::ImageLayout::eDepthAttachmentOptimal,
		{}, {}, {},
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eDontCare,
		{ { 1.0f } }
	);
	m_commandBuffer.beginRendering({ {}, { { 0, 0 }, { static_cast<uint32_t>(m_windowSize.x), static_cast<uint32_t>(m_windowSize.y) } }, 1, {}, colorAttachment, &depthAttachment });
	m_commandBuffer.draw(m_numVertices, 1, 0, 0);
	m_commandBuffer.endRendering();

	m_commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier(
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			{}, {},
			m_colorTarget.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
		)
	);

	m_commandBuffer.copyImageToBuffer(
		m_colorTarget.image,
		vk::ImageLayout::eTransferSrcOptimal,
		m_readbackBuffer.buffer,
		vk::BufferImageCopy(0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, {}, { static_cast<uint32_t>(m_windowSize.x), static_cast<uint32_t>(m_windowSize.y), 1 })
	);

	m_commandBuffer.end();

	vk::SubmitInfo submit({}, {}, m_commandBuffer, {});
	m_queue.submit(submit, m_fence);
	m_device.waitForFences(m_fence, true, std::numeric_limits<uint64_t>::max());
	m_device.resetFences(m_fence);

	SetConsoleCursorPosition(m_console, { 0, 0 });
	WriteConsoleA(m_console, m_readbackBuffer.hostPtr, m_windowSize.x * m_windowSize.y, nullptr, nullptr);
}

glm::ivec2 VkTerminal::getConsoleSize(HANDLE console) {
	CONSOLE_SCREEN_BUFFER_INFO info;
	GetConsoleScreenBufferInfo(console, &info);

	return glm::ivec2{
		info.srWindow.Right - info.srWindow.Left + 1,
		info.srWindow.Bottom - info.srWindow.Top + 1
	};
}

std::vector<uint32_t> VkTerminal::getShaderSource(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	std::vector<uint32_t> ret(file.tellg() / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(ret.data()), ret.size() * sizeof(uint32_t));
	return ret;
}

uint32_t VkTerminal::getMemoryIndex(vk::MemoryPropertyFlags flags, uint32_t mask) {
	for (uint32_t idx = 0; idx < m_memProps.memoryTypeCount; idx++) {
		if (((1 << idx) & mask) && (m_memProps.memoryTypes[idx].propertyFlags & flags) == flags) {
			return idx;
		}
	}
}

VkTerminal::RenderTarget VkTerminal::createRenderTarget(glm::ivec2 dimensions, vk::Format format, vk::ImageUsageFlags usage, uint32_t queueFamily) {
	RenderTarget ret;

	ret.image = m_device.createImage({ {},
		vk::ImageType::e2D,
		format,
		{ static_cast<uint32_t>(dimensions.x), static_cast<uint32_t>(dimensions.y), 1 },
		1,
		1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		usage,
		{},
		queueFamily,
	});

	vk::MemoryRequirements mrq = m_device.getImageMemoryRequirements(ret.image);
	ret.memory = m_device.allocateMemory({
		mrq.size,
		getMemoryIndex(vk::MemoryPropertyFlagBits::eDeviceLocal, mrq.memoryTypeBits)
	});
	m_device.bindImageMemory(ret.image, ret.memory, 0);

	vk::ImageAspectFlags flags = (usage & vk::ImageUsageFlagBits::eColorAttachment) ? vk::ImageAspectFlagBits::eColor : vk::ImageAspectFlagBits::eDepth;
	ret.view = m_device.createImageView({ {}, ret.image, vk::ImageViewType::e2D, format, {}, { flags, 0, 1, 0, 1 } });

	return ret;
}

void VkTerminal::destroyRenderTarget(RenderTarget target) {
	m_device.destroyImageView(target.view);
	m_device.destroyImage(target.image);
	m_device.freeMemory(target.memory);
}

VkTerminal::HostVisibleBuffer VkTerminal::createHostVisibleBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, uint32_t queueFamily, bool deviceLocal) {
	HostVisibleBuffer ret;
	ret.buffer = m_device.createBuffer({ {}, size, usage, {}, queueFamily });

	vk::MemoryPropertyFlags flags = vk::MemoryPropertyFlagBits::eHostVisible;
	if (deviceLocal) {
		flags |= vk::MemoryPropertyFlagBits::eDeviceLocal;
	}

	vk::MemoryRequirements mrq = m_device.getBufferMemoryRequirements(ret.buffer);

	if(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
		vk::StructureChain<vk::MemoryAllocateInfo, vk::MemoryAllocateFlagsInfo> info{
			{ mrq.size, getMemoryIndex(flags, mrq.memoryTypeBits)},
			{ vk::MemoryAllocateFlagBits::eDeviceAddress }
		};
		ret.memory = m_device.allocateMemory(info.get<vk::MemoryAllocateInfo>());
	}
	else {
		ret.memory = m_device.allocateMemory({ mrq.size, getMemoryIndex(flags, mrq.memoryTypeBits)});
	}

	m_device.bindBufferMemory(ret.buffer, ret.memory, 0);
	ret.hostPtr = static_cast<char*>(m_device.mapMemory(ret.memory, 0, vk::WholeSize));

	return ret;
}

void VkTerminal::destroyHostVisibleBuffer(HostVisibleBuffer buffer) {
	m_device.unmapMemory(buffer.memory);
	m_device.destroyBuffer(buffer.buffer);
	m_device.freeMemory(buffer.memory);
}