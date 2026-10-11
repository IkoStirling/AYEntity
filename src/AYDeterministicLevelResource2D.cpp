#include <AYEntity/DeterministicLevelResource2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/ResourceRegistry.h>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace ayt::entity {
DetLevelResource2D::DetLevelResource2D() { _type=TYPE; }
bool DetLevelResource2D::load(const std::string& path) {
    namespace fs=std::filesystem;
    std::error_code ec;
    const auto status=fs::symlink_status(fs::path(path),ec);
    if(ec || !fs::is_regular_file(status) || fs::is_symlink(status)) {
        error_="Cooked level must be an ordinary file";return false;
    }
    if(!asset_format::isCanonicalPath(path,asset_format::Id::DeterministicLevelCooked)) {
        error_="Cooked level requires .dlc";return false;
    }
    const auto size=fs::file_size(path,ec);
    if(ec || size<kDetLevelCooked2DHeaderBytes || size>kDetLevelCooked2DMaxBytes) {
        error_="Cooked level file size is outside the bounded envelope";return false;
    }
    std::ifstream stream(path,std::ios::binary);
    if(!stream) {error_="Cannot open cooked level";return false;}
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(stream.gcount()!=static_cast<std::streamsize>(bytes.size()) || stream.peek()!=std::char_traits<char>::eof() || stream.bad()) {
        error_="Cooked level changed size or could not be read completely";return false;
    }
    std::string publishedPath=path; // Allocate before replacing an existing snapshot.
    if(!loadFromBinary(bytes.data(),bytes.size()))return false;
    _path=std::move(publishedPath);return true;
}
bool DetLevelResource2D::reload(const std::string& path) {return load(path);}
bool DetLevelResource2D::loadFromBinary(const void* data,std::size_t bytes) {
    if(!data || bytes<kDetLevelCooked2DHeaderBytes || bytes>kDetLevelCooked2DMaxBytes) {
        error_="Cooked level memory size is outside the bounded envelope";return false;
    }
    auto decoded=std::make_shared<DetLevel2D>();
    std::string error;
    if(!decodeDetLevelCooked2D({static_cast<const std::uint8_t*>(data),bytes},*decoded,error)) {
        error_=std::move(error);return false;
    }
    level_=std::move(decoded);bytes_=bytes;_loaded=true;_path.clear();error_.clear();return true;
}
bool DetLevelResource2D::unload() {
    level_.reset();bytes_=0;_loaded=false;_path.clear();error_.clear();return true;
}
bool DetLevelLoader2D::canLoad(const std::string& path) const {
    return asset_format::isCanonicalPath(path,asset_format::Id::DeterministicLevelCooked);
}
std::shared_ptr<resource::IResource> DetLevelLoader2D::load(const std::string& path) {
    if(!canLoad(path))return {};
    auto result=std::make_shared<DetLevelResource2D>();return result->load(path)?result:nullptr;
}
std::shared_ptr<resource::IResource> DetLevelLoader2D::loadFromBinary(const void* data,std::size_t bytes) {
    auto result=std::make_shared<DetLevelResource2D>();return result->loadFromBinary(data,bytes)?result:nullptr;
}
std::shared_ptr<resource::IResource> DetLevelLoader2D::loadAsync(const std::string& path,
    std::function<void(std::shared_ptr<resource::IResource>)> callback) {
    auto result=load(path);if(callback)callback(result);return result;
}
bool registerDetLevelResources2D(std::string& error) {
    using Registry=resource::ResourceRegistry;
    if(!resource::areLoadersInitialized()) {error="Call initializeLoaders before registering deterministic levels";return false;}
    const auto suffix=std::string(asset_format::suffix(asset_format::Id::DeterministicLevelCooked));
    const auto& mapping=Registry::getTypeFromExtension(suffix);
    if(!mapping.empty() && mapping!=DetLevelResource2D::TYPE) {error="Cooked level suffix already belongs to another resource type";return false;}
    const auto loader=Registry::createLoader(DetLevelResource2D::TYPE);
    if(loader && !dynamic_cast<DetLevelLoader2D*>(loader.get())) {error="Cooked level type already belongs to another loader";return false;}
    if(!loader && !Registry::registerLoader(DetLevelResource2D::TYPE,+[]()->std::unique_ptr<resource::IResourceLoader>{return std::make_unique<DetLevelLoader2D>();})) {
        error="Cannot register cooked level loader";return false;
    }
    Registry::registerExtension(suffix,DetLevelResource2D::TYPE);error.clear();return true;
}
} // namespace ayt::entity
