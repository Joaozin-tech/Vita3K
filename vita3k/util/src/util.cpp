// Vita3K emulator project
// Copyright (C) 2023 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <util/arm.h>
#include <util/bytes.h>
#include <util/log.h>
#include <util/net_utils.h>
#include <util/string_utils.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#endif

#include <algorithm>
#include <codecvt> // std::codecvt_utf8
#include <iostream>
#include <locale> // std::wstring_convert
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include <memory>
#include <stdexcept>

namespace logging {

static const fs::path &LOG_FILE_NAME = "vita3k.log";
static const char *LOG_PATTERN = "%^[%H:%M:%S.%e] |%L| [%!]: %v%$";
std::vector<spdlog::sink_ptr> sinks;

ExitCode init(const Root &root_paths, bool use_stdout) {
    sinks.clear();
    if (use_stdout)
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (add_sink(root_paths.get_base_path() / LOG_FILE_NAME) != Success)
        return InitConfigFailed;

    spdlog::set_error_handler([](const std::string &msg) {
        std::cerr << "spdlog error: " << msg << std::endl;
        assert(0);
    });

    return Success;
}

void set_level(spdlog::level::level_enum log_level) {
    spdlog::set_level(log_level);
}

ExitCode add_sink(const fs::path &log_path) {
    try {
#ifdef WIN32
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.generic_path().wstring(), true));
#else
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.generic_path().string(), true));
#endif
    } catch (const spdlog::spdlog_ex &ex) {
        std::cerr << "File log initialization failed: " << ex.what() << std::endl;
        return InitConfigFailed;
    }

#ifdef _MSC_VER
    if (sinks.size() == 2) { // spdlog is being initialized
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    }
#endif

    spdlog::set_default_logger(std::make_shared<spdlog::logger>("vita3k logger", begin(sinks), end(sinks)));
    spdlog::set_pattern(LOG_PATTERN);
    return Success;
}

typedef std::set<std::string> NameSet;
static std::mutex mutex;
static NameSet logged;

int ret_error_impl(const char *name, const char *error_str, std::uint32_t error_val) {
    bool inserted = false;

    {
        const std::lock_guard<std::mutex> lock(mutex);
        inserted = logged.insert(name).second;
    }

    if (inserted) {
        LOG_ERROR("{} returned {} ({})", name, error_str, log_hex(error_val));
    }

    return error_val;
}

} // namespace logging

namespace string_utils {

std::vector<std::string> split_string(const std::string &str, char delimiter) {
    std::stringstream str_stream(str);
    std::string segment;
    std::vector<std::string> seglist;

    const size_t num_segments = std::count_if(str.begin(), str.end(), [&](char c) {
        return c == delimiter;
    }) + (str.empty() ? 1 : 0);

    seglist.reserve(num_segments);

    while (std::getline(str_stream, segment, delimiter)) {
        seglist.push_back(segment);
    }
    return seglist;
}

std::wstring utf_to_wide(const std::string &str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.from_bytes(str);
}

std::string wide_to_utf(const std::wstring &str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.to_bytes(str);
}

std::string remove_special_chars(std::string str) {
    for (char &c : str) {
        switch (c) {
        case '/':
        case '\\':
        case ':':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
        case '*':
            c = ' ';
            break;
        default:
            continue;
        }
    }
    return str;
}

// Based on: https://stackoverflow.com/a/23135441
// Search and replace "in" with "out" in the given string
void replace(std::string &str, const std::string &in, const std::string &out) {
    size_t pos = 0;
    while ((pos = str.find(in, pos)) != std::string::npos) {
        str.replace(pos, in.length(), out);
        pos += out.length();
    }
}

std::basic_string<uint8_t> string_to_byte_array(std::string string) {
    std::basic_string<uint8_t> hex_bytes;

    for (size_t i = 0; i < string.length(); i += 2) {
        uint16_t byte;
        std::string nextbyte = string.substr(i, 2);
        std::istringstream(nextbyte) >> std::hex >> byte;
        hex_bytes.push_back(static_cast<uint8_t>(byte));
    }
    return hex_bytes;
}

#ifdef WIN32
std::string utf16_to_utf8(const std::u16string &str) {
    std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> myconv;
    auto p = reinterpret_cast<const int16_t *>(str.data());
    return myconv.to_bytes(p, p + str.size());
}

std::u16string utf8_to_utf16(const std::string &str) {
    static_assert(sizeof(std::wstring::value_type) == sizeof(std::u16string::value_type),
        "std::wstring and std::u16string are expected to have the same character size");

    std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> myconv;
    auto p = reinterpret_cast<const char *>(str.data());
    auto a = myconv.from_bytes(p, p + std::strlen(p));
    return std::u16string(a.begin(), a.end());
}
#else
std::string utf16_to_utf8(const std::u16string &str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> myconv;
    return myconv.to_bytes(str);
}

std::u16string utf8_to_utf16(const std::string &str) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> myconv;
    return myconv.from_bytes(str);
}
#endif

