// Separate experimental C++ UCI engine. Existing Python engine is unchanged.
#include "vendor/chess.hpp"
#include "../nnue/upstream/main.cpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <limits>
#include <memory>
#include <unordered_map>
using CB = chess::Board;
using Move = chess::Move;
using Clock = std::chrono::steady_clock;
constexpr int INF=31000, MATE_SCORE=30000, MAXPLY=96;
constexpr int values[7]={100,320,330,500,900,0,0};
std::mutex output_mutex;
void say(const std::string& s) { std::lock_guard<std::mutex> lock(output_mutex); std::cout<<s<<std::endl; }
Position position(const CB& board) {
    Position p; p.stm=board.sideToMove()==chess::Color::BLACK;
    for(int sq=0;sq<64;++sq) { auto pc=board.at(chess::Square(sq)); p.b[sq]=pc==chess::Piece::NONE?0:int(pc)+1; }
    return p;
}
uint64_t mix(uint64_t x) { x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; return x^(x>>31); }
struct Limits { int depth=64; uint64_t nodes=UINT64_MAX; double ms=-1; bool infinite=false; std::vector<Move> roots; bool restricted=false; };
struct Entry { uint64_t key=0; int depth=-1, score=0, bound=0; Move move{0}; };
struct Halt {};
struct Search {
    CB board; const QuantNet& net; std::atomic<bool>& stop; Limits limits;
    std::vector<QuantNet::State> states;
    std::vector<Entry> table;
    std::unordered_map<uint64_t,Move> hints;
    std::array<std::array<Move,2>,MAXPLY+1> killers{};
    int history[2][64][64]{};
    uint64_t context;
    uint64_t nodes=0,qnodes=0,cutoffs=0,ttcuts=0;
    Clock::time_point start,deadline;
    Search(CB b,const QuantNet& n,std::atomic<bool>& s,Limits l,uint64_t ctx,int hash):
        board(std::move(b)),net(n),stop(s),limits(l),states(MAXPLY+1),
        table(std::max<size_t>(1,size_t(hash)*1024*1024/sizeof(Entry))),context(ctx) {
        for(auto& k:killers) k={Move(0),Move(0)};
        states[0]=net.start(position(board));
    }
    void tick(bool q) {
        if(stop.load(std::memory_order_relaxed)||nodes>=limits.nodes||Clock::now()>=deadline) throw Halt{};
        ++nodes; qnodes+=q;
    }
    int evaluate(int ply) const { return std::clamp(int(std::lround(net.eval(states[ply]))),-28000,28000); }
    int order(Move m,Move first,int ply) {
        if(m==first) return 2000000;
        int promo=m.typeOf()==Move::PROMOTION?values[int(m.promotionType())]:0;
        if(board.isCapture(m)) {
            int victim=m.typeOf()==Move::ENPASSANT?100:values[int(board.at<chess::PieceType>(m.to()))];
            return 1000000+victim*10-values[int(board.at<chess::PieceType>(m.from()))]+promo;
        }
        if(promo) return 900000+promo;
        if(m==killers[ply][0]) return 800000;
        if(m==killers[ply][1]) return 799999;
        return history[int(board.sideToMove())][m.from().index()][m.to().index()];
    }
    std::vector<Move> ordered(const chess::Movelist& list,Move first,int ply,bool q,bool check) {
        std::vector<std::pair<int,Move>> ranked;
        for(auto m:list) if(!q||check||board.isCapture(m)||m.typeOf()==Move::PROMOTION) ranked.emplace_back(order(m,first,ply),m);
        std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        std::vector<Move> result; result.reserve(ranked.size());
        for(auto& x:ranked) result.push_back(x.second);
        return result;
    }
    int child(Move m,int depth,int alpha,int beta,int ply,uint64_t ctx,std::vector<Move>& pv) {
        board.makeMove(m);
        try {
            states[ply]=states[ply-1]; net.update(states[ply],position(board));
            uint64_t next=board.halfMoveClock()==0?mix(board.hash()):mix(ctx^board.hash());
            int score=-search(depth,-beta,-alpha,ply,next,pv);
            board.unmakeMove(m); return score;
        } catch(...) { board.unmakeMove(m); throw; }
    }
    static int store_score(int score,int ply) {return score>29000?score+ply:score< -29000?score-ply:score;}
    static int read_score(int score,int ply) {return score>29000?score-ply:score< -29000?score+ply:score;}
    int search(int depth,int alpha,int beta,int ply,uint64_t ctx,std::vector<Move>& pv) {
        bool q=depth<=0; tick(q); pv.clear();
        bool check=board.inCheck();
        chess::Movelist list; chess::movegen::legalmoves(list,board);
        if(list.empty()) return check?-MATE_SCORE+ply:0;
        if(board.isHalfMoveDraw()||board.isInsufficientMaterial()||board.isRepetition(2)) return 0;
        if(ply>=MAXPLY) return evaluate(ply);
        int original_alpha=alpha;
        // Include reversible path history and clock in score-cache identity.
        // Position-only hints still share move ordering across transpositions.
        uint64_t key=mix(board.hash()^ctx^mix(board.halfMoveClock()+1));
        Entry* slot=&table[key%table.size()];
        Move first(0);
        auto hint=hints.find(board.hash()); if(hint!=hints.end()) first=hint->second;
        if(!q&&slot->key==key&&slot->depth>=0) {
            first=slot->move; int score=read_score(slot->score,ply);
            // Do not truncate PV nodes using cached scores.
            if(beta==alpha+1&&slot->depth>=depth&&(slot->bound==0||(slot->bound==1&&score>=beta)||(slot->bound==2&&score<=alpha))) {++ttcuts;return score;}
        }
        int best=-INF;
        if(q&&!check) { best=evaluate(ply); if(best>=beta) {++cutoffs;return best;} alpha=std::max(alpha,best); }
        auto moves=ordered(list,first,ply,q,check);
        Move bestmove(0); bool searched=false;
        for(auto m:moves) {
            std::vector<Move> tail;
            int score;
            if(!q&&searched) {
                score=child(m,depth-1,alpha,alpha+1,ply+1,ctx,tail);
                if(score>alpha&&score<beta) score=child(m,depth-1,alpha,beta,ply+1,ctx,tail);
            } else score=child(m,depth-1,alpha,beta,ply+1,ctx,tail);
            searched=true;
            if(score>best) {best=score;bestmove=m;pv=tail;pv.insert(pv.begin(),m);}
            alpha=std::max(alpha,score);
            if(alpha>=beta) {
                ++cutoffs;
                if(!q&&!board.isCapture(m)&&m.typeOf()!=Move::PROMOTION) {
                    if(killers[ply][0]!=m) {killers[ply][1]=killers[ply][0];killers[ply][0]=m;}
                    auto& h=history[int(board.sideToMove())][m.from().index()][m.to().index()]; h=std::min(600000,h+depth*depth);
                }
                break;
            }
        }
        if(!q&&bestmove.move()) {
            if(hints.size()>65536) hints.clear();
            hints[board.hash()]=bestmove;
            *slot={key,depth,store_score(best,ply),best>=beta?1:best<=original_alpha?2:0,bestmove};
        }
        return best;
    }
    void info(int depth,int score,const std::vector<Move>& pv) {
        auto ms=std::max<int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
        std::ostringstream out;
        out<<"info depth "<<depth<<" score ";
        if(std::abs(score)>29000) out<<"mate "<<((MATE_SCORE-std::abs(score)+1)/2)*(score>0?1:-1);
        else out<<"cp "<<score;
        out<<" nodes "<<nodes<<" time "<<ms<<" nps "<<nodes*1000/ms<<" pv";
        for(auto m:pv) out<<' '<<chess::uci::moveToUci(m);
        say(out.str());
    }
    void run() {
        start=Clock::now(); deadline=limits.ms>=0&&!limits.infinite?start+std::chrono::milliseconds(int64_t(limits.ms)):Clock::time_point::max();
        chess::Movelist all; chess::movegen::legalmoves(all,board);
        auto roots=limits.restricted?limits.roots:std::vector<Move>(all.begin(),all.end());
        Move best=roots.empty()?Move(0):roots[0];
        int done=0,lastscore=0; std::vector<Move> lastpv;
        if(!roots.empty()&&(board.isHalfMoveDraw()||board.isInsufficientMaterial()||board.isRepetition(2))) {
            info(0,0,{best}); limits.depth=0;
        }
        for(int depth=1;depth<=limits.depth&&!roots.empty();++depth) {
            int score=-INF; std::vector<Move> pv;
            std::stable_sort(roots.begin(),roots.end(),[&](Move a,Move b){return (a==best)>(b==best);});
            try {
                for(auto m:roots) {
                    std::vector<Move> tail;
                    int value=child(m,depth-1,score,INF,1,context,tail);
                    if(value>score) {score=value;pv=tail;pv.insert(pv.begin(),m);}
                }
            } catch(const Halt&) {break;}
            best=pv[0]; done=depth;lastscore=score;lastpv=pv;info(done,lastscore,lastpv);
            if(std::abs(score)>29000) break;
        }
        if(limits.infinite) while(!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(done) info(done,lastscore,lastpv);
        say("info string qnodes "+std::to_string(qnodes)+" beta_cutoffs "+std::to_string(cutoffs)+" tt_cutoffs "+std::to_string(ttcuts));
        say("bestmove "+(best.move()?chess::uci::moveToUci(best):"0000"));
    }
};
uint64_t perft(CB& b,int depth) {
    if(!depth) return 1;
    chess::Movelist moves;chess::movegen::legalmoves(moves,b);
    if(depth==1) return moves.size();
    uint64_t count=0; for(auto m:moves) {b.makeMove(m);count+=perft(b,depth-1);b.unmakeMove(m);}return count;
}
class Engine {
    CB board; QuantNet net; std::thread worker; std::atomic<bool> stop{false};
    int overhead=100,hash=32; uint64_t context=mix(board.hash()); std::string model;
    void halt(){stop=true;if(worker.joinable())worker.join();}
    Move legal(const std::string& text) {
        chess::Movelist moves;chess::movegen::legalmoves(moves,board);
        for(auto m:moves) if(chess::uci::moveToUci(m)==text) return m;
        throw std::runtime_error("illegal move "+text);
    }
public:
    explicit Engine(std::string file):model(std::move(file)){net.load(model);}
    ~Engine(){halt();}
    bool command(const std::string& line) {
        std::istringstream in(line);std::string cmd;in>>cmd;
        if(cmd=="uci") {
            say("id name KittenFish C++ experimental");say("id author crazykitten43");
            say("option name Move Overhead type spin default 100 min 0 max 5000");
            say("option name Hash type spin default 32 min 1 max 1024");
            say("option name EvalFile type string default "+model);say("uciok");
        } else if(cmd=="isready") say("readyok");
        else if(cmd=="stop") halt();
        else if(cmd=="quit") {halt();return false;}
        else if(cmd=="ucinewgame") {halt();board=CB();context=mix(board.hash());}
        else if(cmd=="setoption") {
            halt(); auto at=line.find(" value ");if(at==std::string::npos)throw std::runtime_error("missing value");
            auto name=line.substr(15,at-15), value=line.substr(at+7);
            if(name=="Hash")hash=std::clamp(std::stoi(value),1,1024);
            else if(name=="Move Overhead")overhead=std::clamp(std::stoi(value),0,5000);
            else if(name=="EvalFile") {QuantNet next;next.load(value);net=std::move(next);model=value;}
        } else if(cmd=="position") {
            halt();std::string token;in>>token;
            if(token=="startpos")board=CB();
            else if(token=="fen") {std::string f;for(int i=0;i<6;++i){if(!(in>>token))throw std::runtime_error("incomplete FEN");f+=token+' ';}if(!board.setFen(f))throw std::runtime_error("invalid FEN");}
            else throw std::runtime_error("invalid position");
            context=mix(board.hash());
            if(in>>token) {if(token!="moves")throw std::runtime_error("expected moves");while(in>>token){board.makeMove(legal(token));context=board.halfMoveClock()==0?mix(board.hash()):mix(context^board.hash());}}
        } else if(cmd=="eval") {halt();auto state=net.start(position(board));say("info string eval "+std::to_string(int(std::lround(net.eval(state)))));}
        else if(cmd=="perft") {halt();int d;if(!(in>>d)||d<0||d>7)throw std::runtime_error("perft depth 0..7");say("info string perft "+std::to_string(perft(board,d)));}
        else if(cmd=="moves") {
            halt();chess::Movelist moves;chess::movegen::legalmoves(moves,board);std::string out="info string moves";
            for(auto m:moves) out+=' '+chess::uci::moveToUci(m);
            say(out);
        }
        else if(cmd=="verify") {
            halt();CB saved=board;auto state=net.start(position(board));
            // Exercise every initial legal move, including special moves.
            chess::Movelist initial;chess::movegen::legalmoves(initial,board);
            for(auto m:initial) {
                board.makeMove(m);auto updated=state;net.update(updated,position(board));auto fresh=net.start(position(board));board.unmakeMove(m);
                if(updated.a!=fresh.a||net.eval(updated)!=net.eval(fresh))throw std::runtime_error("NNUE special-move mismatch");
            }
            std::mt19937 rng(43);int checked=0;
            for(;checked<200;++checked){chess::Movelist moves;chess::movegen::legalmoves(moves,board);if(moves.empty())break;board.makeMove(moves[rng()%moves.size()]);net.update(state,position(board));auto fresh=net.start(position(board));if(state.a!=fresh.a||net.eval(state)!=net.eval(fresh))throw std::runtime_error("NNUE mismatch");}
            board=std::move(saved);say("info string verified "+std::to_string(checked));
        } else if(cmd=="go") {
            halt();Limits l;std::unordered_map<std::string,uint64_t> p;std::string t;
            const std::vector<std::string> keys={"depth","nodes","movetime","wtime","btime","winc","binc","movestogo","mate"};
            while(in>>t) {
                if(t=="infinite")l.infinite=true;
                else if(t=="ponder")throw std::runtime_error("ponder unsupported; use ponder false");
                else if(t=="searchmoves") {
                    l.restricted=true;
                    while(in>>t) {
                        if(std::find(keys.begin(),keys.end(),t)!=keys.end()||t=="infinite")break;
                        auto m=legal(t);if(std::find(l.roots.begin(),l.roots.end(),m)==l.roots.end())l.roots.push_back(m);
                    }
                    if(!in)break;
                    if(t=="infinite"){l.infinite=true;continue;}
                    uint64_t n;if(!(in>>n))throw std::runtime_error("missing limit");p[t]=n;
                } else if(std::find(keys.begin(),keys.end(),t)!=keys.end()) {uint64_t n;if(!(in>>n))throw std::runtime_error("missing limit");p[t]=n;}
            }
            if(p.count("depth"))l.depth=int(std::clamp<uint64_t>(p["depth"],1,MAXPLY-1));
            else if(p.count("mate"))l.depth=int(std::clamp<uint64_t>(p["mate"],1,47))*2;
            if(p.count("nodes"))l.nodes=p["nodes"];
            auto side=board.sideToMove()==chess::Color::WHITE?"wtime":"btime";
            auto inc=board.sideToMove()==chess::Color::WHITE?"winc":"binc";
            if(p.count("movetime"))l.ms=std::max(1.0,double(p["movetime"])-overhead);
            else if(p.count(side)){double available=std::max(1.0,double(p[side])-overhead);l.ms=std::min(available,std::max(1.0,available/std::max<uint64_t>(1,p.count("movestogo")?p["movestogo"]:30)+p[inc]*.8));}
            say("info string evaluator NNUE budget_ms "+std::to_string(l.infinite?-1:l.ms));
            stop=false;
            worker=std::thread([this,l] {try {Search s(board,net,stop,l,context,hash);s.run();}catch(const std::exception& e){say(std::string("info string search failed ")+e.what());say("bestmove 0000");}});
        }
        return true;
    }
};
int main(int argc,char** argv) {
    try {
        Engine engine(argc>1?argv[1]:"networks/kf100m.nnue");std::string line;
        while(std::getline(std::cin,line)) {try{if(!engine.command(line))break;}catch(const std::exception& e){say(std::string("info string invalid command ")+e.what());}}
    }catch(const std::exception& e){std::cerr<<"NNUE startup failed: "<<e.what()<<'\n';return 1;}
}
