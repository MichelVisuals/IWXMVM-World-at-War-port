#pragma once
#include "StdInclude.hpp"
#include "Mod.hpp"

// T4-local fork of core's Signatures.hpp. Vendored because core/ stays 1:1
// with upstream IWXMVM (no VirtualQuery-aware scanner, throws on miss, no
// HardAddr) but t4 needs all three to function:
//
// 1. CoDWaWmp.exe has a huge virtual .data section with many uncommitted /
//    PAGE_NOACCESS pages. Reading those triggers an access violation that
//    takes down init — upstream's linear scan crashes; this scanner walks
//    only committed readable regions via VirtualQuery.
// 2. Many T4 signatures are unverified placeholders; throwing on miss
//    kills the whole init pass. Return 0 on miss + log a warning instead.
// 3. HardAddr<address> is the API-compatible alternative for addresses
//    confirmed via external RE (Ghidra, t4-rtx).
//
// This is intentionally a near-duplicate of upstream's Signatures.hpp —
// any upstream improvements should be backported manually.

namespace IWXMVM::T4::Signatures
{
    namespace Lambdas
    {
        inline auto IsAbsoluteJumpOrCall = [](std::uintptr_t address) {
            static constexpr std::uint8_t JUMP_OPCODE = 0xE9;
            static constexpr std::uint8_t CALL_OPCODE = 0xE8;

            if (*reinterpret_cast<std::uint8_t*>(address) == JUMP_OPCODE ||
                *reinterpret_cast<std::uint8_t*>(address) == CALL_OPCODE)
                return true;
            else
                return false;
        };

        inline auto DereferenceAddress = [](std::uintptr_t address) {
            return *reinterpret_cast<std::uintptr_t*>(address);
        };

        inline auto DereferenceCallOffset = [](std::uintptr_t address) {
            return *reinterpret_cast<std::uintptr_t*>(address + 1) + 5;
        };

        inline auto FollowCodeFlow = [](std::uintptr_t address) {
            const std::uintptr_t orgAddress = address;

            while (IsAbsoluteJumpOrCall(address))
                address += DereferenceCallOffset(address);

            return (address == orgAddress) ? 0 : address;
        };
    }  // namespace Lambdas

    inline constexpr std::uint16_t maskValue = UINT8_MAX + 1;

    enum struct GameAddressType : std::uint8_t
    {
        Data = 4,
        Code = 5
    };

    inline constexpr void CheckMaskCountAndOffset(const auto& bytes, std::size_t frontMaskCount, std::intptr_t offset,
                                                  GameAddressType type)
    {
        const std::size_t unsOffset = (offset >= 0) ? static_cast<std::size_t>(offset) : 0 - offset;

        if (unsOffset >= bytes.size())
            return;

        const std::size_t requiredMaskCount = static_cast<std::size_t>(type);

        if (offset >= 0)
        {
            std::size_t count = 0;

            for (std::size_t i = unsOffset; i < bytes.size() && i < unsOffset + requiredMaskCount; ++i)
            {
                if (bytes[i] == maskValue)
                    ++count;
            }

            if (count < requiredMaskCount)
                throw std::runtime_error("Incorrect signature.");
        }
        else
        {
            if (unsOffset >= requiredMaskCount)
                return;

            if (unsOffset + frontMaskCount < requiredMaskCount)
                throw std::runtime_error("Incorrect signature.");
        }
    }

    inline constexpr std::size_t GetFrontMaskCount(const auto& bytes)
    {
        std::size_t count = 0;

        for (const auto byte : bytes)
        {
            if (byte == maskValue)
                ++count;
            else
                break;
        }

        if (count >= bytes.size())
            throw std::runtime_error("Number of masks equals or exceeds total length of signature.");

        return count;
    };

    inline constexpr std::size_t HexToDecimal(char c)
    {
        constexpr std::array<std::uint8_t, 4> boundaries{'0', '9', 'A', 'F'};
        const std::uint8_t val = static_cast<std::uint8_t>(c);

        if (val >= boundaries[0] && val <= boundaries[1])  // 0-9
            return val - boundaries[0];

        if (val >= boundaries[2] && val <= boundaries[3])  // A-F
            return val - boundaries[2] + 10;

        throw std::runtime_error("Not a hexadecimal value.");
    }

    template <std::size_t size>
    constexpr auto ConvertStringToBytes(const char* byteString)
    {
        std::array<std::uint16_t, (size + 2) / 3> bytes;
        bytes.fill(maskValue + 1);

        if constexpr (bytes.size() <= 4)
            throw std::runtime_error("Signature length is too short.");

        for (std::size_t strIndex = 0, byteIndex = 0; strIndex + 1 < size;)
        {
            if (byteString[strIndex] == ' ')
            {
                ++strIndex;
                continue;
            }

            if (byteString[strIndex] == '?')
            {
                bytes[byteIndex] = maskValue;

                if (byteString[strIndex + 1] == '?')
                    ++strIndex;
                else
                    throw std::runtime_error("Single mask character detected.");

                ++byteIndex, ++strIndex;
                continue;
            }

            if (byteIndex >= (size + 2) / 3)
                throw std::runtime_error("Incorrect signature input.");

            bytes[byteIndex++] =
                static_cast<uint16_t>((HexToDecimal(byteString[strIndex]) << 4) + HexToDecimal(byteString[strIndex + 1]));
            strIndex += 2;
        }

        if (std::find(bytes.begin(), bytes.end(), maskValue + 1) != bytes.end())
            throw std::runtime_error("Signature parsed incorrectly.");

        if (bytes.back() == maskValue)
            throw std::runtime_error("Incorrect signature input.");

        return bytes;
    }

