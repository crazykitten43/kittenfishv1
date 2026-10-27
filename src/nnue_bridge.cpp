#include "../nnue/upstream/main.cpp"
#include <memory>
struct Evaluator {
    QuantNet net;
    vector<QuantNet::State> stack;
};
extern "C" {
void* kf_load(const char* path) {
    try { auto e=make_unique<Evaluator>(); e->net.load(path); return e.release(); }
    catch (...) { return nullptr; }
}
void kf_free(void* ptr) { delete static_cast<Evaluator*>(ptr); }
int kf_push(void* ptr,const int* board,int stm) {
    try {
        auto& e=*static_cast<Evaluator*>(ptr); Position p; p.stm=stm;
        copy(board,board+64,p.b.begin());
        if(e.stack.empty()) e.stack.push_back(e.net.start(p));
        else { auto s=e.stack.back(); e.net.update(s,p); e.stack.push_back(s); }
        return 1;
    } catch (...) { return 0; }
}
void kf_pop(void* ptr) { auto& s=static_cast<Evaluator*>(ptr)->stack; if(!s.empty())s.pop_back(); }
int kf_eval(void* ptr) {
    auto& e=*static_cast<Evaluator*>(ptr);
    return int(lround(e.net.eval(e.stack.back())));
}
}
