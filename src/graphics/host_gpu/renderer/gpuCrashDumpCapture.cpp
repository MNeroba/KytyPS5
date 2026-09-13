#include "graphics/host_gpu/renderer/gpuCrashDumpCapture.h"

#include "common/logging/log.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

namespace Libs::Graphics {

GpuCrashDumpCapture::~GpuCrashDumpCapture() {
	Disable();
}

bool GpuCrashDumpCapture::Enable(const std::filesystem::path& output_folder) {
	if (m_enabled) {
		return true;
	}
	if (output_folder.empty()) {
		LOGF("NVIDIA Aftermath diagnostics requested without an output folder\n");
		return false;
	}

	std::error_code error;
	std::filesystem::create_directories(output_folder, error);
	if (error || !std::filesystem::is_directory(output_folder, error)) {
		LOGF("NVIDIA Aftermath diagnostics could not create output folder: %s\n",
		     output_folder.string().c_str());
		return false;
	}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	const char* configured_dll = std::getenv("KYTY_AFTERMATH_DLL");
	if (configured_dll != nullptr && configured_dll[0] != '\0') {
		m_module = static_cast<void*>(LoadLibraryA(configured_dll));
	}
	if (m_module == nullptr) {
		m_module = static_cast<void*>(LoadLibraryA("GFSDK_Aftermath_Lib.x64.dll"));
	}
	if (m_module == nullptr) {
		LOGF("NVIDIA Aftermath diagnostics requested but GFSDK_Aftermath_Lib.x64.dll is "
		     "unavailable\n");
		return false;
	}

	m_enable = reinterpret_cast<EnableFunction>(
	    GetProcAddress(static_cast<HMODULE>(m_module), "GFSDK_Aftermath_EnableGpuCrashDumps"));
	m_disable = reinterpret_cast<DisableFunction>(
	    GetProcAddress(static_cast<HMODULE>(m_module), "GFSDK_Aftermath_DisableGpuCrashDumps"));
	if (m_enable == nullptr || m_disable == nullptr) {
		LOGF("NVIDIA Aftermath diagnostics library is missing the crash-dump entry points\n");
		FreeLibrary(static_cast<HMODULE>(m_module));
		m_module  = nullptr;
		m_enable  = nullptr;
		m_disable = nullptr;
		return false;
	}

	m_output_folder = output_folder;
	// GFSDK_Aftermath_Version_API (SDK 2.27), Vulkan watched API, and deferred
	// shader-debug callbacks so only shaders related to a crash are persisted.
	constexpr uint32_t aftermath_api_version      = 0x0000021Bu;
	constexpr uint32_t watched_vulkan             = 0x00000002u;
	constexpr uint32_t defer_debug_info_callbacks = 0x00000001u;
	const uint32_t     result =
	    m_enable(aftermath_api_version, watched_vulkan, defer_debug_info_callbacks, &CrashDumpThunk,
	             &ShaderDebugThunk, nullptr, nullptr, this);
	if (result != 0x1u) { // GFSDK_Aftermath_Result_Success
		LOGF("NVIDIA Aftermath EnableGpuCrashDumps failed: 0x%08x\n", result);
		FreeLibrary(static_cast<HMODULE>(m_module));
		m_module  = nullptr;
		m_enable  = nullptr;
		m_disable = nullptr;
		m_output_folder.clear();
		return false;
	}

	m_enabled = true;
	LOGF("NVIDIA Aftermath GPU crash dumps enabled: output=%s\n", m_output_folder.string().c_str());
	return true;
#else
	(void)output_folder;
	LOGF("NVIDIA Aftermath GPU crash dumps are only available on Windows\n");
	return false;
#endif
}

void GpuCrashDumpCapture::Disable() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	if (m_enabled && m_disable != nullptr) {
		(void)m_disable();
	}
	if (m_module != nullptr) {
		FreeLibrary(static_cast<HMODULE>(m_module));
	}
	m_module  = nullptr;
	m_enable  = nullptr;
	m_disable = nullptr;
#endif
	m_enabled = false;
	m_output_folder.clear();
}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS

void __cdecl GpuCrashDumpCapture::CrashDumpThunk(const void* data, uint32_t size, void* user_data) {
	if (user_data != nullptr) {
		static_cast<GpuCrashDumpCapture*>(user_data)->WriteArtifact("gpu-crash-dump", data, size);
	}
}

void __cdecl GpuCrashDumpCapture::ShaderDebugThunk(const void* data, uint32_t size,
                                                   void* user_data) {
	if (user_data != nullptr) {
		static_cast<GpuCrashDumpCapture*>(user_data)->WriteArtifact("shader-debug-info", data,
		                                                            size);
	}
}

void GpuCrashDumpCapture::WriteArtifact(const char* prefix, const void* data, uint32_t size) {
	if (prefix == nullptr || data == nullptr || size == 0 || m_output_folder.empty()) {
		return;
	}

	std::lock_guard lock(m_mutex);
	const uint32_t  index        = m_dump_index.fetch_add(1, std::memory_order_relaxed);
	char            filename[96] = {};
	std::snprintf(filename, sizeof(filename), "%s-%04u.bin", prefix, index);
	std::ofstream stream(m_output_folder / filename, std::ios::binary | std::ios::trunc);
	if (!stream) {
		LOGF("NVIDIA Aftermath could not write %s\n",
		     (m_output_folder / filename).string().c_str());
		return;
	}
	stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	stream.flush();
	LOGF("NVIDIA Aftermath wrote %s bytes=%u\n", (m_output_folder / filename).string().c_str(),
	     size);
}

#endif

} // namespace Libs::Graphics
