#include "CDNAudioFile.h"

#include <errno.h>           // for errno
#include <string.h>          // for memcpy
#include <chrono>
#include <cstdlib>           // for strtoull
#include <functional>        // for __base
#include <initializer_list>  // for initializer_list
#include <map>               // for operator!=, operator==
#include <stdexcept>         // for runtime_error
#include <string>            // for to_string
#include <string_view>       // for string_view
#include <type_traits>       // for remove_extent_t

#include "AccessKeyFetcher.h"  // for AccessKeyFetcher
#include "BellLogger.h"        // for AbstractLogger
#include "Crypto.h"
#include "Logger.h"            // for CSPOT_LOG
#include "Packet.h"            // for cspot
#include "SocketStream.h"      // for SocketStream
#include "Utils.h"             // for bigNumAdd, bytesToHexString, string...
#include "WrappedSemaphore.h"  // for WrappedSemaphore
#ifdef BELL_ONLY_CJSON
#include "cJSON.h"
#else
#include "nlohmann/json.hpp"      // for basic_json<>::object_t, basic_json
#include "nlohmann/json_fwd.hpp"  // for json
#endif

using namespace cspot;

CDNAudioFile::CDNAudioFile(const std::string& cdnUrl,
                           const std::vector<uint8_t>& audioKey)
    : cdnUrl(cdnUrl), audioKey(audioKey) {
  this->crypto = std::make_unique<Crypto>();
}

size_t CDNAudioFile::getPosition() {
  return this->position;
}

void CDNAudioFile::seek(size_t newPos) {
  this->enableRequestMargin = true;
  this->position = newPos;
}

void CDNAudioFile::openStream() {
  CSPOT_LOG(info, "Opening HTTP stream to %s", this->cdnUrl.c_str());

  // Open connection, read first 128 bytes
  this->httpConnection = bell::HTTPClient::get(
      this->cdnUrl,
      {bell::HTTPClient::RangeHeader::range(0, OPUS_HEADER_SIZE - 1)});

  this->httpConnection->stream().read((char*)header.data(), OPUS_HEADER_SIZE);
  if (this->httpConnection->stream().gcount() != OPUS_HEADER_SIZE)
    throw std::runtime_error("Short read of audio header");

  const size_t encryptedFileSize = this->httpConnection->totalLength();
  if (encryptedFileSize <= SPOTIFY_OPUS_HEADER + OPUS_FOOTER_PREFFERED)
    throw std::runtime_error("Invalid audio file size");
  this->totalFileSize = encryptedFileSize - SPOTIFY_OPUS_HEADER;

  this->decrypt(header.data(), OPUS_HEADER_SIZE, 0);

  // Location must be dividable by 16
  size_t footerStartLocation =
      (this->totalFileSize - OPUS_FOOTER_PREFFERED + SPOTIFY_OPUS_HEADER) -
      (this->totalFileSize - OPUS_FOOTER_PREFFERED + SPOTIFY_OPUS_HEADER) % 16;

  this->footer = std::vector<uint8_t>(
      this->totalFileSize - footerStartLocation + SPOTIFY_OPUS_HEADER);
  this->httpConnection->get(
      cdnUrl, {bell::HTTPClient::RangeHeader::last(footer.size())});

  this->httpConnection->stream().read((char*)footer.data(),
                                      this->footer.size());
  if (this->httpConnection->stream().gcount() !=
      static_cast<std::streamsize>(this->footer.size()))
    throw std::runtime_error("Short read of audio footer");

  this->decrypt(footer.data(), footer.size(), footerStartLocation);
  CSPOT_LOG(info, "Header and footer bytes received");
  this->position = 0;
  this->lastRequestPosition = 0;
  this->lastRequestCapacity = 0;
  this->httpBuffer.resize(HTTP_BUFFER_SIZE);
}

