#pragma once
#include <AYEntity/DeterministicLevel2D.h>
#include <AYResource/IResource.h>
#include <AYResource/IResourceLoader.h>
#include <memory>
#include <string>

namespace ayt::entity {
/** @brief Optional immutable, validated initial-level resource loaded from loose or Pak .dlc bytes.
 * @note Link AYEntity::DeterministicLevelResources and register its loader during startup.
 * Decode uses the existing default-options Level validator and the complete canonical authoring
 * payload. Application registration overrides are validated later by configureDetLevel2D.
 * A successful load publishes a new snapshot. Existing snapshots survive unload/reload, and
 * failed load/reload preserves the previous snapshot. Mutation follows AYResource's owner-thread
 * rules; this object is not a concurrent reload service. sizeInBytes reports cooked input bytes.
 * Loading never owns/configures a Session, World or Scene, imports a checkpoint, or changes a
 * running simulation. Load before constructing its Host recipe; configure the returned level
 * through the existing public facade. Do not perform resource/file reads in a Sim callback.
 */
/// @brief Immutable cooked initial-level snapshot for opt-in ResourceManager loading.
/// @note Acquire before simulation configuration; this resource never owns or mutates a Session.
class DetLevelResource2D final : public resource::IResource {
public:
    static constexpr const char* TYPE="DeterministicLevel2D";
    DetLevelResource2D();
    bool load(const std::string& path) override;
    bool reload(const std::string& path) override;
    bool unload() override;
    std::size_t sizeInBytes() const override { return bytes_; }
    /// @brief Atomically decode bounded cooked memory; failure retains the previous level.
    bool loadFromBinary(const void* data,std::size_t bytes);
    /// @brief Retain initial content independently of ResourceManager cache and resource lifetime.
    std::shared_ptr<const DetLevel2D> level() const { return level_; }
    /// @brief Last load diagnostic; the cooked checksum and content hash are not authentication.
    const std::string& lastError() const { return error_; }
private:
    std::shared_ptr<const DetLevel2D> level_;
    std::size_t bytes_=0;
    std::string error_;
};

/// @brief Optional ResourceManager loader; .dlt authoring files are not runtime resources.
class DetLevelLoader2D final : public resource::IResourceLoader {
public:
    bool canLoad(const std::string& path) const override;
    const char* getResourceType() const override { return DetLevelResource2D::TYPE; }
    std::shared_ptr<resource::IResource> load(const std::string& path) override;
    std::shared_ptr<resource::IResource> loadFromBinary(const void* data,std::size_t bytes) override;
    std::shared_ptr<resource::IResource> loadAsync(const std::string& path,
        std::function<void(std::shared_ptr<resource::IResource>)> callback={}) override;
};

/** @details Call after initializeLoaders and before concurrent loads. Registration is idempotent
 * for this loader and rejects a suffix or type already owned by another loader. Bootstrap may
 * register .dlc metadata for optional cook builds; metadata alone does not install this loader.
 * No default AYResource dependency on the deterministic kernel is introduced.
 */
/// @brief Register the optional immutable level loader during quiescent resource startup.
/// @note Returns false with a diagnostic on bootstrap/registry conflict; not a Sim operation.
bool registerDetLevelResources2D(std::string& error);
} // namespace ayt::entity
