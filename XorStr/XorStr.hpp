#pragma once

// ─── Optional feature defines ─────────────────────────────────────────────────
//  XS_ENABLE_BLOAT    — inject stack/SIMD junk into every crypt_get() call
//  XS_ENABLE_BIGSTACK — inject decompiler-crash logic into every crypt_get()
// ─────────────────────────────────────────────────────────────────────────────

#include <immintrin.h>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <type_traits>

#ifdef XS_ENABLE_BLOAT
#  include <vector>
#  include <string>
#  include <sstream>
#  include <functional>
#  include <iomanip>
#endif

// ─── Public macros ────────────────────────────────────────────────────────────

#define xorstr_(str) FrameWork::Security::XorStr::xor_string( \
    []() { return str; }, \
    std::integral_constant<std::size_t, sizeof(str) / sizeof(*str)>{}, \
    std::integral_constant<std::size_t, __COUNTER__>{}, \
    std::make_index_sequence<FrameWork::Security::XorStr::Detail::_buffer_size<sizeof(str)>()>{})

#define xorstr_with(str, fn) (xorstr_(str).with(fn))

#define _(str)  xorstr_(str).crypt_get()
#define _w(str) xorstr_(str).crypt_get()

// Alias para compatibilidade com o código existente que usa xorstr("str")
#define xorstr(str) _(str)

#define XS_APPLY_BLOAT \
    FrameWork::Security::XorStr::Obfuscation::Apply<static_cast<int>(__COUNTER__ * 0x9E3779B9UL)>()
#define XS_APPLY_BIGSTACK \
    FrameWork::Security::XorStr::Obfuscation::DecompilerCrashStack<static_cast<int>(__COUNTER__ * 0x517CC1B727220A95ULL)>()

namespace FrameWork {
    namespace Security {
        namespace XorStr {
            namespace Detail {

                template<std::size_t Size>
                __forceinline constexpr std::size_t _buffer_size() {
                    return ((Size / 16) + (Size % 16 != 0)) * 2;
                }

                __forceinline constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
                    x += 0x9E3779B97F4A7C15ULL;
                    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
                    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
                    return x ^ (x >> 31);
                }

                template<std::uint32_t Seed>
                __forceinline constexpr std::uint32_t key4() noexcept {
                    std::uint32_t v = Seed;
                    for (char c : __TIME__) v = static_cast<std::uint32_t>((v ^ static_cast<unsigned char>(c)) * 16777619ULL);
                    for (char c : __DATE__) v = static_cast<std::uint32_t>((v ^ static_cast<unsigned char>(c)) * 16777619ULL);
                    for (char c : __FILE__) v = static_cast<std::uint32_t>((v ^ static_cast<unsigned char>(c)) * 16777619ULL);
                    return v;
                }

                template<std::size_t S, std::size_t UniqueId>
                __forceinline constexpr std::uint64_t key8() noexcept {
                    constexpr auto seed   = static_cast<std::uint32_t>(2166136261UL ^ (UniqueId * 0x9e3779b9UL) ^ (S * 0x1000193UL));
                    constexpr auto first  = key4<seed>();
                    constexpr auto second = key4<first ^ static_cast<std::uint32_t>(UniqueId)>();
                    return (static_cast<std::uint64_t>(first) << 32) | second;
                }

                template<std::size_t S, std::size_t UniqueId>
                __forceinline constexpr std::uint64_t combined_key() noexcept {
                    constexpr std::uint64_t k1 = key8<S, UniqueId>();
                    constexpr std::uint64_t k2 = splitmix64(k1);
                    constexpr std::uint64_t iv = splitmix64(k2);
                    return k1 ^ ((k2 << 3) & (k1 << 6)) ^ (iv ^ 0xFC11ULL);
                }

                inline std::uint64_t xs_runtime_key() noexcept {
                    static const std::uint64_t key = splitmix64(
                        reinterpret_cast<std::uint64_t>(&xs_runtime_key)
                    );
                    return key;
                }

