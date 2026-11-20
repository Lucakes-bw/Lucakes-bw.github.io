// 《备忘录模式》：压测——1000 次调色存档（每 100 次切一次 256 KB LUT），对比「整份深拷贝」vs「shared_ptr<const> COW 共享」
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int kSaves      = 1000;
constexpr int kSwitchEvery = 100;         // 每 100 次切一次 LUT，共 10 份不同 LUT
constexpr size_t kLutFloats = 65536;      // 65536 * 4 B = 256 KB
constexpr int kRounds     = 7;

struct LutTable {
  std::string name;
  std::vector<float> data;
  explicit LutTable(std::string n, float fill) : name(std::move(n)), data(kLutFloats, fill) {}
};

// 方案 A：整份深拷贝备忘录
struct DeepSnapshot {
  int brightness, contrast, saturation;
  LutTable lut;
};

struct DeepGrader {
  int brightness = 0, contrast = 100, saturation = 100;
  LutTable lut{"LUT_0", 1.0f};
  DeepSnapshot save() const { return {brightness, contrast, saturation, lut}; }
};

// 方案 B：共享不可变大对象（COW）备忘录
struct CowSnapshot {
  int brightness, contrast, saturation;
  std::shared_ptr<const LutTable> lut;
};

struct CowGrader {
  int brightness = 0, contrast = 100, saturation = 100;
  std::shared_ptr<const LutTable> lut = std::make_shared<const LutTable>("LUT_0", 1.0f);
  CowSnapshot save() const { return {brightness, contrast, saturation, lut}; }
};

volatile double g_sink = 0;

double median(std::vector<double>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

int main() {
  std::vector<double> deepNs, cowNs;
  size_t deepBytes = 0, cowBytes = 0;

  // 预建 10 份 LUT（模拟已加载的预设表，避免把构造 LUT 算进 save 耗时）
  std::vector<LutTable> rawLuts;
  std::vector<std::shared_ptr<const LutTable>> sharedLuts;
  for (int i = 0; i < kSaves / kSwitchEvery; ++i) {
    rawLuts.emplace_back("LUT_" + std::to_string(i), 1.0f + i * 0.01f);
    sharedLuts.push_back(std::make_shared<const LutTable>("LUT_" + std::to_string(i), 1.0f + i * 0.01f));
  }

  for (int r = 0; r < kRounds; ++r) {
    // 1. 整份深拷贝
    {
      DeepGrader g;
      std::vector<DeepSnapshot> hist;
      hist.reserve(kSaves);
      double acc = 0;
      auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < kSaves; ++i) {
        if (i % kSwitchEvery == 0) g.lut = rawLuts[i / kSwitchEvery];
        g.brightness = (i % 200) - 100;
        hist.push_back(g.save());
        acc += hist.back().lut.data[0];
      }
      auto t1 = std::chrono::steady_clock::now();
      g_sink += acc;
      double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kSaves;
      deepNs.push_back(ns);
      if (r == 0) {
        deepBytes = hist.size() * sizeof(DeepSnapshot) +
                    hist.size() * kLutFloats * sizeof(float);
      }
    }

    // 2. COW 共享不可变大对象
    {
      CowGrader g;
      std::vector<CowSnapshot> hist;
      hist.reserve(kSaves);
      double acc = 0;
      auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < kSaves; ++i) {
        if (i % kSwitchEvery == 0) g.lut = sharedLuts[i / kSwitchEvery];
        g.brightness = (i % 200) - 100;
        hist.push_back(g.save());
        acc += hist.back().lut->data[0];
      }
      auto t1 = std::chrono::steady_clock::now();
      g_sink += acc;
      double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kSaves;
      cowNs.push_back(ns);
      if (r == 0) {
        std::set<const LutTable*> uniq;
        for (const auto& s : hist) uniq.insert(s.lut.get());
        cowBytes = hist.size() * sizeof(CowSnapshot) +
                   uniq.size() * (sizeof(LutTable) + kLutFloats * sizeof(float));
      }
    }
  }

  double dMed = median(deepNs);
  double cMed = median(cowNs);
  std::printf("1000 次存档（每 100 次切一次 256 KB LUT，%d 轮中位数）：\n", kRounds);
  std::printf("  整份深拷贝：单次 save = %8.1f ns (%.2f us) | 总内存 = %6.2f MB | sizeof(Snapshot) = %zu B\n",
              dMed, dMed / 1000.0, deepBytes / (1024.0 * 1024.0), sizeof(DeepSnapshot));
  std::printf("  COW 共享  ：单次 save = %8.1f ns (%.2f us) | 总内存 = %6.2f MB | sizeof(Snapshot) = %zu B\n",
              cMed, cMed / 1000.0, cowBytes / (1024.0 * 1024.0), sizeof(CowSnapshot));
  std::printf("  倍率对比  ：耗时加速 %.1fx | 内存降至 %.1f%%\n",
              dMed / cMed, 100.0 * cowBytes / deepBytes);
}
