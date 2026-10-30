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
#include "see.hpp"
struct SearchOptions { bool nullmove=true,lmr=true,see=true,futility=true,aspiration=true,extensions=true,adaptive=true; };
std::mutex output_mutex;
void say(const std::string& s) { std::lock_guard<std::mutex> lock(output_mutex); std::cout<<s<<std::endl; }
Position position(const CB& board) {
    Position p; p.stm=board.sideToMove()==chess::Color::BLACK;
    for(int sq=0;sq<64;++sq) { auto pc=board.at(chess::Square(sq)); p.b[sq]=pc==chess::Piece::NONE?0:int(pc)+1; }
    return p;
}
uint64_t mix(uint64_t x) { x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; return x^(x>>31); }
struct Limits { int depth=64; uint64_t nodes=UINT64_MAX; double ms=-1,softms=-1; bool infinite=false,ponder=false,suggestPonder=false; std::vector<Move> roots; bool restricted=false; };
struct Entry { uint64_t key=0; int depth=-1, score=0, bound=0; Move move{0}; uint32_t generation=0; };
#include "search_memory.hpp"
struct Halt {};
struct Search {
    CB board; SearchOptions options; const QuantNet& net; std::atomic<bool>& stop; Limits limits;
    std::vector<QuantNet::State> states;
    SearchMemory& memory; SearchControl& control;
    std::vector<Entry>& table;
    std::unordered_map<uint64_t,Move>& hints;
    std::array<std::array<Move,2>,MAXPLY+1>& killers;
    int (&history)[2][64][64];
    uint64_t context;
    uint64_t nodes=0,qnodes=0,cutoffs=0,ttcuts=0,nullcuts=0,reductions=0,researches=0,futilitycuts=0,seecuts=0,checkextensions=0;
    int seldepth=0;uint64_t retainedBefore=0;
    Clock::time_point start,deadline,activeStart;
    bool clockActive=false;
    Search(CB b,const QuantNet& n,std::atomic<bool>& s,Limits l,uint64_t ctx,SearchMemory& mem,SearchControl& ctl,SearchOptions opt={}):
        board(std::move(b)),options(opt),net(n),stop(s),limits(l),states(MAXPLY+1),
        memory(mem),control(ctl),table(mem.table),hints(mem.hints),killers(mem.killers),history(mem.history),context(ctx) {
        retainedBefore=memory.count();++memory.searches;
        states[0]=net.start(position(board));
    }
    void activate_clock() {
        if(clockActive||control.pondering.load(std::memory_order_acquire))return;
        auto hit=control.hit_ns.load(std::memory_order_relaxed);
        activeStart=limits.ponder&&hit?Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(hit))):start;
        deadline=limits.ms>=0&&!limits.infinite?activeStart+std::chrono::milliseconds(int64_t(limits.ms)):Clock::time_point::max();
        clockActive=true;
    }
    void tick(bool q) {
        activate_clock();
        if(stop.load(std::memory_order_relaxed)||nodes>=limits.nodes||Clock::now()>=deadline) throw Halt{};
        ++nodes; qnodes+=q;
    }
    int evaluate(int ply) const { return std::clamp(int(std::lround(net.eval(states[ply]))),-28000,28000); }
    int order(Move m,Move first,int ply) {
        if(m==first) return 2000000;
        int promo=m.typeOf()==Move::PROMOTION?values[int(m.promotionType())]:0;
        if(board.isCapture(m)) {
            int victim=m.typeOf()==Move::ENPASSANT?100:values[int(board.at<chess::PieceType>(m.to()))];
            return (options.see&&static_exchange(board,m)<0?-1000000:1000000)+victim*10-values[int(board.at<chess::PieceType>(m.from()))]+promo;
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
    int child(Move m,int depth,int alpha,int beta,int ply,uint64_t ctx,std::vector<Move>& pv,int extensions=0,bool nullpath=false) {
        board.makeMove(m);
        try {
            states[ply]=states[ply-1]; net.update(states[ply],position(board));
            uint64_t next=board.halfMoveClock()==0?mix(board.hash()):mix(ctx^board.hash());
            int score=-search(depth,-beta,-alpha,ply,next,pv,true,extensions,nullpath);
            board.unmakeMove(m); return score;
        } catch(...) { board.unmakeMove(m); throw; }
    }
    static int store_score(int score,int ply) {return score>29000?score+ply:score< -29000?score-ply:score;}
    static int read_score(int score,int ply) {return score>29000?score-ply:score< -29000?score+ply:score;}
    int search(int depth,int alpha,int beta,int ply,uint64_t ctx,std::vector<Move>& pv,
               bool allowNull=true,int extensions=0,bool nullpath=false) {
        bool q=depth<=0; tick(q); pv.clear(); seldepth=std::max(seldepth,ply);
        bool check=board.inCheck();
        chess::Movelist list; chess::movegen::legalmoves(list,board);
        if(list.empty()) return check?-MATE_SCORE+ply:0;
        if(board.isInsufficientMaterial()||(!nullpath&&(board.isHalfMoveDraw()||board.isRepetition(2)))) return 0;
        if(ply>=MAXPLY) return evaluate(ply);
        if(!q&&check&&options.extensions&&extensions<8) {++depth;++extensions;++checkextensions;}
        bool pvnode=beta-alpha>1;
        int original_alpha=alpha;
        // Preserve reversible-path identity: score bounds must not cross draw
        // histories. Position-only hints still transfer move ordering safely.
        uint64_t key=mix(board.hash()^ctx^mix(board.halfMoveClock()+1));
        Entry* slot=&table[key%table.size()];
        Move first(0);
        auto hint=hints.find(board.hash()); if(hint!=hints.end()) first=hint->second;
        if(!q&&!nullpath&&slot->key==key&&slot->depth>=0) {
            first=slot->move; int score=read_score(slot->score,ply);
            if(!pvnode&&slot->depth>=depth&&(slot->bound==0||(slot->bound==1&&score>=beta)||(slot->bound==2&&score<=alpha))) {++ttcuts;return score;}
        }
        int staticEval=check?0:evaluate(ply);
        // Never pass in check, in pawn/sparse endings, near the fifty-move
        // boundary, or inside an artificial null subtree. Verify deep cutoffs.
        auto nonpawn=board.us(board.sideToMove()) & ~board.pieces(chess::PieceType::PAWN,chess::PieceType::KING);
        if(options.nullmove&&allowNull&&!nullpath&&!q&&!pvnode&&!check&&depth>=3&&
           nonpawn.count()>=2&&board.halfMoveClock()<90&&staticEval>=beta&&std::abs(beta)<28000) {
            int reduction=2+depth/4;
            std::vector<Move> ignored; int score;
            board.makeNullMove();
            try {
                states[ply+1]=states[ply]; states[ply+1].p.stm^=1;
                score=-search(depth-1-reduction,-beta,-beta+1,ply+1,mix(ctx^board.hash()),ignored,false,extensions,true);
                board.unmakeNullMove();
            } catch(...) {board.unmakeNullMove();throw;}
            if(score>=beta) {
                bool verified=depth<6;
                if(!verified) verified=search(depth-reduction,beta-1,beta,ply,ctx,ignored,false,extensions,false)>=beta;
                if(verified) {++nullcuts;return std::min(score,28000);}
            }
        }
        int best=-INF;
        if(q&&!check) {best=staticEval;if(best>=beta){++cutoffs;return best;}alpha=std::max(alpha,best);}
        auto moves=ordered(list,first,ply,q,check);
        Move bestmove(0); int searched=0;
        std::vector<Move> quiets;
        for(auto m:moves) {
            bool quiet=!board.isCapture(m)&&m.typeOf()!=Move::PROMOTION;
            bool givesCheck=board.givesCheck(m)!=chess::CheckType::NO_CHECK;
            if(q&&!check&&options.see&&!quiet&&m.typeOf()!=Move::PROMOTION&&!givesCheck&&static_exchange(board,m)<0) {++seecuts;continue;}
            if(options.futility&&!q&&!pvnode&&!check&&searched>0&&depth<=3&&quiet&&!givesCheck&&
               m!=first&&m!=killers[ply][0]&&m!=killers[ply][1]&&std::abs(alpha)<28000&&staticEval+120*depth<=alpha) {++futilitycuts;continue;}
            int reduction=0;
            if(options.lmr&&!q&&!check&&quiet&&!givesCheck&&depth>=3&&searched>=3&&
               m!=first&&m!=killers[ply][0]&&m!=killers[ply][1]&&
               history[int(board.sideToMove())][m.from().index()][m.to().index()]<depth*depth*8) {
                reduction=std::clamp(int(std::log(double(depth))*std::log(double(searched+1))/2),1,std::min(3,depth-2));
                if(pvnode) reduction=std::max(0,reduction-1);
            }
            std::vector<Move> tail; int score;
            if(!q&&searched) {
                if(reduction) {++reductions;score=child(m,depth-1-reduction,alpha,alpha+1,ply+1,ctx,tail,extensions,nullpath);}
                if(!reduction||score>alpha) {
                    if(reduction) ++researches;
                    score=child(m,depth-1,alpha,alpha+1,ply+1,ctx,tail,extensions,nullpath);
                }
                if(score>alpha&&score<beta) {++researches;score=child(m,depth-1,alpha,beta,ply+1,ctx,tail,extensions,nullpath);}
            } else score=child(m,depth-1,alpha,beta,ply+1,ctx,tail,extensions,nullpath);
            ++searched;if(quiet) quiets.push_back(m);
            if(score>best) {best=score;bestmove=m;pv=tail;pv.insert(pv.begin(),m);}
            alpha=std::max(alpha,score);
            if(alpha>=beta) {
                ++cutoffs;
                if(!q&&quiet) {
                    if(killers[ply][0]!=m){killers[ply][1]=killers[ply][0];killers[ply][0]=m;}
                    int bonus=std::min(2000,depth*depth*16);
                    auto update=[&](Move move,int delta){auto& h=history[int(board.sideToMove())][move.from().index()][move.to().index()];h+=delta-h*std::abs(delta)/16384;};
                    update(m,bonus);for(auto failed:quiets)if(failed!=m)update(failed,-bonus);
                }
                break;
            }
        }
        if(!q&&!nullpath&&bestmove.move()) {
            if(hints.size()>65536) hints.clear();
            hints[board.hash()]=bestmove;
            // A shallow warm-up iteration must not erase a deeper retained bound.
            if((slot->key==key&&depth>=slot->depth)||
               (slot->key!=key&&(slot->generation!=uint32_t(memory.searches)||depth+3>=slot->depth)))
                *slot={key,depth,store_score(best,ply),best>=beta?1:best<=original_alpha?2:0,bestmove,uint32_t(memory.searches)};
        }
        return best;
    }
    void info(int depth,int score,const std::vector<Move>& pv) {
        auto ms=std::max<int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
        std::ostringstream out;
        out<<"info depth "<<depth<<" seldepth "<<seldepth<<" score ";
        if(std::abs(score)>29000) out<<"mate "<<((MATE_SCORE-std::abs(score)+1)/2)*(score>0?1:-1);
        else out<<"cp "<<score;
        out<<" nodes "<<nodes<<" time "<<ms<<" nps "<<nodes*1000/ms<<" pv";
        for(auto m:pv) out<<' '<<chess::uci::moveToUci(m);
        say(out.str());
    }
    void run() {
        start=Clock::now(); deadline=Clock::time_point::max();activate_clock();
        say("info string persistent_search "+std::to_string(memory.searches)+" retained_entries "+std::to_string(retainedBefore));
        chess::Movelist all; chess::movegen::legalmoves(all,board);
        auto roots=limits.restricted?limits.roots:std::vector<Move>(all.begin(),all.end());
        Move best=roots.empty()?Move(0):roots[0];
        auto previous=hints.find(board.hash());
        if(previous!=hints.end()&&std::find(roots.begin(),roots.end(),previous->second)!=roots.end())best=previous->second;
        int done=0,lastscore=0; std::vector<Move> lastpv;
        if(!roots.empty()&&(board.isHalfMoveDraw()||board.isInsufficientMaterial()||board.isRepetition(2))) {
            info(0,0,{best}); limits.depth=0;
        }
        int stability=0;
        for(int depth=1;depth<=limits.depth&&!roots.empty();++depth) {
            int score=-INF; std::vector<Move> pv;
            std::stable_sort(roots.begin(),roots.end(),[&](Move a,Move b){return (a==best)>(b==best);});
            int window=options.aspiration&&depth>=4&&std::abs(lastscore)<28000?25:INF;
            try {
                while(true) {
                    int alpha=window==INF?-INF:std::max(-INF,lastscore-window);
                    int beta=window==INF?INF:std::min(INF,lastscore+window);
                    int initialAlpha=alpha;score=-INF;pv.clear();int count=0;
                    for(auto m:roots) {
                        std::vector<Move> tail;int value;
                        if(count++) {
                            value=child(m,depth-1,alpha,alpha+1,1,context,tail);
                            if(value>alpha&&value<beta)value=child(m,depth-1,alpha,beta,1,context,tail);
                        } else value=child(m,depth-1,alpha,beta,1,context,tail);
                        if(value>score){score=value;pv=tail;pv.insert(pv.begin(),m);}
                        alpha=std::max(alpha,value);if(alpha>=beta)break;
                    }
                    if(window==INF||(score>initialAlpha&&score<beta))break;
                    window=window>=4096?INF:window*2;
                }
            } catch(const Halt&) {break;}
            stability=pv[0]==best?stability+1:0;
            int drop=done?lastscore-score:0;
            best=pv[0]; done=depth;lastscore=score;lastpv=pv;
            if(!limits.restricted){if(hints.size()>65536)hints.clear();hints[board.hash()]=best;}
            info(done,lastscore,lastpv);
            if(std::abs(score)>29000) break;
            activate_clock();
            if(clockActive&&!limits.infinite&&limits.softms>=0) {
                double factor=options.adaptive?std::clamp(1.5-stability*.15+std::max(0,drop)/100.0,.6,2.5):1;
                if(roots.size()==1)factor=.2;
                double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-activeStart).count();
                if(elapsed>=limits.softms*factor)break;
            }
        }
        if(limits.infinite||control.pondering.load()) while(!stop.load()&&(limits.infinite||control.pondering.load())) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(done) info(done,lastscore,lastpv);
        say("info string qnodes "+std::to_string(qnodes)+" beta_cutoffs "+std::to_string(cutoffs)+" tt_cutoffs "+std::to_string(ttcuts)+" null_cutoffs "+std::to_string(nullcuts)+" lmr_reductions "+std::to_string(reductions)+" researches "+std::to_string(researches)+" futility_prunes "+std::to_string(futilitycuts)+" see_prunes "+std::to_string(seecuts)+" check_extensions "+std::to_string(checkextensions)+" persistent_searches "+std::to_string(memory.searches)+" retained_before "+std::to_string(retainedBefore));
        say("bestmove "+(best.move()?chess::uci::moveToUci(best):"0000")+(limits.suggestPonder&&lastpv.size()>1?" ponder "+chess::uci::moveToUci(lastpv[1]):""));
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
    SearchOptions options; SearchMemory memory; SearchControl control;
    bool ponderEnabled=false,treeProtected=false;std::string treeFile;uint64_t modelIdentity=0;
    int overhead=100,hash=32; uint64_t context=mix(board.hash()); std::string model;
    void halt(){stop=true;if(worker.joinable())worker.join();}
    Move legal(const std::string& text) {
        chess::Movelist moves;chess::movegen::legalmoves(moves,board);
        for(auto m:moves) if(chess::uci::moveToUci(m)==text) return m;
        throw std::runtime_error("illegal move "+text);
    }
public:
    explicit Engine(std::string file):model(std::move(file)){net.load(model);modelIdentity=SearchMemory::identity(net);}
    ~Engine(){halt();if(!treeFile.empty()&&!treeProtected)try{memory.save(treeFile,modelIdentity,SearchMemory::signature(options));}catch(const std::exception& e){say(std::string("info string tree save failed ")+e.what());}}
    bool command(const std::string& line) {
        std::istringstream in(line);std::string cmd;in>>cmd;
        if(cmd=="uci") {
            say("id name KittenFish C++ experimental");say("id author crazykitten43");
            say("option name Move Overhead type spin default 100 min 0 max 5000");
            say("option name Hash type spin default 32 min 1 max 1024");
            say("option name Ponder type check default false");
            say("option name TreeFile type string default <empty>");
            say("option name Clear Hash type button");say("option name Save Tree type button");say("option name Load Tree type button");
            for(auto name:{"Null Move Pruning","LMR","SEE","Futility Pruning","Aspiration Windows","Check Extensions","Adaptive Time"})say(std::string("option name ")+name+" type check default true");
            say("option name EvalFile type string default "+model);say("uciok");
        } else if(cmd=="isready") say("readyok");
        else if(cmd=="stop") halt();
        else if(cmd=="ponderhit") {if(control.pondering.load())control.hit();}
        else if(cmd=="quit") {halt();return false;}
        else if(cmd=="ucinewgame") {halt();board=CB();context=mix(board.hash());memory.clear();}
        else if(cmd=="setoption") {
            halt();
            std::string button=line.substr(15);
            if(button=="Clear Hash"){memory.clear();return true;}
            if(button=="Save Tree"||button=="Load Tree"){
                if(treeFile.empty())throw std::runtime_error("set TreeFile first");
                if(button=="Save Tree")memory.save(treeFile,modelIdentity,SearchMemory::signature(options));
                else {treeProtected=true;memory.load(treeFile,modelIdentity,SearchMemory::signature(options));}
                treeProtected=false;return true;
            }
            auto at=line.find(" value ");if(at==std::string::npos)throw std::runtime_error("missing value");
            auto name=line.substr(15,at-15), value=line.substr(at+7);
            bool enabled=value=="true";
            if(name=="Ponder")ponderEnabled=enabled;
            else if(name=="TreeFile"){treeFile=value=="<empty>"?"":value;treeProtected=!treeFile.empty()&&std::ifstream(treeFile,std::ios::binary).good();}
            else if(name=="Null Move Pruning")options.nullmove=enabled;
            else if(name=="LMR")options.lmr=enabled;
            else if(name=="SEE")options.see=enabled;
            else if(name=="Futility Pruning")options.futility=enabled;
            else if(name=="Aspiration Windows")options.aspiration=enabled;
            else if(name=="Check Extensions")options.extensions=enabled;
            else if(name=="Adaptive Time")options.adaptive=enabled;
            else if(name=="Hash"){hash=std::clamp(std::stoi(value),1,1024);memory=SearchMemory(hash);}
            else if(name=="Move Overhead")overhead=std::clamp(std::stoi(value),0,5000);
            else if(name=="EvalFile") {QuantNet next;next.load(value);net=std::move(next);model=value;modelIdentity=SearchMemory::identity(net);memory.clear();}
                    if(name!="Ponder"&&name!="TreeFile"&&name!="Move Overhead"&&name!="Adaptive Time"&&name!="Hash")memory.clear();
        } else if(cmd=="position") {
            halt();std::string token;in>>token;
            if(token=="startpos")board=CB();
            else if(token=="fen") {std::string f;for(int i=0;i<6;++i){if(!(in>>token))throw std::runtime_error("incomplete FEN");f+=token+' ';}if(!board.setFen(f))throw std::runtime_error("invalid FEN");}
            else throw std::runtime_error("invalid position");
            context=mix(board.hash());
            if(in>>token) {if(token!="moves")throw std::runtime_error("expected moves");while(in>>token){board.makeMove(legal(token));context=board.halfMoveClock()==0?mix(board.hash()):mix(context^board.hash());}}
        } else if(cmd=="see") {halt();std::string move;in>>move;say("info string see "+std::to_string(static_exchange(board,legal(move))));}
        else if(cmd=="eval") {halt();auto state=net.start(position(board));say("info string eval "+std::to_string(int(std::lround(net.eval(state)))));}
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
                else if(t=="ponder")l.ponder=true;
                else if(t=="searchmoves") {
                    l.restricted=true;
                    while(in>>t) {
                        if(std::find(keys.begin(),keys.end(),t)!=keys.end()||t=="infinite"||t=="ponder")break;
                        auto m=legal(t);if(std::find(l.roots.begin(),l.roots.end(),m)==l.roots.end())l.roots.push_back(m);
                    }
                    if(!in)break;
                    if(t=="infinite"){l.infinite=true;continue;}
                    if(t=="ponder"){l.ponder=true;continue;}
                    uint64_t n;if(!(in>>n))throw std::runtime_error("missing limit");p[t]=n;
                } else if(std::find(keys.begin(),keys.end(),t)!=keys.end()) {uint64_t n;if(!(in>>n))throw std::runtime_error("missing limit");p[t]=n;}
            }
            if(p.count("depth"))l.depth=int(std::clamp<uint64_t>(p["depth"],1,MAXPLY-1));
            else if(p.count("mate"))l.depth=int(std::clamp<uint64_t>(p["mate"],1,47))*2;
            if(p.count("nodes"))l.nodes=p["nodes"];
            auto side=board.sideToMove()==chess::Color::WHITE?"wtime":"btime";
            auto inc=board.sideToMove()==chess::Color::WHITE?"winc":"binc";
            if(p.count("movetime"))l.ms=std::max(1.0,double(p["movetime"])-overhead);
            else if(p.count(side)) {
                double available=std::max(1.0,double(p[side])-overhead);
                l.softms=std::min(available,std::max(1.0,available/std::max<uint64_t>(1,p.count("movestogo")?p["movestogo"]:30)+p[inc]*.8));
                l.ms=options.adaptive?std::min(available,l.softms*4):l.softms;
            }
            say("info string evaluator NNUE budget_ms "+std::to_string(l.infinite?-1:l.ms));
            l.suggestPonder=ponderEnabled;control.hit_ns.store(0);control.pondering.store(l.ponder);
            stop=false;
            worker=std::thread([this,l] {try {Search s(board,net,stop,l,context,memory,control,options);s.run();}catch(const std::exception& e){say(std::string("info string search failed ")+e.what());say("bestmove 0000");}});
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
