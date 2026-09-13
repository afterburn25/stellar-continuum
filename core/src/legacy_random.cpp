// Compatibility adaptation of .NET 8 System.Random.Net5CompatImpl.cs.
// Copyright (c) .NET Foundation and Contributors; MIT (third_party/dotnet/LICENSE.TXT).
#include <stellar/core/legacy_random.hpp>
#include <bit>
#include <limits>
#include <stdexcept>
namespace stellar::core {
namespace {
constexpr int maximum=std::numeric_limits<std::int32_t>::max();
// C# unchecked int arithmetic must not become C++ signed-overflow undefined behavior.
int subtract(int a,int b) { return std::bit_cast<std::int32_t>(std::uint32_t(a)-std::uint32_t(b)); }
int add(int a,int b) { return std::bit_cast<std::int32_t>(std::uint32_t(a)+std::uint32_t(b)); }
}
LegacyRandom::LegacyRandom(std::int32_t seed) {
    int mj=161803398-(seed==std::numeric_limits<std::int32_t>::min()?maximum:(seed<0?-seed:seed));
    seed_array_[55]=mj; int mk=1,ii=0;
    for(int i=1;i<55;++i) {
        if((ii+=21)>=55) ii-=55;
        seed_array_[ii]=mk; mk=subtract(mj,mk); if(mk<0) mk=add(mk,maximum); mj=seed_array_[ii];
    }
    for(int k=1;k<5;++k) for(int i=1;i<56;++i) {
        int n=i+30; if(n>=55) n-=55;
        seed_array_[i]=subtract(seed_array_[i],seed_array_[1+n]);
        if(seed_array_[i]<0) seed_array_[i]=add(seed_array_[i],maximum);
    }
}
int LegacyRandom::next() {
    if(++inext_>=56) inext_=1;
    if(++inextp_>=56) inextp_=1;
    int value=subtract(seed_array_[inext_],seed_array_[inextp_]);
    if(value==maximum) --value;
    if(value<0) value=add(value,maximum);
    seed_array_[inext_]=value; return value;
}
double LegacyRandom::next_double() { return next()*(1.0/maximum); }
int LegacyRandom::next(int exclusive_max) {
    if(exclusive_max<0) throw std::invalid_argument("Random upper bound must be nonnegative");
    return static_cast<int>(next_double()*exclusive_max);
}
std::int32_t population_seed(std::int64_t seed,std::int32_t salt) {
    const auto bits=std::bit_cast<std::uint64_t>(seed);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits^(bits>>32)^std::uint32_t(salt)));
}
}
