#pragma once
// design reference: AYEntity/design.md Stage45 bounded cooked level envelope.
#include <AYEntity/DeterministicLevel2D.h>
#include <span>

namespace ayt::entity {
/// @brief Version of the pure-memory AYDLC001 envelope, independent of Level1/2/3 semantics.
inline constexpr std::uint32_t kDetLevelCooked2DFormatVersion=1;
/// @brief Exact envelope header width: magic8/version4/length4/semanticHash8/payloadHash8.
inline constexpr std::size_t kDetLevelCooked2DHeaderBytes=32;
/// @brief Maximum whole cooked resource: fixed header plus bounded canonical source text.
inline constexpr std::size_t kDetLevelCooked2DMaxBytes=kDetLevelCooked2DHeaderBytes+kDetLevel2DMaxTextBytes;
/** @brief Encode a validated level into a bounded little-endian AYDLC001 memory buffer.
 * @note Uses the default Level options and complete canonical encodeDetLevel2D
 * text. Full semantic identity remains owned by the existing Level pipeline;
 * the two FNV64 values identify content and detect accidental damage, not
 * authentication. Invalid level/default registration combinations throw.
 * This performs no file IO, resource acquisition or Session tick ownership.
 */
std::vector<std::uint8_t> encodeDetLevelCooked2D(const DetLevel2D& level);
/** @brief Strictly decode a whole cooked buffer; any failure leaves output unchanged.
 * @note Requires exact header/length/EOF, matching payload FNV64, actual default
 * Level decode/preflight, byte-identical canonical text and semantic contentHash.
 * Whitespace-equivalent/noncanonical source is rejected even with new checksums.
 * No alternate semantic decoder, checkpoint import or native float parser is
 * used. Applications select runtime registration options separately when they
 * call configureDetLevel2D; this codec carries no options or callback authority.
 */
bool decodeDetLevelCooked2D(std::span<const std::uint8_t> bytes,DetLevel2D& output,std::string& error);
} // namespace ayt::entity
