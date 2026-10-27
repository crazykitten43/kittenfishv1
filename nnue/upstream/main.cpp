// KittenFish NNUE inference. Complete trainer moved to kittenfishtraining/v1.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;
constexpr int H=864, F=16*768, Q=64;
using Board=array<int,64>; // 0 empty; white P..K = 1..6, black = 7..12
struct Position { Board b{}; int stm=0; };
Position fen(const string& s) {
    Position p; istringstream in(s); string placement, side; in>>placement>>side;
    int rank=7,file=0; string pieces=" PNBRQKpnbrqk";
    for(char c:placement) {
        if(c=='/') { if(file!=8||rank==0) throw runtime_error("bad FEN rank"); --rank; file=0; }
        else if(c>='1'&&c<='8') {file+=c-'0'; if(file>8) throw runtime_error("bad FEN width");}
        else { auto n=pieces.find(c); if(n==string::npos||n==0||file>=8) throw runtime_error("bad FEN piece"); p.b[rank*8+file++]=int(n); }
    }
    if(rank!=0||file!=8||(side!="w"&&side!="b")) throw runtime_error("bad FEN");
    if(count(p.b.begin(),p.b.end(),6)!=1||count(p.b.begin(),p.b.end(),12)!=1) throw runtime_error("FEN requires both kings");
    if(count_if(p.b.begin(),p.b.end(),[](int x){return x!=0;})>32) throw runtime_error("too many pieces");
    p.stm=(side=="b"); return p;
}
int bucket(const Position& p,int view) {
    int k=int(find(p.b.begin(),p.b.end(),view?12:6)-p.b.begin());
    if(view) k^=56;
    return (k/16)*4+(k%8)/2;
}
int feature(int pc,int sq,int view,int bk) {
    int color=(pc-1)/6, type=(pc-1)%6;
    return bk*768+((color^view)*6+type)*64+(view?(sq^56):sq);
}
vector<int> active(const Position& p,int view) {
    vector<int> a; int bk=bucket(p,view);
    for(int s=0;s<64;s++) if(p.b[s]) a.push_back(feature(p.b[s],s,view,bk));
    return a;
}
template<class T> void readv(istream& i,vector<T>& v) { i.read(reinterpret_cast<char*>(v.data()),v.size()*sizeof(T)); if(!i) throw runtime_error("truncated model"); }
struct QuantNet {
    vector<int16_t>w=vector<int16_t>(size_t(F)*H),b=vector<int16_t>(H),out=vector<int16_t>(2*H); int32_t bias=0;
    struct State { Position p; alignas(64) array<array<int32_t,H>,2>a{}; };
    void refresh(State& s,int v) const {copy(b.begin(),b.end(),s.a[v].begin());for(int f:active(s.p,v))for(int h=0;h<H;h++)s.a[v][h]+=w[size_t(f)*H+h];}
    State start(const Position& p) const {State s;s.p=p;refresh(s,0);refresh(s,1);return s;}
    // The caller supplies a final board; captures, promotion, castling and EP
    // are all handled as square changes. This is NOT a legality checker.
    void update(State& s,const Position& next) const {
        Position old=s.p;s.p=next;
        for(int v=0;v<2;v++) {
            int bk=bucket(old,v); if(bk!=bucket(next,v)) {refresh(s,v);continue;}
            for(int sq=0;sq<64;sq++)if(old.b[sq]!=next.b[sq]) {
                if(old.b[sq]) {int f=feature(old.b[sq],sq,v,bk);for(int h=0;h<H;h++)s.a[v][h]-=w[size_t(f)*H+h];}
                if(next.b[sq]) {int f=feature(next.b[sq],sq,v,bk);for(int h=0;h<H;h++)s.a[v][h]+=w[size_t(f)*H+h];}
            }
        }
    }
    double eval(const State& s) const {int64_t y=bias;for(int v=0;v<2;v++)for(int h=0;h<H;h++)y+=int64_t(out[v*H+h])*clamp(s.a[s.p.stm^v][h],0,Q);return double(y)*400/(Q*Q);}
    void load(const string& path) {ifstream i(path,ios::binary);char m[4];i.read(m,4);if(!i||string(m,4)!="KFQ1")throw runtime_error("wrong quantized model");readv(i,w);readv(i,b);readv(i,out);i.read(reinterpret_cast<char*>(&bias),4);if(!i)throw runtime_error("truncated bias");}
};
