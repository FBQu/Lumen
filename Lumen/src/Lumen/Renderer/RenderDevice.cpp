#include "Lumen/Renderer/RenderDevice.h"

#include "Lumen/Core/Log.h"

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <volk.h>

// nvrhi's Vulkan backend uses Vulkan-Hpp's dynamic dispatcher; its storage must be defined in exactly one translation unit.
// It lives here (not in its own file) so the static-library link order always pulls it in.
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <algorithm>
#include <cstring>
#include <mutex>

namespace Lumen {

	namespace {

		class MessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			void message(nvrhi::MessageSeverity severity, const char* text) override
			{
				switch (severity)
				{
					case nvrhi::MessageSeverity::Info:    LM_INFO("nvrhi: {}", text); break;
					case nvrhi::MessageSeverity::Warning: LM_WARN("nvrhi: {}", text); break;
					case nvrhi::MessageSeverity::Error:   LM_ERROR("nvrhi: {}", text); break;
					case nvrhi::MessageSeverity::Fatal:   LM_FATAL("nvrhi: {}", text); break;
				}
			}
		};

		MessageCallback s_MessageCallback;

		// Value-initializes a Vulkan struct and sets its sType/pNext (avoids partial brace-initialization).
		template<typename T>
		T MakeVk(VkStructureType type, const void* next = nullptr)
		{
			T value{};
			value.sType = type;
			value.pNext = const_cast<void*>(next);
			return value;
		}