std::string toupper(const std::string &s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return r;
}

std::string tolower(const std::string &s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return r;
}

} // namespace string_utils

namespace net_utils {

// 0 is ok, negative is bad
SceHttpErrorCode parse_url(std::string url, parsedUrl &out) {
    out.scheme = url.substr(0, url.find(':'));

    if (out.scheme != "http" && out.scheme != "https")
        return SCE_HTTP_ERROR_UNKNOWN_SCHEME;

    // Check if URL is opaque, if it is opaque then its invalid
    // http://
    //+    012
    //      ^^ Check these 2
    char url1Slash = *(url.c_str() + (out.scheme.length() + 1)); // get uint8 value
    char url2Slash = *(url.c_str() + (out.scheme.length() + 2)); // get next uint8 value
    // Compare the 2 characters that are supposed to be slahses
    if (url1Slash != '/' || url2Slash != '/') {
        out.invalid = true;
        return (SceHttpErrorCode)0;
    }

    auto end_scheme_pos = url.find(':');
    auto path_pos = url.find('/', end_scheme_pos + 3);
    auto has_path = path_pos != std::string::npos;

    auto full_wo_scheme = url.substr(end_scheme_pos + 3);

    if (has_path) {
        // username:password@lttstore.com:727/wysi/cookie.php?pog=gers#extremeexploit

        {
            path_pos = std::string(full_wo_scheme).find('/');
            auto full_no_scheme_path = full_wo_scheme.substr(0, path_pos);
            // full_no_scheme_path = username:password@lttstore.com:727
            auto c = full_no_scheme_path.find('@');
            if (c != std::string::npos) { // we have credentials
                auto credentials = full_no_scheme_path.substr(0, c);
                // credentials = username:password
                auto semicolon_pos = credentials.find(':');

                if (semicolon_pos != std::string::npos) { // we have password?
                    auto username = credentials.substr(0, semicolon_pos);
                    auto password = credentials.substr(semicolon_pos + 1);

                    if (username.length() >= SCE_HTTP_USERNAME_MAX_SIZE)
                        return SCE_HTTP_ERROR_OUT_OF_SIZE;
                    if (password.length() >= SCE_HTTP_USERNAME_MAX_SIZE)
                        return SCE_HTTP_ERROR_OUT_OF_SIZE;

                    out.username = username;
                    out.password = password;
                } else { // we don't have password
                    out.username = credentials;
                }
            } else { // no credentials
                // lttstore.com:727
                auto semicolon_pos = full_no_scheme_path.find(':');

                if (semicolon_pos != std::string::npos) { // we have port
                    auto hostname = full_no_scheme_path.substr(0, semicolon_pos);
                    auto port = full_no_scheme_path.substr(semicolon_pos + 1);

                    out.hostname = hostname;
                    out.port = port;
                } else { // no port
                    out.hostname = full_no_scheme_path;
                }
            }
        }
        {
            auto path_pos = std::string(full_wo_scheme).find('/');
            auto full_no_scheme_hostname = full_wo_scheme.substr(path_pos);
            // /wysi/cookie.php?pog=gers#extremeexploit

            auto query_pos = full_no_scheme_hostname.find('?');
            if (query_pos != std::string::npos) { // we have query
                auto path = full_no_scheme_hostname.substr(0, query_pos);
                out.path = path;

                auto query_frag = full_no_scheme_hostname.substr(query_pos);

                auto frag_pos = query_frag.find('#');
                if (frag_pos != std::string::npos) {
                    // query and fragment

                    auto query = query_frag.substr(0, frag_pos);
                    auto fragment = query_frag.substr(frag_pos);

                    out.query = query;
                    out.fragment = fragment;
                } else { // no fragment
                    out.query = query_frag;
                }

            } else { // no query, dunno about fragment
                auto frag_pos = full_no_scheme_hostname.find('#');

                if (frag_pos != std::string::npos) { // we have fragment
                    auto path = full_no_scheme_hostname.substr(0, frag_pos);
                    auto fragment = full_no_scheme_hostname.substr(frag_pos);

                    out.path = path;
                    out.fragment = fragment;
                } else { // no fragment, only path
                    out.path = full_no_scheme_hostname;
                }
            }
        }
    } else {
        // username:password@lttstore.com:727
        auto c = full_wo_scheme.find('@');
        if (c != std::string::npos) { // we have credentials
            auto credentials = full_wo_scheme.substr(0, c);
            // credentials = username:password
            auto semicolon_pos = credentials.find(':');

            if (semicolon_pos != std::string::npos) { // we have password?
                auto username = credentials.substr(0, semicolon_pos);
                auto password = credentials.substr(semicolon_pos + 1);

                if (username.length() >= SCE_HTTP_USERNAME_MAX_SIZE)
                    return SCE_HTTP_ERROR_OUT_OF_SIZE;
                if (password.length() >= SCE_HTTP_USERNAME_MAX_SIZE)
                    return SCE_HTTP_ERROR_OUT_OF_SIZE;

                out.username = username;
                out.password = password;
            } else { // we don't have password
                out.username = credentials;
            }
        } else { // no credentials
            // lttstore.com:727
            auto semicolon_pos = full_wo_scheme.find(':');

            if (semicolon_pos != std::string::npos) { // we have port
                auto hostname = full_wo_scheme.substr(0, semicolon_pos);
                auto port = full_wo_scheme.substr(semicolon_pos + 1);

                out.hostname = hostname;
                out.port = port;
            } else { // no port
                out.hostname = full_wo_scheme;
            }
        }
    }

    return (SceHttpErrorCode)0;
}

int char_method_to_int(const char *method) {
    if (strcmp(method, "GET") == 0) {
        return SCE_HTTP_METHOD_GET;
    } else if (strcmp(method, "POST") == 0) {
        return SCE_HTTP_METHOD_POST;
    } else if (strcmp(method, "HEAD") == 0) {
        return SCE_HTTP_METHOD_HEAD;
    } else if (strcmp(method, "OPTIONS") == 0) {
        return SCE_HTTP_METHOD_OPTIONS;
    } else if (strcmp(method, "PUT") == 0) {
        return SCE_HTTP_METHOD_PUT;
    } else if (strcmp(method, "DELETE") == 0) {
        return SCE_HTTP_METHOD_DELETE;
    } else if (strcmp(method, "TRACE") == 0) {
        return SCE_HTTP_METHOD_TRACE;
    } else if (strcmp(method, "CONNECT") == 0) {
        return SCE_HTTP_METHOD_CONNECT;
    } else {
        return -1;
    }
}

const char *int_method_to_char(const int n) {
    switch (n) {
    case 0: return "GET";
    case 1: return "POST";
    case 2: return "HEAD";
    case 3: return "OPTIONS";
    case 4: return "PUT";
    case 5: return "DELETE";
    case 6: return "TRACE";
    case 7: return "CONNECT";

    default:
        return "INVALID";
        break;
    }
}

std::string constructHeaders(std::map<std::string, std::string> &headers) {
    std::string headersString;
    for (auto head : headers) {
        headersString.append(head.first);
        headersString.append(": ");
        headersString.append(head.second);
        headersString.append("\r\n");
    }

    return headersString;
}

void parseResponse(std::string res, SceRequestResponse &reqres) {
    auto statusLine = res.substr(0, res.find("\r\n"));

    reqres.httpVer = statusLine.substr(0, 8);
    SceInt statusCode = std::stoi(statusLine.substr(9, 3));
    reqres.statusCode = statusCode;
    reqres.reasonPhrase = statusLine.substr(13);

    auto headersRaw = res.substr(res.find("\r\n") + 2);
    char *ptr;
    ptr = strtok(headersRaw.data(), "\r\n");
    // use while loop to check ptr is not null
    while (ptr != NULL) {
        auto line = std::string(ptr);
        auto name = line.substr(0, line.find(':'));
        auto value = line.substr(line.find(' ') + 1);

        reqres.headers.insert({ name, value });
        ptr = strtok(NULL, "\r\n");
    }

    auto length_it = reqres.headers.find("Content-Length");
    if (length_it == reqres.headers.end()) {
        LOG_WARN("Response has no Content-Length");
        return;
    }

    SceULong64 length = std::stoi(length_it->second);
    reqres.contentLength = length;
}

bool socketSetBlocking(int sockfd, bool blocking) {
#ifdef WIN32
    ioctlsocket(sockfd, FIONBIO, (u_long *)&blocking);
#else
    if (blocking) { // Blocking
        int flags = fcntl(sockfd, F_GETFL); // Get flags
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK); // Set NONBLOCK flag off
    } else { // Non blocking
        int flags = fcntl(sockfd, F_GETFL); // Get flags
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK); // Set NONBLOCK flag on
    }
