#pragma once
#include <AYEntity/DeterministicSession.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ayt::entity {
/// @brief Experimental .dsv envelope version; the existing checkpoint payload is unchanged.
inline constexpr std::uint32_t kDetSaveFormatVersion=1;
/// @brief Existing canonical checkpoint bound, including its own checksum.
inline constexpr std::size_t kDetSaveMaxCheckpointBytes=8*1024*1024;
/// @brief Eight-byte AYDSAVE1 magic, LE version/length and one complete checkpoint.
inline constexpr std::size_t kDetSaveHeaderBytes=16;
inline constexpr std::size_t kDetSaveMaxFileBytes=kDetSaveHeaderBytes+kDetSaveMaxCheckpointBytes;

/** @brief Decode a bounded .dsv file without changing output on failure.
 * @note The file must be quiescent, ordinary and not a symbolic link. Its envelope is
 * AYDSAVE1, LE uint32 version1, LE uint32 payload length, then the complete existing
 * encodeDetCheckpoint bytes, with exact EOF. Raw checkpoints are not .dsv files.
 * The checkpoint codec validates canonical shape/checksum; this function does not
 * execute application validators or establish compatibility with an installed Session.
 * Checksums detect corruption, not authenticity. No migration or code loading occurs.
 * @threading Call outside Sim callbacks; file I/O is synchronous and may allocate.
 */
bool readDetSave(const std::filesystem::path& path,DetSessionCheckpoint& output,std::string& error);

/** @brief Create a fresh .dsv file containing one complete canonical checkpoint.
 * @note Validates checkpoint encoding before exclusive creation. Existing files,
 * directories and symbolic links are rejected; this API never replaces a save.
 * A write/flush/close failure may leave a partial new file: choose a fresh path or
 * handle that file explicitly. No automatic deletion, transaction or crash durability
 * is promised. The caller keeps the parent/source quiescent; hostile path mutation
 * is outside this ordinary-file utility's contract. Codec validity does not establish
 * application semantic validity. Resource handles and unregistered state are absent.
 */
bool writeDetSave(const std::filesystem::path& path,const DetSessionCheckpoint& checkpoint,std::string& error);

/** @brief Capture a valid quiescent Session boundary and create a fresh .dsv file.
 * @note Single owner thread, outside system/validator callbacks. A failed capture
 * creates no file. The destination follows writeDetSave's fresh-only policy.
 */
bool saveDetSession(const std::filesystem::path& path,const DeterministicSession& session,std::string& error);

/** @brief Decode into temporary state, then restore through the configured Session.
 * @note Exact manifest and pure semantic validators remain Session::restore's
 * responsibility. Invalid input/rejected restore preserves registered Sim state;
 * diagnostics may change. Requires a sealed quiescent Session on its owner thread.
 * Host callers instead use readDetSave then controller.restore at an external frame
 * boundary, retaining Host pause/resume, mode and presentation-handle rules.
 */
bool loadDetSession(const std::filesystem::path& path,DeterministicSession& session,std::string& error);
} // namespace ayt::entity