    inline std::uintptr_t SignatureScanner(const auto& signature, const auto& moduleHandles)
    {
        constexpr DWORD READABLE =
            PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

        for (const auto handle : moduleHandles)
        {
            MODULEINFO process{};

            if (!::GetModuleInformation(::GetCurrentProcess(), handle, &process, sizeof(process)) ||
                !process.lpBaseOfDll)
                return 0;

            const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(process.lpBaseOfDll);
            const std::uintptr_t endOfDll = imageBase + process.SizeOfImage;
            const std::size_t sigLen = signature._bytes.size();

            std::uintptr_t i = imageBase;
            while (i < endOfDll)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (::VirtualQuery(reinterpret_cast<LPCVOID>(i), &mbi, sizeof(mbi)) == 0)
                    break;

                const std::uintptr_t regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                const std::uintptr_t regionEnd = regionStart + mbi.RegionSize;

                const bool readable = (mbi.State == MEM_COMMIT) && ((mbi.Protect & READABLE) != 0) &&
                                      ((mbi.Protect & PAGE_GUARD) == 0);

                if (!readable)
                {
                    i = regionEnd;
                    continue;
                }

                const std::uintptr_t scanEnd = (regionEnd > sigLen) ? (regionEnd - sigLen) : 0;
                const std::uintptr_t scanLimit = (scanEnd < endOfDll) ? scanEnd : endOfDll;
                for (; i <= scanLimit; ++i)
                {
                    std::size_t j = signature._frontMaskCount;
                    for (; j < sigLen; ++j)
                    {
                        if (signature._bytes[j] != maskValue &&
                            signature._bytes[j] != *reinterpret_cast<std::uint8_t*>(i + j))
                            break;
                    }
                    if (j == sigLen)
                        return i + signature._offset;
                }
                i = regionEnd;
            }
        }

        return 0;
    }

    using callable_t = decltype([]() {});

    template <std::size_t size, typename Callable = callable_t>
    struct SignatureImpl
    {
        constexpr SignatureImpl(const char (&str)[size], GameAddressType type, std::intptr_t offset = 0,
                                Callable callable = callable_t{}) noexcept
            : _string(std::to_array(str)),
              _bytes(ConvertStringToBytes<size>(str)),
              _frontMaskCount(GetFrontMaskCount(_bytes)),
              _offset(offset),
              _callable(callable)
        {
            CheckMaskCountAndOffset(_bytes, _frontMaskCount, _offset, type);

            static_assert(size > 0);
        }

        auto Scan(const auto& moduleHandles) const
        {
            const std::uintptr_t address = SignatureScanner(*this, moduleHandles);

            if (address == 0)
            {
                LOG_WARN("Signature scan failed (continuing): {}", _string.data());
                return std::uintptr_t{0};
            }

            if constexpr (requires { std::declval<Callable>()(address); })
            {
                try
                {
                    const std::uintptr_t newAddress = _callable(address);
                    if (newAddress == 0)
                    {
                        LOG_WARN("Signature post-process returned 0: {}", _string.data());
                        return std::uintptr_t{0};
                    }
                    return newAddress;
                }
                catch (...)
                {
                    LOG_WARN("Signature post-process threw: {}", _string.data());
                    return std::uintptr_t{0};
                }
            }
            else
            {
                return address;
            }
        }

        std::array<char, size> _string{};
        std::array<std::uint16_t, (size + 2) / 3> _bytes{};
        std::size_t _frontMaskCount{};
        std::intptr_t _offset{};
        Callable _callable;
    };

    template <auto intSignature, Types::ModuleType type = Types::ModuleType::BaseModule>
    struct Signature
    {
        constexpr Signature()
        {
            if (const auto modules = Mod::GetGameInterface()->GetModuleHandles(type); modules.has_value())
                _address = _signature.Scan(modules.value());
        }

        static constexpr auto _signature = intSignature;
        std::uintptr_t _address{};

        std::uintptr_t operator()() const
        {
            return GetAddress();
        }

        std::uintptr_t GetAddress() const
        {
            return _address;
        }
    };

    template <std::uintptr_t address>
    struct HardAddr
    {
        constexpr HardAddr() = default;
        constexpr std::uintptr_t operator()() const { return address; }
        constexpr std::uintptr_t GetAddress() const { return address; }
    };
}  // namespace IWXMVM::T4::Signatures