size_t CDNAudioFile::readBytes(uint8_t* dst, size_t bytes) {
  size_t offsetPosition = position + SPOTIFY_OPUS_HEADER;
  size_t actualFileSize = this->totalFileSize + SPOTIFY_OPUS_HEADER;

  if (position + bytes >= this->totalFileSize) {
    return 0;
  }

  // // Opus tries to read header, use prefetched data
  if (offsetPosition < OPUS_HEADER_SIZE &&
      bytes + offsetPosition <= OPUS_HEADER_SIZE) {
    memcpy(dst, this->header.data() + offsetPosition, bytes);
    position += bytes;
    return bytes;
  }

  // // Opus tries to read footer, use prefetched data
  if (offsetPosition >= (actualFileSize - this->footer.size())) {
    size_t toReadBytes = bytes;

    if ((position + bytes) > this->totalFileSize) {
      // Tries to read outside of bounds, truncate
      toReadBytes = this->totalFileSize - position;
    }

    size_t footerOffset =
        offsetPosition - (actualFileSize - this->footer.size());
    memcpy(dst, this->footer.data() + footerOffset, toReadBytes);

    position += toReadBytes;
    return toReadBytes;
  }

  // Data not in the headers. Make sense of whats going on.
  // Position in bounds :)
  if (offsetPosition >= this->lastRequestPosition &&
      offsetPosition < this->lastRequestPosition + this->lastRequestCapacity) {
    // Keep LOOKAHEAD downloaded ahead while the connection is there.
    const size_t windowEnd = lastRequestPosition + lastRequestCapacity;
    if (windowEnd - offsetPosition < static_cast<size_t>(LOOKAHEAD) &&
        windowEnd < actualFileSize && !topUpFailed && networkUp)
      topUp(offsetPosition);

    size_t toRead = bytes;

    if ((toRead + offsetPosition) >
        this->lastRequestPosition + lastRequestCapacity) {
      toRead = this->lastRequestPosition + lastRequestCapacity - offsetPosition;
    }

    memcpy(dst, this->httpBuffer.data() + offsetPosition - lastRequestPosition,
           toRead);
    position += toRead;

    return toRead;
  } else {
    size_t requestPosition = (offsetPosition) - ((offsetPosition) % 16);
    if (this->enableRequestMargin && requestPosition > SEEK_MARGIN_SIZE) {
      requestPosition = (offsetPosition - SEEK_MARGIN_SIZE) -
                        ((offsetPosition - SEEK_MARGIN_SIZE) % 16);
      this->enableRequestMargin = false;
    }

    size_t readCapacity = 0;
    try {
      // Known to be offline: wait for it (the player holds the song) rather
      // than block here on a connection attempt.
      if (!networkUp) throw std::runtime_error("network down");
      readCapacity =
          fetchRange(requestPosition, httpBuffer.size(), httpBuffer.data());
      if (readCapacity == 0) {
        this->lastRequestPosition = 0;
        this->lastRequestCapacity = 0;
        return 0;
      }
    } catch (const std::exception& e) {
      // A network failure must not propagate through the vorbis C call
      // frames above us (TrackPlayer::runTask has no handler). Invalidate
      // the cache window.
      CSPOT_LOG(error, "Failed to read audio chunk at %u: %s",
                (unsigned int)requestPosition, e.what());
      this->lastRequestPosition = 0;
      this->lastRequestCapacity = 0;
      this->readFailed = true;
      // Tremor reads a failed read with errno set as an error, which ends the
      // song; with errno clear it is an end of data the player can hold.
      errno = 0;
      // No retry here: retrying every second for 90 s (a TLS handshake each, on
      // the system core) starved FS, graphics and audio while a game launched,
      // and HOME failed to open. The player retries once the service sees the
      // connection back (TrackPlayer.cpp, holdOnStreamLoss).
      return 0;
    }

    this->lastRequestPosition = requestPosition;
    this->lastRequestCapacity = readCapacity;
    this->topUpFailed = false;

    return readBytes(dst, bytes);
  }

  return bytes;
}

