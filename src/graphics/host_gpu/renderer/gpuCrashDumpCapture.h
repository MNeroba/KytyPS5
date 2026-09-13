#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHDUMPCAPTURE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHDUMPCAPTURE_H_

#include "common/common.h"

#include <atomic>
#include <filesystem>
#include <mutex>

namespace Libs::Graphics {

// Optional NVIDIA Nsight Aftermath crash-dump capture.  The implementation is
// deliberately dynamically loaded so normal builds do not depend on the
// proprietary SDK or DLL.  It has no effect unless explicitly enabled.
class GpuCrashDumpCapture {
public:
	GpuCrashDumpCapture() = default;
	~GpuCrashDumpCapture();
	KYTY_CLASS_NO_COPY(GpuCrashDumpCapture);

	[[nodiscard]] bool Enable(const std::filesystem::path& output_folder);
	void               Disable();
	[[nodiscard]] bool Enabled() const noexcept { return m_enabled; }

private:
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	using CrashDumpCallback      = void(__cdecl*)(const void*, uint32_t, void*);
	using ShaderDebugCallback    = void(__cdecl*)(const void*, uint32_t, void*);
	using AddDescriptionCallback = void(__cdecl*)(uint32_t, const char*);
	using DescriptionCallback    = void(__cdecl*)(AddDescriptionCallback, void*);
	using ResolveMarkerCallback  = void(__cdecl*)(const void*, uint32_t, void*, void*);
	using EnableFunction  = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, CrashDumpCallback,
	                                           ShaderDebugCallback, DescriptionCallback,
	                                           ResolveMarkerCallback, void*);
	using DisableFunction = uint32_t(__cdecl*)();

	static void __cdecl CrashDumpThunk(const void* data, uint32_t size, void* user_data);
	static void __cdecl ShaderDebugThunk(const void* data, uint32_t size, void* user_data);
	void                WriteArtifact(const char* prefix, const void* data, uint32_t size);

	void*           m_module  = nullptr;
	EnableFunction  m_enable  = nullptr;
	DisableFunction m_disable = nullptr;
#endif

	std::filesystem::path m_output_folder;
	std::mutex            m_mutex;
	std::atomic<uint32_t> m_dump_index {0};
	bool                  m_enabled = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHDUMPCAPTURE_H_