                template<std::size_t N, class CharT>
                __forceinline constexpr std::uint64_t load_xored_str8(std::uint64_t key, std::size_t idx, const CharT* str) noexcept {
                    using cast_type = typename std::make_unsigned<CharT>::type;
                    constexpr auto value_size = sizeof(CharT);
                    constexpr auto idx_offset = 8 / value_size;

                    std::uint64_t value = key;
                    for (std::size_t i = 0; i < idx_offset && i + idx * idx_offset < N; ++i)
                        value ^= (std::uint64_t{static_cast<cast_type>(str[i + idx * idx_offset])} << ((i % idx_offset) * 8 * value_size));
                    return value;
                }

                __forceinline std::uint64_t load_from_reg(std::uint64_t value) noexcept {
#if defined(__clang__) || defined(__GNUC__)
                    asm("" : "=r"(value) : "0"(value) : "memory");
                    return value;
#else
                    volatile std::uint64_t reg = value;
                    return reg;
#endif
                }

                template<int N>
                __forceinline bool always_true() noexcept {
                    volatile int n = N;
                    return (load_from_reg(static_cast<std::uint64_t>(n)) ^ load_from_reg(static_cast<std::uint64_t>(n))) == 0;
                }

            } // namespace Detail

            namespace Obfuscation {

                namespace SeedCache {
                    static constexpr std::uint64_t Seed = []() constexpr -> std::uint64_t {
                        std::uint64_t h = 14695981039346656037ULL;
                        for (char c : __TIME__) h = (h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c))) * 1099511628211ULL;
                        for (char c : __DATE__) h = (h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c))) * 1099511628211ULL;
                        return h;
                    }();
                    __forceinline constexpr std::uint64_t Get() noexcept { return Seed; }
                }

                template<int N>
                __forceinline void StackBloat() noexcept {
                    volatile int           x     = N;
                    volatile std::uint64_t dummy  = SeedCache::Get();

                    auto confuse_branch = [](int v) -> int {
                        volatile std::uint64_t s =
                            (static_cast<std::uint64_t>(v) * (SeedCache::Get() | 0x7F2E3D1C5A9B8F07ULL))
                            ^ (SeedCache::Get() | 0xC4D3E2F1A0B9C8D7ULL);
                        return static_cast<int>(s % 6);
                    };

                    switch (confuse_branch(x)) {
#define XS_DISPATCH(i) \
    case i: goto xs_hop1_##i; \
    xs_hop1_##i: goto xs_hop2_##i; \
    xs_hop2_##i: goto xs_label##i;
                    XS_DISPATCH(0) XS_DISPATCH(1) XS_DISPATCH(2)
                    XS_DISPATCH(3) XS_DISPATCH(4) XS_DISPATCH(5)
#undef XS_DISPATCH
                    default: goto xs_end;
                    }

#define XS_LABEL(i) \
    xs_label##i: { \
        volatile std::uint64_t lx = static_cast<std::uint64_t>(i * 2); \
        volatile std::uint64_t ly = lx + 1; \
        volatile std::uint64_t lz = ly + 3; \
        for (int j = 0; j < 5; ++j) { \
            volatile std::uint64_t a = SeedCache::Get() ^ (lx + static_cast<std::uint64_t>(j)); \
            if ((a & 1) == 0) { \
                if ((a ^ ly) % 3 == 0) { \
                    if (((a + lz) & 7) != 4) { dummy ^= a & ly; dummy ^= Detail::load_from_reg(a); } \
                    else                      { dummy ^= lx | lz; } \
                } else { dummy ^= ly | a; } \
            } else { \
                if ((a | lx) % 5 == 1) { \
                    if (((lz ^ a) & 0xF) == 2) { dummy ^= lz & ly; } \
                    else                        { dummy ^= lx | lx; } \
                } else { dummy ^= a | lz; } \
            } \
        } \
        dummy ^= Detail::load_from_reg(lx); \
        dummy ^= Detail::load_from_reg(ly); \
        dummy ^= Detail::load_from_reg(lz); \
        goto xs_end; \
    }
                    XS_LABEL(0) XS_LABEL(1) XS_LABEL(2)
                    XS_LABEL(3) XS_LABEL(4) XS_LABEL(5)
