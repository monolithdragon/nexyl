#include <Nexyl/Base/Version.h>

namespace Nexyl {

Version GetEngineVersion() {
    return Version{NX_ENGINE_VERSION_MAJOR, NX_ENGINE_VERSION_MINOR, NX_ENGINE_VERSION_PATCH};
}

} // namespace Nexyl