#endif
    return true;
}

} // namespace net_utils

template <>
std::uint16_t byte_swap(std::uint16_t val) {
    return (val >> 8) | (val << 8);
}

template <>
std::uint32_t byte_swap(std::uint32_t val) {
    //        AA              BB00                      CC0000                       DD000000
    return (val >> 24) | ((val >> 8) & 0xFF00) | ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
}

template <>
std::uint64_t byte_swap(std::uint64_t val) {
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL) | ((val >> 8) & 0x00FF00FF00FF00FFULL);
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL) | ((val >> 16) & 0x0000FFFF0000FFFFULL);

    return (val << 32) | (val >> 32);
}

template <>
std::int16_t byte_swap(std::int16_t val) {
    return byte_swap(static_cast<std::uint16_t>(val));
}

template <>
std::int32_t byte_swap(std::int32_t val) {
    return byte_swap(static_cast<std::uint32_t>(val));
}

template <>
std::int64_t byte_swap(std::int64_t val) {
    return byte_swap(static_cast<std::uint64_t>(val));
}

/*
float32 to float16 conversion
we can have 3 cases
1 program compiled with AVX2 or F16C flags  - use fast conversion
2 program compiled without AVX2 or F16C flags:
2a we can include and use F16C intrinsic without AVX2 or F16C flags - autodetect and use fast or basic conversion depends of runtime cpu
2b we can't include and use F16C intrinsic - use basic conversion
msvc allow to include any intrinsic independent of architecture flags, other compilers disallow this
*/
#if (defined(__AVX__) && defined(__F16C__)) || defined(__AVX2__) || (defined(_MSC_VER) && !defined(__clang__))
#include <immintrin.h>
void float_to_half_AVX_F16C(const float *src, std::uint16_t *dest, const int total) {
    int left = total;

    while (left >= 8) {
        __m256 float_vector = _mm256_loadu_ps(src);
        __m128i half_vector = _mm256_cvtps_ph(float_vector, 0);
        _mm_storeu_si128((__m128i *)dest, half_vector);

        left -= 8;
        src += 8;
        dest += 8;
    }

    if (left > 0) {
        alignas(32) float data[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        std::copy_n(src, left, data);
        __m256 float_vector = _mm256_load_ps(data);
        __m128i half_vector = _mm256_cvtps_ph(float_vector, 0);
        _mm_store_si128(reinterpret_cast<__m128i *>(data), half_vector);
        std::copy_n(reinterpret_cast<uint16_t *>(data), left, dest);
    }
}
#endif

#if (defined(__AVX__) && defined(__F16C__)) || defined(__AVX2__)
// forced use AVX+F16C instruction set
// AVX2 checked intentionally cause MSVC does not have __F16C__ macros
// and checking AVX is not enough for some CPU architectures (Intel Sandy bridge)
void float_to_half(const float *src, std::uint16_t *dest, const int total) {
    float_to_half_AVX_F16C(src, dest, total);
}
#else
#include <util/float_to_half.h>
void float_to_half_basic(const float *src, std::uint16_t *dest, const int total) {
    for (int i = 0; i < total; i++) {
        dest[i] = util::encode_flt16(src[i]);
    }
}
#if (defined(_MSC_VER) && !defined(__clang__))
// check and use AVX+F16C instruction set if possible

// use function variable as imitation of self-modifying code.
// on first use we check processor features and set appropriate realisation, later we immediately use appropriate realisation
void float_to_half_init(const float *src, std::uint16_t *dest, const int total);

void (*float_to_half_var)(const float *src, std::uint16_t *dest, const int total) = float_to_half_init;

#include <util/instrset_detect.h>
void float_to_half_init(const float *src, std::uint16_t *dest, const int total) {
    if (util::instrset::hasF16C()) {
        float_to_half_var = float_to_half_AVX_F16C;
        LOG_INFO("AVX+F16C instruction set is supported. Using fast f32 to f16 conversion");
    } else {
        float_to_half_var = float_to_half_basic;
        LOG_INFO("AVX+F16C instruction set is not supported. Using basic f32 to f16 conversion");
    }
    (*float_to_half_var)(src, dest, total);
}

void float_to_half(const float *src, std::uint16_t *dest, const int total) {
    (*float_to_half_var)(src, dest, total);
}
#else
void float_to_half(const float *src, std::uint16_t *dest, const int total) {
    float_to_half_basic(src, dest, total);
}
#endif
#endif

// pent0 found on stackoverflow
// https://stackoverflow.com/questions/4398711/round-to-the-nearest-power-of-two/4398799
std::uint32_t next_power_of_two(std::uint32_t num) {
    num--;
    num |= num >> 1;
    num |= num >> 2;
    num |= num >> 4;
    num |= num >> 8;
    num |= num >> 16;
    num++;

    return num;
}

// Encode code taken from https://github.com/yifanlu/UVLoader/blob/master/resolve.c

uint32_t encode_arm_inst(uint8_t type, uint32_t immed, uint16_t reg) {
    switch (type) {
    case INSTRUCTION_MOVW:
        // 1110 0011 0000 XXXX YYYY XXXXXXXXXXXX
        // where X is the immediate and Y is the register
        // Upper bits == 0xE30
        return ((uint32_t)0xE30 << 20) | ((uint32_t)(immed & 0xF000) << 4) | (immed & 0xFFF) | (reg << 12);
    case INSTRUCTION_MOVT:
        // 1110 0011 0100 XXXX YYYY XXXXXXXXXXXX
        // where X is the immediate and Y is the register
        // Upper bits == 0xE34
        return ((uint32_t)0xE34 << 20) | ((uint32_t)(immed & 0xF000) << 4) | (immed & 0xFFF) | (reg << 12);
    case INSTRUCTION_SYSCALL:
        // Syscall does not have any immediate value, the number should
        // already be in R12
        return (uint32_t)0xEF000000;
    case INSTRUCTION_BRANCH:
        // 1110 0001 0010 111111111111 0001 YYYY
        // BX Rn has 0xE12FFF1 as top bytes
        return ((uint32_t)0xE12FFF1 << 4) | reg;
    case INSTRUCTION_BLX:
        return ((uint32_t)0x7D << 25) | ((immed & 0x80000000) >> 8) | (immed & 0x7fffff);
    case INSTRUCTION_UNKNOWN:
    default:
        return 0;
    }
}

inline uint32_t encode_thumb_blx(uint32_t immed) {
    const uint32_t S = (immed & 0x80000000) >> 31;
    const uint32_t I1 = (immed & 0x800000) >> 22;
    const uint32_t I2 = (immed & 0x400000) >> 21;
    const uint32_t immhi = (immed & 0x3ff000) >> 11;
    const uint32_t immlo = (immed & 0xffc) >> 2;
    const uint32_t J1 = ~I1 ^ S;
    const uint32_t J2 = ~I2 ^ S;
    return ((uint32_t)0x1E << 27) | (S << 26) | (immhi << 16) | ((uint32_t)0x3 << 14) | (J1 << 13) | (J2 << 11) | (immlo << 1);
}

uint32_t encode_thumb_inst(uint8_t type, uint32_t immed, uint16_t reg) {
    switch (type) {
    case INSTRUCTION_MOVW:
        return ((uint32_t)0x1E << 27) | ((uint32_t)(immed & 0x800) << 15) | ((uint32_t)0x24 << 20) | ((immed & 0xf000) << 4) | ((immed & 0x700) << 4) | (reg << 8) | (immed & 0xff);
    case INSTRUCTION_MOVT:
        return ((uint32_t)0x1E << 27) | ((uint32_t)(immed & 0x800) << 15) | ((uint32_t)0x2C << 20) | ((immed & 0xf000) << 4) | ((immed & 0x700) << 4) | (reg << 8) | (immed & 0xff);
    case INSTRUCTION_BRANCH:
        return ((((uint32_t)0x8E << 7) | (reg << 3)) << 16) | (uint32_t)0xBF00;
    case INSTRUCTION_BLX:
        return encode_thumb_blx(immed);
    case INSTRUCTION_UNKNOWN:
    default:
        return 0;
    }
}