		int ScoreDevice(const VkPhysicalDeviceProperties& properties)
		{
			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 4;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 2;
				case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 1;
				default:                                     return 0;
			}
		}

		bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
		{
			return std::any_of(list.begin(), list.end(), [name](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
		}

	}

	struct RenderDevice::Vulkan
	{
		VkInstance Instance = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		std::vector<std::string> InstanceExtensions;
		std::vector<std::string> DeviceExtensions;
	};

	Ref<RenderDevice> RenderDevice::Create(const Desc& desc)
	{
		static std::mutex s_Mutex;
		static std::weak_ptr<RenderDevice> s_Instance;
		std::lock_guard lock(s_Mutex);
		if (Ref<RenderDevice> existing = s_Instance.lock())
			return existing;
		Ref<RenderDevice> created = CreateNew(desc);
		s_Instance = created;
		return created;
	}

	Ref<RenderDevice> RenderDevice::CreateNew(const Desc& desc)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			LM_ERROR("Vulkan loader not found; no renderer available");
			return nullptr;
		}

		Ref<RenderDevice> self(new RenderDevice());
		self->m_Vulkan = CreateScope<Vulkan>();
		Vulkan& vk = *self->m_Vulkan;

		// --- Instance -------------------------------------------------------------------------------------------------
		VkApplicationInfo appInfo = MakeVk<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
		appInfo.pApplicationName = "Lumen";
		appInfo.pEngineName = "Lumen";
		appInfo.apiVersion = VK_API_VERSION_1_3;

		uint32_t count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> availableInstanceExtensions(count);
		vkEnumerateInstanceExtensionProperties(nullptr, &count, availableInstanceExtensions.data());
		if (HasExtension(availableInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			vk.InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		std::vector<const char*> layers;
		if (desc.EnableValidation)
		{
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> available(count);
			vkEnumerateInstanceLayerProperties(&count, available.data());
			for (const VkLayerProperties& layer : available)
				if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
					layers.push_back("VK_LAYER_KHRONOS_validation");
		}

		std::vector<const char*> instanceExtensionNames;
		for (const std::string& name : vk.InstanceExtensions)
			instanceExtensionNames.push_back(name.c_str());

		VkInstanceCreateInfo instanceInfo = MakeVk<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
		instanceInfo.pApplicationInfo = &appInfo;
		instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensionNames.size());
		instanceInfo.ppEnabledExtensionNames = instanceExtensionNames.data();
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		instanceInfo.ppEnabledLayerNames = layers.data();
		if (VkResult result = vkCreateInstance(&instanceInfo, nullptr, &vk.Instance); result != VK_SUCCESS)
		{
			LM_ERROR("vkCreateInstance failed: {}", nvrhi::vulkan::resultToString(result));
			return nullptr;
		}
		volkLoadInstance(vk.Instance);

		// --- Physical device ------------------------------------------------------------------------------------------
		vkEnumeratePhysicalDevices(vk.Instance, &count, nullptr);
		std::vector<VkPhysicalDevice> physicalDevices(count);
		vkEnumeratePhysicalDevices(vk.Instance, &count, physicalDevices.data());

		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		uint32_t queueFamily = 0;
		int bestScore = -1;
		VkPhysicalDeviceProperties bestProperties{};
		for (VkPhysicalDevice candidate : physicalDevices)
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(candidate, &properties);
			if (properties.apiVersion < VK_API_VERSION_1_3)
				continue;

			uint32_t familyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

			for (uint32_t i = 0; i < familyCount; i++)
			{
				const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
				if ((families[i].queueFlags & required) == required && ScoreDevice(properties) > bestScore)
				{
					bestScore = ScoreDevice(properties);
					physicalDevice = candidate;
					queueFamily = i;
					bestProperties = properties;
					break;
				}
			}
		}
		if (physicalDevice == VK_NULL_HANDLE)
		{
			LM_ERROR("No Vulkan 1.3 device with graphics and compute queues found");
			return nullptr;
		}
		self->m_DeviceName = bestProperties.deviceName;

		// --- Logical device -------------------------------------------------------------------------------------------
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> availableDeviceExtensions(count);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, availableDeviceExtensions.data());
		for (const char* wanted : { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_EXTENSION_NAME })
			if (HasExtension(availableDeviceExtensions, wanted))
				vk.DeviceExtensions.push_back(wanted);

		std::vector<const char*> deviceExtensionNames;
		for (const std::string& name : vk.DeviceExtensions)
			deviceExtensionNames.push_back(name.c_str());

		// Enable every feature the device supports (except robust buffer access, which costs performance).
		VkPhysicalDeviceVulkan13Features features13 = MakeVk<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
		VkPhysicalDeviceVulkan12Features features12 = MakeVk<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13);
		VkPhysicalDeviceVulkan11Features features11 = MakeVk<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12);
		VkPhysicalDeviceFeatures2 features2 = MakeVk<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features11);
		vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
		features2.features.robustBufferAccess = VK_FALSE;

		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = MakeVk<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		queueInfo.queueFamilyIndex = queueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;

		VkDeviceCreateInfo deviceInfo = MakeVk<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features2);
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
		deviceInfo.ppEnabledExtensionNames = deviceExtensionNames.data();
		if (VkResult result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &vk.Device); result != VK_SUCCESS)
		{
			LM_ERROR("vkCreateDevice failed: {}", nvrhi::vulkan::resultToString(result));
			return nullptr;
		}

		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(vk.Device, queueFamily, 0, &queue);

		// --- nvrhi ----------------------------------------------------------------------------------------------------
		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &s_MessageCallback;
		deviceDesc.instance = vk.Instance;
		deviceDesc.physicalDevice = physicalDevice;
		deviceDesc.device = vk.Device;
		deviceDesc.graphicsQueue = queue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(queueFamily);
		deviceDesc.instanceExtensions = instanceExtensionNames.data();
		deviceDesc.numInstanceExtensions = instanceExtensionNames.size();
		deviceDesc.deviceExtensions = deviceExtensionNames.data();
		deviceDesc.numDeviceExtensions = deviceExtensionNames.size();
		deviceDesc.bufferDeviceAddressSupported = features12.bufferDeviceAddress == VK_TRUE;

		// A statically linked nvrhi leaves dispatcher setup to the application. The dispatcher is process-global, so only
		// one RenderDevice should be alive at a time.
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk.Instance, vkGetInstanceProcAddr, vk.Device, vkGetDeviceProcAddr);

		self->m_NvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
		if (!self->m_NvrhiDevice)
		{
			LM_ERROR("nvrhi failed to create a Vulkan device");
			return nullptr;
		}
		if (desc.EnableValidation)
			self->m_NvrhiDevice = nvrhi::validation::createValidationLayer(self->m_NvrhiDevice);

		LM_INFO("Render device: {}", self->m_DeviceName);
		return self;
	}

	RenderDevice::~RenderDevice()
	{
		if (m_NvrhiDevice)
		{
			m_NvrhiDevice->waitForIdle();
			m_NvrhiDevice->runGarbageCollection();
			m_NvrhiDevice = nullptr; // releases nvrhi's Vulkan resources before the device goes away
		}
		if (m_Vulkan)
		{
			if (m_Vulkan->Device != VK_NULL_HANDLE)
				vkDestroyDevice(m_Vulkan->Device, nullptr);
			if (m_Vulkan->Instance != VK_NULL_HANDLE)
				vkDestroyInstance(m_Vulkan->Instance, nullptr);
		}
	}

	nvrhi::TextureHandle RenderDevice::CreateRenderTarget(uint32_t width, uint32_t height, nvrhi::Format format, const char* name)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = format;
		desc.isRenderTarget = true;
		desc.isShaderResource = true;
		desc.initialState = nvrhi::ResourceStates::RenderTarget;
		desc.keepInitialState = true;
		desc.debugName = name;
		return m_NvrhiDevice->createTexture(desc);
	}

	ImageData RenderDevice::ReadTexture(nvrhi::ITexture* texture)
	{
		const nvrhi::TextureDesc& desc = texture->getDesc();
		const nvrhi::FormatInfo& format = nvrhi::getFormatInfo(desc.format);

		nvrhi::TextureDesc stagingDesc = desc;
		stagingDesc.isRenderTarget = false;
		stagingDesc.isShaderResource = false;
		stagingDesc.debugName = "ReadbackStaging";
		nvrhi::StagingTextureHandle staging = m_NvrhiDevice->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);

		ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			commandList->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
		});

		ImageData image;
		image.Width = desc.width;
		image.Height = desc.height;
		image.BytesPerPixel = format.bytesPerBlock;
		image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * image.BytesPerPixel);

		size_t rowPitch = 0;
		const uint8_t* mapped = static_cast<const uint8_t*>(m_NvrhiDevice->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		LM_ASSERT(mapped != nullptr, "failed to map staging texture");
		const size_t rowBytes = static_cast<size_t>(image.Width) * image.BytesPerPixel;
		for (uint32_t y = 0; y < image.Height; y++)
			std::memcpy(&image.Pixels[y * rowBytes], mapped + y * rowPitch, rowBytes);
		m_NvrhiDevice->unmapStagingTexture(staging);
		return image;
	}

}
