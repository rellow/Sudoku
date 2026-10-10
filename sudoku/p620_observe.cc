// P620 observation-only memory timing and provenance collector.
// No row activation schedules, refresh modification, or disturbance patterns.
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/mempolicy.h>
#include "internal/assembly.h"
#include "internal/pool.h"

struct Options {
  std::string output;
  std::string run_id;
  std::string revision = "unknown";
  uint64_t pages = 4;
  uint64_t pairs_per_frame = 4096;
  uint64_t trials = 64;
  uint64_t seed = 515151;
  int expected_numa = -1;
  int expected_cpu = -1;
};
struct Frame {
  uint64_t pa_base = 0;
  int numa_node = -1;
  char* va_base = nullptr;
};
static bool to_u64(const char* s, uint64_t* out) {
  if (!s || !*s || *s == '-') return false;
  char* end = nullptr;
  errno = 0;
  unsigned long long v = strtoull(s, &end, 0);
  if (errno || *end || end == s) return false;
  *out = static_cast<uint64_t>(v);
  return true;
}
static bool to_int(const char* s, int* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno || *end || end == s || v < -1 || v > 65535) return false;
  *out = static_cast<int>(v);
  return true;
}
static void usage() {
  std::cerr << "Usage: p620_observe --output PREFIX [--run-id ID]"
            << " [--pages N] [--pairs-per-frame N] [--trials N]"
            << " [--seed N] [--expected-numa N] [--expected-cpu N]"
            << " [--source-revision SHA]\n"
            << "Passive raw timing only; requires 1-GiB hugepages and privileges"
            << " to read pagemap PFNs. Run under numactl.\n";
}
static std::string hex64(uint64_t x) {
  std::ostringstream o;
  o << "0x" << std::hex << x;
  return o.str();
}
static std::string json_string(const std::string& s) {
  std::ostringstream o; o << '"';
  for (unsigned char c : s) {
    if (c == '\\' || c == '"') { o << '\\' << c; }
    else if (c == '\n') o << "\\n";
    else if (c == '\r') o << "\\r";
    else if (c < 32) o << '?';
    else o << c;
  }
  o << '"'; return o.str();
}
static int node_of(void* p) {
#ifdef SYS_get_mempolicy
  int node = -1;
  long rc = syscall(SYS_get_mempolicy, &node, nullptr, 0,
                    p, MPOL_F_NODE | MPOL_F_ADDR);
  return rc == 0 ? node : -1;
#else
  (void)p; return -1;
#endif
}
static uint64_t percentile(const std::vector<uint64_t>& sorted, unsigned p) {
  size_t index = static_cast<size_t>(
      (static_cast<unsigned long long>(sorted.size() - 1) * p) / 100);
  return sorted[index];
}
static bool parse(int argc, char** argv, Options* o) {
  for (int i=1; i<argc; ++i) {
    std::string key=argv[i];
    if (key=="--help" || key=="-h") { usage(); exit(0); }
    if (i+1>=argc) return false;
    const char* val=argv[++i];
    uint64_t n=0; int nn=0;
    if (key=="--output") o->output=val;
    else if (key=="--run-id") o->run_id=val;
    else if (key=="--source-revision") o->revision=val;
    else if (key=="--pages" && to_u64(val,&n)) o->pages=n;
    else if (key=="--pairs-per-frame" && to_u64(val,&n)) o->pairs_per_frame=n;
    else if (key=="--trials" && to_u64(val,&n)) o->trials=n;
    else if (key=="--seed" && to_u64(val,&n)) o->seed=n;
    else if (key=="--expected-numa" && to_int(val,&nn)) o->expected_numa=nn;
    else if (key=="--expected-cpu" && to_int(val,&nn)) o->expected_cpu=nn;
    else return false;
  }
  return !o->output.empty() && o->pages >= 2 && o->pages <= 64 &&
         o->pairs_per_frame > 0 && o->pairs_per_frame <= 1000000 &&
         o->trials > 0 && o->trials <= 100000 &&
         o->pages <= UINT64_MAX/o->pairs_per_frame &&
         o->pages * o->pairs_per_frame <= UINT64_MAX/o->trials;
}
int main(int argc,char** argv) {
#ifndef COMPILE_P620_ZEN3_DDR4
  (void)argc; (void)argv;
  std::cerr << "p620_observe requires the P620_ZEN3_DDR4 build target\n";
  return 2;
#else
  Options opt;
  if (!parse(argc,argv,&opt)) { usage(); return 2; }
  if (geteuid()!=0) {
    std::cerr << "Run with root privileges for physical-frame observations\n";
    return 2;
  }
  if (opt.run_id.empty()) {
    std::ostringstream id;
    id << std::time(nullptr) << "-" << getpid();
    opt.run_id=id.str();
  }
  // Use exclusive creation to avoid accidentally combining independent runs.
  for (const auto& suffix : {".raw.csv",".summary.csv",".manifest.json"}) {
    std::ifstream existing(opt.output+suffix);
    if (existing.good()) {
      std::cerr << "Refusing to overwrite " << opt.output+suffix << "\n";
      return 2;
    }
  }
  MemoryPoolConfig cfg(1ULL<<30,opt.pages,64,true);
  MemoryPool pool(&cfg);
  if (!InitMemoryPool(&pool)) {
    std::cerr << "Unable to allocate 1-GiB hugepage pool\n";
    return 2;
  }
  std::vector<Frame> frames;
  std::set<uint64_t> physical_bases;
  for (uint64_t i=0;i<opt.pages;++i) {
    Frame f;
    f.va_base=pool.pages[i];
    // Access a page to ensure it is present.
    *(volatile unsigned char*)f.va_base = 0;
    f.pa_base=VirtToPhys(reinterpret_cast<uint64_t>(f.va_base));
    const auto tail=VirtToPhys(reinterpret_cast<uint64_t>(
         f.va_base+(1ULL<<30)-4096));
    if ((f.pa_base & ((1ULL<<30)-1)) != 0 ||
        tail != f.pa_base + (1ULL<<30) - 4096 ||
        !physical_bases.insert(f.pa_base).second) {
      std::cerr << "Physical frame not aligned/contiguous/unique\n";
      FreeMemoryPool(&pool); return 2;
    }
    f.numa_node=node_of(f.va_base);
    if (opt.expected_numa>=0 && f.numa_node>=0 &&
        f.numa_node!=opt.expected_numa) {
      std::cerr << "Unexpected NUMA node for frame " << i << ": "
                << f.numa_node << "\n";
      FreeMemoryPool(&pool); return 2;
    }
    if (f.numa_node<0) {
      std::cerr << "Warning: could not resolve observed NUMA for frame "
                << i << "; mark as unknown\n";
    }
    frames.push_back(f);
  }
  std::ofstream raw(opt.output+".raw.csv",std::ios::out|std::ios::trunc);
  std::ofstream summary(opt.output+".summary.csv",std::ios::out|std::ios::trunc);
  if (!raw || !summary) {
    std::cerr << "Unable to open output files\n";
    FreeMemoryPool(&pool); return 2;
  }
  raw << "run_id,pair_id,trial,frame_index,frame_base_phys,numa_node,"
         "paddr_a,paddr_b,order_ab,elapsed_cycles\n";
  summary << "run_id,pair_id,frame_index,frame_base_phys,numa_node,"
             "paddr_a,paddr_b,trials,mean,median,p10,p90,min,max\n";
  std::mt19937_64 rng(opt.seed);
  constexpr uint64_t lines_per_frame=(1ULL<<30)/64;
  std::uniform_int_distribution<uint64_t> offset_dist(0,lines_per_frame-1);
  int cpu_start=sched_getcpu();
  if (opt.expected_cpu>=0 && cpu_start!=opt.expected_cpu) {
    std::cerr << "Wrong starting CPU: " << cpu_start << "\n";
    FreeMemoryPool(&pool); return 2;
  }
  uint64_t pair_id=0, migrations=0;
  for (size_t fi=0;fi<frames.size();++fi) {
    const Frame& f=frames[fi];
    for (uint64_t p=0;p<opt.pairs_per_frame;++p,++pair_id) {
      uint64_t off_a=offset_dist(rng),off_b=offset_dist(rng);
      while (off_a==off_b) off_b=offset_dist(rng);
      char* a=f.va_base+off_a*64;
      char* b=f.va_base+off_b*64;
      uint64_t pa_a=f.pa_base+off_a*64,pa_b=f.pa_base+off_b*64;
      std::vector<uint64_t> elapsed;
      elapsed.reserve(static_cast<size_t>(opt.trials));
      long double sum=0;
      for (uint64_t j=0;j<opt.trials;++j) {
        int order_ab=static_cast<int>(rng() & 1ULL);
        clflushopt(a); clflushopt(b); mfence();
        uint64_t before=rdtscp();
        if (order_ab) {
          (void)*(volatile unsigned char*)a;
          (void)*(volatile unsigned char*)b;
        } else {
          (void)*(volatile unsigned char*)b;
          (void)*(volatile unsigned char*)a;
        }
        lfence();
        uint64_t after=rdtscp();
        uint64_t cycles=after-before;
        elapsed.push_back(cycles);
        sum+=cycles;
        raw << opt.run_id << ',' << pair_id << ',' << j << ','
            << fi << ',' << hex64(f.pa_base) << ','
            << f.numa_node << ',' << hex64(pa_a) << ','
            << hex64(pa_b) << ',' << order_ab << ',' << cycles << '\n';
      }
      std::sort(elapsed.begin(),elapsed.end());
      summary << opt.run_id << ',' << pair_id << ',' << fi << ','
              << hex64(f.pa_base) << ',' << f.numa_node << ','
              << hex64(pa_a) << ',' << hex64(pa_b) << ','
              << opt.trials << ',' << std::fixed << std::setprecision(3)
              << static_cast<double>(sum/opt.trials) << ','
              << percentile(elapsed,50) << ',' << percentile(elapsed,10)
              << ',' << percentile(elapsed,90) << ','
              << elapsed.front() << ',' << elapsed.back() << '\n';
      if (sched_getcpu()!=cpu_start) ++migrations;
    }
    std::cerr << "frame " << fi << " physical=" << hex64(f.pa_base)
              << " observed_numa=" << f.numa_node
              << " pairs=" << opt.pairs_per_frame << "\n";
    raw.flush(); summary.flush();
    if (!raw || !summary) {
      std::cerr << "Output write failed\n";
      FreeMemoryPool(&pool); return 2;
    }
  }
  raw.close(); summary.close();
  std::ofstream manifest(opt.output+".manifest.json");
  if (!manifest) {
    std::cerr << "Could not create manifest\n";
    FreeMemoryPool(&pool); return 2;
  }
  char hostname[256]={};
  if (gethostname(hostname,sizeof(hostname)-1)!=0) strcpy(hostname,"unknown");
  manifest << "{\n  \"format_version\": 1,\n  \"measurement\": "
           << "\"passive paired-access timing (two cached-line flushes, "
           << "two reads)\",\n"
           << "  \"run_id\": " << json_string(opt.run_id) << ",\n"
           << "  \"source_revision\": " << json_string(opt.revision) << ",\n"
           << "  \"host\": " << json_string(hostname) << ",\n"
           << "  \"timestamp_completed\": " << std::time(nullptr) << ",\n"
           << "  \"seed\": " << opt.seed << ",\n"
           << "  \"pages\": " << opt.pages << ",\n"
           << "  \"pairs_per_frame\": " << opt.pairs_per_frame << ",\n"
           << "  \"trials_per_pair\": " << opt.trials << ",\n"
           << "  \"expected_numa\": " << opt.expected_numa << ",\n"
           << "  \"expected_cpu\": " << opt.expected_cpu << ",\n"
           << "  \"observed_cpu_at_start\": " << cpu_start << ",\n"
           << "  \"observed_cpu_migrations\": " << migrations << ",\n"
           << "  \"frames\": [\n";
  for (size_t i=0;i<frames.size();++i) {
    manifest << "    {\"index\": " << i << ", \"base_phys\": "
             << json_string(hex64(frames[i].pa_base))
             << ", \"numa_node\": " << frames[i].numa_node
             << ", \"numa_verified\": " << (frames[i].numa_node>=0?"true":"false")
             << "}" << (i+1==frames.size()?"\n":",\n");
  }
  manifest << "  ],\n  \"topology_inferred\": false,\n"
           << "  \"automatic_function_recovery_enabled\": false\n}\n";
  manifest.close();
  FreeMemoryPool(&pool);
  if (migrations) {
    std::cerr << "WARNING: " << migrations
              << " CPU changes detected; treat timing as confounded.\n";
  }
  std::cerr << "Wrote " << opt.output << ".{raw.csv,summary.csv,manifest.json}\n";
  return migrations?3:0;
#endif
}
