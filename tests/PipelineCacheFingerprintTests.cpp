#include "graphics/host_gpu/renderer/pipeline/pipelineCacheFingerprint.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using Libs::Graphics::MakePipelineSemanticFingerprint;
using Libs::Graphics::ShaderType;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "PipelineCacheFingerprintTests: failed: %s\n", message);
		std::abort();
	}
}

uint64_t Fingerprint(uint64_t spirv_hash = 0x1111, uint64_t specialization_hash = 0x2222,
                     uint64_t layout_hash = 0x3333, uint64_t pipeline_flags = 0x4444) {
	return MakePipelineSemanticFingerprint(ShaderType::Compute, 32, spirv_hash, 128,
	                                       specialization_hash, layout_hash, pipeline_flags, 16);
}

void TestStableIdentity() {
	Check(Fingerprint() == Fingerprint(), "identical semantic inputs changed fingerprint");
}

void TestCreationInputsAffectIdentity() {
	const auto baseline = Fingerprint();
	Check(Fingerprint(0x1112) != baseline, "SPIR-V identity is not part of fingerprint");
	Check(Fingerprint(0x1111, 0x2223) != baseline,
	      "specialization identity is not part of fingerprint");
	Check(Fingerprint(0x1111, 0x2222, 0x3334) != baseline,
	      "descriptor-layout identity is not part of fingerprint");
	Check(Fingerprint(0x1111, 0x2222, 0x3333, 0x4445) != baseline,
	      "pipeline flags are not part of fingerprint");
}

} // namespace

int main() {
	TestStableIdentity();
	TestCreationInputsAffectIdentity();
	return 0;
}