#undef XS_LABEL
                xs_end:;
                    (void)dummy;
                }

                template<int N>
                __forceinline void LogicBloatA() noexcept {
                    volatile int x = 0;
                    constexpr std::uint64_t val1 = SeedCache::Get() ^ 0x6D24B3A58F7E1C90ULL;
#if defined(__AVX2__)
                    alignas(32) std::uint64_t data[8] = {};
                    __m256i vec = _mm256_set1_epi64x(static_cast<long long>(val1));
                    for (int i = 0; i < 8; i += 4) {
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&data[i]), vec);
                        vec = _mm256_xor_si256(vec, _mm256_set1_epi64x(i ^ N));
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(data[0]);
#else
                    alignas(16) std::uint64_t data[4] = {};
                    __m128i vec = _mm_set1_epi64x(static_cast<long long>(val1));
                    for (int i = 0; i < 4; i += 2) {
                        _mm_store_si128(reinterpret_cast<__m128i*>(&data[i]), vec);
                        vec = _mm_xor_si128(vec, _mm_set1_epi64x(i ^ N));
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(data[0]);
#endif
                    x = static_cast<int>(Detail::load_from_reg(sink));
                    (void)x;
                }

                template<int N>
                __forceinline void LogicBloatB() noexcept {
                    volatile int x = 0;
                    constexpr std::uint64_t val1 = SeedCache::Get() ^ 0x8A3E6B59247D1C0FULL;
#if defined(__AVX2__)
                    alignas(32) std::uint64_t arr[8] = {};
                    __m256i v = _mm256_set1_epi64x(static_cast<long long>(val1));
                    for (int i = 0; i < 8; i += 4) {
                        v = _mm256_add_epi64(v, _mm256_set1_epi64x(i));
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&arr[i]), v);
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(arr[7]);
#else
                    alignas(16) std::uint64_t arr[4] = {};
                    __m128i v = _mm_set1_epi64x(static_cast<long long>(val1));
                    for (int i = 0; i < 4; i += 2) {
                        v = _mm_add_epi64(v, _mm_set1_epi64x(i));
                        _mm_store_si128(reinterpret_cast<__m128i*>(&arr[i]), v);
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(arr[3]);
#endif
                    x = static_cast<int>(Detail::load_from_reg(sink));
                    (void)x;
                }

                template<int N>
                __forceinline void LogicBloatC() noexcept {
                    volatile int x = 0;
#if defined(__AVX2__)
                    alignas(32) std::uint64_t buf[8] = {};
                    __m256i v = _mm256_setzero_si256();
                    int toggle = 0;
                    for (int i = 0; i < 8; i += 4) {
                        if (toggle) v = _mm256_or_si256(v, _mm256_set1_epi64x(i));
                        else        v = _mm256_and_si256(v, _mm256_set1_epi64x(~i));
                        _mm256_store_si256(reinterpret_cast<__m256i*>(&buf[i]), v);
                        toggle = !toggle;
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(buf[0]);
#else
                    alignas(16) std::uint64_t buf[4] = {};
                    __m128i v = _mm_setzero_si128();
                    int toggle = 0;
                    for (int i = 0; i < 4; i += 2) {
                        if (toggle) v = _mm_or_si128(v, _mm_set1_epi64x(i));
                        else        v = _mm_and_si128(v, _mm_set1_epi64x(~i));
                        _mm_store_si128(reinterpret_cast<__m128i*>(&buf[i]), v);
                        toggle = !toggle;
                    }
                    volatile std::uint64_t sink = Detail::load_from_reg(buf[0]);
#endif
                    x = static_cast<int>(Detail::load_from_reg(sink));
                    (void)x;
                }

#ifdef XS_ENABLE_BLOAT
                __forceinline void StdBloat() noexcept {
                    std::vector<std::pair<std::string, int>> data;
                    std::stringstream ss;
                    for (int i = 0; i < 16; ++i) {
                        ss.str({}); ss.clear();
                        ss << std::hex << std::setw(4) << std::setfill('0') << (i * 1337);
                        data.emplace_back(ss.str(), i);
                    }
                    std::function<int(const std::pair<std::string, int>&)> fn =
                        std::bind([](const std::pair<std::string, int>& p) -> int {
                            std::stringstream inner; inner << p.first;
                            int out; inner >> std::hex >> out;
                            return out ^ (p.second << 2);
                        }, std::placeholders::_1);
                    volatile int dummy = 0;
                    for (const auto& e : data) dummy += fn(e) & 0xFF;
                    (void)dummy;
                }
#endif

                template<int Key>
                __forceinline void BloatRandom() noexcept {
                    constexpr int choice = Key % 3;
                    if constexpr (choice == 0)      LogicBloatA<(Key % 24) + 8>();
                    else if constexpr (choice == 1) LogicBloatB<(Key % 21) + 4>();
                    else                            LogicBloatC<(Key % 24) + 8>();
                }

                template<int Key>
                __forceinline void Apply() noexcept {
                    if (Detail::always_true<Key>()) return;
                    constexpr int sz = (Key % 24) + 12;
                    StackBloat<sz>();
                    BloatRandom<Key>();
                    StackBloat<sz>();
#ifdef XS_ENABLE_BLOAT
                    StdBloat();
#endif
                    StackBloat<sz>();
                    BloatRandom<Key>();
                    StackBloat<sz>();
                }

                template<int Value>
                __forceinline void DecompilerCrashStack() noexcept {
                    if (Detail::always_true<Value>()) return;
                    volatile std::uint64_t crash_buffer[2];
                    using Fn = int(*)(int);
                    auto* ptr = const_cast<std::uint64_t*>(crash_buffer) + ((Value % 0x400000) + 0x4000000);
                    Fn fn = reinterpret_cast<Fn>(ptr);
                    Detail::load_from_reg(reinterpret_cast<std::uint64_t>(fn));
                }

            } // namespace Obfuscation

            template<class CharT, std::size_t Size, std::size_t UniqueId, class Keys, class Indices>
            class xor_string;

            template<class CharT, std::size_t Size, std::size_t UniqueId,
                     std::uint64_t... Keys, std::size_t... Indices>
            class xor_string<CharT, Size, UniqueId,
                             std::integer_sequence<std::uint64_t, Keys...>,
                             std::index_sequence<Indices...>> {

#if defined(__AVX2__)
                constexpr static inline std::size_t alignment = 32;
#else
                constexpr static inline std::size_t alignment = 16;
#endif
                alignas(alignment) std::uint64_t _storage[sizeof...(Keys)];

            public:
                using value_type    = CharT;
                using size_type     = std::size_t;
                using pointer       = CharT*;
                using const_pointer = const CharT*;

                template<class L>
                __forceinline xor_string(L l,
                    std::integral_constant<std::size_t, Size>,
                    std::integral_constant<std::size_t, UniqueId>,
                    std::index_sequence<Indices...>) noexcept
                    : _storage{ Detail::load_from_reg(
                        (std::integral_constant<std::uint64_t,
                            Detail::load_xored_str8<Size>(Keys, Indices, l())>::value))... }
                {
                    const std::uint64_t rk = Detail::xs_runtime_key();
                    for (auto& slot : _storage) slot ^= rk;
                }

                __forceinline ~xor_string() noexcept {
                    volatile auto* ptr = reinterpret_cast<volatile std::uint8_t*>(_storage);
                    for (std::size_t i = 0; i < sizeof(_storage); ++i)
                        ptr[i] = 0u;
                }

                xor_string(const xor_string&)            = delete;
                xor_string& operator=(const xor_string&) = delete;

                __forceinline constexpr size_type size() const noexcept { return Size - 1; }

                __forceinline void crypt() noexcept {
                    alignas(alignment) std::uint64_t keys[]{ Detail::load_from_reg(Keys)... };
                    _do_xor(keys);
                }

                __forceinline void lock() noexcept {
                    alignas(alignment) std::uint64_t keys[]{ Detail::load_from_reg(Keys)... };
                    _do_xor(keys);
                }

                __forceinline const_pointer get() const noexcept {
                    return reinterpret_cast<const_pointer>(_storage);
                }

                __forceinline pointer get() noexcept {
                    return reinterpret_cast<pointer>(_storage);
                }

                __forceinline pointer crypt_get() noexcept {
#ifdef XS_ENABLE_BIGSTACK
                    Obfuscation::DecompilerCrashStack<static_cast<int>(UniqueId * 0x9E3779B9UL)>();
#endif
                    alignas(alignment) std::uint64_t keys[]{ Detail::load_from_reg(Keys)... };
                    _do_xor(keys);
#ifdef XS_ENABLE_BLOAT
                    Obfuscation::Apply<static_cast<int>(UniqueId * 0x517CC1B727220A95ULL)>();
#endif
                    return reinterpret_cast<pointer>(_storage);
                }

                template<typename Fn>
                __forceinline auto with(Fn&& fn) noexcept
                    -> std::invoke_result_t<Fn, const_pointer>
                {
                    alignas(alignment) std::uint64_t keys[]{ Detail::load_from_reg(Keys)... };
                    _do_xor(keys);
                    if constexpr (!std::is_void_v<std::invoke_result_t<Fn, const_pointer>>) {
                        auto result = std::forward<Fn>(fn)(static_cast<const_pointer>(reinterpret_cast<pointer>(_storage)));
                        _do_xor(keys);
                        return result;
                    } else {
                        std::forward<Fn>(fn)(static_cast<const_pointer>(reinterpret_cast<pointer>(_storage)));
                        _do_xor(keys);
                    }
                }

            private:
                __forceinline void _do_xor(std::uint64_t* keys) noexcept {
                    constexpr std::size_t n16 = sizeof...(Keys) / 2;
                    const std::uint64_t rk = Detail::xs_runtime_key();

#if defined(__AVX2__)
                    const __m256i rk256 = _mm256_set1_epi64x(static_cast<long long>(rk));
                    constexpr std::size_t n32 = n16 / 2;
                    for (std::size_t i = 0; i < n32; ++i) {
                        _mm256_store_si256(
                            reinterpret_cast<__m256i*>(_storage) + i,
                            _mm256_xor_si256(
                                _mm256_load_si256(reinterpret_cast<const __m256i*>(_storage) + i),
                                _mm256_xor_si256(
                                    _mm256_load_si256(reinterpret_cast<const __m256i*>(keys) + i),
                                    rk256)));
                    }
                    if constexpr (n16 % 2 != 0) {
                        const __m128i rk128 = _mm_set1_epi64x(static_cast<long long>(rk));
                        _mm_store_si128(
                            reinterpret_cast<__m128i*>(_storage) + (n16 - 1),
                            _mm_xor_si128(
                                _mm_load_si128(reinterpret_cast<const __m128i*>(_storage) + (n16 - 1)),
                                _mm_xor_si128(
                                    _mm_load_si128(reinterpret_cast<const __m128i*>(keys) + (n16 - 1)),
                                    rk128)));
                    }
#else
                    const __m128i rk128 = _mm_set1_epi64x(static_cast<long long>(rk));
                    ((Indices >= n16 ? static_cast<void>(0) : _mm_store_si128(
                        reinterpret_cast<__m128i*>(_storage) + Indices,
                        _mm_xor_si128(
                            _mm_load_si128(reinterpret_cast<const __m128i*>(_storage) + Indices),
                            _mm_xor_si128(
                                _mm_load_si128(reinterpret_cast<const __m128i*>(keys) + Indices),
                                rk128)))) , ...);
#endif
                }
            };

            template<class L, std::size_t Size, std::size_t UniqueId, std::size_t... Indices>
            xor_string(L l,
                std::integral_constant<std::size_t, Size>,
                std::integral_constant<std::size_t, UniqueId>,
                std::index_sequence<Indices...>) -> xor_string<
                    std::remove_const_t<std::remove_reference_t<decltype(l()[0])>>,
                    Size,
                    UniqueId,
                    std::integer_sequence<std::uint64_t, Detail::combined_key<Indices, UniqueId>()...>,
                    std::index_sequence<Indices...>>;

        } // namespace XorStr
    } // namespace Security
} // namespace FrameWork
