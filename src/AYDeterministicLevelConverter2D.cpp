#include <AYResource/Converter/DeterministicLevelConverter2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYIO/File.h>
#include <AYStorage/Guid.h>
#include <filesystem>
#include <stdexcept>

namespace ayt::resource {
namespace {
namespace fs=std::filesystem;
fs::path nativeFilesystemPath(const fs::path& path) {
#if defined(_WIN32)
    const auto native=fs::absolute(path).lexically_normal().make_preferred().native();
    if(native.rfind(L"\\\\?\\",0)==0)return fs::path(native);
    if(native.rfind(L"\\\\",0)==0)return fs::path(L"\\\\?\\UNC\\"+native.substr(2));
    return fs::path(L"\\\\?\\"+native);
#else
    return path;
#endif
}
fs::path canonicalIdentityPath(const fs::path& path) {
#if defined(_WIN32)
    // canonical/weakly_canonical may remove the extended spelling. Normalize
    // that spelling only for relative identity and containment comparisons;
    // every subsequent physical access reacquires the native spelling.
    const auto& native=path.native();
    if(native.rfind(L"\\\\?\\UNC\\",0)==0)return fs::path(L"\\\\"+native.substr(8));
    if(native.rfind(L"\\\\?\\",0)==0)return fs::path(native.substr(4));
#endif
    return path;
}
}
ConversionResult DeterministicLevelConverter2D::convert() {
    using asset_format::Id;
    const auto input=fs::path(source_);
    if(!asset_format::isCanonicalPath(source_,Id::DeterministicLevelSource))
        throw std::runtime_error("Deterministic level importer requires .dlt");
    if(!fs::is_regular_file(fs::symlink_status(nativeFilesystemPath(input))))
        throw std::runtime_error("Deterministic level source must be an ordinary file");
    const auto source=canonicalIdentityPath(fs::canonical(nativeFilesystemPath(input)));
    // A single-file import without a project root deliberately produces a basename.
    // ProjectBuild always supplies its explicit asset root through ImportJob.
    const auto root=root_.empty()?source.parent_path()
        :canonicalIdentityPath(fs::canonical(nativeFilesystemPath(fs::path(root_))));
    const auto relative=source.lexically_relative(root);
    if(relative.empty() || relative.is_absolute())throw std::runtime_error("Level source is outside its asset root");
    for(const auto& part:relative)if(part==".." || part==".")throw std::runtime_error("Level source escapes its asset root");
    const auto logical=relative.generic_string();
    for(unsigned char value:logical)if(value<32 || value==127 || value==':' || value=='\\')
        throw std::runtime_error("Level source logical path is not portable");
    entity::DetLevel2D level;std::string error;
    if(!entity::loadDetLevel2D(nativeFilesystemPath(source),level,error))throw std::runtime_error(error);
    const auto bytes=entity::encodeDetLevelCooked2D(level);
    auto output=relative;output.replace_extension(asset_format::suffix(Id::DeterministicLevelCooked));
    if(!output_.empty()) {
        const auto nativeOutputRoot=nativeFilesystemPath(fs::path(output_));
        fs::create_directories(nativeOutputRoot);
        const auto outputRoot=canonicalIdentityPath(fs::canonical(nativeOutputRoot));
        const auto destination=outputRoot/output;
        const auto physicalDestination=nativeFilesystemPath(destination);
        // Existing linked directories or files must not redirect the write outside the cache.
        const auto resolved=canonicalIdentityPath(fs::weakly_canonical(physicalDestination));
        const auto bounded=resolved.lexically_relative(outputRoot);
        if(bounded.empty() || bounded.is_absolute())throw std::runtime_error("Cooked level output escapes its root");
        for(const auto& part:bounded)if(part=="..")throw std::runtime_error("Cooked level output escapes its root");
        if(fs::is_symlink(fs::symlink_status(physicalDestination)))throw std::runtime_error("Cooked level output may not be a symlink");
        fs::create_directories(physicalDestination.parent_path());
        if(!io::File::atomicWrite(physicalDestination.string(),bytes.data(),bytes.size()))throw std::runtime_error("Cannot write cooked deterministic level");
    }
    ConversionResult result;result.importerContractTag="deterministic-level-cooked-v1";
    ConversionResult::ConvertedResource resource;
    resource.path=output.generic_string();resource.type="DeterministicLevel2D";resource.size=bytes.size();
    resource.guid=storage::Guid::computeFromData(bytes.data(),bytes.size());
    result.resources.push_back(std::move(resource));return result;
}
} // namespace ayt::resource
