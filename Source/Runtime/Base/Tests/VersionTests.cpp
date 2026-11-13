#include <Nexyl/Base/Version.h>

// TODO(phase-1): rewrite on the engine's Test module once it exists.
int main() {
    const Nexyl::Version version = Nexyl::GetEngineVersion();
    const bool isExpected = version.major == NX_ENGINE_VERSION_MAJOR && version.minor == NX_ENGINE_VERSION_MINOR &&
                            version.patch == NX_ENGINE_VERSION_PATCH;
    return isExpected ? 0 : 1;
}
