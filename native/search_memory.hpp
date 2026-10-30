#pragma once
#include <cstdio>
// A bounded, transposition-backed search tree. Score identities retain game
// history; position-only best-move edges are used for ordering, never scores.
struct SearchMemory {
    std::vector<Entry> table;
    std::unordered_map<uint64_t,Move> hints;
    std::array<std::array<Move,2>,MAXPLY+1> killers{};
    int history[2][64][64]{};
    uint64_t searches=0;
    explicit SearchMemory(int mb=32):table(std::max<size_t>(1,size_t(mb)*1024*1024/sizeof(Entry))) {}
    void clear() {
        std::fill(table.begin(),table.end(),Entry{});hints.clear();
        for(auto& k:killers) k={Move(0),Move(0)};
        for(auto& side:history)for(auto& from:side)for(auto& h:from)h=0;
        searches=0;
    }
    uint64_t count() const {uint64_t n=0;for(const auto& e:table)n+=e.depth>=0;return n;}
    static uint64_t identity(const QuantNet& net) {
        uint64_t h=1469598103934665603ULL;
        auto feed=[&](const void* data,size_t size){auto p=static_cast<const unsigned char*>(data);while(size--){h^=*p++;h*=1099511628211ULL;}};
        feed(net.w.data(),net.w.size()*sizeof(int16_t));feed(net.b.data(),net.b.size()*sizeof(int16_t));
        feed(net.out.data(),net.out.size()*sizeof(int16_t));feed(&net.bias,sizeof(net.bias));return h;
    }
    static uint32_t signature(const SearchOptions& o) {
        return o.nullmove|(o.lmr<<1)|(o.see<<2)|(o.futility<<3)|(o.aspiration<<4)|(o.extensions<<5);
    }
    static void put(std::ostream& out,uint64_t value,int bytes) {for(int i=0;i<bytes;++i)out.put(char((value>>(8*i))&255));}
    static uint64_t get(std::istream& in,int bytes) {
        uint64_t value=0;for(int i=0;i<bytes;++i){int c=in.get();if(c==EOF)throw std::runtime_error("truncated tree snapshot");value|=uint64_t(c)<<(8*i);}return value;
    }
    static uint64_t checksum(const std::string& path,bool footer=false) {
        std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("cannot read tree checksum");
        auto size=in.tellg();if(size<std::streamoff(footer?8:0))throw std::runtime_error("truncated tree snapshot");
        uint64_t remaining=uint64_t(size)-uint64_t(footer?8:0),h=1469598103934665603ULL;
        in.seekg(0);char buffer[8192];
        while(remaining){auto n=std::min<uint64_t>(remaining,sizeof(buffer));in.read(buffer,std::streamsize(n));if(!in)throw std::runtime_error("tree checksum read failed");for(uint64_t i=0;i<n;++i){h^=static_cast<unsigned char>(buffer[i]);h*=1099511628211ULL;}remaining-=n;}return h;
    }
    void save(const std::string& path,uint64_t model,uint32_t options) const {
        const std::string temp=path+".tmp";
        try {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot write tree snapshot");
            out.write("KFTREE02",8);put(out,model,8);put(out,options,4);put(out,count(),8);put(out,hints.size(),4);
            for(const auto& e:table)if(e.depth>=0){put(out,e.key,8);put(out,e.depth,4);put(out,uint32_t(e.score),4);put(out,e.bound,1);put(out,e.move.move(),2);}
            for(const auto& h:hints){put(out,h.first,8);put(out,h.second.move(),2);}
            out.flush();if(!out)throw std::runtime_error("tree snapshot write failed");out.close();
            auto digest=checksum(temp);std::ofstream footer(temp,std::ios::binary|std::ios::app);put(footer,digest,8);footer.close();if(!footer)throw std::runtime_error("tree checksum write failed");
            if(std::rename(temp.c_str(),path.c_str()))throw std::runtime_error("tree snapshot rename failed");
        }catch(...){std::remove(temp.c_str());throw;}
    }
    void load(const std::string& path,uint64_t model,uint32_t options) {
        auto digest=checksum(path,true);
        std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("cannot read tree snapshot");
        char magic[8];in.read(magic,8);if(!in||std::string(magic,8)!="KFTREE02")throw std::runtime_error("wrong tree snapshot format");
        if(get(in,8)!=model||get(in,4)!=options)throw std::runtime_error("tree snapshot model/options mismatch");
        auto entries=get(in,8),edges=get(in,4);
        if(entries>64ULL*1024*1024||edges>65537)throw std::runtime_error("tree snapshot counts out of range");
        // Validate the whole file before changing the active tree, and allocate
        // only the configured Hash size regardless of file-provided counts.
        std::vector<Entry> next(table.size());std::unordered_map<uint64_t,Move> links;
        for(uint64_t i=0;i<entries;++i){
            Entry e;e.key=get(in,8);e.depth=int(get(in,4));e.score=int32_t(uint32_t(get(in,4)));e.bound=int(get(in,1));e.move=Move(uint16_t(get(in,2)));
            if(e.depth<0||e.depth>MAXPLY+8||std::abs(int64_t(e.score))>INF+MAXPLY||e.bound>2||!e.move.move())throw std::runtime_error("invalid tree entry");
            auto& slot=next[e.key%next.size()];if(e.depth>=slot.depth)slot=e;
        }
        for(uint64_t i=0;i<edges;++i){auto key=get(in,8);Move move{uint16_t(get(in,2))};if(!move.move())throw std::runtime_error("invalid tree edge");links[key]=move;}
        if(get(in,8)!=digest)throw std::runtime_error("tree snapshot checksum mismatch");
        if(in.peek()!=EOF)throw std::runtime_error("tree snapshot trailing data");
        clear();table.swap(next);hints.swap(links);
    }
};
struct SearchControl {
    std::atomic<bool> pondering{false};
    std::atomic<int64_t> hit_ns{0};
    void hit(){hit_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(),std::memory_order_relaxed);pondering.store(false,std::memory_order_release);}
};
