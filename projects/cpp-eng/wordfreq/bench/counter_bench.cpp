#include <benchmark/benchmark.h>

#include "text_gen.h"
#include "wordfreq/counter.h"
#include "wordfreq/tokenizer.h"

namespace {

const std::string& text(std::size_t bytes)
{
    static std::string cache;
    if (cache.size() < bytes) cache = generateText(bytes);
    return cache;
}

void BM_Tokenize(benchmark::State& state)
{
    const std::string& t = text(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        auto words = wordfreq::tokenize(t);
        benchmark::DoNotOptimize(words.data());
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Tokenize)->Arg(1 << 20)->Unit(benchmark::kMillisecond);

void BM_CounterAdd(benchmark::State& state)
{
    const std::string& t = text(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        wordfreq::Counter c;
        c.add(t);
        benchmark::DoNotOptimize(c.total());
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_CounterAdd)->Arg(1 << 20)->Arg(16 << 20)->Unit(benchmark::kMillisecond);

void BM_CountParallel(benchmark::State& state)
{
    const std::string& t = text(16 << 20);
    for (auto _ : state) {
        auto c = wordfreq::countParallel(t, static_cast<unsigned>(state.range(0)));
        benchmark::DoNotOptimize(c.total());
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(t.size()));
}
BENCHMARK(BM_CountParallel)->Arg(1)->Arg(2)->Arg(4)->Unit(benchmark::kMillisecond)->UseRealTime();

}  // namespace

BENCHMARK_MAIN();