// One range request into dst, decrypted. 0 when the CDN refused the song;
// throws on a network failure. Any answer but the bytes asked for drops the
// connection: an unread body left on a kept-alive socket made every later
// request fail ("Cannot parse http response") until the song ended.
size_t CDNAudioFile::fetchRange(size_t start, size_t length, uint8_t* dst) {
  try {
    return fetchRangeOnce(start, length, dst);
  } catch (...) {
    this->httpConnection = std::make_unique<bell::HTTPClient::Response>();
    throw;
  }
}

size_t CDNAudioFile::fetchRangeOnce(size_t start, size_t length, uint8_t* dst) {
  const auto requestStarted = std::chrono::steady_clock::now();
  this->httpConnection->get(
      cdnUrl, {bell::HTTPClient::RangeHeader::range(start, start + length - 1)});
  // An error page used to be decrypted as if it were audio, which reaches the
  // decoder as a gap. An address that stopped working will not recover: give
  // up on this song right away. Anything else is retried like a lost socket.
  const int status = this->httpConnection->statusCode();
  if (status == 401 || status == 403 || status == 404 || status == 410) {
    CSPOT_LOG(error, "Audio CDN refused the song: HTTP %d", status);
    this->httpConnection = std::make_unique<bell::HTTPClient::Response>();
    return 0;
  }
  // 206 only: a 200 is the whole file, not the range.
  if (status != 206)
    throw std::runtime_error("Audio CDN returned HTTP " +
                             std::to_string(status));

  const size_t readCapacity = this->httpConnection->contentLength();
  const std::string range(this->httpConnection->header("content-range"));
  // "bytes <first>-<last>/<total>": it must start where asked.
  const size_t space = range.find(' ');
  const bool rightStart =
      space != std::string::npos &&
      std::strtoull(range.c_str() + space + 1, nullptr, 10) == start;
  if (readCapacity == 0 || readCapacity > length || !rightStart)
    throw std::runtime_error("Audio CDN answered another range (" + range +
                             ", " + std::to_string(readCapacity) + " bytes)");

  this->httpConnection->stream().read((char*)dst, readCapacity);
  if (this->httpConnection->stream().gcount() != (std::streamsize)readCapacity)
    throw std::runtime_error("Short read of audio chunk body");

  const auto requestMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - requestStarted)
                             .count();
  if (requestMs >= 250)
    CSPOT_LOG(info, "Audio CDN range took %lld ms for %u bytes",
              static_cast<long long>(requestMs),
              static_cast<unsigned int>(readCapacity));

  this->decrypt(dst, readCapacity, start);
  return readCapacity;
}

// Slides the unread part of the window to the front and appends what follows.
// A failure keeps the window as it was: what is downloaded still plays.
void CDNAudioFile::topUp(size_t offsetPosition) {
  const size_t windowEnd = lastRequestPosition + lastRequestCapacity;
  const size_t keepFrom = offsetPosition - (offsetPosition % 16);
  const size_t kept = windowEnd - keepFrom;
  memmove(httpBuffer.data(), httpBuffer.data() + (keepFrom - lastRequestPosition),
          kept);
  lastRequestPosition = keepFrom;
  lastRequestCapacity = kept;

  size_t room = httpBuffer.size() - kept;
  room -= room % 16;
  if (room == 0) return;
  try {
    const size_t got = fetchRange(windowEnd, room, httpBuffer.data() + kept);
    if (got == 0) {
      topUpFailed = true;
      return;
    }
    lastRequestCapacity += got;
  } catch (const std::exception& e) {
    CSPOT_LOG(error, "Audio read-ahead failed at %u: %s",
              (unsigned int)windowEnd, e.what());
    // Until the window runs out; then the regular read decides.
    topUpFailed = true;
  }
}

size_t CDNAudioFile::getSize() {
  return this->totalFileSize;
}

void CDNAudioFile::decrypt(uint8_t* dst, size_t nbytes, size_t pos) {
  auto calculatedIV = bigNumAdd(audioAESIV, pos / 16);

  this->crypto->aesCTRXcrypt(this->audioKey, calculatedIV, dst, nbytes);
}
